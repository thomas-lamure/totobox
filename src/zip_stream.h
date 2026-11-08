// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include <vector>

// Archive ZIP envoyée au fil de l'eau (GET /api/download d'un dossier), sans dépendance matérielle: testée sur
// ordinateur. Fichiers stockés sans compression (des MP3, déjà compressés): taille de l'archive connue d'avance
// (Content-Length), rien à garder en mémoire. Le CRC de chaque fichier n'est connu qu'après l'avoir lu: il part
// dans un descripteur de données après le contenu (bit 3), puis dans le répertoire central. Les tailles sont
// aussi dans l'en-tête local, pour les extracteurs qui lisent l'archive dans l'ordre. Pas de ZIP64: archive de
// moins de 4 Go et de 65535 fichiers.
namespace zip_stream {

struct Entry {
  String name;             // chemin dans l'archive, "/" comme séparateur, en UTF-8
  uint32_t size = 0;       // taille du fichier
  uint16_t dosTime = 0;    // heure et date au format MS-DOS
  uint16_t dosDate = 0x21; // 1er janvier 1980 par défaut (pas d'horloge à pile)
  uint32_t crc = 0;        // connu après lecture du fichier
  uint32_t offset = 0;     // position de l'en-tête local dans l'archive
};

// CRC-32 (ZIP, polynôme 0xEDB88320): départ à 0, appels successifs sur les morceaux.
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length);

std::vector<uint8_t> localHeader(const Entry& entry);
std::vector<uint8_t> dataDescriptor(const Entry& entry);
std::vector<uint8_t> centralHeader(const Entry& entry);
std::vector<uint8_t> endOfCentralDirectory(uint16_t count, uint32_t centralSize, uint32_t centralOffset);

// Taille totale de l'archive; 0 si elle dépasse les limites du format sans ZIP64 (ou liste vide).
uint32_t archiveSize(const std::vector<Entry>& entries);

// Date et heure (année complète, mois 1-12) au format MS-DOS; avant 1980: 1er janvier 1980.
void dosDateTime(int year, int month, int day, int hour, int minute, int second, uint16_t& date, uint16_t& time);

// Nom dans l'archive d'un fichier du dossier téléchargé, préfixé du nom de ce dossier:
// ("/content/music", "/content/music/comptines/a.mp3") -> "music/comptines/a.mp3". Pour /content: "totobox/…".
String entryName(const String& folderPath, const String& filePath);

// Nom du fichier .zip proposé au navigateur: "music.zip", "totobox.zip" pour /content.
String archiveName(const String& folderPath);

}  // namespace zip_stream
