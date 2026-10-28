// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include "app_types.h"
#include "nfc_service.h"

// Mise en veille profonde après une période d'inactivité, réveil par carte NFC (IRQ du PN532) ou par
// appui sur l'encodeur. Le réveil redémarre la carte (setup()); les signets et réglages sont en NVS.
//
// Carte laissée sur la boîte au moment de dormir: le PN532 la redétecterait aussitôt. On n'arme alors pas
// le réveil NFC mais un minuteur: la boîte vérifie brièvement la carte toutes les kHeldCardCheckMs et se
// rendort tant que c'est la même; une fois retirée, le réveil NFC est réarmé.
class PowerManager {
 public:
  enum class WakeReason { PowerOn, Card, Button, Timer };

  // Tout début de setup(): libère les broches maintenues pendant le sommeil et identifie le réveil.
  // Sur un réveil de vérification où rien n'a changé, rendort la carte et ne revient pas.
  WakeReason begin(NFCService& nfc);

  // Toute interaction (lecture, carte, molette, requête web hors statut) repousse la mise en veille.
  void noteActivity(RuntimeState& runtime) const;
  bool isIdle(const RuntimeState& runtime) const;

  // Coupe le Wi-Fi, fige les broches sensibles, arme les réveils et dort. Ne revient pas.
  [[noreturn]] void sleep(NFCService& nfc, const String& cardOnReader);

 private:
  void releaseSleepHolds() const;
  void holdPinsForSleep() const;
};
