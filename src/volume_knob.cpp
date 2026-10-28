// SPDX-License-Identifier: GPL-3.0-or-later
#include "volume_knob.h"

#include <soc/gpio_reg.h>

#include "totobox_config.h"

namespace {

// Lecture directe du registre: utilisable en interruption, même pendant une écriture flash (NVS).
inline uint8_t IRAM_ATTR readEncoderState() {
  const uint32_t in = REG_READ(GPIO_IN_REG);
  return ((in >> totobox::kEncoderClk) & 1) << 1 | ((in >> totobox::kEncoderDt) & 1);
}

}  // namespace

void VolumeKnob::begin() {
  pinMode(totobox::kEncoderClk, INPUT_PULLUP);
  pinMode(totobox::kEncoderDt, INPUT_PULLUP);
  pinMode(totobox::kEncoderSw, INPUT_PULLUP);
  decoder_.state = readEncoderState();
  lastButton_ = digitalRead(totobox::kEncoderSw);
  attachInterruptArg(digitalPinToInterrupt(totobox::kEncoderClk), onEdge, this, CHANGE);
  attachInterruptArg(digitalPinToInterrupt(totobox::kEncoderDt), onEdge, this, CHANGE);
}

void IRAM_ATTR VolumeKnob::onEdge(void* arg) {
  static_cast<VolumeKnob*>(arg)->handleEdge();
}

void IRAM_ATTR VolumeKnob::handleEdge() {
  const uint8_t current = readEncoderState();
  portENTER_CRITICAL_ISR(&mux_);
  detents_ += decoder_.update(current);
  portEXIT_CRITICAL_ISR(&mux_);
}

void VolumeKnob::tick() {
  const int button = digitalRead(totobox::kEncoderSw);
  if (lastButton_ == HIGH && button == LOW) {
    buttonPressed_ = true;
  }
  lastButton_ = button;
}

int8_t VolumeKnob::takeDelta() {
  portENTER_CRITICAL(&mux_);
  const int16_t detents = detents_;
  detents_ = 0;
  portEXIT_CRITICAL(&mux_);
  const int16_t oriented = totobox::kEncoderReversed ? -detents : detents;
  return static_cast<int8_t>(constrain(oriented, -127, 127));
}

bool VolumeKnob::wasButtonPressed() {
  const bool pressed = buttonPressed_;
  buttonPressed_ = false;
  return pressed;
}
