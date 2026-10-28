// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "led_controller.h"

// Remise à zéro: sauvegarde d'abord les cartes associées sur la carte SD (card_backup), puis efface toute la NVS (réglages, Wi-Fi maison, verrouillage, cartes et marque-pages, ainsi que ce
// que la pile Wi-Fi y garde), puis redémarre. Restent la partition d'usine (mot de passe de l'étiquette) et les
// fichiers de la carte SD: la boîte repart en point d'accès TotoBox-XXXX, déverrouillée.
namespace factory_reset {

// À appeler au démarrage, avant de lire la configuration: si GPIO47 est relié à la masse, compte à rebours
// (bande qui se remplit de rouge, pont à retirer pour annuler), effacement, puis attente du retrait du pont
// (vert clignotant) pour ne pas recommencer au démarrage suivant.
void checkJumperAtBoot(LedController& leds);

// Demandée depuis l'interface web.
[[noreturn]] void eraseAndRestart(LedController& leds);

}  // namespace factory_reset
