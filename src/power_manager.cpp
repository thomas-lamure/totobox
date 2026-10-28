// SPDX-License-Identifier: GPL-3.0-or-later
#include "power_manager.h"

#include <WiFi.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "logger.h"
#include "totobox_config.h"

namespace {

// Survit au sommeil profond: UID de la carte restée sur la boîte (vide si aucune).
RTC_DATA_ATTR char rtcCardOnReader[24] = "";

struct HeldPin {
  uint8_t pin;
  int level;
};

// Broches figées pendant le sommeil pour éviter qu'elles flottent:
// LEDs éteintes, ampli sans horloge (pas de bruit), PN532 et SD désélectionnés.
const HeldPin kHeldPins[] = {
    {totobox::kLedPin, LOW},
    {totobox::kI2sBclk, LOW},
    {totobox::kI2sLrc, LOW},
    {totobox::kI2sDout, LOW},
    {totobox::kPn532Ss, HIGH},
    {totobox::kSdCs, HIGH},
};

const char* wakeReasonName(PowerManager::WakeReason reason) {
  switch (reason) {
    case PowerManager::WakeReason::PowerOn:
      return "mise sous tension";
    case PowerManager::WakeReason::Card:
      return "carte NFC";
    case PowerManager::WakeReason::Button:
      return "bouton de l'encodeur";
    case PowerManager::WakeReason::Timer:
      return "vérification de la carte posée";
  }
  return "?";
}

}  // namespace

PowerManager::WakeReason PowerManager::begin(NFCService& nfc) {
  releaseSleepHolds();

  WakeReason reason = WakeReason::PowerOn;
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT1: {
      const uint64_t pins = esp_sleep_get_ext1_wakeup_status();
      reason = (pins & (1ULL << totobox::kEncoderSw)) ? WakeReason::Button : WakeReason::Card;
      break;
    }
    case ESP_SLEEP_WAKEUP_TIMER:
      reason = WakeReason::Timer;
      break;
    default:
      rtcCardOnReader[0] = '\0';
      break;
  }

  if (reason == WakeReason::Timer) {
    // Réveil de vérification: on ne démarre que le NFC, sans LEDs ni Wi-Fi.
    const String held = rtcCardOnReader;
    String present;
    if (nfc.begin()) {
      for (int attempt = 0; attempt < 3 && present.isEmpty(); ++attempt) {
        nfc.detectOnce(present, 150);
      }
    }
    if (present == held) {
      sleep(nfc, held);  // même carte toujours posée
    }
    if (present.isEmpty()) {
      logLine("power", "Carte %s retirée pendant la veille: réveil NFC réarmé", held.c_str());
      sleep(nfc, "");
    }
    logLine("power", "Nouvelle carte %s posée pendant la veille", present.c_str());
    reason = WakeReason::Card;
  }

  rtcCardOnReader[0] = '\0';
  logLine("power", "Démarrage: %s", wakeReasonName(reason));
  return reason;
}

void PowerManager::noteActivity(RuntimeState& runtime) const {
  runtime.lastActivityMs = millis();
}

bool PowerManager::isIdle(const RuntimeState& runtime) const {
  return totobox::kIdleSleepMs > 0 && millis() - runtime.lastActivityMs >= totobox::kIdleSleepMs;
}

void PowerManager::sleep(NFCService& nfc, const String& cardOnReader) {
  uint64_t wakePins = 1ULL << totobox::kEncoderSw;
  if (cardOnReader.isEmpty()) {
    // Le PN532 cherche une carte et baissera son IRQ à son arrivée.
    if (nfc.armWakeOnCard()) {
      wakePins |= 1ULL << totobox::kPn532Irq;
    } else {
      logLine("power", "PN532 non armé: réveil par l'encodeur uniquement");
    }
    rtcCardOnReader[0] = '\0';
  } else {
    strlcpy(rtcCardOnReader, cardOnReader.c_str(), sizeof(rtcCardOnReader));
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(totobox::kHeldCardCheckMs) * 1000ULL);
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Le bouton du KY-040 n'a souvent pas de résistance de tirage: on garde celle de l'ESP32 pendant le sommeil.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en(static_cast<gpio_num_t>(totobox::kEncoderSw));
  rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(totobox::kEncoderSw));
  esp_sleep_enable_ext1_wakeup(wakePins, ESP_EXT1_WAKEUP_ANY_LOW);

  holdPinsForSleep();
  Serial.flush();
  esp_deep_sleep_start();
}

void PowerManager::holdPinsForSleep() const {
  for (const HeldPin& held : kHeldPins) {
    pinMode(held.pin, OUTPUT);
    digitalWrite(held.pin, held.level);
    gpio_hold_en(static_cast<gpio_num_t>(held.pin));
  }
  gpio_deep_sleep_hold_en();
}

void PowerManager::releaseSleepHolds() const {
  gpio_deep_sleep_hold_dis();
  for (const HeldPin& held : kHeldPins) {
    gpio_hold_dis(static_cast<gpio_num_t>(held.pin));
  }
  // Les broches de réveil restent en mode RTC après le sommeil: on les rend au GPIO numérique.
  rtc_gpio_deinit(static_cast<gpio_num_t>(totobox::kEncoderSw));
  rtc_gpio_deinit(static_cast<gpio_num_t>(totobox::kPn532Irq));
}
