// SPDX-License-Identifier: GPL-3.0-or-later
#include "card_backup.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "access_lock.h"
#include "config_store.h"

namespace card_backup {

String serialize(const std::vector<CardBinding>& bindings, const String& savedAt) {
  JsonDocument doc;
  doc["format"] = kFormat;
  doc["version"] = kVersion;
  doc["savedAt"] = savedAt;
  ConfigStore::writeBindings(doc["bindings"].to<JsonArray>(), bindings);
  String payload;
  serializeJsonPretty(doc, payload);
  return payload;
}

bool parse(const String& payload, Backup& out) {
  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    return false;
  }
  if (String(static_cast<const char*>(doc["format"] | "")) != kFormat || (doc["version"] | 0) != kVersion ||
      !doc["bindings"].is<JsonArrayConst>()) {
    return false;
  }
  out.bindings = ConfigStore::readBindings(doc["bindings"].as<JsonArrayConst>());
  out.savedAt = String(static_cast<const char*>(doc["savedAt"] | ""));
  return true;
}

size_t dropLockCards(std::vector<CardBinding>& bindings, const DeviceConfig& config) {
  const size_t before = bindings.size();
  bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                [&config](const CardBinding& binding) {
                                  return access_lock::isLockCard(config, binding.uid);
                                }),
                 bindings.end());
  return before - bindings.size();
}

}  // namespace card_backup
