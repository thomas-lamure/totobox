// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include <Audio.h>
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "app_types.h"
#include "playback_rules.h"

// Le décodage tourne dans une tâche FreeRTOS dédiée (cœur 1, à l'écart du Wi-Fi): la lib ne décode qu'une trame MP3 (~26 ms)
// tous les 3 appels à Audio::loop(), elle doit donc être appelée en continu, indépendamment de loop().
// Toutes les méthodes publiques sont protégées par un mutex partagé avec cette tâche.
class AudioService {
 public:
  // Point de reprise dans la piste en cours.
  struct ResumePoint {
    uint32_t ms = 0;
    uint32_t byte = 0;
  };

  bool begin(uint8_t initialVolumePercent);
  bool playPath(const String& path, uint16_t resumeTrackIndex, uint32_t resumeMs, uint32_t resumeByte);
  ResumePoint pause();
  void stop();
  bool isPlaying() const;
  bool isSdReady() const;
  // État de la carte SD, tenu à jour par une tâche de surveillance (insertion à chaud): lecture sans attente.
  SdState sdState() const;
  // Formate en FAT (FAT32, ou FAT16 pour une petite carte) une carte illisible, puis la monte. Efface tout.
  // Bloquant (plusieurs secondes sur une grande carte). Renvoie true si la carte est prête.
  bool formatSd();
  void setVolumePercent(uint8_t percent);
  uint32_t currentPositionMs();
  String currentPath() const;
  uint16_t currentTrackIndex() const;
  uint16_t trackCount() const;
  bool consumePlaybackFinished();
  // Lecture interrompue par le retrait de la carte SD: piste et position à garder dans le signet (une seule fois).
  bool consumeSdLostPlayback(uint16_t& trackIndex, ResumePoint& point);

 private:
  class Lock {
   public:
    explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTakeRecursive(mutex_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGiveRecursive(mutex_); }

   private:
    SemaphoreHandle_t mutex_;
  };

  static void taskEntry(void* arg);
  void tick();
  uint32_t positionMsLocked();
  uint32_t positionByteLocked();
  // Surveillance de la carte SD: montage tant qu'elle n'est pas montée, présence ensuite. Sans carte, une
  // initialisation SPI bloque de 0,3 à plus de 2 s (attentes de la lib SD): jamais dans loop() (serveur web, NFC et
  // LEDs y figeraient), ni sous le verrou audio (loop() le prend à chaque tour). sdMutex_ empêche seulement un essai
  // et un formatage simultanés.
  static void sdTaskEntry(void* arg);
  void sdWatchStep();
  void mountSd();
  bool cardResponds();
  bool mountedCardResponds(uint8_t* sectorBuffer);
  void handleSdLost();
  void publishSdState(SdState state);
  bool formatSdHeld();
  bool buildPlaylist(const String& path);
  bool startCurrentTrack(uint32_t resumeMs, uint32_t resumeByte);

  Audio* audio_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  TaskHandle_t task_ = nullptr;
  TaskHandle_t sdTask_ = nullptr;
  SemaphoreHandle_t sdMutex_ = nullptr;
  volatile bool sdReady_ = false;
  volatile SdState sdState_ = SdState::Absent;
  uint32_t lastFullMountMs_ = 0;
  // Dernière position connue (la lib peut s'arrêter d'elle-même dès que la carte ne répond plus) et lecture
  // interrompue par un retrait de la carte, en attente d'être relevée par loop().
  ResumePoint lastPoint_;
  uint32_t lastPointAtMs_ = 0;
  bool sdLostPending_ = false;
  uint16_t sdLostTrack_ = 0;
  ResumePoint sdLostPoint_;
  bool playing_ = false;
  bool playbackFinished_ = false;
  uint32_t pausedAtMs_ = 0;
  uint16_t pendingResumeSec_ = 0;
  // Reprise par octet: l'horloge de la lib repart de 0 (débit encore inconnu au moment du saut); la position
  // est donc comptée depuis le signet, à partir de l'horloge relevée une fois la piste lancée.
  playback_rules::PlaybackClock clock_;
  String requestedPath_;
  String currentPath_;
  std::vector<String> playlist_;
  uint16_t currentTrackIndex_ = 0;
};
