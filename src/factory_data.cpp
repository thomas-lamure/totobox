// SPDX-License-Identifier: GPL-3.0-or-later
#include "factory_data.h"

#include <ArduinoJson.h>

#include <cstring>

#include "network_names.h"

namespace factory_data {

namespace {

constexpr char kMagic[4] = {'T', 'B', 'X', 'F'};

uint32_t readLe32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

bool parse(const uint8_t* data, size_t size, FactoryData& out) {
  if (data == nullptr || size < kHeaderSize || memcmp(data, kMagic, sizeof(kMagic)) != 0 || data[4] != kVersion) {
    return false;
  }
  const uint32_t length = readLe32(data + 8);
  if (length == 0 || length > size - kHeaderSize) {
    return false;
  }
  const uint8_t* payload = data + kHeaderSize;
  if (crc32(payload, length) != readLe32(data + 12)) {
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, reinterpret_cast<const char*>(payload), length) != DeserializationError::Ok) {
    return false;
  }
  const String apPassword = String(static_cast<const char*>(doc["apPassword"] | ""));
  // Mot de passe inutilisable par le point d'accès: on l'ignore plutôt que d'empêcher la boîte de démarrer.
  if (!network_names::isValidApPassword(apPassword)) {
    return false;
  }
  out.apPassword = apPassword;
  return true;
}

}  // namespace factory_data
