// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <vector>

#include "totobox_config.h"

enum class PlaybackMode {
  SdLocal,
  HomeAssistant
};

enum class DeviceState {
  Booting,
  Ready,
  Playing,
  Paused,
  UnknownCard,
  Error
};

// Carte SD: absente, présente mais illisible (pas de FAT16/FAT32: exFAT des cartes de 64 Go et plus, NTFS,
// carte vierge...), ou montée.
enum class SdState {
  Absent,
  Unreadable,
  Ready
};

inline const char* sdStateToString(SdState state) {
  switch (state) {
    case SdState::Unreadable:
      return "unreadable";
    case SdState::Ready:
      return "ready";
    case SdState::Absent:
    default:
      return "absent";
  }
}

struct CardBinding {
  String uid;
  String label;
  String mediaPath;
  PlaybackMode mode = PlaybackMode::SdLocal;
  uint16_t resumeTrackIndex = 0;
  // Position de reprise dans la piste resumeTrackIndex: en millisecondes (affichage, et secours pour les anciens
  // marque-pages), et en octets dans le fichier (reprise exacte, y compris en MP3 à débit variable). 0 = inconnu.
  uint32_t resumeMs = 0;
  uint32_t resumeByte = 0;
};

// Carte maîtresse: déverrouille les réglages (un parent = une carte).
struct MasterCard {
  String uid;
  String label;
};

struct DeviceConfig {
  // Assistant de première connexion terminé (ou remis à plus tard). Faux sur une boîte neuve ou remise à zéro.
  bool setupDone = false;
  uint8_t volumeMin = 20;
  uint8_t volumeMax = 85;
  uint8_t currentVolume = 55;
  String wifiSsid;
  String wifiPassword;
  // Vides tant que ConfigStore::ensureAccessPointDefaults() ne les a pas remplis (nom tiré de l'adresse MAC,
  // mot de passe aléatoire).
  String apSsid;
  String apPassword;
  // Mot de passe tiré par la boîte (et non choisi): seul celui-là est affiché dans la console série.
  bool apPasswordGenerated = false;
  // Mot de passe d'usine (partition "mfg", imprimé sur l'étiquette); vide si la boîte n'en a pas.
  // Lu à chaque démarrage, jamais enregistré en NVS.
  String factoryApPassword;
  // Nom sur le réseau: http://<hostname>.local (mDNS) et nom DHCP.
  String hostname = totobox::kHostname;
  // Verrouillage des réglages (voir access_lock.h).
  bool lockEnabled = false;
  std::vector<MasterCard> lockCards;
  String lockPinSalt;
  String lockPinHash;
  // Clé privée X25519 (hex) des secrets envoyés par la page (secret_channel); tirée au premier démarrage.
  String channelSecretKey;
  // Mises à jour: la page cherche les nouvelles versions sur GitHub et les annonce (updateNotify); la boîte les
  // cherche elle-même une fois par jour et les installe (autoUpdate, voir update_rules::shouldAutoCheck).
  bool updateNotify = true;
  bool autoUpdate = false;
  // Dernière recherche automatique aboutie, en secondes depuis 1970 (heure NTP); 0 = jamais.
  uint32_t lastUpdateCheck = 0;
};

struct RuntimeState {
  DeviceState state = DeviceState::Booting;
  bool sdReady = false;  // sdState == SdState::Ready
  SdState sdState = SdState::Absent;
  bool nfcReady = false;
  bool wifiConnected = false;
  String wifiMode = "off";
  String wifiIp;
  String wifiMac;
  // Réglages réseau modifiés depuis l'interface: NetworkService les applique sans redémarrage.
  bool networkConfigChanged = false;
  // Suivi d'une connexion lancée depuis l'interface, affiché par la page: "" (aucune), "trying",
  // "connected" ou "failed" (avec networkError: wrong_password, network_not_found, connection_failed).
  String networkAttempt;
  // Numéro de l'essai, incrémenté à chaque nouvelle tentative: la page ignore le résultat d'un essai précédent.
  uint16_t networkAttemptId = 0;
  String networkAttemptSsid;
  String networkAttemptIp;
  String networkError;
  // Fermeture prévue du point d'accès après une connexion réussie depuis celui-ci (0 = aucune).
  uint32_t apCloseAtMs = 0;
  String activeCardUid;
  String activeMediaPath;
  uint16_t activeTrackIndex = 0;
  uint32_t activePositionMs = 0;
  uint32_t lastCardSeenMs = 0;
  uint32_t lastVolumeOverlayMs = 0;
  bool volumeOverlayActive = false;
  // Volume modifié (molette ou web) et pas encore écrit en NVS: l'écriture attend que le volume soit stable.
  bool volumeSaveDue = false;
  uint32_t lastVolumeChangeMs = 0;
  // Dernière interaction (lecture, carte, molette, requête web hors statut): base de la mise en veille.
  uint32_t lastActivityMs = 0;
  // Déverrouillé par la carte maîtresse ou le code; jamais persisté: verrouillé à chaque démarrage.
  bool unlocked = false;
  bool audioPlaying = false;
  // Remise à zéro demandée depuis l'interface: effacement à cet instant (0 = aucune), après la réponse HTTP.
  uint32_t factoryResetAtMs = 0;
  // Envoi de fichiers en cours: le serveur web garde la main jusqu'à la fin, les LEDs le signalent.
  bool uploading = false;
  // Mise à jour en ligne en cours (UpdateService): cartes ignorées, pas de veille, LEDs comme pour un envoi.
  bool updating = false;
  uint16_t activeTrackCount = 0;
  // Dernière carte inconnue posée, conservée après retrait pour pouvoir l'associer depuis l'interface web.
  String lastUnknownCardUid;
  // Histoire interrompue par le retrait de la carte SD: reprise à la réinsertion si sa carte NFC est toujours posée.
  bool resumeAfterSd = false;
};

inline const char* playbackModeToString(PlaybackMode mode) {
  switch (mode) {
    case PlaybackMode::SdLocal:
      return "sd_local";
    case PlaybackMode::HomeAssistant:
      return "home_assistant";
  }
  return "sd_local";
}

inline PlaybackMode playbackModeFromString(const String& value) {
  if (value == "home_assistant") {
    return PlaybackMode::HomeAssistant;
  }
  return PlaybackMode::SdLocal;
}

inline const char* deviceStateToString(DeviceState state) {
  switch (state) {
    case DeviceState::Booting:
      return "booting";
    case DeviceState::Ready:
      return "ready";
    case DeviceState::Playing:
      return "playing";
    case DeviceState::Paused:
      return "paused";
    case DeviceState::UnknownCard:
      return "unknown_card";
    case DeviceState::Error:
      return "error";
  }
  return "error";
}
