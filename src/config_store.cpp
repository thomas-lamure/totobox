// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_store.h"

#include <ArduinoJson.h>

#include "network_names.h"
#include "totobox_config.h"

namespace {

constexpr char kNamespace[] = "totobox";
constexpr char kConfigKey[] = "config";
constexpr char kBindingsKey[] = "bindings";

}  // namespace

bool ConfigStore::ensureAccessPointDefaults(DeviceConfig& config, const uint8_t mac[6]) {
  bool changed = false;
  if (config.apSsid.isEmpty() || config.apSsid == totobox::kLegacyApSsid) {
    config.apSsid = network_names::defaultApSsid(mac);
    changed = true;
  }
  const bool unset = config.apPassword.isEmpty() || config.apPassword == totobox::kLegacyApPassword;
  if (!config.factoryApPassword.isEmpty()) {
    // Le mot de passe de l'étiquette remplace celui tiré par la boîte (provisionnée après coup), pas un choisi.
    if ((unset || config.apPasswordGenerated) && config.apPassword != config.factoryApPassword) {
      config.apPassword = config.factoryApPassword;
      config.apPasswordGenerated = false;
      changed = true;
    }
  } else if (unset) {
    config.apPassword = network_names::generateApPassword();
    config.apPasswordGenerated = true;
    changed = true;
  }
  return changed;
}

bool ConfigStore::begin() {
  return preferences_.begin(kNamespace, false);
}

DeviceConfig ConfigStore::loadConfig() {
  DeviceConfig config;
  const String payload = preferences_.getString(kConfigKey, "");
  JsonDocument doc;
  if (payload.isEmpty() || deserializeJson(doc, payload) != DeserializationError::Ok) {
    // Boîte neuve ou configuration illisible: assistant de première connexion, sauf si des cartes existent.
    config.setupDone = hasStoredBindings();
    return config;
  }

  config.volumeMin = doc["volumeMin"] | config.volumeMin;
  config.volumeMax = doc["volumeMax"] | config.volumeMax;
  config.currentVolume = doc["currentVolume"] | config.currentVolume;
  config.wifiSsid = String(static_cast<const char*>(doc["wifiSsid"] | ""));
  config.wifiPassword = String(static_cast<const char*>(doc["wifiPassword"] | ""));
  config.apSsid = String(static_cast<const char*>(doc["apSsid"] | config.apSsid.c_str()));
  config.apPassword = String(static_cast<const char*>(doc["apPassword"] | config.apPassword.c_str()));
  config.apPasswordGenerated = doc["apPasswordGenerated"] | false;
  // Nom absent (ancienne version) ou invalide: nom par défaut.
  const String hostname = network_names::normalizeHostname(String(static_cast<const char*>(doc["hostname"] | "")));
  if (network_names::isValidHostname(hostname)) {
    config.hostname = hostname;
  }
  config.lockEnabled = doc["lockEnabled"] | false;
  for (JsonObject item : doc["lockCards"].as<JsonArray>()) {
    MasterCard card;
    card.uid = String(static_cast<const char*>(item["uid"] | ""));
    card.label = String(static_cast<const char*>(item["label"] | ""));
    if (!card.uid.isEmpty()) {
      config.lockCards.push_back(card);
    }
  }
  // Ancien format: une seule carte maîtresse.
  const String legacyCardUid = String(static_cast<const char*>(doc["lockCardUid"] | ""));
  if (config.lockCards.empty() && !legacyCardUid.isEmpty()) {
    config.lockCards.push_back({legacyCardUid, ""});
  }
  config.lockPinSalt = String(static_cast<const char*>(doc["lockPinSalt"] | ""));
  config.lockPinHash = String(static_cast<const char*>(doc["lockPinHash"] | ""));
  config.channelSecretKey = String(static_cast<const char*>(doc["channelSecretKey"] | ""));
  // Verrouillage incomplet (ancienne version, NVS corrompue): on ne bloque pas la boîte.
  if (config.lockCards.empty() || config.lockPinHash.isEmpty()) {
    config.lockEnabled = false;
  }
  // Configuration d'avant l'assistant: une boîte déjà utilisée (Wi-Fi, verrouillage ou cartes) ne le revoit pas.
  if (doc["setupDone"].is<bool>()) {
    config.setupDone = doc["setupDone"];
  } else {
    config.setupDone = !config.wifiSsid.isEmpty() || config.lockEnabled || hasStoredBindings();
  }
  return config;
}

bool ConfigStore::hasStoredBindings() {
  const String payload = preferences_.getString(kBindingsKey, "[]");
  JsonDocument doc;
  return deserializeJson(doc, payload) == DeserializationError::Ok && doc.is<JsonArray>() &&
         !readBindings(doc.as<JsonArrayConst>()).empty();
}

bool ConfigStore::saveConfig(const DeviceConfig& config) {
  JsonDocument doc;
  doc["setupDone"] = config.setupDone;
  doc["volumeMin"] = config.volumeMin;
  doc["volumeMax"] = config.volumeMax;
  doc["currentVolume"] = config.currentVolume;
  doc["wifiSsid"] = config.wifiSsid;
  doc["wifiPassword"] = config.wifiPassword;
  doc["apSsid"] = config.apSsid;
  doc["apPassword"] = config.apPassword;
  doc["apPasswordGenerated"] = config.apPasswordGenerated;
  doc["hostname"] = config.hostname;
  doc["lockEnabled"] = config.lockEnabled;
  JsonArray lockCards = doc["lockCards"].to<JsonArray>();
  for (const MasterCard& card : config.lockCards) {
    JsonObject item = lockCards.add<JsonObject>();
    item["uid"] = card.uid;
    item["label"] = card.label;
  }
  doc["lockPinSalt"] = config.lockPinSalt;
  doc["lockPinHash"] = config.lockPinHash;
  doc["channelSecretKey"] = config.channelSecretKey;

  String payload;
  serializeJson(doc, payload);
  return preferences_.putString(kConfigKey, payload) > 0;
}

std::vector<CardBinding> ConfigStore::loadBindings() {
  const String payload = preferences_.getString(kBindingsKey, "[]");
  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok || !doc.is<JsonArray>()) {
    return {};
  }
  return readBindings(doc.as<JsonArrayConst>());
}

bool ConfigStore::saveBindings(const std::vector<CardBinding>& bindings) {
  JsonDocument doc;
  writeBindings(doc.to<JsonArray>(), bindings);
  String payload;
  serializeJson(doc, payload);
  return preferences_.putString(kBindingsKey, payload) > 0;
}

void ConfigStore::writeBindings(JsonArray array, const std::vector<CardBinding>& bindings) {
  for (const CardBinding& binding : bindings) {
    JsonObject item = array.add<JsonObject>();
    item["uid"] = binding.uid;
    item["label"] = binding.label;
    item["mediaPath"] = binding.mediaPath;
    item["mode"] = playbackModeToString(binding.mode);
    item["resumeTrackIndex"] = binding.resumeTrackIndex;
    item["resumeMs"] = binding.resumeMs;
    item["resumeByte"] = binding.resumeByte;
  }
}

std::vector<CardBinding> ConfigStore::readBindings(JsonArrayConst array) {
  std::vector<CardBinding> bindings;
  for (JsonObjectConst item : array) {
    CardBinding binding;
    binding.uid = String(static_cast<const char*>(item["uid"] | ""));
    binding.label = String(static_cast<const char*>(item["label"] | ""));
    binding.mediaPath = String(static_cast<const char*>(item["mediaPath"] | ""));
    binding.mode = playbackModeFromString(String(static_cast<const char*>(item["mode"] | "sd_local")));
    binding.resumeTrackIndex = item["resumeTrackIndex"] | 0;
    binding.resumeMs = item["resumeMs"] | 0;
    binding.resumeByte = item["resumeByte"] | 0;
    if (!binding.uid.isEmpty()) {
      bindings.push_back(binding);
    }
  }
  return bindings;
}
