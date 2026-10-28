// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

// Attente entre deux essais de reconnexion Wi-Fi (sans dépendance matérielle: testé sur ordinateur): doublée à
// chaque échec jusqu'à `maximumMs`; NetworkService la remet à sa valeur initiale une fois connecté.
namespace retry_delay {

inline uint32_t next(uint32_t currentMs, uint32_t maximumMs) {
  return currentMs >= maximumMs / 2 ? maximumMs : currentMs * 2;
}

}  // namespace retry_delay
