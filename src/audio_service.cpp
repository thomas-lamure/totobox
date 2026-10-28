// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_service.h"

#include <ff.h>
#include <diskio.h>
#include <sd_diskio.h>

#include <algorithm>

#include "logger.h"
#include "totobox_config.h"

static unsigned long fileReadStart = 0; // Timestamp for when file reading starts

namespace {

constexpr uint32_t kAudioTaskStackBytes = 8192;
// Cœur 1, celui de loop(): la pile Wi-Fi (priorité 23) occupe le cœur 0 par rafales pendant la connexion
// (WPA2, DHCP, calibration radio) et y affamait la tâche audio (blanc d'~0,5 s constaté sur la carte).
// Priorité au-dessus de loopTask (1): la tâche audio reprend la main dès qu'elle en a besoin, même si
// loop() travaille.
constexpr UBaseType_t kAudioTaskPriority = 3;
constexpr BaseType_t kAudioTaskCore = 1;
constexpr uint32_t kSdTaskStackBytes = 4096;
constexpr UBaseType_t kSdTaskPriority = 1;
constexpr uint32_t kPositionSnapshotMs = 250;
constexpr size_t kSectorBytes = 512;

bool hasExtension(const String& path, std::initializer_list<const char*> extensions) {
  String lower = path;
  lower.toLowerCase();
  for (const char* extension : extensions) {
    if (lower.endsWith(extension)) {
      return true;
    }
  }
  return false;
}

// connecttoFS(path, octet) recale lui-même la position sur une trame pour ces formats.
bool supportsByteResume(const String& path) {
  return hasExtension(path, {".mp3", ".wav", ".m4a", ".flac", ".aac"});
}

// setAudioPlayPosition(): position estimée par le débit moyen (exacte en débit constant), refusée en M4A.
bool supportsSecondResume(const String& path) {
  return hasExtension(path, {".mp3", ".wav", ".flac", ".aac"});
}

}  // namespace

bool AudioService::begin(uint8_t initialVolumePercent) {
  SPI.begin(totobox::kSdSck, totobox::kSdMiso, totobox::kSdMosi, totobox::kSdCs);
  mutex_ = xSemaphoreCreateRecursiveMutex();
  sdMutex_ = xSemaphoreCreateMutex();
  // Au démarrage, avant le serveur web: l'attente éventuelle (carte absente) ne gêne personne.
  mountSd();

  audio_ = new Audio();
  audio_->setPinout(totobox::kI2sBclk, totobox::kI2sLrc, totobox::kI2sDout);
  audio_->setVolume(mapVolume(initialVolumePercent));

  xTaskCreatePinnedToCore(taskEntry, "audio", kAudioTaskStackBytes, this, kAudioTaskPriority, &task_, kAudioTaskCore);
  // Même cœur et même priorité que loop(): les deux se partagent le temps pendant un essai, la tâche audio
  // (priorité 3) passe avant. Pas sur le cœur 0, dont la tâche idle est surveillée par le chien de garde.
  xTaskCreatePinnedToCore(sdTaskEntry, "sdwatch", kSdTaskStackBytes, this, kSdTaskPriority, &sdTask_, kAudioTaskCore);
  return sdReady_;
}

void AudioService::taskEntry(void* arg) {
  AudioService* self = static_cast<AudioService*>(arg);
  for (;;) {
    {
      Lock lock(self->mutex_);
      self->tick();
    }
    // Laisse respirer les autres tâches du cœur (et le watchdog de la tâche idle).
    vTaskDelay(1);
  }
}

void AudioService::tick() {
  if (audio_->isRunning()) {
    // Serial.print(millis()-fileReadStart);
    // Serial.print("-a-");
    audio_->loop();
    // Serial.println(millis()-fileReadStart);
    playing_ = true;
    // Instantané gardé seulement tant que le fichier se lit (position dans le fichier): après un retrait de la
    // carte, la lib tourne encore quelques secondes sur une position invalide.
    if (millis() - lastPointAtMs_ >= kPositionSnapshotMs) {
      const uint32_t byte = positionByteLocked();
      if (byte > 0) {
        lastPoint_.ms = positionMsLocked();
        lastPoint_.byte = byte;
      }
      lastPointAtMs_ = millis();
    }

    // Ancien marque-page sans octet: reprise en secondes une fois le débit connu (après lecture de l'en-tête).
    if (pendingResumeSec_ > 0 && audio_->getAudioFileDuration() > 0) {
      if (audio_->setAudioPlayPosition(pendingResumeSec_)) {
        logLine("audio", "Reprise à %us (estimée par le débit moyen)", pendingResumeSec_);
      } else {
        logLine("audio", "Reprise à %us refusée par la lib: lecture depuis le début", pendingResumeSec_);
      }
      pendingResumeSec_ = 0;
    }
    // Reprise par octet: le saut est fait avant le décodage, l'horloge de la lib est relevée une fois la piste
    // lancée pour compter la position à partir du marque-page.
    if (timeAnchorPending_ && audio_->getAudioFileDuration() > 0) {
      libTimeAnchorSec_ = audio_->getAudioCurrentTime();
      timeAnchorPending_ = false;
    }
    return;
  }

  if (playing_) {
    // La lib arrête aussi la piste quand la carte SD ne répond plus: ce n'est pas une fin d'histoire (le signet
    // serait remis à zéro). Une lecture d'un secteur par fin de piste suffit à les distinguer.
    static uint8_t sector[kSectorBytes];
    if (!mountedCardResponds(sector)) {
      handleSdLost();
      return;
    }
    logLine("audio", "Piste %u terminée", currentTrackIndex_ + 1);
    if (currentTrackIndex_ + 1 < playlist_.size()) {
      ++currentTrackIndex_;
      startCurrentTrack(0, 0);
      return;
    }

    playing_ = false;
    playbackFinished_ = true;
    pausedAtMs_ = 0;
    currentPath_ = "";
  }
}

bool AudioService::playPath(const String& path, uint16_t resumeTrackIndex, uint32_t resumeMs, uint32_t resumeByte) {
  Lock lock(mutex_);
  if (!sdReady_) {
    return false;
  }

  stop();
  requestedPath_ = path;
  if (!buildPlaylist(path)) {
    return false;
  }

  // Marque-page d'une piste qui n'existe plus (dossier modifié): on repart au début de la dernière piste.
  if (resumeTrackIndex >= playlist_.size()) {
    resumeMs = 0;
    resumeByte = 0;
  }
  currentTrackIndex_ = std::min<size_t>(resumeTrackIndex, playlist_.size() - 1);
  return startCurrentTrack(resumeMs, resumeByte);
}

AudioService::ResumePoint AudioService::pause() {
  Lock lock(mutex_);
  ResumePoint point;
  point.ms = positionMsLocked();
  point.byte = positionByteLocked();
  logLine("audio", "Pause à %lums (octet %lu)", static_cast<unsigned long>(point.ms),
          static_cast<unsigned long>(point.byte));
  audio_->stopSong();
  playing_ = false;
  pausedAtMs_ = point.ms;
  return point;
}

void AudioService::stop() {
  Lock lock(mutex_);
  if (audio_->isRunning()) {
    logLine("audio", "Arrêt de la piste en cours");
    audio_->stopSong();
  }
  playing_ = false;
  playbackFinished_ = false;
  pausedAtMs_ = 0;
  pendingResumeSec_ = 0;
  byteResumed_ = false;
  timeAnchorPending_ = false;
  requestedPath_ = "";
  currentPath_ = "";
  playlist_.clear();
  currentTrackIndex_ = 0;
}

bool AudioService::isPlaying() const {
  Lock lock(mutex_);
  return playing_;
}

bool AudioService::isSdReady() const {
  return sdReady_;
}

SdState AudioService::sdState() const {
  return sdState_;
}

void AudioService::sdTaskEntry(void* arg) {
  AudioService* self = static_cast<AudioService*>(arg);
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(totobox::kSdRetryMs));
    self->sdWatchStep();
  }
}

// Carte absente: un essai de montage. Illisible: on vérifie seulement qu'elle est toujours là (pas de montage voué
// à l'échec à chaque fois), avec un montage complet toutes les kSdUnreadableRemountMs au cas où elle aurait été
// échangée entre deux essais. Montée: lecture d'un secteur (~1 ms) pour détecter un retrait à chaud.
void AudioService::sdWatchStep() {
  static uint8_t sector[kSectorBytes];
  xSemaphoreTake(sdMutex_, portMAX_DELAY);
  if (sdState_ == SdState::Ready) {
    if (!mountedCardResponds(sector)) {
      handleSdLost();
    }
  } else if (sdState_ == SdState::Unreadable && millis() - lastFullMountMs_ < totobox::kSdUnreadableRemountMs) {
    if (!cardResponds()) {
      publishSdState(SdState::Absent);
    }
  } else {
    mountSd();
  }
  xSemaphoreGive(sdMutex_);
}

// Une seule initialisation SPI quand la carte est absente (c'est elle qui attend); si la carte répond, montage
// normal, qui échoue seulement si elle n'a pas de système de fichiers lisible (SD.begin() ne distingue pas les deux).
void AudioService::mountSd() {
  lastFullMountMs_ = millis();
  // Carte retirée à chaud: on libère l'ancien montage avant de chercher la nouvelle (sans effet sinon).
  SD.end();
  if (!cardResponds()) {
    publishSdState(SdState::Absent);
    return;
  }
  publishSdState(SD.begin(totobox::kSdCs) ? SdState::Ready : SdState::Unreadable);
}

// La carte répond-elle (initialisation SPI réussie), sans la monter.
bool AudioService::cardResponds() {
  const uint8_t pdrv = sdcard_init(totobox::kSdCs, &SPI, 4000000);
  if (pdrv == 0xFF) {
    return false;
  }
  const bool present = (disk_initialize(pdrv) & STA_NOINIT) == 0 && sdcard_type(pdrv) != CARD_NONE &&
                       sdcard_type(pdrv) != CARD_UNKNOWN;
  sdcard_uninit(pdrv);
  return present;
}

// Carte montée: lecture brute du secteur 0. Une carte retirée (ou réinsérée, donc à réinitialiser) ne répond plus.
// Le verrou SPI de la lib sérialise cette lecture avec celles de la tâche audio et du serveur web.
bool AudioService::mountedCardResponds(uint8_t* sectorBuffer) {
  return sdReady_ && SD.readRAW(sectorBuffer, 0);
}

// Retrait à chaud d'une carte montée: arrêt de la lecture en gardant sa position pour le signet (relevée par
// loop()), puis état Absent. Le démontage (SD.end()) est fait par la tâche de surveillance au prochain essai.
void AudioService::handleSdLost() {
  Lock lock(mutex_);
  if (sdState_ != SdState::Ready) {
    return;
  }
  logLine("audio", "Carte SD retirée");
  if (playing_) {
    // Dernier instantané valide: la position en direct n'a plus de sens, le fichier ne se lit plus.
    sdLostTrack_ = currentTrackIndex_;
    sdLostPoint_ = lastPoint_;
    sdLostPending_ = true;
    logLine("audio", "Lecture interrompue à %lums (octet %lu)", static_cast<unsigned long>(sdLostPoint_.ms),
            static_cast<unsigned long>(sdLostPoint_.byte));
  }
  stop();
  publishSdState(SdState::Absent);
}

bool AudioService::consumeSdLostPlayback(uint16_t& trackIndex, ResumePoint& point) {
  Lock lock(mutex_);
  if (!sdLostPending_) {
    return false;
  }
  sdLostPending_ = false;
  trackIndex = sdLostTrack_;
  point = sdLostPoint_;
  return true;
}

// Sous le verrou audio: playPath() lit sdReady_ sous ce même verrou.
void AudioService::publishSdState(SdState state) {
  Lock lock(mutex_);
  sdState_ = state;
  sdReady_ = state == SdState::Ready;
}

bool AudioService::formatSd() {
  xSemaphoreTake(sdMutex_, portMAX_DELAY);
  const bool ready = formatSdHeld();
  xSemaphoreGive(sdMutex_);
  return ready;
}

bool AudioService::formatSdHeld() {
  if (sdState_ != SdState::Unreadable) {
    return sdState_ == SdState::Ready;
  }
  logLine("audio", "Formatage de la carte SD");
  const uint32_t startedAt = millis();
  const uint8_t pdrv = sdcard_init(totobox::kSdCs, &SPI, 4000000);
  if (pdrv == 0xFF) {
    return false;
  }
  // format_if_empty: f_mkfs (FAT32 ou FAT16 selon la taille, l'exFAT n'est pas disponible) si aucun système de
  // fichiers FAT n'est trouvé, puis montage. On démonte ensuite pour laisser SD.begin() reprendre la main.
  const bool formatted = sdcard_mount(pdrv, "/sd", 1, true);
  sdcard_unmount(pdrv);
  sdcard_uninit(pdrv);
  logLine("audio", "Formatage %s en %lu s", formatted ? "terminé" : "échoué",
          static_cast<unsigned long>((millis() - startedAt) / 1000));
  if (!formatted) {
    return false;
  }
  mountSd();
  if (sdState_ != SdState::Ready) {
    return false;
  }
  SD.mkdir("/content");
  return true;
}

void AudioService::setVolumePercent(uint8_t percent) {
  Lock lock(mutex_);
  audio_->setVolume(mapVolume(percent));
}

uint32_t AudioService::currentPositionMs() {
  Lock lock(mutex_);
  return positionMsLocked();
}

uint32_t AudioService::positionMsLocked() {
  if (!playing_) {
    return pausedAtMs_;
  }
  if (!byteResumed_) {
    return audio_->getAudioCurrentTime() * 1000UL;
  }
  if (timeAnchorPending_) {
    return resumeBaseMs_;
  }
  const uint32_t libTime = audio_->getAudioCurrentTime();
  return resumeBaseMs_ + (libTime > libTimeAnchorSec_ ? libTime - libTimeAnchorSec_ : 0) * 1000UL;
}

// Octet en cours de lecture: position de lecture du fichier moins ce qui attend encore dans le tampon de la lib.
uint32_t AudioService::positionByteLocked() {
  if (!playing_ || !audio_->isRunning()) {
    return 0;
  }
  const uint32_t filePos = audio_->getFilePos();
  const uint32_t buffered = audio_->inBufferFilled();
  // Carte SD retirée: la lib continue de tourner et position() renvoie -1 (0xFFFFFFFF), hors du fichier.
  if (filePos > audio_->getFileSize()) {
    return 0;
  }
  return filePos > buffered ? filePos - buffered : 0;
}

String AudioService::currentPath() const {
  Lock lock(mutex_);
  return currentPath_;
}

uint16_t AudioService::currentTrackIndex() const {
  Lock lock(mutex_);
  return currentTrackIndex_;
}

uint16_t AudioService::trackCount() const {
  Lock lock(mutex_);
  return playlist_.size();
}

bool AudioService::consumePlaybackFinished() {
  Lock lock(mutex_);
  const bool finished = playbackFinished_;
  playbackFinished_ = false;
  return finished;
}

bool AudioService::buildPlaylist(const String& path) {
  playlist_.clear();

  File node = SD.open(path.c_str());
  if (!node) {
    logLine("audio", "Impossible d'ouvrir %s", path.c_str());
    return false;
  }

  if (node.isDirectory()) {
    File entry = node.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        String entryPath = entry.name();
        if (!entryPath.startsWith("/")) {
          if (path == "/") {
            entryPath = "/" + entryPath;
          } else {
            entryPath = path + "/" + entryPath;
          }
        }

        if (isAudioFile(entryPath)) {
          playlist_.push_back(entryPath);
        }
      }
      entry = node.openNextFile();
    }

    std::sort(playlist_.begin(), playlist_.end(), [](const String& left, const String& right) {
      return strcasecmp(left.c_str(), right.c_str()) < 0;
    });
  } else if (isAudioFile(path)) {
    playlist_.push_back(path);
  }

  node.close();
  return !playlist_.empty();
}

bool AudioService::startCurrentTrack(uint32_t resumeMs, uint32_t resumeByte) {
  if (currentTrackIndex_ >= playlist_.size()) {
    return false;
  }

  currentPath_ = playlist_[currentTrackIndex_];
  pausedAtMs_ = resumeMs;
  lastPoint_.ms = resumeMs;
  lastPoint_.byte = resumeByte;
  lastPointAtMs_ = millis();
  playbackFinished_ = false;
  fileReadStart = millis(); // Record the timestamp when file reading starts
  pendingResumeSec_ = 0;
  byteResumed_ = false;
  timeAnchorPending_ = false;
  // Signet au-delà de la fin du fichier (fichier remplacé, ou signet faussé): on repart du début de la piste.
  if (resumeByte > 0) {
    File file = SD.open(currentPath_.c_str());
    if (!file || resumeByte >= file.size()) {
      logLine("audio", "Signet hors du fichier (octet %lu): lecture depuis le début", static_cast<unsigned long>(resumeByte));
      resumeByte = 0;
      resumeMs = 0;
    }
  }
  uint32_t startByte = 0;
  if (resumeByte > 0 && supportsByteResume(currentPath_)) {
    // Reprise exacte: le 3e paramètre de connecttoFS() est une position en OCTETS, recalée sur une trame par la lib.
    startByte = resumeByte;
    byteResumed_ = true;
    timeAnchorPending_ = true;
    resumeBaseMs_ = resumeMs;
    logLine("audio", "Reprise à l'octet %lu (%lus)", static_cast<unsigned long>(resumeByte),
            static_cast<unsigned long>(resumeMs / 1000UL));
  } else if (resumeMs >= 1000UL && supportsSecondResume(currentPath_)) {
    // Ancien marque-page sans octet: on démarre au début puis on se repositionne dans tick().
    pendingResumeSec_ = static_cast<uint16_t>(std::min<uint32_t>(resumeMs / 1000UL, UINT16_MAX));
  } else if (resumeMs >= 1000UL || resumeByte > 0) {
    logLine("audio", "Reprise impossible pour ce format: lecture depuis le début");
  }
  playing_ = audio_->connecttoFS(SD, currentPath_.c_str(), startByte);
  if (!playing_) {
    logLine("audio", "Échec du démarrage de %s", currentPath_.c_str());
    currentPath_ = "";
    pausedAtMs_ = 0;
  }
  logLine("audio", "Lecture de %s", currentPath_.c_str());
  return playing_;
}

bool AudioService::isAudioFile(const String& path) const {
  String lower = path;
  lower.toLowerCase();
  return lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".flac") ||
         lower.endsWith(".aac") || lower.endsWith(".m4a") || lower.endsWith(".ogg") ||
         lower.endsWith(".opus");
}

uint8_t AudioService::mapVolume(uint8_t percent) const {
  return map(constrain(percent, 0, 100), 0, 100, 0, 21);
}

// Callbacks de la lib audio, appelés depuis la tâche audio. Le temps affiché est relatif au début de la piste.
void audio_showstation(const char *info)
{
  logLine("audio", "+%lums format: %s", millis() - fileReadStart, info);
}
void audio_error_demo(const char *info)
{
  logLine("audio", "+%lums ERREUR DE DECODAGE: %s", millis() - fileReadStart, info);
}
void audio_eof_mp3(const char *info)
{
  logLine("audio", "+%lums fin du fichier: %s", millis() - fileReadStart, info);
}
void audio_eof_stream(const char *info)
{
  logLine("audio", "+%lums fin du flux: %s", millis() - fileReadStart, info);
}
void audio_info(const char *info)
{
  logLine("audio", "+%lums %s", millis() - fileReadStart, info);
}
void audio_id3data(const char *info)
{
  logLine("audio", "+%lums id3: %s", millis() - fileReadStart, info);
}
