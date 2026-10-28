// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include "encoder_decoder.h"

// Encodeur KY-040: rotation lue par interruption (CLK et DT), bouton lu dans loop().
// Pendant la lecture, la tâche audio (même cœur, priorité plus haute) espace les tours de loop() de plusieurs ms:
// une lecture en scrutation y perdait des crans, voire les lisait à l'envers. Les fronts sont donc suivis dans
// une routine d'interruption (décodage dans encoder_decoder.h, testé sur ordinateur), loop() ne fait que relever
// les crans comptés.
class VolumeKnob {
 public:
  void begin();
  void tick();
  // Crans tournés depuis le dernier appel: positif quand on monte le son (sens horaire, kEncoderReversed compris).
  int8_t takeDelta();
  bool wasButtonPressed();

 private:
  static void IRAM_ATTR onEdge(void* arg);
  void IRAM_ATTR handleEdge();

  // Décodage (modifié seulement par l'interruption, sous mux_) et crans à relever.
  encoder_decoder::Decoder decoder_;
  volatile int16_t detents_ = 0;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  bool buttonPressed_ = false;
  int lastButton_ = 1;
};
