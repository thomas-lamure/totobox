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
  bool stopCurrent = false;  // arrêter la carte active (retrait confirmé, ou deuxième carte posée)
  bool present = false;      // traiter la carte qui vient d'être posée
};

// Deux cartes posées: la lecture reprend quand une seule carte répond depuis ce délai (le lecteur rend les deux
// cartes en alternance, pas forcément à chaque recherche).
constexpr uint32_t kTwoCardsClearMs = 1500;
// Une carte différente de la dernière vue, moins de ce délai après elle: deux cartes sur le lecteur (ou un échange
// très rapide, qui attend alors kTwoCardsClearMs avant de lancer la nouvelle carte). L'état « deux cartes » prend
// fin après ce délai sans aucune carte.
constexpr uint32_t kTwoCardsWindowMs = 1000;

// `cardPresent`: le lecteur voit la carte `uid`. Le retrait n'est confirmé qu'après `graceMs` sans carte (une
// recherche peut manquer une carte posée). Une autre carte vue moins de kTwoCardsWindowMs après la dernière signifie
// deux cartes sur le lecteur (il les rend en alternance, séparées de recherches sans carte): l'active est arrêtée,
// signet compris, et l'état « deux cartes » (DeviceState::TwoCards) dure jusqu'à ce qu'une seule carte réponde
// pendant kTwoCardsClearMs (elle est alors traitée comme posée) ou qu'aucune ne réponde pendant kTwoCardsWindowMs.
NfcStep onNfcEvent(RuntimeState& runtime, bool cardPresent, const String& uid, uint32_t now, uint32_t graceMs);

// Recherche de carte à lancer maintenant? Sans carte posée, elle attend `pauseMs` après le dernier cran de molette
// (`lastKnobTurnMs`, 0 = jamais): le champ radio du lecteur perturbe l'encodeur. Une carte posée (ou deux) est
// toujours suivie: son retrait doit arrêter la lecture tout de suite.
bool shouldSearch(const RuntimeState& runtime, uint32_t now, uint32_t lastKnobTurnMs, uint32_t pauseMs);

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

// Arrêt demandé à distance (Home Assistant), la carte restant posée: signet enregistré (`stoppedAt`: position de
// la lecture arrêtée, nullptr si rien ne jouait), reprise après la carte SD annulée, état « en pause » jusqu'au
// retrait de la carte. Sans carte associée ni lecture (ou reprise) en cours, ne fait rien et renvoie faux.
// Vrai si l'arrêt a eu lieu.
bool onRemoteStop(RuntimeState& runtime, CardBinding* binding, const Bookmark* stoppedAt);

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
