// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include <vector>

#include "app_types.h"

// Suivi des cartes NFC posées sur la boîte (sans dépendance matérielle: testé sur ordinateur). Ces fonctions
// décident et mettent à jour RuntimeState et les signets; main.cpp exécute le reste (lecture, LEDs, NVS).
namespace card_flow {

// Position où reprendre une histoire.
struct Bookmark {
  uint16_t track = 0;
  uint32_t ms = 0;
  uint32_t byte = 0;
};

// Ce qu'une réponse du lecteur NFC demande de faire, dans cet ordre.
struct NfcStep {
  bool stopCurrent = false;  // arrêter la carte active (retrait confirmé, ou remplacée par une autre)
  bool present = false;      // traiter la carte qui vient d'être posée
};

// `cardPresent`: le lecteur voit la carte `uid`. Le retrait n'est confirmé qu'après `graceMs` sans carte (une
// recherche peut manquer une carte posée). Une autre carte posée avant la fin de ce délai remplace l'active:
// elle est d'abord arrêtée, signet compris.
NfcStep onNfcEvent(RuntimeState& runtime, bool cardPresent, const String& uid, uint32_t now, uint32_t graceMs);

enum class Presented : uint8_t {
  Unlock,   // carte maîtresse: réglages déverrouillés, rien n'est lu
  Unknown,  // carte inconnue: signalée (LEDs, page) pour être associée depuis la page
  Play,     // carte associée: lancer sa lecture au signet
};

// Carte posée: devient la carte active; met à jour l'état pour une carte maîtresse ou inconnue.
Presented onCardPresented(RuntimeState& runtime, const DeviceConfig& config,
                          const std::vector<CardBinding>& bindings, const String& uid);

// Carte active retirée (ou remplacée). `stoppedAt`: position de la lecture arrêtée, nullptr si rien ne jouait.
// Vrai si le signet de `binding` a changé (à enregistrer).
bool onCardRemoved(RuntimeState& runtime, CardBinding* binding, const Bookmark* stoppedAt);

// Histoire lue jusqu'au bout: le signet repart du début. Vrai s'il a changé.
bool onStoryFinished(RuntimeState& runtime, CardBinding* binding);

// Carte SD retirée pendant la lecture: signet gardé, reprise à la réinsertion si la carte reste posée.
bool onSdLost(RuntimeState& runtime, CardBinding* binding, const Bookmark& stoppedAt);

// Carte SD de nouveau prête (`wasReady`: état précédent): vrai s'il faut reprendre l'histoire interrompue.
bool shouldResumeAfterSd(RuntimeState& runtime, bool wasReady);

// Carte inconnue toujours posée qui vient d'être associée depuis la page: vrai s'il faut la lire.
bool shouldPlayNewlyBound(const RuntimeState& runtime, const std::vector<CardBinding>& bindings);

// État au repos: "prête" seulement si la carte SD et le lecteur NFC le sont. Une carte posée ou retirée, ou une
// histoire finie, ne doit pas effacer une erreur matérielle (LEDs rouges, statut de la page).
DeviceState restingState(const RuntimeState& runtime);

CardBinding* findBinding(std::vector<CardBinding>& bindings, const String& uid);
const CardBinding* findBinding(const std::vector<CardBinding>& bindings, const String& uid);

// Identifiant lu par le PN532, en hexadécimal majuscule ("04A1B2C3").
String formatUid(const uint8_t* uid, uint8_t length);

}  // namespace card_flow
