// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include <functional>

// Règles des chemins de la bibliothèque sur la carte SD (sans dépendance matérielle: testées sur ordinateur).
// Toute route web qui reçoit un chemin ou un nom le fait passer par ici avant de toucher à la carte: rien ne
// sort de /content, aucun nom réservé ni caractère douteux n'est créé.
namespace library_paths {

constexpr char kLibraryRoot[] = "/content";
constexpr char kTrashRoot[] = "/content/.trash";

// Extensions lues par la boîte (ESP32-audioI2S), sans tenir compte de la casse.
bool isAudioFile(const String& path);

// Durée estimée d'après la taille, à 128 kbit/s (le débit réel n'est connu qu'en lisant le fichier).
uint32_t estimateDurationSec(uint32_t sizeBytes);

// Chemin absolu dans /content, sans "/" final ("\\" accepté comme séparateur, vide = /content); "" s'il
// contient ".." ou sort de /content.
String normalizeLibraryPath(const String& rawPath);

// Noms saisis ou envoyés: caractères hors [A-Za-z0-9 _-] (et "." pour un fichier) remplacés par "_", espaces et
// points de fin retirés, dossier éventuel du nom de fichier ignoré. "" si le nom est vide ou réservé.
String sanitizeFileName(const String& rawName);
String sanitizeFolderName(const String& rawName);

// ".", "..", "content", "trash", ".trash" (sans tenir compte de la casse), ou vide.
bool isReservedName(const String& name);

// La corbeille ou l'un de ses éléments.
bool isInTrash(const String& path);

// "replace", "reject" ou "rename" (par défaut, et pour toute autre valeur).
String normalizeConflictMode(const String& rawMode);

// Chemin où écrire `name` dans `dir` selon le mode (déjà normalisé): le chemin tel quel s'il est libre ou en
// "replace"; en "rename", le premier "nom-1.ext", "nom-2.ext"… libre (jusqu'à 999); "" en "reject" si le nom
// est pris, ou si aucun nom n'est libre.
String resolveConflict(const String& dir, const String& name, const String& mode,
                       const std::function<bool(const String&)>& exists);

// Emplacement dans la corbeille d'un élément de la bibliothèque (chemin normalisé): même arborescence sous
// /content/.trash. Faux pour /content lui-même et ce qui est déjà dans la corbeille.
struct TrashTarget {
  String dir;   // dossier de la corbeille où le déplacer
  String name;  // nom de l'élément (à résoudre avec resolveConflict en cas de doublon)
};
bool trashTargetFor(const String& libraryPath, TrashTarget& target);

// Emplacement d'origine d'un élément de la corbeille; "" si le chemin n'est pas dans la corbeille.
String originalPathFromTrash(const String& trashPath);

// Contrôles d'un déplacement (chemins normalisés) avant tout accès à la carte: nullptr s'il est permis, sinon
// "invalid_path" (vide, racine, corbeille) ou "invalid_move_target" (un dossier dans lui-même ou un descendant).
const char* moveError(const String& sourcePath, const String& targetDir);

}  // namespace library_paths
