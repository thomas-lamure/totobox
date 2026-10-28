// SPDX-License-Identifier: GPL-3.0-or-later
#include "factory_reset.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <nvs_flash.h>

#include "card_backup_sd.h"
#include "config_store.h"
#include "logger.h"
#include "totobox_config.h"

namespace factory_reset {

namespace {

bool jumperClosed() {
  return digitalRead(totobox::kFactoryResetPin) == LOW;
}

void eraseSettings() {
  // nvs_flash_erase() ferme d'abord la NVS si elle est ouverte. La partition "mfg" n'est pas concernée.
  const esp_err_t result = nvs_flash_erase();
  if (result == ESP_OK) {
    logLine("reset", "Configuration effacée");
  } else {
    logLine("reset", "Échec de l'effacement: %s", esp_err_to_name(result));
  }
}

// Remise à zéro d'atelier: elle a lieu avant l'initialisation de la carte SD et de la configuration, on les
// ouvre ici pour sauvegarder les cartes.
void backupCardsBeforeErase() {
  ConfigStore store;
  store.begin();
  const std::vector<CardBinding> bindings = store.loadBindings();
  if (bindings.empty()) {
    logLine("reset", "Aucune carte associée: sauvegarde inutile");
    return;
  }
  SPI.begin(totobox::kSdSck, totobox::kSdMiso, totobox::kSdMosi, totobox::kSdCs);
  if (!SD.begin(totobox::kSdCs)) {
    logLine("reset", "Carte SD absente: cartes non sauvegardées");
    return;
  }
  card_backup::save(SD, bindings, "");
  SD.end();
}

}  // namespace

void checkJumperAtBoot(LedController& leds) {
  pinMode(totobox::kFactoryResetPin, INPUT_PULLUP);
  delay(2);
  if (!jumperClosed()) {
    return;
  }

  logLine("reset", "Pont de remise à zéro détecté: le retirer dans les %lu s pour annuler",
          static_cast<unsigned long>(totobox::kFactoryResetConfirmMs / 1000));
  const uint32_t startedAt = millis();
  while (millis() - startedAt < totobox::kFactoryResetConfirmMs) {
    if (!jumperClosed()) {
      logLine("reset", "Pont retiré: remise à zéro annulée");
      leds.off();
      return;
    }
    const uint8_t lit = 1 + (millis() - startedAt) * totobox::kLedCount / totobox::kFactoryResetConfirmMs;
    leds.fill(CRGB::Red, lit);
    delay(20);
  }

  leds.fill(CRGB::Red, totobox::kLedCount);
  backupCardsBeforeErase();
  eraseSettings();

  // Pont encore en place: redémarrer maintenant recommencerait la remise à zéro.
  logLine("reset", "Retirer le pont pour redémarrer");
  bool on = false;
  while (jumperClosed()) {
    on = !on;
    leds.fill(CRGB::Green, on ? totobox::kLedCount : 0);
    delay(300);
  }
  leds.off();
  esp_restart();
}

void eraseAndRestart(LedController& leds) {
  logLine("reset", "Remise à zéro demandée depuis l'interface");
  leds.fill(CRGB::Red, totobox::kLedCount);
  eraseSettings();
  delay(200);
  leds.off();
  esp_restart();
  for (;;) {
  }
}

}  // namespace factory_reset
