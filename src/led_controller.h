// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include "app_types.h"
#include "totobox_config.h"

struct CRGB
{
  uint8_t r;
  uint8_t g;
  uint8_t b;

  // Couleurs prédéfinies
  static const CRGB Black;
  static const CRGB Blue;
  static const CRGB Yellow;
  static const CRGB Green;
  static const CRGB Red;
  static const CRGB White;

  // Permet d'ajuster la luminosité (équivalent de nscale8)
  void nscale8(uint8_t scaledown)
  {
    r = (r * scaledown) >> 8;
    g = (g * scaledown) >> 8;
    b = (b * scaledown) >> 8;
  }
};

enum class ChaseMode {
  Bounce,   // aller-retour
  Forward,  // aller uniquement, en boucle
};

class LedController {
 public:
  void begin();
  void tick(const RuntimeState& runtime);
  void showPercentOverlay(uint8_t percent);
  // Éteint toute la bande (avant la veille).
  void off();
  // Allume les `lit` premières LEDs (dans le sens visible de la bande) d'une couleur, éteint les autres. Pour les
  // séquences hors de loop() (remise à zéro).
  void fill(const CRGB& color, uint8_t lit);

 private:
   void showChase(const CRGB &color, ChaseMode mode);
   void showSolid(const CRGB &color);
   void showFaded(const CRGB &color);
   void showErrorBlink();
   void showUploadFill();
   void showOverlay();
   void show();

   rmt_obj_t *rmt_ = nullptr;
   rmt_data_t rmtBuffer_[totobox::kLedCount * 24];
   CRGB leds_[totobox::kLedCount];
   uint8_t brightness_ = 255; // 0-255
   CRGB sentLeds_[totobox::kLedCount];
   uint8_t sentBrightness_ = 0;
   bool frameSent_ = false;
   uint32_t lastChaseStepMs_ = 0;
   uint8_t chaseIndex_ = 0;
   int8_t chaseDirection_ = 1;
   uint8_t overlayPercent_ = 0;
   CRGB overlayColor_ = CRGB::White;
   bool overlayDirty_ = false;
   bool blinkOn_ = false;
   uint32_t lastBlinkMs_ = 0;
   uint8_t uploadFill_ = 0;
   uint32_t lastUploadStepMs_ = 0;
};
