// SPDX-License-Identifier: GPL-3.0-or-later
#include "network_names.h"

#include <esp_random.h>

#include "totobox_config.h"

namespace network_names {

String normalizeHostname(const String& raw) {
  String name = raw;
  name.trim();
  name.toLowerCase();
  return name;
}

bool isValidHostname(const String& name) {
  if (name.isEmpty() || name.length() > kMaxHostnameLength) {
    return false;
  }
  if (name[0] == '-' || name[name.length() - 1] == '-') {
    return false;
  }
  for (size_t index = 0; index < name.length(); ++index) {
    const char c = name[index];
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

bool isValidSsid(const String& ssid) {
  return !ssid.isEmpty() && ssid.length() <= kMaxSsidLength;
}

bool isValidWifiPassword(const String& password) {
  return password.isEmpty() || isValidApPassword(password);
}

bool isValidApPassword(const String& password) {
  return password.length() >= kMinWpaPasswordLength && password.length() <= kMaxWpaPasswordLength;
}

String defaultApSsid(const uint8_t mac[6]) {
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  return String(totobox::kApSsidPrefix) + suffix;
}

String generateApPassword() {
  static const char kAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
  constexpr size_t kAlphabetSize = sizeof(kAlphabet) - 1;
  constexpr size_t kGroups = 3;
  constexpr size_t kGroupLength = 4;

  uint8_t random[kGroups * kGroupLength];
  esp_fill_random(random, sizeof(random));
  String password;
  for (size_t index = 0; index < sizeof(random); ++index) {
    if (index > 0 && index % kGroupLength == 0) {
      password += '-';
    }
    // 256 n'est pas multiple de 31: léger biais sans conséquence pour un mot de passe de cette longueur.
    password += kAlphabet[random[index] % kAlphabetSize];
  }
  return password;
}

}  // namespace network_names
