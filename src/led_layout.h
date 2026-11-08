// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

// Position des LEDs sur la bande et animations (sans dépendance matérielle: testé sur ordinateur).
namespace led_layout {

// Position visible (0 = début de la bande vue de face) vers indice électrique. Bande montée à l'envers
// (`reversed`): la première LED visible est la dernière de la chaîne.
inline uint8_t physicalIndex(uint8_t visibleIndex, uint8_t count, bool reversed) {
  return reversed ? count - 1 - visibleIndex : visibleIndex;
}

// LEDs allumées pour une jauge de `percent` %: arrondi au plus proche, au moins une dès que la valeur n'est pas
// nulle.
inline uint8_t litCount(uint8_t percent, uint8_t count) {
  if (percent > 100) {
    percent = 100;
  }
  const uint8_t lit = static_cast<uint8_t>((static_cast<uint16_t>(percent) * count + 50) / 100);
  return percent > 0 && lit == 0 ? 1 : lit;
}

// Chenillard: indice électrique de la LED suivante. `forwardStep` (+1 ou -1) est le sens "aller" vu de face;
// en aller-retour (`bounce`), la direction s'inverse aux extrémités.
struct Chase {
  uint8_t index = 0;
  int8_t direction = 1;
};

inline Chase nextChase(Chase chase, uint8_t count, bool bounce, int8_t forwardStep) {
  if (!bounce) {
    chase.direction = forwardStep;
  } else if (chase.index + chase.direction < 0 || chase.index + chase.direction >= count) {
    chase.direction = static_cast<int8_t>(-chase.direction);
  }
  chase.index = static_cast<uint8_t>((chase.index + count + chase.direction) % count);
  return chase;
}

// Déverrouillage: bande entière allumée pendant `holdMs`, puis elle « s'ouvre en deux »: les LEDs s'éteignent du
// centre vers les deux bouts, une paire à la fois, en `openMs`. Symétrique: le sens de montage ne compte pas.
inline bool unlockLit(uint8_t visibleIndex, uint8_t count, uint32_t elapsedMs, uint32_t holdMs, uint32_t openMs) {
  if (elapsedMs < holdMs) {
    return true;
  }
  // Rang depuis le centre: 0 pour la ou les LEDs du milieu, jusqu'aux deux extrémités.
  const uint8_t half = count / 2;
  const uint8_t ring = count % 2 == 1 ? (visibleIndex >= half ? visibleIndex - half : half - visibleIndex)
                                      : (visibleIndex >= half ? visibleIndex - half : half - 1 - visibleIndex);
  const uint8_t rings = (count + 1) / 2;
  // Le rang k s'éteint après (k + 1) / rings de l'ouverture: tout est éteint à holdMs + openMs.
  return static_cast<uint64_t>(elapsedMs - holdMs) * rings < static_cast<uint64_t>(ring + 1) * openMs;
}

}  // namespace led_layout
