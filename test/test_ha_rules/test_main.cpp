// SPDX-License-Identifier: GPL-3.0-or-later
// Tests des règles de la liaison Home Assistant: adresse du broker, sujets, découverte, état, commandes.

#include <ArduinoJson.h>
#include <unity.h>

#include "ha_rules.h"

using namespace ha_rules;

namespace {

constexpr char kId[] = "totobox_a1b2c3d4e5f6";

DeviceInfo device() {
  DeviceInfo info;
  info.id = kId;
  info.name = "totobox-chambre";
  info.version = "1.3.0";
  info.url = "http://totobox-chambre.local/";
  info.volumeMin = 20;
  info.volumeMax = 85;
  return info;
}

State playing() {
  State state;
  state.state = "playing";
  state.story = "Les trois petits cochons";
  state.card = "04A1B2C3";
  state.track = 2;
  state.tracks = 5;
  state.positionSec = 312;
  state.volume = 55;
  state.volumeMin = 20;
  state.volumeMax = 85;
  state.sd = "ready";
  state.locked = true;
  state.rssi = -61;
  return state;
}

void assertAddress(const char* input, const char* host, uint16_t port) {
  Broker broker;
  TEST_ASSERT_NULL_MESSAGE(parseAddress(input, broker), input);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(host, broker.host.c_str(), input);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(port, broker.port, input);
}

const Message* findMessage(const std::vector<Message>& messages, const String& topicName) {
  for (const Message& message : messages) {
    if (message.topic == topicName) {
      return &message;
    }
  }
  return nullptr;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_plain_hosts_use_the_mqtt_port() {
  assertAddress("homeassistant.local", "homeassistant.local", 1883);
  assertAddress("192.168.1.10", "192.168.1.10", 1883);
  assertAddress("  HA.Maison.lan  ", "ha.maison.lan", 1883);
}

void test_explicit_mqtt_port_is_kept() {
  assertAddress("192.168.1.20:1884", "192.168.1.20", 1884);
  assertAddress("mqtt://broker.lan:1884", "broker.lan", 1884);
  assertAddress("MQTT://broker.lan", "broker.lan", 1883);
  assertAddress("tcp://broker.lan:2000/", "broker.lan", 2000);
}

void test_home_assistant_page_address_points_to_its_broker() {
  // Le port web (8123) n'est pas celui du broker.
  assertAddress("http://192.168.1.10:8123/", "192.168.1.10", 1883);
  assertAddress("https://ha.maison.lan/lovelace/0?edit=1#top", "ha.maison.lan", 1883);
  assertAddress("http://homeassistant.local:8123", "homeassistant.local", 1883);
}

void test_tls_is_not_supported_yet() {
  Broker broker;
  TEST_ASSERT_EQUAL_STRING("tls_unsupported", parseAddress("mqtts://broker.lan:8883", broker));
  TEST_ASSERT_EQUAL_STRING("tls_unsupported", parseAddress("ssl://broker.lan", broker));
}

void test_unreadable_addresses_are_refused() {
  for (const char* bad : {"", "   ", "ftp://broker.lan", "user:pass@broker.lan", "mqtt://user@broker.lan",
                          "broker.lan:0", "broker.lan:65536", "broker.lan:abc", "broker.lan:", "a:1:2",
                          "[fe80::1]:1883", "-broker.lan", "broker.lan.", "broker..lan", "bro ker.lan",
                          "http://", "broker_lan"}) {
    Broker broker;
    TEST_ASSERT_EQUAL_STRING_MESSAGE("invalid_address", parseAddress(bad, broker), bad);
  }
}

void test_local_names_are_detected() {
  TEST_ASSERT_TRUE(isLocalName("homeassistant.local"));
  TEST_ASSERT_FALSE(isLocalName("192.168.1.10"));
  TEST_ASSERT_FALSE(isLocalName("local.lan"));
}

void test_device_id_comes_from_the_mac_address() {
  const uint8_t mac[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0x06};
  TEST_ASSERT_EQUAL_STRING("totobox_a1b2c3d4e506", deviceId(mac).c_str());
  TEST_ASSERT_EQUAL_STRING("totobox/totobox_a1b2c3d4e506/volume/set", topic(deviceId(mac), kVolumeSetLeaf).c_str());
}

void test_discovery_announces_every_entity_of_one_device() {
  const std::vector<Message> messages = discoveryMessages(device());
  const char* expected[] = {
      "homeassistant/sensor/totobox_a1b2c3d4e5f6/state/config",
      "homeassistant/sensor/totobox_a1b2c3d4e5f6/story/config",
      "homeassistant/number/totobox_a1b2c3d4e5f6/volume/config",
      "homeassistant/button/totobox_a1b2c3d4e5f6/stop/config",
      "homeassistant/tag/totobox_a1b2c3d4e5f6/card/config",
      "homeassistant/device_automation/totobox_a1b2c3d4e5f6/card_removed/config",
      "homeassistant/sensor/totobox_a1b2c3d4e5f6/sd/config",
      "homeassistant/binary_sensor/totobox_a1b2c3d4e5f6/lock/config",
      "homeassistant/sensor/totobox_a1b2c3d4e5f6/rssi/config",
  };
  TEST_ASSERT_EQUAL(sizeof(expected) / sizeof(expected[0]), messages.size());
  for (const char* topicName : expected) {
    const Message* message = findMessage(messages, topicName);
    TEST_ASSERT_NOT_NULL_MESSAGE(message, topicName);

    JsonDocument doc;
    TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, deserializeJson(doc, message->payload).code(), topicName);
    // Un seul appareil dans Home Assistant pour toutes les entités.
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kId, doc["device"]["identifiers"][0].as<const char*>(), topicName);
    TEST_ASSERT_EQUAL_STRING("totobox-chambre", doc["device"]["name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("1.3.0", doc["device"]["sw_version"].as<const char*>());
    // Le message entier tient dans le tampon du client MQTT (en-tête de 5 octets au plus, longueur du sujet).
    TEST_ASSERT_LESS_THAN_MESSAGE(kMqttBufferBytes, message->topic.length() + message->payload.length() + 7,
                                  topicName);
  }
}

void test_entities_are_unique_and_unavailable_while_asleep() {
  const std::vector<Message> messages = discoveryMessages(device());
  std::vector<String> uniqueIds;
  for (const Message& message : messages) {
    JsonDocument doc;
    deserializeJson(doc, message.payload);
    if (doc["unique_id"].isNull()) {
      continue;  // étiquette et déclencheur: pas d'entité
    }
    const String uniqueId = doc["unique_id"].as<const char*>();
    TEST_ASSERT_TRUE(uniqueId.startsWith(String(kId) + "_"));
    for (const String& other : uniqueIds) {
      TEST_ASSERT_TRUE_MESSAGE(other != uniqueId, uniqueId.c_str());
    }
    uniqueIds.push_back(uniqueId);
    TEST_ASSERT_EQUAL_STRING("totobox/totobox_a1b2c3d4e5f6/availability",
                             doc["availability_topic"].as<const char*>());
  }
  TEST_ASSERT_EQUAL(7, uniqueIds.size());
}

void test_volume_slider_is_bounded_by_the_box_limits() {
  const std::vector<Message> messages = discoveryMessages(device());
  const Message* volume = findMessage(messages, "homeassistant/number/totobox_a1b2c3d4e5f6/volume/config");
  TEST_ASSERT_NOT_NULL(volume);
  JsonDocument doc;
  deserializeJson(doc, volume->payload);
  TEST_ASSERT_EQUAL(20, doc["min"].as<int>());
  TEST_ASSERT_EQUAL(85, doc["max"].as<int>());
  TEST_ASSERT_EQUAL(5, doc["step"].as<int>());
  TEST_ASSERT_EQUAL_STRING("totobox/totobox_a1b2c3d4e5f6/volume/set", doc["command_topic"].as<const char*>());
}

void test_cards_go_to_the_tag_scanner_and_removal_trigger() {
  const std::vector<Message> messages = discoveryMessages(device());
  JsonDocument tag;
  deserializeJson(tag, findMessage(messages, "homeassistant/tag/totobox_a1b2c3d4e5f6/card/config")->payload);
  TEST_ASSERT_EQUAL_STRING("totobox/totobox_a1b2c3d4e5f6/tag", tag["topic"].as<const char*>());

  JsonDocument trigger;
  deserializeJson(trigger,
                  findMessage(messages, "homeassistant/device_automation/totobox_a1b2c3d4e5f6/card_removed/config")
                      ->payload);
  TEST_ASSERT_EQUAL_STRING("trigger", trigger["automation_type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("totobox/totobox_a1b2c3d4e5f6/card_removed", trigger["topic"].as<const char*>());
}

void test_state_json_carries_playback_and_settings() {
  JsonDocument doc;
  TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, stateJson(playing())).code());
  TEST_ASSERT_EQUAL_STRING("playing", doc["state"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Les trois petits cochons", doc["story"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("04A1B2C3", doc["card"].as<const char*>());
  TEST_ASSERT_EQUAL(2, doc["track"].as<int>());
  TEST_ASSERT_EQUAL(5, doc["tracks"].as<int>());
  TEST_ASSERT_EQUAL(312, doc["position"].as<int>());
  TEST_ASSERT_EQUAL(55, doc["volume"].as<int>());
  TEST_ASSERT_EQUAL(20, doc["volume_min"].as<int>());
  TEST_ASSERT_EQUAL(85, doc["volume_max"].as<int>());
  TEST_ASSERT_EQUAL_STRING("ready", doc["sd"].as<const char*>());
  TEST_ASSERT_TRUE(doc["locked"].as<bool>());
  TEST_ASSERT_EQUAL(-61, doc["rssi"].as<int>());

  State unknownSignal = playing();
  unknownSignal.rssi = 0;
  deserializeJson(doc, stateJson(unknownSignal));
  TEST_ASSERT_TRUE(doc["rssi"].isNull());
}

void test_remote_stop_is_published_as_stopped() {
  RuntimeState runtime;
  runtime.state = DeviceState::Playing;
  TEST_ASSERT_EQUAL_STRING("playing", stateName(runtime));
  runtime.state = DeviceState::Paused;
  TEST_ASSERT_EQUAL_STRING("paused", stateName(runtime));
  runtime.stoppedRemotely = true;
  TEST_ASSERT_EQUAL_STRING("stopped", stateName(runtime));
  // Erreur matérielle pendant l'arrêt: c'est elle qui compte.
  runtime.state = DeviceState::Error;
  TEST_ASSERT_EQUAL_STRING("error", stateName(runtime));
}

void test_state_is_published_on_change_or_to_refresh_the_position() {
  const State before = playing();

  // Seule la position (et le signal) a bougé: rafraîchie toutes les 15 s pendant la lecture.
  State moved = before;
  moved.positionSec += 5;
  moved.rssi = -70;
  TEST_ASSERT_FALSE(shouldPublishState(before, moved, kStateRefreshMs - 1));
  TEST_ASSERT_TRUE(shouldPublishState(before, moved, kStateRefreshMs));

  // Sans lecture, rien à rafraîchir.
  State idle = before;
  idle.state = "ready";
  TEST_ASSERT_FALSE(shouldPublishState(idle, idle, 10 * kStateRefreshMs));

  // Tout autre changement part tout de suite.
  State changed = before;
  changed.volume = 60;
  TEST_ASSERT_TRUE(shouldPublishState(before, changed, 0));
  changed = before;
  changed.state = "stopped";
  TEST_ASSERT_TRUE(shouldPublishState(before, changed, 0));
  changed = before;
  changed.locked = false;
  TEST_ASSERT_TRUE(shouldPublishState(before, changed, 0));
  changed = before;
  changed.volumeMax = 90;
  TEST_ASSERT_TRUE(shouldPublishState(before, changed, 0));
}

void test_master_cards_are_never_published() {
  DeviceConfig config;
  MasterCard master;
  master.uid = "04MASTER";
  config.lockCards.push_back(master);

  TEST_ASSERT_TRUE(shouldPublishCard(config, "04A1B2C3"));
  TEST_ASSERT_FALSE(shouldPublishCard(config, ""));
  // Même verrouillage désactivé.
  config.lockEnabled = false;
  TEST_ASSERT_FALSE(shouldPublishCard(config, "04MASTER"));
  config.lockEnabled = true;
  TEST_ASSERT_FALSE(shouldPublishCard(config, "04MASTER"));
}

void test_tags_posted_before_connection_expire() {
  TEST_ASSERT_TRUE(isTagFresh(0));
  TEST_ASSERT_TRUE(isTagFresh(kTagMaxAgeMs));
  TEST_ASSERT_FALSE(isTagFresh(kTagMaxAgeMs + 1));
}

void test_command_topics_are_subscribed() {
  const std::vector<String> topics = commandTopics(kId);
  TEST_ASSERT_EQUAL(2, topics.size());
  TEST_ASSERT_EQUAL_STRING("totobox/totobox_a1b2c3d4e5f6/volume/set", topics[0].c_str());
  TEST_ASSERT_EQUAL_STRING("totobox/totobox_a1b2c3d4e5f6/stop", topics[1].c_str());
}

void test_volume_commands_are_rounded_and_bounded() {
  const String volumeTopic = topic(kId, kVolumeSetLeaf);
  struct Case {
    const char* payload;
    uint8_t volume;
  };
  for (const Case& item : {Case{"55", 55}, Case{"55.0", 55}, Case{" 54.5 ", 55}, Case{"54.4", 54},
                           Case{"0", 20}, Case{"100", 85}, Case{"999", 85}}) {
    const Command command = parseCommand(kId, volumeTopic, item.payload, 20, 85);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(CommandType::Volume), static_cast<int>(command.type), item.payload);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(item.volume, command.volume, item.payload);
  }
  for (const char* bad : {"", "abc", "-5", "55%", "5.", ".5", "1000", "55.0.0", "{\"volume\":55}"}) {
    const Command command = parseCommand(kId, volumeTopic, bad, 20, 85);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(CommandType::None), static_cast<int>(command.type), bad);
  }
}

void test_stop_and_unknown_topics() {
  TEST_ASSERT_EQUAL(static_cast<int>(CommandType::Stop),
                    static_cast<int>(parseCommand(kId, topic(kId, kStopLeaf), "PRESS", 20, 85).type));
  // Commande adressée à une autre boîte, ou sujet que la boîte publie elle-même.
  TEST_ASSERT_EQUAL(static_cast<int>(CommandType::None),
                    static_cast<int>(parseCommand(kId, "totobox/totobox_000000000000/stop", "PRESS", 20, 85).type));
  TEST_ASSERT_EQUAL(static_cast<int>(CommandType::None),
                    static_cast<int>(parseCommand(kId, topic(kId, kStateLeaf), "{}", 20, 85).type));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_plain_hosts_use_the_mqtt_port);
  RUN_TEST(test_explicit_mqtt_port_is_kept);
  RUN_TEST(test_home_assistant_page_address_points_to_its_broker);
  RUN_TEST(test_tls_is_not_supported_yet);
  RUN_TEST(test_unreadable_addresses_are_refused);
  RUN_TEST(test_local_names_are_detected);
  RUN_TEST(test_device_id_comes_from_the_mac_address);
  RUN_TEST(test_discovery_announces_every_entity_of_one_device);
  RUN_TEST(test_entities_are_unique_and_unavailable_while_asleep);
  RUN_TEST(test_volume_slider_is_bounded_by_the_box_limits);
  RUN_TEST(test_cards_go_to_the_tag_scanner_and_removal_trigger);
  RUN_TEST(test_state_json_carries_playback_and_settings);
  RUN_TEST(test_remote_stop_is_published_as_stopped);
  RUN_TEST(test_state_is_published_on_change_or_to_refresh_the_position);
  RUN_TEST(test_master_cards_are_never_published);
  RUN_TEST(test_tags_posted_before_connection_expire);
  RUN_TEST(test_command_topics_are_subscribed);
  RUN_TEST(test_volume_commands_are_rounded_and_bounded);
  RUN_TEST(test_stop_and_unknown_topics);
  return UNITY_END();
}
