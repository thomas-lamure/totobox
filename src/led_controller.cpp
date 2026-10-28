// SPDX-License-Identifier: GPL-3.0-or-later
#include "led_controller.h"

#include <Arduino.h>

#include <cstring>

#include "led_layout.h"
#include "logger.h"
#include "totobox_config.h"

// ---------- ÉQUIVALENT CRGB ET FASTLED ALLÉGÉ ----------

// Définition des couleurs statiques
const CRGB CRGB::Black = {0, 0, 0};
const CRGB CRGB::Blue = {0, 0, 255};
const CRGB CRGB::Yellow = {255, 255, 0};
const CRGB CRGB::Green = {0, 255, 0};
const CRGB CRGB::Red = {255, 0, 0};
const CRGB CRGB::White = {255, 255, 255};

namespace {
// Envoi de fichiers: cyan, distinct du bleu "prête" et du vert "lecture".
constexpr CRGB kUploadColor = {0, 160, 255};
constexpr uint32_t kUploadStepMs = 150;
}  // namespace

namespace {

constexpr size_t kBitsPerLed = 24;
// Sens de montage de la bande: true si la première LED visible (début de la jauge, départ du chenillard
// "aller") est la dernière LED électrique de la chaîne.
constexpr bool kStripReversed = true;
constexpr int8_t kChaseForwardStep = kStripReversed ? -1 : 1;

// Position visible (0 = début de la bande vue de face) vers indice électrique.
uint8_t physicalIndex(uint8_t visibleIndex)
{
  return led_layout::physicalIndex(visibleIndex, totobox::kLedCount, kStripReversed);
}

void fill_solid(CRGB *targetLeds, int numLeds, CRGB color)
{
  for (int i = 0; i < numLeds; i++)
  {
    targetLeds[i] = color;
  }
}

// Timings WS2812 avec un tick RMT de 100 ns.
void encodeByte(rmt_data_t *out, uint8_t value)
{
  for (int bit = 0; bit < 8; bit++)
  {
    const bool high = value & (1 << (7 - bit));
    out[bit].level0 = 1;
    out[bit].duration0 = high ? 8 : 4;
    out[bit].level1 = 0;
    out[bit].duration1 = high ? 4 : 8;
  }
}

}  // namespace

void LedController::begin() {
  rmt_ = rmtInit(totobox::kLedPin, RMT_TX_MODE, RMT_MEM_128);
  if (rmt_ == nullptr) {
    logLine("led", "Échec de l'initialisation RMT");
    return;
  }
  rmtSetTick(rmt_, 100);
  brightness_ = totobox::kLedBrightness;
  fill_solid(leds_, totobox::kLedCount, CRGB::Black);
  show();
}

// Envoie toute la bande en une seule trame: un neopixelWrite() par LED ne pilote que la première
// si le délai entre deux appels dépasse le temps de reset des WS2812.
void LedController::show()
{
  if (rmt_ == nullptr) {
    return;
  }

  // La plupart des états affichent une couleur fixe: inutile de renvoyer une trame identique à chaque tick.
  if (frameSent_ && sentBrightness_ == brightness_ && memcmp(sentLeds_, leds_, sizeof(leds_)) == 0) {
    return;
  }
  memcpy(sentLeds_, leds_, sizeof(leds_));
  sentBrightness_ = brightness_;
  frameSent_ = true;

  for (size_t i = 0; i < totobox::kLedCount; i++)
  {
    rmt_data_t *out = &rmtBuffer_[i * kBitsPerLed];
    encodeByte(out, (leds_[i].g * brightness_) >> 8);  // Ordre WS2812: vert, rouge, bleu
    encodeByte(out + 8, (leds_[i].r * brightness_) >> 8);
    encodeByte(out + 16, (leds_[i].b * brightness_) >> 8);
  }
  rmtWriteBlocking(rmt_, rmtBuffer_, totobox::kLedCount * kBitsPerLed);
}

void LedController::tick(const RuntimeState& runtime) {
  // Serial.print("-l");
  if (runtime.volumeOverlayActive) {
    showOverlay();
    return;
  }
  if (runtime.uploading || runtime.updating) {
    showUploadFill();
    return;
  }

  switch (runtime.state) {
    case DeviceState::Booting:
      // Serial.print("*");
      showFaded(CRGB::Blue);
      break;
    case DeviceState::Ready:
      // Serial.print(".");
      showChase(CRGB::Blue, ChaseMode::Bounce);
      break;
    case DeviceState::Playing:
      // Serial.print(">");
      showChase(CRGB::Green, ChaseMode::Forward);
      break;
    case DeviceState::Paused:
      // Serial.print("=");
      showFaded(CRGB::Yellow);
      break;
    case DeviceState::UnknownCard:
      // Serial.print("?");
      showFaded(CRGB::Red);
      break;
    case DeviceState::Error:
      // Serial.print("!");
      showErrorBlink();
      break;
  }
}

void LedController::off() {
  fill_solid(leds_, totobox::kLedCount, CRGB::Black);
  frameSent_ = false;  // force l'envoi même si la dernière trame était déjà noire
  show();
}

void LedController::fill(const CRGB& color, uint8_t lit) {
  fill_solid(leds_, totobox::kLedCount, CRGB::Black);
  for (uint8_t index = 0; index < lit && index < totobox::kLedCount; ++index) {
    leds_[physicalIndex(index)] = color;
  }
  show();
}

void LedController::showPercentOverlay(uint8_t percent) {
  overlayPercent_ = constrain(percent, 0, 100);
  overlayColor_ = CRGB::White;
  overlayDirty_ = true;
}

void LedController::showChase(const CRGB &color, ChaseMode mode)
{
  const uint32_t now = millis();
  if (now - lastChaseStepMs_ < 120) {
    return;
  }
  CRGB dimColor = color;
  dimColor.nscale8(20);
  fill_solid(leds_, totobox::kLedCount, dimColor);
  leds_[chaseIndex_] = color;
  show();
  led_layout::Chase chase;
  chase.index = chaseIndex_;
  chase.direction = chaseDirection_;
  const led_layout::Chase next = led_layout::nextChase(chase, totobox::kLedCount, mode != ChaseMode::Forward,
                                                       kChaseForwardStep);
  chaseIndex_ = next.index;
  chaseDirection_ = next.direction;
  lastChaseStepMs_ = now;
}

void LedController::showSolid(const CRGB &color)
{
  fill_solid(leds_, totobox::kLedCount, color);
  show();
}

void LedController::showFaded(const CRGB &color)
{
  CRGB dimColor = color;
  dimColor.nscale8(50);
  fill_solid(leds_, totobox::kLedCount, dimColor);
  show();
}

// La bande se remplit de cyan depuis le début, puis repart de zéro: "des fichiers arrivent".
void LedController::showUploadFill() {
  const uint32_t now = millis();
  if (now - lastUploadStepMs_ < kUploadStepMs) {
    return;
  }
  lastUploadStepMs_ = now;
  CRGB dimColor = kUploadColor;
  dimColor.nscale8(20);
  fill_solid(leds_, totobox::kLedCount, dimColor);
  for (uint8_t index = 0; index < uploadFill_; ++index) {
    leds_[physicalIndex(index)] = kUploadColor;
  }
  show();
  uploadFill_ = (uploadFill_ + 1) % (totobox::kLedCount + 1);
}

void LedController::showErrorBlink() {
  const uint32_t now = millis();
  if (now - lastBlinkMs_ < 250) {
    return;
  }

  blinkOn_ = !blinkOn_;
  fill_solid(leds_, totobox::kLedCount, blinkOn_ ? CRGB::Red : CRGB::Black);
  show();
  lastBlinkMs_ = now;
}

void LedController::showOverlay() {
  if (!overlayDirty_) {
    return;
  }

  fill_solid(leds_, totobox::kLedCount, CRGB::Black);
  const uint8_t lit = led_layout::litCount(overlayPercent_, totobox::kLedCount);
  for (uint8_t index = 0; index < lit; ++index) {
    leds_[physicalIndex(index)] = overlayColor_;
  }
  show();
  overlayDirty_ = false;
}
