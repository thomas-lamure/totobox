// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

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

}  // namespace update_rules
