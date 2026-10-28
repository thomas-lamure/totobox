// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// NVS simulée en mémoire pour les tests sur ordinateur: toutes les instances partagent le même stockage,
// comme sur la carte. nativeTestClearPreferences() remet tout à zéro entre deux tests.

#include <map>
#include <string>

#include "Arduino.h"

inline std::map<std::string, std::string>& nativeTestPreferencesStorage() {
  static std::map<std::string, std::string> storage;
  return storage;
}

inline void nativeTestClearPreferences() {
  nativeTestPreferencesStorage().clear();
}

class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false) {
    (void)readOnly;
    namespace_ = name != nullptr ? name : "";
    return true;
  }

  String getString(const char* key, const String& defaultValue = String()) {
    const auto found = nativeTestPreferencesStorage().find(fullKey(key));
    return found == nativeTestPreferencesStorage().end() ? defaultValue : String(found->second);
  }

  size_t putString(const char* key, const String& value) {
    nativeTestPreferencesStorage()[fullKey(key)] = value.c_str();
    return value.length();
  }

 private:
  std::string fullKey(const char* key) const { return namespace_ + "/" + key; }

  std::string namespace_;
};
