// SPDX-License-Identifier: GPL-3.0-or-later
#include "ha_rules.h"

#include <ArduinoJson.h>

#include "access_lock.h"

namespace ha_rules {

namespace {

bool isHostChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
}

bool isValidHost(const String& host) {
  if (host.isEmpty() || host.length() > 253) {
    return false;
  }
  const char first = host[0];
  const char last = host[host.length() - 1];
  if (first == '.' || first == '-' || last == '.' || last == '-' || host.indexOf("..") >= 0) {
    return false;
  }
  for (size_t index = 0; index < host.length(); ++index) {
    if (!isHostChar(host[index])) {
      return false;
    }
  }
  return true;
}

// Port de 1 à 65535, chiffres seulement.
bool parsePort(const String& text, uint16_t& port) {
  if (text.isEmpty() || text.length() > 5) {
    return false;
  }
  uint32_t value = 0;
  for (size_t index = 0; index < text.length(); ++index) {
    if (!isDigit(text[index])) {
      return false;
    }
    value = value * 10 + (text[index] - '0');
  }
  if (value == 0 || value > 65535) {
    return false;
  }
  port = static_cast<uint16_t>(value);
  return true;
}

// Bloc « device » commun à toutes les entités: Home Assistant les regroupe sous un seul appareil.
void addDevice(JsonObject root, const DeviceInfo& device) {
  JsonObject info = root["device"].to<JsonObject>();
  info["identifiers"].to<JsonArray>().add(device.id);
  info["name"] = device.name;
  info["manufacturer"] = "TotoBox";
  info["model"] = "HermitX CardReader";
  info["sw_version"] = device.version;
  if (!device.url.isEmpty()) {
    info["configuration_url"] = device.url;
  }
  JsonObject origin = root["origin"].to<JsonObject>();
  origin["name"] = "TotoBox";
  origin["sw_version"] = device.version;
  origin["support_url"] = "https://github.com/thomas-lamure/totobox";
}

// Entité lisant l'état publié (sujet "state"), indisponible quand la boîte dort.
JsonObject entity(JsonDocument& doc, const DeviceInfo& device, const char* object, const char* name) {
  JsonObject root = doc.to<JsonObject>();
  root["name"] = name;
  root["unique_id"] = device.id + "_" + object;
  root["availability_topic"] = topic(device.id, kAvailabilityLeaf);
  root["state_topic"] = topic(device.id, kStateLeaf);
  return root;
}

Message finish(JsonDocument& doc, const DeviceInfo& device, const char* component, const char* object) {
  addDevice(doc.as<JsonObject>(), device);
  Message message;
  message.topic = String(kDiscoveryPrefix) + "/" + component + "/" + device.id + "/" + object + "/config";
  serializeJson(doc, message.payload);
  return message;
}

}  // namespace

const char* parseAddress(const String& input, Broker& broker) {
  String rest = input;
  rest.trim();
  bool usePort = true;

  const int schemeEnd = rest.indexOf("://");
  if (schemeEnd >= 0) {
    String scheme = rest.substring(0, schemeEnd);
    scheme.toLowerCase();
    rest = rest.substring(schemeEnd + 3);
    if (scheme == "mqtts" || scheme == "ssl") {
      return "tls_unsupported";
    }
    if (scheme == "http" || scheme == "https") {
      // Adresse de la page Home Assistant: le broker est sur la même machine, sur le port MQTT.
      usePort = false;
    } else if (scheme != "mqtt" && scheme != "tcp") {
      return "invalid_address";
    }
  }

  // Chemin, paramètres ou ancre d'une adresse copiée depuis le navigateur.
  for (const char separator : {'/', '?', '#'}) {
    const int found = rest.indexOf(separator);
    if (found >= 0) {
      rest = rest.substring(0, found);
    }
  }
  // Identifiants dans l'adresse (user@hôte): ils vont dans leurs champs; IPv6 non pris en charge.
  if (rest.indexOf('@') >= 0 || rest.indexOf('[') >= 0) {
    return "invalid_address";
  }

  uint16_t port = kDefaultPort;
  const int colon = rest.indexOf(':');
  if (colon >= 0) {
    if (rest.indexOf(':', colon + 1) >= 0 || !parsePort(rest.substring(colon + 1), port)) {
      return "invalid_address";
    }
    rest = rest.substring(0, colon);
  }
  if (!isValidHost(rest)) {
    return "invalid_address";
  }
  rest.toLowerCase();
  broker.host = rest;
  broker.port = usePort ? port : kDefaultPort;
  return nullptr;
}

bool isLocalName(const String& host) {
  return host.endsWith(".local");
}

String deviceId(const uint8_t mac[6]) {
  static const char kHex[] = "0123456789abcdef";
  String id = "totobox_";
  for (int index = 0; index < 6; ++index) {
    id += kHex[mac[index] >> 4];
    id += kHex[mac[index] & 0x0F];
  }
  return id;
}

String topic(const String& id, const char* leaf) {
  return "totobox/" + id + "/" + leaf;
}

std::vector<Message> discoveryMessages(const DeviceInfo& device) {
  std::vector<Message> messages;
  JsonDocument doc;

  {
    JsonObject root = entity(doc, device, "state", "État");
    root["value_template"] = "{{ value_json.state }}";
    root["device_class"] = "enum";
    JsonArray options = root["options"].to<JsonArray>();
    for (const char* option : {"booting", "ready", "playing", "paused", "stopped", "unknown_card", "two_cards", "error"}) {
      options.add(option);
    }
    root["icon"] = "mdi:play-box-outline";
    messages.push_back(finish(doc, device, "sensor", "state"));
  }
  {
    JsonObject root = entity(doc, device, "story", "Histoire");
    root["value_template"] = "{{ value_json.story }}";
    root["json_attributes_topic"] = topic(device.id, kStateLeaf);
    root["json_attributes_template"] =
        "{{ {'carte': value_json.card, 'piste': value_json.track, 'pistes': value_json.tracks, "
        "'position': value_json.position} | tojson }}";
    root["icon"] = "mdi:book-open-variant";
    messages.push_back(finish(doc, device, "sensor", "story"));
  }
  {
    JsonObject root = entity(doc, device, "volume", "Volume");
    root["command_topic"] = topic(device.id, kVolumeSetLeaf);
    root["value_template"] = "{{ value_json.volume }}";
    root["min"] = device.volumeMin;
    root["max"] = device.volumeMax;
    root["step"] = 5;
    root["mode"] = "slider";
    root["unit_of_measurement"] = "%";
    root["icon"] = "mdi:volume-high";
    messages.push_back(finish(doc, device, "number", "volume"));
  }
  {
    JsonObject root = entity(doc, device, "stop", "Arrêter");
    root.remove("state_topic");
    root["command_topic"] = topic(device.id, kStopLeaf);
    root["payload_press"] = "PRESS";
    root["icon"] = "mdi:stop";
    messages.push_back(finish(doc, device, "button", "stop"));
  }
  {
    JsonObject root = doc.to<JsonObject>();
    root["topic"] = topic(device.id, kTagLeaf);
    messages.push_back(finish(doc, device, "tag", "card"));
  }
  {
    JsonObject root = doc.to<JsonObject>();
    root["automation_type"] = "trigger";
    root["topic"] = topic(device.id, kCardRemovedLeaf);
    root["type"] = "card_removed";
    root["subtype"] = "card";
    messages.push_back(finish(doc, device, "device_automation", "card_removed"));
  }
  {
    JsonObject root = entity(doc, device, "sd", "Carte SD");
    root["value_template"] = "{{ value_json.sd }}";
    root["device_class"] = "enum";
    JsonArray options = root["options"].to<JsonArray>();
    for (const char* option : {"absent", "unreadable", "ready"}) {
      options.add(option);
    }
    root["entity_category"] = "diagnostic";
    root["icon"] = "mdi:sd";
    messages.push_back(finish(doc, device, "sensor", "sd"));
  }
  {
    // Classe « lock » de Home Assistant: ON = déverrouillé.
    JsonObject root = entity(doc, device, "lock", "Réglages");
    root["value_template"] = "{{ 'OFF' if value_json.locked else 'ON' }}";
    root["device_class"] = "lock";
    root["entity_category"] = "diagnostic";
    messages.push_back(finish(doc, device, "binary_sensor", "lock"));
  }
  {
    JsonObject root = entity(doc, device, "rssi", "Signal Wi-Fi");
    root["value_template"] = "{{ value_json.rssi }}";
    root["device_class"] = "signal_strength";
    root["state_class"] = "measurement";
    root["unit_of_measurement"] = "dBm";
    root["entity_category"] = "diagnostic";
    root["enabled_by_default"] = false;
    messages.push_back(finish(doc, device, "sensor", "rssi"));
  }
  return messages;
}

const char* stateName(const RuntimeState& runtime) {
  if (runtime.stoppedRemotely && runtime.state == DeviceState::Paused) {
    return "stopped";
  }
  return deviceStateToString(runtime.state);
}

String stateJson(const State& state) {
  JsonDocument doc;
  doc["state"] = state.state;
  doc["story"] = state.story;
  doc["card"] = state.card;
  doc["track"] = state.track;
  doc["tracks"] = state.tracks;
  doc["position"] = state.positionSec;
  doc["volume"] = state.volume;
  doc["volume_min"] = state.volumeMin;
  doc["volume_max"] = state.volumeMax;
  doc["sd"] = state.sd;
  doc["locked"] = state.locked;
  if (state.rssi != 0) {
    doc["rssi"] = state.rssi;
  } else {
    doc["rssi"] = nullptr;
  }
  String payload;
  serializeJson(doc, payload);
  return payload;
}

bool shouldPublishState(const State& previous, const State& current, uint32_t sinceLastMs) {
  // Le signal Wi-Fi varie sans cesse: rafraîchi avec la position, pas à chaque variation.
  const bool changed = previous.state != current.state || previous.story != current.story ||
                       previous.card != current.card || previous.track != current.track ||
                       previous.tracks != current.tracks || previous.volume != current.volume ||
                       previous.volumeMin != current.volumeMin || previous.volumeMax != current.volumeMax ||
                       previous.sd != current.sd || previous.locked != current.locked;
  if (changed) {
    return true;
  }
  return current.state == "playing" && sinceLastMs >= kStateRefreshMs;
}

bool shouldPublishCard(const DeviceConfig& config, const String& uid) {
  return !uid.isEmpty() && access_lock::findCard(config, uid) == nullptr;
}

std::vector<String> commandTopics(const String& id) {
  return {topic(id, kVolumeSetLeaf), topic(id, kStopLeaf)};
}

Command parseCommand(const String& id, const String& receivedTopic, const String& payload, uint8_t volumeMin,
                     uint8_t volumeMax) {
  Command command;
  if (receivedTopic == topic(id, kStopLeaf)) {
    command.type = CommandType::Stop;
    return command;
  }
  if (receivedTopic != topic(id, kVolumeSetLeaf)) {
    return command;
  }

  // Nombre décimal positif: "55", "55.0", "54.6" (arrondi).
  String text = payload;
  text.trim();
  uint32_t whole = 0;
  size_t index = 0;
  size_t digits = 0;
  while (index < text.length() && isDigit(text[index])) {
    if (++digits > 3) {
      return command;
    }
    whole = whole * 10 + (text[index] - '0');
    ++index;
  }
  if (digits == 0) {
    return command;
  }
  if (index < text.length()) {
    if (text[index] != '.') {
      return command;
    }
    ++index;
    const size_t decimalsStart = index;
    while (index < text.length() && isDigit(text[index])) {
      ++index;
    }
    if (index != text.length() || index == decimalsStart) {
      return command;
    }
    if (text[decimalsStart] >= '5') {
      ++whole;
    }
  }

  if (whole < volumeMin) {
    whole = volumeMin;
  }
  if (whole > volumeMax) {
    whole = volumeMax;
  }
  command.type = CommandType::Volume;
  command.volume = static_cast<uint8_t>(whole);
  return command;
}

}  // namespace ha_rules
