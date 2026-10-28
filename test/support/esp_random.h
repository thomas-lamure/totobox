// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Aléa simulé pour les tests sur ordinateur (déterministe mais différent à chaque appel).

#include <cstddef>
#include <cstdint>
#include <random>

inline void esp_fill_random(void* buffer, size_t length) {
  static std::mt19937 generator(0x70707070);
  auto* bytes = static_cast<uint8_t*>(buffer);
  for (size_t index = 0; index < length; ++index) {
    bytes[index] = static_cast<uint8_t>(generator());
  }
}
