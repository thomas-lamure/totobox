// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include <vector>

// Décisions de lecture d'AudioService qui ne dépendent pas du matériel (testées sur ordinateur): comment reprendre
// une piste au signet, quelle position enregistrer, dans quel ordre lire un dossier, quel niveau donner à la lib.
// Contexte: ESP32-audioI2S 2.3.0, voir « Pièges connus » dans AGENTS.md.
namespace playback_rules {

// connecttoFS(path, octet) recale lui-même la position sur une trame pour ces formats.
bool supportsByteResume(const String& path);
// setAudioPlayPosition(secondes): position estimée par le débit moyen (exacte en débit constant), refusée en M4A.
bool supportsSecondResume(const String& path);

enum class ResumeKind {
  FromStart,  // début de la piste
  ByByte,     // connecttoFS() à l'octet du signet
  BySeconds,  // ancien signet sans octet: début, puis setAudioPlayPosition() une fois le débit connu
};

struct ResumePlan {
  ResumeKind kind = ResumeKind::FromStart;
  uint32_t startByte = 0;  // ByByte: octet passé à connecttoFS()
  uint32_t baseMs = 0;     // ByByte: position du signet, origine de l'horloge (celle de la lib repart de 0)
  uint16_t seconds = 0;    // BySeconds: position à demander à la lib
  bool beyondEnd = false;  // signet au-delà de la fin du fichier (fichier remplacé, signet faussé): ignoré
  bool unsupported = false;  // signet présent mais format sans reprise possible (OGG, Opus…)
};

// `fileSize`: taille de la piste, 0 si elle n'a pas pu être ouverte (le signet par octet est alors ignoré).
ResumePlan planResume(const String& path, uint32_t resumeMs, uint32_t resumeByte, uint32_t fileSize);

// Position affichée et enregistrée, en ms, pendant la lecture. Sans reprise par octet, c'est l'horloge de la lib;
// après une reprise par octet, l'horloge de la lib repart de 0 au saut: on compte depuis le signet (baseMs) le
// temps écoulé depuis l'instant où la piste a démarré (anchorSec, relevé dans tick()).
struct PlaybackClock {
  bool byteResumed = false;
  bool anchorPending = false;  // piste pas encore démarrée après le saut: position = signet
  uint32_t baseMs = 0;
  uint32_t anchorSec = 0;
};
uint32_t positionMs(const PlaybackClock& clock, uint32_t libTimeSec);

// Octet en cours de lecture: position de lecture du fichier moins ce qui attend dans le tampon de la lib. 0 si la
// position est hors du fichier: après un retrait de la carte SD, la lib tourne encore et getFilePos() renvoie -1.
uint32_t positionByte(uint32_t filePos, uint32_t buffered, uint32_t fileSize);

// Pistes d'un dossier, dans l'ordre alphabétique sans tenir compte de la casse (d'où les noms préfixés 01-, 02-…).
void sortPlaylist(std::vector<String>& playlist);

// Volume 0-100 % vers les 22 niveaux (0-21) de la lib.
uint8_t libVolume(uint8_t percent);

}  // namespace playback_rules
