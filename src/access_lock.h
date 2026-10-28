// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include "app_types.h"

// Verrouillage des réglages: une carte maîtresse posée sur la boîte, ou un code de secours à 6 chiffres,
// déverrouille jusqu'à un appui sur "Verrouiller" ou un redémarrage (l'état déverrouillé n'est qu'en RAM).
// Le code n'est jamais stocké en clair: seulement un sel aléatoire et l'empreinte SHA-256 sel+code.
namespace access_lock {

constexpr size_t kPinLength = 6;
constexpr size_t kMaxCards = 8;

bool isLocked(const DeviceConfig& config, const RuntimeState& runtime);
bool isValidPin(const String& pin);
bool isLockCard(const DeviceConfig& config, const String& uid);
const MasterCard* findCard(const DeviceConfig& config, const String& uid);

// Active le verrouillage avec une première carte maîtresse et le code (le code doit être valide).
void enable(DeviceConfig& config, const MasterCard& card, const String& pin);
void disable(DeviceConfig& config);
void setPin(DeviceConfig& config, const String& pin);
bool checkPin(const DeviceConfig& config, const String& pin);
bool addCard(DeviceConfig& config, const MasterCard& card);
// Refuse de retirer la dernière carte: il faut alors désactiver le verrouillage.
bool removeCard(DeviceConfig& config, const String& uid);

// Limite les essais de code: après kMaxFailures échecs, attente de kBlockMs avant un nouvel essai.
class PinAttemptLimiter {
 public:
  static constexpr uint8_t kMaxFailures = 5;
  static constexpr uint32_t kBlockMs = 60000;

  // Secondes restantes avant de pouvoir réessayer, 0 si autorisé.
  uint32_t retryInSec(uint32_t nowMs) const;
  // Enregistre un échec, renvoie le nombre d'essais restants avant blocage.
  uint8_t recordFailure(uint32_t nowMs);
  void reset();

 private:
  uint8_t failures_ = 0;
  uint32_t blockedUntilMs_ = 0;
};

}  // namespace access_lock
