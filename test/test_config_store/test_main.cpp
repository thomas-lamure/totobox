// SPDX-License-Identifier: GPL-3.0-or-later
// Tests de ConfigStore: sérialisation JSON de la configuration et du catalogue des cartes en NVS.

#include <unity.h>

#include <Preferences.h>

#include "config_store.h"

namespace {

// Écrit directement une valeur brute dans la NVS simulée (anciens formats, données corrompues).
void writeRaw(const char* key, const char* json) {
  Preferences preferences;
  preferences.begin("totobox", false);
  preferences.putString(key, json);
}

}  // namespace

void setUp() {
  nativeTestClearPreferences();
}
void tearDown() {}

void test_empty_storage_gives_defaults() {
  ConfigStore store;
  store.begin();
  const DeviceConfig config = store.loadConfig();
  const DeviceConfig defaults;
  TEST_ASSERT_EQUAL_UINT8(defaults.volumeMin, config.volumeMin);
  TEST_ASSERT_EQUAL_UINT8(defaults.volumeMax, config.volumeMax);
  TEST_ASSERT_EQUAL_UINT8(defaults.currentVolume, config.currentVolume);
  TEST_ASSERT_TRUE(config.apSsid.isEmpty());  // rempli au démarrage par ensureAccessPointDefaults()
  TEST_ASSERT_TRUE(config.apPassword.isEmpty());
  TEST_ASSERT_EQUAL_STRING("totobox", config.hostname.c_str());
  TEST_ASSERT_FALSE(config.lockEnabled);
  TEST_ASSERT_EQUAL_UINT32(0, store.loadBindings().size());
}

void test_config_round_trip() {
  DeviceConfig saved;
  saved.volumeMin = 10;
  saved.volumeMax = 70;
  saved.currentVolume = 42;
  saved.wifiSsid = "Maison";
  saved.wifiPassword = "secret";
  saved.apSsid = "TotoBox-Test";
  saved.apPassword = "motdepasse";
  saved.apPasswordGenerated = true;
  saved.hostname = "totobox-salon";
  saved.lockEnabled = true;
  saved.lockCards.push_back({"04AAAA0001", "Maman"});
  saved.lockCards.push_back({"04BBBB0002", "Papa"});
  saved.lockPinSalt = "00112233445566778899aabbccddeeff";
  saved.lockPinHash = "abcdef";
  saved.channelSecretKey = "00ff";

  ConfigStore store;
  store.begin();
  TEST_ASSERT_TRUE(store.saveConfig(saved));

  ConfigStore reloaded;
  reloaded.begin();
  const DeviceConfig config = reloaded.loadConfig();
  TEST_ASSERT_EQUAL_UINT8(10, config.volumeMin);
  TEST_ASSERT_EQUAL_UINT8(70, config.volumeMax);
  TEST_ASSERT_EQUAL_UINT8(42, config.currentVolume);
  TEST_ASSERT_EQUAL_STRING("Maison", config.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("secret", config.wifiPassword.c_str());
  TEST_ASSERT_EQUAL_STRING("TotoBox-Test", config.apSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("motdepasse", config.apPassword.c_str());
  TEST_ASSERT_TRUE(config.apPasswordGenerated);
  TEST_ASSERT_EQUAL_STRING("totobox-salon", config.hostname.c_str());
  TEST_ASSERT_TRUE(config.lockEnabled);
  TEST_ASSERT_EQUAL_UINT32(2, config.lockCards.size());
  TEST_ASSERT_EQUAL_STRING("04BBBB0002", config.lockCards[1].uid.c_str());
  TEST_ASSERT_EQUAL_STRING("Papa", config.lockCards[1].label.c_str());
  TEST_ASSERT_EQUAL_STRING("00112233445566778899aabbccddeeff", config.lockPinSalt.c_str());
  TEST_ASSERT_EQUAL_STRING("abcdef", config.lockPinHash.c_str());
  TEST_ASSERT_EQUAL_STRING("00ff", config.channelSecretKey.c_str());
}

void test_legacy_single_master_card_is_migrated() {
  writeRaw("config", R"({"lockEnabled":true,"lockCardUid":"04AAAA0001","lockPinSalt":"aa","lockPinHash":"bb"})");
  ConfigStore store;
  store.begin();
  const DeviceConfig config = store.loadConfig();
  TEST_ASSERT_TRUE(config.lockEnabled);
  TEST_ASSERT_EQUAL_UINT32(1, config.lockCards.size());
  TEST_ASSERT_EQUAL_STRING("04AAAA0001", config.lockCards[0].uid.c_str());
}

void test_incomplete_lock_is_disabled() {
  // Verrouillage sans empreinte de code: on ne bloque pas la boîte.
  writeRaw("config", R"({"lockEnabled":true,"lockCards":[{"uid":"04AAAA0001","label":""}]})");
  ConfigStore store;
  store.begin();
  TEST_ASSERT_FALSE(store.loadConfig().lockEnabled);

  // Verrouillage sans carte maîtresse.
  writeRaw("config", R"({"lockEnabled":true,"lockPinSalt":"aa","lockPinHash":"bb"})");
  TEST_ASSERT_FALSE(store.loadConfig().lockEnabled);
}

void test_corrupted_config_gives_defaults() {
  writeRaw("config", "{pas du json");
  ConfigStore store;
  store.begin();
  const DeviceConfig config = store.loadConfig();
  TEST_ASSERT_EQUAL_UINT8(DeviceConfig().currentVolume, config.currentVolume);
  TEST_ASSERT_FALSE(config.lockEnabled);
}

void test_hostname_normalized_or_defaulted_on_load() {
  ConfigStore store;
  store.begin();

  writeRaw("config", R"({"hostname":"  TotoBox-Chambre "})");
  TEST_ASSERT_EQUAL_STRING("totobox-chambre", store.loadConfig().hostname.c_str());

  // Nom invalide (édité à la main, ancienne version...): nom par défaut plutôt qu'un mDNS cassé.
  writeRaw("config", R"({"hostname":"toto box"})");
  TEST_ASSERT_EQUAL_STRING("totobox", store.loadConfig().hostname.c_str());

  // Ancienne configuration sans nom.
  writeRaw("config", R"({"volumeMin":10})");
  TEST_ASSERT_EQUAL_STRING("totobox", store.loadConfig().hostname.c_str());
}

void test_access_point_defaults_from_mac_and_random() {
  const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0xAB, 0x1A, 0x2B};
  DeviceConfig config;
  TEST_ASSERT_TRUE(ConfigStore::ensureAccessPointDefaults(config, mac));
  TEST_ASSERT_EQUAL_STRING("TotoBox-1A2B", config.apSsid.c_str());
  TEST_ASSERT_EQUAL_UINT32(14, config.apPassword.length());
  TEST_ASSERT_TRUE(config.apPasswordGenerated);
  // Déjà rempli: rien ne change d'un démarrage à l'autre.
  const String password = config.apPassword;
  TEST_ASSERT_FALSE(ConfigStore::ensureAccessPointDefaults(config, mac));
  TEST_ASSERT_EQUAL_STRING(password.c_str(), config.apPassword.c_str());
}

void test_legacy_access_point_values_are_replaced_custom_ones_kept() {
  const uint8_t mac[6] = {0, 0, 0, 0, 0xFF, 0x01};
  DeviceConfig legacy;
  legacy.apSsid = "TotoBox-Setup";
  legacy.apPassword = "totobox123";
  TEST_ASSERT_TRUE(ConfigStore::ensureAccessPointDefaults(legacy, mac));
  TEST_ASSERT_EQUAL_STRING("TotoBox-FF01", legacy.apSsid.c_str());
  TEST_ASSERT_TRUE(legacy.apPassword != "totobox123");

  DeviceConfig custom;
  custom.apSsid = "Chambre";
  custom.apPassword = "monmotdepasse";
  TEST_ASSERT_FALSE(ConfigStore::ensureAccessPointDefaults(custom, mac));
  TEST_ASSERT_EQUAL_STRING("Chambre", custom.apSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("monmotdepasse", custom.apPassword.c_str());
  TEST_ASSERT_FALSE(custom.apPasswordGenerated);
}

void test_factory_password_wins_over_generated_not_custom() {
  const uint8_t mac[6] = {0, 0, 0, 0, 0x12, 0x34};

  // Boîte neuve provisionnée: mot de passe de l'étiquette.
  DeviceConfig fresh;
  fresh.factoryApPassword = "k7mp-3xqa-9fzt";
  TEST_ASSERT_TRUE(ConfigStore::ensureAccessPointDefaults(fresh, mac));
  TEST_ASSERT_EQUAL_STRING("k7mp-3xqa-9fzt", fresh.apPassword.c_str());
  TEST_ASSERT_FALSE(fresh.apPasswordGenerated);
  TEST_ASSERT_FALSE(ConfigStore::ensureAccessPointDefaults(fresh, mac));

  // Mot de passe tiré par la boîte avant le provisionnement: remplacé par celui de l'étiquette.
  DeviceConfig drawn;
  drawn.apSsid = "TotoBox-1234";
  drawn.apPassword = "aaaa-bbbb-cccc";
  drawn.apPasswordGenerated = true;
  drawn.factoryApPassword = "k7mp-3xqa-9fzt";
  TEST_ASSERT_TRUE(ConfigStore::ensureAccessPointDefaults(drawn, mac));
  TEST_ASSERT_EQUAL_STRING("k7mp-3xqa-9fzt", drawn.apPassword.c_str());

  // Mot de passe choisi dans les réglages: conservé.
  DeviceConfig custom;
  custom.apSsid = "TotoBox-1234";
  custom.apPassword = "monmotdepasse";
  custom.factoryApPassword = "k7mp-3xqa-9fzt";
  TEST_ASSERT_FALSE(ConfigStore::ensureAccessPointDefaults(custom, mac));
  TEST_ASSERT_EQUAL_STRING("monmotdepasse", custom.apPassword.c_str());
}

void test_setup_flag_for_new_and_existing_boxes() {
  ConfigStore store;
  store.begin();
  // Boîte neuve: assistant.
  TEST_ASSERT_FALSE(store.loadConfig().setupDone);

  // Enregistré explicitement.
  DeviceConfig done;
  done.setupDone = true;
  TEST_ASSERT_TRUE(store.saveConfig(done));
  TEST_ASSERT_TRUE(store.loadConfig().setupDone);
  DeviceConfig pending;
  TEST_ASSERT_TRUE(store.saveConfig(pending));
  TEST_ASSERT_FALSE(store.loadConfig().setupDone);

  // Configuration d'avant l'assistant: déjà utilisée si Wi-Fi, verrouillage ou cartes.
  writeRaw("config", R"({"volumeMin":10})");
  TEST_ASSERT_FALSE(store.loadConfig().setupDone);
  writeRaw("config", R"({"volumeMin":10,"wifiSsid":"Maison"})");
  TEST_ASSERT_TRUE(store.loadConfig().setupDone);
  writeRaw("config", R"({"volumeMin":10})");
  writeRaw("bindings", R"([{"uid":"AB12","mediaPath":"/content/b"}])");
  TEST_ASSERT_TRUE(store.loadConfig().setupDone);
}

void test_bindings_round_trip() {
  std::vector<CardBinding> saved(2);
  saved[0].uid = "CACB3C7F";
  saved[0].label = "Popi";
  saved[0].mediaPath = "/content/music/Popi";
  saved[0].resumeTrackIndex = 2;
  saved[0].resumeMs = 76000;
  saved[0].resumeByte = 1234567;
  saved[1].uid = "04C0FFEE99";
  saved[1].label = "Chanson";
  saved[1].mediaPath = "/content/music/chanson.mp3";
  saved[1].mode = PlaybackMode::HomeAssistant;

  ConfigStore store;
  store.begin();
  TEST_ASSERT_TRUE(store.saveBindings(saved));

  const std::vector<CardBinding> loaded = store.loadBindings();
  TEST_ASSERT_EQUAL_UINT32(2, loaded.size());
  TEST_ASSERT_EQUAL_STRING("CACB3C7F", loaded[0].uid.c_str());
  TEST_ASSERT_EQUAL_STRING("Popi", loaded[0].label.c_str());
  TEST_ASSERT_EQUAL_STRING("/content/music/Popi", loaded[0].mediaPath.c_str());
  TEST_ASSERT_EQUAL_UINT16(2, loaded[0].resumeTrackIndex);
  TEST_ASSERT_EQUAL_UINT32(76000, loaded[0].resumeMs);
  TEST_ASSERT_EQUAL_UINT32(1234567, loaded[0].resumeByte);
  TEST_ASSERT_EQUAL_UINT32(0, loaded[1].resumeByte);
  TEST_ASSERT_TRUE(loaded[0].mode == PlaybackMode::SdLocal);
  TEST_ASSERT_EQUAL_STRING("/content/music/chanson.mp3", loaded[1].mediaPath.c_str());
  TEST_ASSERT_TRUE(loaded[1].mode == PlaybackMode::HomeAssistant);
}

void test_bindings_without_uid_are_skipped() {
  writeRaw("bindings", R"([{"uid":"","mediaPath":"/content/a"},{"uid":"AB12","mediaPath":"/content/b"}])");
  ConfigStore store;
  store.begin();
  const std::vector<CardBinding> loaded = store.loadBindings();
  TEST_ASSERT_EQUAL_UINT32(1, loaded.size());
  TEST_ASSERT_EQUAL_STRING("AB12", loaded[0].uid.c_str());
}

void test_bookmark_without_byte_is_still_loaded() {
  // Marque-page enregistré avant la reprise par octet: les millisecondes servent de secours.
  writeRaw("bindings", R"([{"uid":"AB12","mediaPath":"/content/b","resumeTrackIndex":1,"resumeMs":5000}])");
  ConfigStore store;
  store.begin();
  const std::vector<CardBinding> loaded = store.loadBindings();
  TEST_ASSERT_EQUAL_UINT32(1, loaded.size());
  TEST_ASSERT_EQUAL_UINT16(1, loaded[0].resumeTrackIndex);
  TEST_ASSERT_EQUAL_UINT32(5000, loaded[0].resumeMs);
  TEST_ASSERT_EQUAL_UINT32(0, loaded[0].resumeByte);
}

void test_corrupted_bindings_give_empty_catalog() {
  writeRaw("bindings", R"({"pas":"une liste"})");
  ConfigStore store;
  store.begin();
  TEST_ASSERT_EQUAL_UINT32(0, store.loadBindings().size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_storage_gives_defaults);
  RUN_TEST(test_config_round_trip);
  RUN_TEST(test_legacy_single_master_card_is_migrated);
  RUN_TEST(test_incomplete_lock_is_disabled);
  RUN_TEST(test_corrupted_config_gives_defaults);
  RUN_TEST(test_hostname_normalized_or_defaulted_on_load);
  RUN_TEST(test_access_point_defaults_from_mac_and_random);
  RUN_TEST(test_legacy_access_point_values_are_replaced_custom_ones_kept);
  RUN_TEST(test_factory_password_wins_over_generated_not_custom);
  RUN_TEST(test_setup_flag_for_new_and_existing_boxes);
  RUN_TEST(test_bindings_round_trip);
  RUN_TEST(test_bindings_without_uid_are_skipped);
  RUN_TEST(test_bookmark_without_byte_is_still_loaded);
  RUN_TEST(test_corrupted_bindings_give_empty_catalog);
  return UNITY_END();
}
