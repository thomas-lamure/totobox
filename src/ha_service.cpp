// SPDX-License-Identifier: GPL-3.0-or-later
#include "ha_service.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <mdns.h>

#include <vector>

#include "logger.h"
#include "retry_delay.h"
#include "totobox_config.h"

namespace {

constexpr uint32_t kTaskStackBytes = 8 * 1024;  // client, résolution du nom, messages de découverte
constexpr UBaseType_t kTaskPriority = 1;
constexpr BaseType_t kTaskCore = 0;
constexpr uint32_t kTickMs = 20;
constexpr uint16_t kKeepAliveSec = 30;
constexpr uint16_t kSocketTimeoutSec = 5;
constexpr uint32_t kMdnsTimeoutMs = 2000;
constexpr uint32_t kRetryInitialMs = 10000;
constexpr uint32_t kRetryMaxMs = 60000;
constexpr UBaseType_t kCardQueueLength = 8;
constexpr UBaseType_t kCommandQueueLength = 8;

WiFiClient network;
PubSubClient mqtt(network);

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTake(mutex_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex_); }

 private:
  SemaphoreHandle_t mutex_;
};

// Ce qui, dans l'appareil, change les messages de découverte.
String discoveryKey(const ha_rules::DeviceInfo& device) {
  return device.name + "|" + device.version + "|" + device.url + "|" + String(device.volumeMin) + "|" +
         String(device.volumeMax);
}

// Nom en .local: résolu par mDNS (la résolution DNS du framework ne le fait pas).
bool resolve(const String& host, IPAddress& ip) {
  if (ha_rules::isLocalName(host)) {
    esp_ip4_addr_t address = {};
    const String name = host.substring(0, host.length() - 6);
    if (mdns_query_a(name.c_str(), kMdnsTimeoutMs, &address) != ESP_OK) {
      return false;
    }
    ip = IPAddress(address.addr);
    return true;
  }
  if (ip.fromString(host)) {
    return true;
  }
  return WiFi.hostByName(host.c_str(), ip) == 1;
}

}  // namespace

bool HomeAssistantService::begin(const uint8_t mac[6]) {
  id_ = ha_rules::deviceId(mac);
  mutex_ = xSemaphoreCreateMutex();
  cardEvents_ = xQueueCreate(kCardQueueLength, sizeof(CardEvent));
  commands_ = xQueueCreate(kCommandQueueLength, sizeof(ha_rules::Command));
  if (mutex_ == nullptr || cardEvents_ == nullptr || commands_ == nullptr) {
    logLine("ha", "Mémoire insuffisante: liaison Home Assistant désactivée");
    return false;
  }
  mqtt.setBufferSize(ha_rules::kMqttBufferBytes);
  mqtt.setKeepAlive(kKeepAliveSec);
  mqtt.setSocketTimeout(kSocketTimeoutSec);
  mqtt.setCallback([this](char* topic, uint8_t* payload, unsigned int length) { onMessage(topic, payload, length); });
  if (xTaskCreatePinnedToCore(taskEntry, "ha", kTaskStackBytes, this, kTaskPriority, nullptr, kTaskCore) != pdPASS) {
    logLine("ha", "Tâche non créée: liaison Home Assistant désactivée");
    return false;
  }
  return true;
}

void HomeAssistantService::configure(const DeviceConfig& config) {
  if (mutex_ == nullptr) {
    return;
  }
  Lock lock(mutex_);
  enabled_ = config.haEnabled;
  address_ = config.haAddress;
  user_ = config.mqttUser;
  password_ = config.mqttPassword;
  reconfigure_ = true;
}

void HomeAssistantService::update(bool online, const ha_rules::DeviceInfo& device, const ha_rules::State& state) {
  if (mutex_ == nullptr) {
    return;
  }
  Lock lock(mutex_);
  online_ = online;
  device_ = device;
  device_.id = id_;
  state_ = state;
  haveState_ = true;
}

void HomeAssistantService::notifyCardPresented(const String& uid) {
  queueCard(EventType::Presented, uid);
}

void HomeAssistantService::notifyCardRemoved(const String& uid) {
  queueCard(EventType::Removed, uid);
}

void HomeAssistantService::queueCard(EventType type, const String& uid) {
  if (cardEvents_ == nullptr || uid.isEmpty()) {
    return;
  }
  {
    Lock lock(mutex_);
    if (!enabled_) {
      return;
    }
  }
  CardEvent event = {};
  event.type = type;
  event.atMs = millis();
  strlcpy(event.uid, uid.c_str(), sizeof(event.uid));
  // File pleine (broker injoignable depuis longtemps): la plus ancienne n'a plus d'intérêt.
  if (xQueueSend(cardEvents_, &event, 0) != pdTRUE) {
    CardEvent dropped;
    xQueueReceive(cardEvents_, &dropped, 0);
    xQueueSend(cardEvents_, &event, 0);
  }
}

bool HomeAssistantService::takeCommand(ha_rules::Command& command) {
  return commands_ != nullptr && xQueueReceive(commands_, &command, 0) == pdTRUE;
}

HomeAssistantService::Status HomeAssistantService::status() const {
  if (mutex_ == nullptr) {
    return Status();
  }
  Lock lock(mutex_);
  return status_;
}

void HomeAssistantService::shutdown(uint32_t timeoutMs) {
  if (mutex_ == nullptr) {
    return;
  }
  shutdownDone_ = false;
  shutdownRequested_ = true;
  const uint32_t start = millis();
  while (!shutdownDone_ && millis() - start < timeoutMs) {
    delay(10);
  }
}

void HomeAssistantService::taskEntry(void* arg) {
  static_cast<HomeAssistantService*>(arg)->run();
}

void HomeAssistantService::run() {
  for (;;) {
    step();
    vTaskDelay(pdMS_TO_TICKS(kTickMs));
  }
}

void HomeAssistantService::setStatus(const char* state, const char* error) {
  Lock lock(mutex_);
  status_.state = state;
  status_.error = error;
}

void HomeAssistantService::step() {
  if (shutdownRequested_) {
    if (connected_) {
      disconnect(false);
    }
    shutdownDone_ = true;
    // Plus rien jusqu'au sommeil ou au redémarrage.
    for (;;) {
      vTaskDelay(portMAX_DELAY);
    }
  }

  bool enabled;
  bool online;
  bool reconfigure;
  {
    Lock lock(mutex_);
    enabled = enabled_;
    online = online_;
    reconfigure = reconfigure_;
    reconfigure_ = false;
  }

  if (reconfigure) {
    // Nouveaux réglages: on repart de zéro (désactivée: l'appareil est retiré de Home Assistant).
    if (connected_) {
      disconnect(!enabled);
    }
    addressInvalid_ = false;
    retryAtMs_ = 0;
    retryDelayMs_ = kRetryInitialMs;
  }

  if (!enabled) {
    setStatus("off", "");
    // Cartes posées pendant que la liaison était coupée: rien à envoyer.
    xQueueReset(cardEvents_);
    return;
  }
  if (connected_ && !mqtt.connected()) {
    logLine("ha", "Connexion au broker perdue");
    connected_ = false;
    retryAtMs_ = millis() + kRetryInitialMs;
  }
  if (!online) {
    if (connected_) {
      mqtt.disconnect();
      connected_ = false;
    }
    setStatus("error", "no_wifi");
    retryAtMs_ = 0;
    return;
  }
  if (!connected_) {
    if (addressInvalid_ || (retryAtMs_ != 0 && static_cast<int32_t>(millis() - retryAtMs_) < 0)) {
      return;
    }
    if (!connect()) {
      retryAtMs_ = millis() + retryDelayMs_;
      retryDelayMs_ = retry_delay::next(retryDelayMs_, kRetryMaxMs);
      return;
    }
    retryDelayMs_ = kRetryInitialMs;
  }

  mqtt.loop();
  publishPending();
}

bool HomeAssistantService::connect() {
  String address;
  String user;
  String password;
  {
    Lock lock(mutex_);
    address = address_;
    user = user_;
    password = password_;
  }
  ha_rules::Broker broker;
  if (ha_rules::parseAddress(address, broker) != nullptr) {
    logLine("ha", "Adresse de Home Assistant illisible: %s", address.c_str());
    addressInvalid_ = true;
    setStatus("error", "invalid_address");
    return false;
  }

  setStatus("connecting", "");
  IPAddress ip;
  if (!resolve(broker.host, ip)) {
    logLine("ha", "Nom introuvable: %s", broker.host.c_str());
    setStatus("error", "dns");
    return false;
  }

  mqtt.setServer(ip, broker.port);
  const String availability = ha_rules::topic(id_, ha_rules::kAvailabilityLeaf);
  const bool ok = mqtt.connect(id_.c_str(), user.isEmpty() ? nullptr : user.c_str(),
                               user.isEmpty() ? nullptr : password.c_str(), availability.c_str(), 1, true,
                               ha_rules::kOffline);
  if (!ok) {
    const int code = mqtt.state();
    const bool refused = code == MQTT_CONNECT_BAD_CREDENTIALS || code == MQTT_CONNECT_UNAUTHORIZED;
    logLine("ha", "Connexion à %s:%u refusée (%d)", ip.toString().c_str(), broker.port, code);
    setStatus("error", refused ? "auth" : "unreachable");
    return false;
  }

  logLine("ha", "Connectée au broker %s:%u", ip.toString().c_str(), broker.port);
  connected_ = true;
  discoveryPublished_ = false;
  statePublished_ = false;
  for (const String& topic : ha_rules::commandTopics(id_)) {
    mqtt.subscribe(topic.c_str());
  }
  setStatus("connected", "");
  return true;
}

void HomeAssistantService::disconnect(bool removeDevice) {
  if (removeDevice) {
    // Liaison désactivée: Home Assistant oublie l'appareil (messages conservés effacés).
    publishDiscovery(true);
    mqtt.publish(ha_rules::topic(id_, ha_rules::kStateLeaf).c_str(), "", true);
    logLine("ha", "Appareil retiré de Home Assistant");
  }
  mqtt.publish(ha_rules::topic(id_, ha_rules::kAvailabilityLeaf).c_str(), ha_rules::kOffline, true);
  mqtt.disconnect();
  connected_ = false;
  discoveryPublished_ = false;
}

void HomeAssistantService::publishDiscovery(bool remove) {
  ha_rules::DeviceInfo device;
  {
    Lock lock(mutex_);
    device = device_;
  }
  device.id = id_;
  for (const ha_rules::Message& message : ha_rules::discoveryMessages(device)) {
    if (!mqtt.publish(message.topic.c_str(), remove ? "" : message.payload.c_str(), true)) {
      logLine("ha", "Découverte non publiée: %s", message.topic.c_str());
    }
  }
  discoveryKey_ = discoveryKey(device);
}

void HomeAssistantService::publishPending() {
  bool haveState;
  ha_rules::DeviceInfo device;
  ha_rules::State state;
  {
    Lock lock(mutex_);
    haveState = haveState_;
    device = device_;
    state = state_;
  }
  if (!haveState) {
    return;  // loop() n'a encore rien donné: le nom de l'appareil n'est pas connu
  }

  if (!discoveryPublished_ || discoveryKey(device) != discoveryKey_) {
    publishDiscovery(false);
    mqtt.publish(ha_rules::topic(id_, ha_rules::kAvailabilityLeaf).c_str(), ha_rules::kOnline, true);
    discoveryPublished_ = true;
    statePublished_ = false;
  }

  const uint32_t now = millis();
  if (!statePublished_ || ha_rules::shouldPublishState(published_, state, now - lastStateMs_)) {
    if (mqtt.publish(ha_rules::topic(id_, ha_rules::kStateLeaf).c_str(), ha_rules::stateJson(state).c_str(), true)) {
      published_ = state;
      statePublished_ = true;
      lastStateMs_ = now;
    }
  }

  CardEvent event;
  while (xQueueReceive(cardEvents_, &event, 0) == pdTRUE) {
    if (!ha_rules::isTagFresh(now - event.atMs)) {
      continue;
    }
    const char* leaf = event.type == EventType::Presented ? ha_rules::kTagLeaf : ha_rules::kCardRemovedLeaf;
    mqtt.publish(ha_rules::topic(id_, leaf).c_str(), event.uid, false);
  }
}

void HomeAssistantService::onMessage(char* topic, uint8_t* payload, unsigned int length) {
  uint8_t volumeMin;
  uint8_t volumeMax;
  {
    Lock lock(mutex_);
    volumeMin = state_.volumeMin;
    volumeMax = state_.volumeMax;
  }
  String text;
  text.reserve(length);
  for (unsigned int index = 0; index < length; ++index) {
    text += static_cast<char>(payload[index]);
  }
  const ha_rules::Command command = ha_rules::parseCommand(id_, topic, text, volumeMin, volumeMax);
  if (command.type == ha_rules::CommandType::None) {
    logLine("ha", "Commande ignorée sur %s", topic);
    return;
  }
  xQueueSend(commands_, &command, 0);
}
