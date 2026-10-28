// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Règles de la mise à jour en ligne (sans dépendance matérielle: testées sur ordinateur). La page trouve la
// nouvelle version sur GitHub et la demande à la boîte, qui télécharge elle-même les fichiers (GitHub ne les sert
// pas aux navigateurs d'une autre origine) et vérifie leur empreinte SHA-256 avant de les installer.
// Seuls les fichiers publiés dans les Releases du dépôt officiel, d'une version plus récente, sont acceptés: une
// page malveillante ne peut ni installer un autre firmware, ni revenir à une version plus ancienne.
namespace update_rules {

struct Asset {
  String url;
  String sha256;  // 64 caractères hexadécimaux
  uint32_t size = 0;
};

struct Request {
  String version;    // "1.2.0"
  Asset firmware;    // totobox-X.Y.Z-firmware.bin, écrit dans la partition OTA inactive
  Asset page;        // totobox-X.Y.Z-page.html.gz, remplace /index.html.gz dans LittleFS
  String uiVersion;  // version de cette page (ui_version.txt)
};

// Version X.Y.Z (sans "v") vers ses trois nombres; faux si le format n'est pas respecté.
bool parseVersion(const String& text, uint16_t parts[3]);

// Vrai si `candidate` est strictement plus récente que `current`.
bool isNewer(const String& current, const String& candidate);

// Fichier d'une Release du dépôt `repo` ("propriétaire/nom"), pour la version `version`.
bool isReleaseAsset(const String& url, const String& repo, const String& version);

bool isSha256Hex(const String& value);

// nullptr si la demande est acceptable, sinon le code d'erreur à renvoyer à la page: "invalid_version",
// "not_newer", "invalid_url", "invalid_hash", "invalid_size" ou "invalid_ui_version".
const char* check(const Request& request, const String& currentVersion, const String& repo,
                  uint32_t maxFirmwareBytes, uint32_t maxPageBytes);

// Empreinte en hexadécimal minuscule (comparaison avec la valeur publiée).
String toHex(const uint8_t* digest, size_t length);

// Dernière version publiée, lue par la boîte elle-même (mode automatique) dans la réponse de l'API GitHub
// /releases/latest: même lecture que la page (web/lib.js, parseRelease). Les notes de version se terminent par
// le bloc « <!-- totobox-update {...} --> » (scripts/release.py update-block): version, version de la page,
// nom, taille et SHA-256 des fichiers; leur adresse vient de la liste des fichiers de la Release.
// Faux si la Release ne permet pas la mise à jour en ligne (bloc absent ou illisible, fichier manquant).
bool parseRelease(JsonVariantConst release, Request& request);

// Recherche automatique (réglage autoUpdate): une fois par jour, et jamais quand un enfant peut vouloir écouter.
constexpr uint32_t kAutoCheckIntervalSec = 24UL * 3600UL;
// Silence demandé avant de chercher (et donc d'installer): ni lecture ni carte posée depuis une minute, réveil
// compris (un enfant réveille souvent la boîte pour écouter tout de suite).
constexpr uint32_t kAutoCheckQuietMs = 60UL * 1000UL;

struct AutoCheck {
  bool enabled = false;      // réglage autoUpdate
  bool online = false;       // Wi-Fi de la maison (le point d'accès seul n'a pas Internet)
  bool busy = false;         // mise à jour ou recherche déjà en cours
  bool retryWaiting = false; // dernier essai sans réponse de GitHub: attendre avant de réessayer
  uint32_t nowEpoch = 0;     // heure (NTP) en secondes depuis 1970; 0 tant qu'elle n'est pas connue
  uint32_t lastCheckEpoch = 0;
  uint32_t quietMs = 0;      // temps écoulé sans lecture ni carte posée, depuis le réveil au plus
};

bool shouldAutoCheck(const AutoCheck& state);

}  // namespace update_rules
