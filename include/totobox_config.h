// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

namespace totobox {

constexpr uint8_t kLedPin = 16;
constexpr uint8_t kLedCount = 8;
constexpr uint8_t kLedBrightness = 48;

constexpr uint8_t kPn532Miso = 12;
constexpr uint8_t kPn532Mosi = 13;
constexpr uint8_t kPn532Sck = 14;
constexpr uint8_t kPn532Ss = 15;
constexpr uint8_t kPn532Irq = 21;

constexpr uint8_t kSdMosi = 17;
constexpr uint8_t kSdMiso = 8;
constexpr uint8_t kSdSck = 18;
constexpr uint8_t kSdCs = 5;

constexpr uint8_t kI2sDout = 40;
constexpr uint8_t kI2sBclk = 41;
constexpr uint8_t kI2sLrc = 42;

constexpr uint8_t kEncoderClk = 9;
constexpr uint8_t kEncoderDt = 10;
constexpr uint8_t kEncoderSw = 11;
// Sens de la molette: true si tourner dans le sens horaire baissait le son (selon le module, ou CLK et DT
// intervertis au câblage). Constaté sur la boîte avec le câblage du schéma hardware/encodeur-cablage.svg.
constexpr bool kEncoderReversed = true;

// Remise à zéro d'atelier: GPIO47 relié à la masse au démarrage (boîte démontée). Volontairement pas à la portée
// de qui a la boîte en main: elle contourne le verrouillage des réglages.
constexpr uint8_t kFactoryResetPin = 47;
// Pont maintenu ce temps (la bande se remplit de rouge) avant l'effacement; le retirer avant annule.
constexpr uint32_t kFactoryResetConfirmMs = 5000;
// Remise à zéro demandée depuis l'interface: délai laissé à la réponse HTTP avant l'effacement.
constexpr uint32_t kFactoryResetDelayMs = 1500;

constexpr uint32_t kNfcPollIntervalMs = 100;
// Essais du PN532 par recherche (MxRtyPassiveActivation): fini pour qu'il réponde "aucune carte" rapidement.
constexpr uint8_t kNfcSearchRetries = 1;
// Sans réponse du PN532 dans ce délai, l'échange est réinitialisé.
constexpr uint32_t kNfcResponseTimeoutMs = 300;
// Constaté sur la carte: une commande envoyée juste après la lecture d'une réponse n'est pas acquittée.
// Pause à respecter avant la commande suivante (loop() espace déjà ses recherches de kNfcPollIntervalMs).
constexpr uint32_t kNfcSettleMs = 15;
constexpr uint32_t kTagRemovalGraceMs = 250;
// Veille profonde après cette durée sans interaction ni lecture (0 = jamais). Réveil: carte NFC ou encodeur.
// Carte SD absente: nouvel essai à cet intervalle (insertion à chaud, assistant de première connexion).
constexpr uint32_t kSdRetryMs = 2000;
// Carte illisible: entre deux essais, on vérifie seulement sa présence; montage retenté à cet intervalle.
constexpr uint32_t kSdUnreadableRemountMs = 30000;
constexpr uint32_t kIdleSleepMs = 5UL * 60UL * 1000UL;
// Carte laissée sur la boîte pendant la veille: intervalle de vérification de sa présence.
constexpr uint32_t kHeldCardCheckMs = 10UL * 1000UL;
constexpr uint32_t kVolumeOverlayMs = 5000;
// Volume par cran de molette (%). La lib audio n'a que 22 niveaux (0 à 21, ~4,8 % chacun): 5 % fait entendre un
// niveau à chaque cran (avec 2 %, il fallait 2 à 3 crans pour un changement audible).
constexpr uint8_t kVolumeStepPercent = 5;
constexpr uint32_t kVolumeSaveDelayMs = 1500;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
// Wi-Fi maison perdu: nouvelle tentative après ce délai, doublé à chaque échec jusqu'au maximum.
constexpr uint32_t kWifiRetryInitialMs = 10000;
constexpr uint32_t kWifiRetryMaxMs = 60000;
// Réglages réseau modifiés depuis l'interface: appliqués après ce délai, le temps d'envoyer la réponse.
constexpr uint32_t kNetworkApplyDelayMs = 1000;
// Wi-Fi maison rejoint depuis le point d'accès: le point d'accès reste ouvert ce temps, pour que le téléphone
// affiche le résultat et la nouvelle adresse avant de changer de réseau.
constexpr uint32_t kApCloseDelayMs = 60000;
// Réannonce mDNS périodique (les réponses mDNS de l'ESP32 vivent 120 s): garde <nom>.local en cache sur les
// appareils du réseau quand la box ne relaie plus les requêtes multicast vers la boîte.
constexpr uint32_t kMdnsAnnounceMs = 60000;

// Nom réseau: annoncé en mDNS (http://totobox.local) et donné au routeur via DHCP.
constexpr char kHostname[] = "totobox";

// Dépôt GitHub des versions publiées: la page y cherche les nouvelles versions, la boîte n'accepte que ses
// fichiers (update_rules). Un fork qui publie ses propres versions remplace ce nom.
constexpr char kUpdateRepo[] = "thomas-lamure/totobox";
// Taille maximale de la page web téléchargée (index.html.gz): la partition LittleFS en garde deux copies.
constexpr uint32_t kUpdateMaxPageBytes = 80 * 1024;

// Point d'accès de secours: nom "TotoBox-" + fin de l'adresse MAC, mot de passe tiré au hasard au premier
// démarrage (ConfigStore::ensureAccessPointDefaults). Anciennes valeurs fixes, remplacées une fois au démarrage.
constexpr char kApSsidPrefix[] = "TotoBox-";
constexpr char kLegacyApSsid[] = "TotoBox-Setup";
constexpr char kLegacyApPassword[] = "totobox123";

}  // namespace totobox
