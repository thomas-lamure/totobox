// SPDX-License-Identifier: GPL-3.0-or-later
#include "access_lock.h"

#include <esp_random.h>
#include <mbedtls/sha256.h>

namespace access_lock {

namespace {

constexpr size_t kSaltBytes = 16;

String toHex(const uint8_t* data, size_t length) {
  static const char kDigits[] = "0123456789abcdef";
  String out;
  out.reserve(length * 2);
  for (size_t index = 0; index < length; ++index) {
    out += kDigits[data[index] >> 4];
    out += kDigits[data[index] & 0x0F];
  }
  return out;
}

String hashPin(const String& salt, const String& pin) {
  const String input = salt + ":" + pin;
  uint8_t digest[32];
  mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), digest, 0);
  return toHex(digest, sizeof(digest));
}

// Comparaison en temps constant pour ne pas révéler la position de la première différence.
bool equalsConstantTime(const String& left, const String& right) {
  if (left.length() != right.length()) {
    return false;
  }
  uint8_t diff = 0;
  for (size_t index = 0; index < left.length(); ++index) {
    diff |= static_cast<uint8_t>(left[index] ^ right[index]);
  }
  return diff == 0;
}

}  // namespace

bool isLocked(const DeviceConfig& config, const RuntimeState& runtime) {
  return config.lockEnabled && !runtime.unlocked;
}

bool isValidPin(const String& pin) {
  if (pin.length() != kPinLength) {
    return false;
  }
  for (size_t index = 0; index < pin.length(); ++index) {
    if (!isDigit(pin[index])) {
      return false;
    }
  }
  return true;
}

const MasterCard* findCard(const DeviceConfig& config, const String& uid) {
  if (uid.isEmpty()) {
    return nullptr;
  }
  for (const MasterCard& card : config.lockCards) {
    if (card.uid == uid) {
      return &card;
    }
  }
  return nullptr;
}

bool isLockCard(const DeviceConfig& config, const String& uid) {
  return config.lockEnabled && findCard(config, uid) != nullptr;
}

void setPin(DeviceConfig& config, const String& pin) {
  uint8_t salt[kSaltBytes];
  esp_fill_random(salt, sizeof(salt));
  config.lockPinSalt = toHex(salt, sizeof(salt));
  config.lockPinHash = hashPin(config.lockPinSalt, pin);
}

void enable(DeviceConfig& config, const MasterCard& card, const String& pin) {
  config.lockEnabled = true;
  config.lockCards.clear();
  config.lockCards.push_back(card);
  setPin(config, pin);
}

void disable(DeviceConfig& config) {
  config.lockEnabled = false;
  config.lockCards.clear();
  config.lockPinSalt = "";
  config.lockPinHash = "";
}

bool addCard(DeviceConfig& config, const MasterCard& card) {
  if (card.uid.isEmpty() || findCard(config, card.uid) != nullptr || config.lockCards.size() >= kMaxCards) {
    return false;
  }
  config.lockCards.push_back(card);
  return true;
}

bool removeCard(DeviceConfig& config, const String& uid) {
  if (config.lockCards.size() <= 1) {
    return false;
  }
  for (size_t index = 0; index < config.lockCards.size(); ++index) {
    if (config.lockCards[index].uid == uid) {
      config.lockCards.erase(config.lockCards.begin() + index);
      return true;
    }
  }
  return false;
}

bool checkPin(const DeviceConfig& config, const String& pin) {
  if (!config.lockEnabled || !isValidPin(pin) || config.lockPinHash.isEmpty()) {
    return false;
  }
  return equalsConstantTime(hashPin(config.lockPinSalt, pin), config.lockPinHash);
}

uint32_t PinAttemptLimiter::retryInSec(uint32_t nowMs) const {
  if (blockedUntilMs_ == 0 || static_cast<int32_t>(blockedUntilMs_ - nowMs) <= 0) {
    return 0;
  }
  return (blockedUntilMs_ - nowMs + 999) / 1000;
}

uint8_t PinAttemptLimiter::recordFailure(uint32_t nowMs) {
  ++failures_;
  if (failures_ >= kMaxFailures) {
    failures_ = 0;
    blockedUntilMs_ = nowMs + kBlockMs;
    return 0;
  }
  return kMaxFailures - failures_;
}

void PinAttemptLimiter::reset() {
  failures_ = 0;
  blockedUntilMs_ = 0;
}

}  // namespace access_lock
