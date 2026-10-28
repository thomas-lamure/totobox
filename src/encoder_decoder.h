// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

// Attributs ESP32 (code et données accessibles en interruption, même pendant une écriture flash), sans effet
// sur ordinateur (tests).
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#ifndef DRAM_ATTR
#define DRAM_ATTR
#endif

// Décodage de la rotation d'un encodeur en quadrature (KY-040), sans dépendance matérielle: testé sur ordinateur.
// État = CLK << 1 | DT (1 = repos, résistances de rappel). Sens horaire: 11 -> 01 -> 00 -> 10 -> 11 (CLK descend
// le premier). Chaque changement d'un seul signal vaut un quart de pas; un changement des deux à la fois (front
// manqué) ou aucun changement (rebond relu) ne compte pas. Un cran est compté au retour en position de repos (11)
// s'il a été parcouru au moins à moitié dans un même sens: les rebonds (allers-retours entre deux états voisins)
// s'annulent, un front manqué est absorbé, un demi-cran rebroussé ne compte pas.
namespace encoder_decoder {

// Transition (état précédent << 2 | état courant): +1 / -1 quart de pas, 0 sinon.
DRAM_ATTR static const int8_t kQuarterStep[16] = {
    0,  -1, +1, 0,   // depuis 00: -> 01 antihoraire, -> 10 horaire
    +1, 0,  0,  -1,  // depuis 01: -> 00 horaire, -> 11 antihoraire
    -1, 0,  0,  +1,  // depuis 10: -> 00 antihoraire, -> 11 horaire
    0,  +1, -1, 0,   // depuis 11: -> 01 horaire, -> 10 antihoraire
};

constexpr uint8_t kRestState = 3;

struct Decoder {
  uint8_t state = kRestState;
  int8_t quarterSteps = 0;

  // Nouvel état lu sur les broches; renvoie +1 (cran horaire), -1 (cran antihoraire) ou 0.
  inline int8_t IRAM_ATTR update(uint8_t current) {
    current &= 3;
    quarterSteps += kQuarterStep[(state << 2) | current];
    state = current;
    if (current != kRestState) {
      return 0;
    }
    const int8_t detent = quarterSteps >= 2 ? 1 : (quarterSteps <= -2 ? -1 : 0);
    quarterSteps = 0;
    return detent;
  }
};

}  // namespace encoder_decoder
