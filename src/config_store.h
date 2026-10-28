// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>

#include "app_types.h"

class ConfigStore {
 public:
  bool begin();
  DeviceConfig loadConfig();
  bool saveConfig(const DeviceConfig& config);
  // Point d'accès sans réglage ou encore sur les anciennes valeurs fixes: nom tiré de l'adresse MAC. Mot de passe:
  // celui d'usine (config.factoryApPassword) s'il existe, sinon tiré au hasard; un mot de passe choisi dans les
  // réglages est toujours conservé. Renvoie true si la configuration a changé (à enregistrer).
  static bool ensureAccessPointDefaults(DeviceConfig& config, const uint8_t mac[6]);
  std::vector<CardBinding> loadBindings();
  bool saveBindings(const std::vector<CardBinding>& bindings);

  // Format JSON des associations, partagé avec la sauvegarde sur carte SD (card_backup).
  static void writeBindings(JsonArray array, const std::vector<CardBinding>& bindings);
  // Les entrées sans UID sont ignorées.
  static std::vector<CardBinding> readBindings(JsonArrayConst array);

 private:
  bool hasStoredBindings();

  Preferences preferences_;
};
