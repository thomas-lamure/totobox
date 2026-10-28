// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

// Volume de la molette et jauge LED (sans dépendance matérielle: testé sur ordinateur).
namespace volume_rules {

// Volume après `detents` crans de molette (positif: plus fort), `stepPercent` par cran, borné aux limites des
// parents.
inline uint8_t afterDetents(uint8_t current, int8_t detents, uint8_t minimum, uint8_t maximum, uint8_t stepPercent) {
  int next = static_cast<int>(current) + static_cast<int>(detents) * stepPercent;
  if (next > maximum) {
    next = maximum;
  }
  if (next < minimum) {
    next = minimum;
  }
  return static_cast<uint8_t>(next);
}

// Jauge LED: position du volume entre les limites autorisées, de 0 (minimum) à 100 (maximum). Limites égales:
// jauge pleine (le volume ne peut pas bouger).
inline uint8_t gaugePercent(uint8_t current, uint8_t minimum, uint8_t maximum) {
  if (maximum <= minimum) {
    return 100;
  }
  if (current <= minimum) {
    return 0;
  }
  const uint16_t percent = static_cast<uint16_t>(current - minimum) * 100 / (maximum - minimum);
  return percent > 100 ? 100 : static_cast<uint8_t>(percent);
}

}  // namespace volume_rules
