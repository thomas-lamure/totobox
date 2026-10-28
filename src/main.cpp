// SPDX-License-Identifier: GPL-3.0-or-later
#include <Arduino.h>
#include <esp_mac.h>
#include <esp_partition.h>

#include <vector>

#include "access_lock.h"
#include "app_types.h"
#include "audio_service.h"
#include "config_store.h"
#include "factory_data.h"
#include "factory_reset.h"
#include "secret_channel.h"
#include "led_controller.h"
#include "logger.h"
#include "network_service.h"
#include "power_manager.h"
#include "nfc_service.h"
#include "totobox_config.h"
#include "version.h"
#include "volume_knob.h"
#include "web_admin.h"

#define CLOCKLESS_PIXEL_CLOCK_HZ (24 * 100 * 1000)

namespace {

ConfigStore configStore;
DeviceConfig config;
RuntimeState runtime;
std::vector<CardBinding> bindings;
LedController leds;
VolumeKnob knob;
NFCService nfc;
AudioService audio;
WebAdmin* webAdmin = nullptr;
NetworkService network;
PowerManager power;

CardBinding* findBindingByUid(const String& uid) {
  for (CardBinding& binding : bindings) {
    if (binding.uid == uid) {
      return &binding;
    }
  }
  return nullptr;
}

// Mot de passe d'usine lu dans la partition "mfg" (scripts/provision.py); vide si absente ou vierge.
String readFactoryApPassword() {
  const esp_partition_t* partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "mfg");
  if (partition == nullptr) {
    logLine("main", "Pas de partition d'usine (table de partitions ancienne)");
    return "";
  }
  std::vector<uint8_t> buffer(partition->size);
  factory_data::FactoryData data;
  if (esp_partition_read(partition, 0, buffer.data(), buffer.size()) != ESP_OK ||
      !factory_data::parse(buffer.data(), buffer.size(), data)) {
    logLine("main", "Partition d'usine vierge: mot de passe du point d'accès tiré par la boîte");
    return "";
  }
  logLine("main", "Données d'usine lues");
  return data.apPassword;
}

// Met à jour l'état de la carte SD vu par le reste de la boîte (LEDs, page web).
void applySdState(SdState state) {
  if (state == runtime.sdState) {
    return;
  }
  runtime.sdState = state;
  runtime.sdReady = state == SdState::Ready;
  switch (state) {
    case SdState::Ready:
      logLine("main", "Carte SD prête");
      if (runtime.nfcReady && runtime.state == DeviceState::Error) {
        runtime.state = DeviceState::Ready;
      }
      break;
    case SdState::Unreadable:
      logLine("main", "Carte SD illisible: elle doit être formatée en FAT32 (exFAT non pris en charge)");
      runtime.state = DeviceState::Error;
      break;
    case SdState::Absent:
      logLine("main", "Carte SD absente");
      runtime.state = DeviceState::Error;
      break;
  }
}

void persistBindings();
void handleKnownCard(CardBinding& binding);

// Histoire interrompue par le retrait de la carte SD, reprise à la réinsertion si sa carte NFC est toujours posée.
bool resumeAfterSd = false;

// Carte SD insérée, remplacée ou retirée après le démarrage: détectée sans redémarrer par la tâche de surveillance
// d'AudioService; loop() ne fait que relever son état (sans attente).
void syncSdState() {
  uint16_t trackIndex = 0;
  AudioService::ResumePoint point;
  if (audio.consumeSdLostPlayback(trackIndex, point) && !runtime.activeCardUid.isEmpty()) {
    CardBinding* binding = findBindingByUid(runtime.activeCardUid);
    if (binding != nullptr) {
      binding->resumeTrackIndex = trackIndex;
      binding->resumeMs = point.ms;
      binding->resumeByte = point.byte;
      persistBindings();
      resumeAfterSd = true;
      logLine("main", "Carte SD retirée pendant la lecture: signet gardé pour %s", binding->uid.c_str());
    }
  }

  const bool wasReady = runtime.sdReady;
  applySdState(audio.sdState());
  if (!wasReady && runtime.sdReady && resumeAfterSd) {
    resumeAfterSd = false;
    CardBinding* binding = runtime.activeCardUid.isEmpty() ? nullptr : findBindingByUid(runtime.activeCardUid);
    if (binding != nullptr) {
      logLine("main", "Carte SD revenue: reprise de %s", binding->uid.c_str());
      handleKnownCard(*binding);
    }
  }
}

// État au repos: "Prête" seulement si la carte SD et le lecteur NFC le sont. Une carte posée ou retirée, ou une
// histoire finie, ne doit pas effacer une erreur matérielle (LEDs rouges, statut de la page).
DeviceState restingState() {
  return runtime.sdReady && runtime.nfcReady ? DeviceState::Ready : DeviceState::Error;
}

void persistBindings() {
  configStore.saveBindings(bindings);
}

void handleKnownCard(CardBinding& binding) {
  logLine("main", "Carte connue %s", binding.uid.c_str());
  runtime.activeCardUid = binding.uid;
  runtime.activeMediaPath = binding.mediaPath;
  runtime.activeTrackIndex = binding.resumeTrackIndex;
  runtime.activePositionMs = binding.resumeMs;

  if (binding.mode == PlaybackMode::SdLocal) {
    logLine("main", "Lecture demandée: %s", binding.mediaPath.c_str());
    if (audio.playPath(binding.mediaPath, binding.resumeTrackIndex, binding.resumeMs, binding.resumeByte)) {
      runtime.activeMediaPath = audio.currentPath();
      runtime.activeTrackIndex = audio.currentTrackIndex();
      logLine("main", "Lecture démarrée: %s", runtime.activeMediaPath.c_str());
      runtime.state = DeviceState::Playing;
      return;
    }

    logLine("main", "Échec de lecture: %s", binding.mediaPath.c_str());
    runtime.state = DeviceState::Error;
    return;
  }

  runtime.state = DeviceState::Paused;
}

void handleCardPresented(const String& uid) {
  power.noteActivity(runtime);
  logLine("main", "Carte posée %s", uid.c_str());

  // Carte maîtresse: déverrouille les réglages, ne lance rien.
  if (access_lock::isLockCard(config, uid)) {
    const MasterCard* card = access_lock::findCard(config, uid);
    logLine("main", "Carte maîtresse %s: réglages déverrouillés", card->label.isEmpty() ? uid.c_str() : card->label.c_str());
    runtime.unlocked = true;
    runtime.activeCardUid = uid;
    runtime.activeMediaPath = "";
    runtime.activePositionMs = 0;
    runtime.state = restingState();
    // Toute la bande en blanc quelques secondes pour confirmer sur la boîte.
    runtime.volumeOverlayActive = true;
    runtime.lastVolumeOverlayMs = millis();
    leds.showPercentOverlay(100);
    return;
  }

  CardBinding* binding = findBindingByUid(uid);
  if (binding == nullptr) {
    logLine("main", "Carte inconnue %s", uid.c_str());
    runtime.lastUnknownCardUid = uid;
    runtime.activeCardUid = uid;
    runtime.activeMediaPath = "";
    runtime.activePositionMs = 0;
    runtime.state = DeviceState::UnknownCard;
    return;
  }

  handleKnownCard(*binding);
}

void handleCardRemoved() {
  if (runtime.activeCardUid.isEmpty()) {
    return;
  }
  power.noteActivity(runtime);
  logLine("main", "Carte retirée %s", runtime.activeCardUid.c_str());
  resumeAfterSd = false;

  CardBinding* binding = findBindingByUid(runtime.activeCardUid);
  if (binding != nullptr && audio.isPlaying()) {
    binding->resumeTrackIndex = audio.currentTrackIndex();
    const AudioService::ResumePoint point = audio.pause();
    binding->resumeMs = point.ms;
    binding->resumeByte = point.byte;
    runtime.activeTrackIndex = binding->resumeTrackIndex;
    runtime.activePositionMs = binding->resumeMs;
    persistBindings();
  }

  runtime.state = restingState();
  runtime.activeCardUid = "";
  runtime.activeMediaPath = "";
  runtime.activeTrackIndex = 0;
  runtime.activePositionMs = 0;
}

void showVolumeOverlay();

// Veille profonde: termine proprement ce qui doit l'être, éteint les LEDs et dort (ne revient pas).
// Une carte encore posée est confiée au PowerManager pour ne pas réveiller la boîte en boucle.
void goToSleep() {
  logLine("power", "Aucune interaction depuis %lu min: mise en veille",
          static_cast<unsigned long>(totobox::kIdleSleepMs / 60000UL));
  if (runtime.volumeSaveDue) {
    configStore.saveConfig(config);
    runtime.volumeSaveDue = false;
  }
  audio.stop();
  leds.off();
  power.sleep(nfc, runtime.activeCardUid);
}

void handleVolume() {
  // Serial.print("-v");
  knob.tick();
  if (knob.wasButtonPressed()) {
    power.noteActivity(runtime);
  }
  const int8_t delta = knob.takeDelta();
  if (delta == 0) {
    return;
  }
  power.noteActivity(runtime);

  int nextVolume = static_cast<int>(config.currentVolume) + delta * totobox::kVolumeStepPercent;
  nextVolume = constrain(nextVolume, config.volumeMin, config.volumeMax);
  config.currentVolume = static_cast<uint8_t>(nextVolume);
  runtime.volumeSaveDue = true;
  runtime.lastVolumeChangeMs = millis();
  // Affiché même en butée, pour montrer que la molette est prise en compte.
  showVolumeOverlay();
}

// La jauge LED représente la position du volume entre les limites autorisées:
// vide au minimum, pleine au maximum.
void showVolumeOverlay() {
  const uint8_t range = config.volumeMax > config.volumeMin ? config.volumeMax - config.volumeMin : 0;
  const uint8_t above = config.currentVolume > config.volumeMin ? config.currentVolume - config.volumeMin : 0;
  const uint8_t percentOfRange = range > 0 ? min<uint16_t>(100, static_cast<uint16_t>(above) * 100 / range) : 100;
  runtime.volumeOverlayActive = true;
  runtime.lastVolumeOverlayMs = millis();
  leds.showPercentOverlay(percentOfRange);
}

// Applique tout changement de volume, qu'il vienne de la molette ou de l'interface web.
void applyVolumeChanges() {
  static uint8_t appliedVolume = config.currentVolume;
  if (config.currentVolume != appliedVolume) {
    audio.setVolumePercent(config.currentVolume);
    appliedVolume = config.currentVolume;
    showVolumeOverlay();
  }
}

void saveVolumeIfIdle() {
  // Une écriture NVS fige le cache flash des deux cœurs: on attend que le volume soit stable.
  if (runtime.volumeSaveDue && millis() - runtime.lastVolumeChangeMs >= totobox::kVolumeSaveDelayMs) {
    configStore.saveConfig(config);
    runtime.volumeSaveDue = false;
  }
}

void updateVolumeOverlay() {
  // Serial.print("-o");
  if (!runtime.volumeOverlayActive) {
    return;
  }

  if (millis() - runtime.lastVolumeOverlayMs >= totobox::kVolumeOverlayMs) {
    runtime.volumeOverlayActive = false;
  }
}

void pollNfc() {
  // Non bloquant: tick() ne fait que lancer une recherche ou lire une réponse signalée par l'IRQ du PN532.
  const NFCService::Event event = nfc.tick();
  const uint32_t now = millis();

  if (event == NFCService::Event::CardPresent) {
    runtime.lastCardSeenMs = now;
    const String uid = nfc.lastUid();
    if (uid != runtime.activeCardUid) {
      handleCardPresented(uid);
    }
    return;
  }

  // Retrait confirmé seulement après plusieurs recherches sans carte (kTagRemovalGraceMs).
  if (event == NFCService::Event::NoCard && !runtime.activeCardUid.isEmpty() &&
      now - runtime.lastCardSeenMs >= totobox::kTagRemovalGraceMs) {
    handleCardRemoved();
  }
}

}  // namespace

void setup() {
  logBegin();
  logLine("main", "TotoBox %s (%s)", totobox::kFirmwareVersion, totobox::kFirmwareBuild);
  // Avant tout le reste: un réveil de vérification de carte peut se rendormir ici sans démarrer la boîte.
  power.begin(nfc);

  Serial.setDebugOutput(true);

  delay(5);

  leds.begin();
  // Pont d'atelier GPIO47-masse: remise à zéro avant même de lire la configuration.
  factory_reset::checkJumperAtBoot(leds);
  knob.begin();

  configStore.begin();
  config = configStore.loadConfig();
  bindings = configStore.loadBindings();

  // Point d'accès propre à chaque boîte: nom tiré de l'adresse MAC; mot de passe d'usine s'il a été programmé
  // sur la ligne de production, sinon tiré au hasard et enregistré en NVS.
  config.factoryApPassword = readFactoryApPassword();
  uint8_t apMac[6] = {};
  esp_read_mac(apMac, ESP_MAC_WIFI_SOFTAP);
  if (ConfigStore::ensureAccessPointDefaults(config, apMac)) {
    configStore.saveConfig(config);
    logLine("net", "Point d'accès de secours initialisé");
  }
  // Clés pour recevoir les secrets de la page chiffrés (mots de passe, code de secours).
  if (secret_channel::begin(config)) {
    configStore.saveConfig(config);
  }
  // Mot de passe tiré par la boîte: seul moyen de le retrouver sans l'interface, la console série (USB), affichée
  // après un flash. Celui d'usine est sur l'étiquette, un mot de passe choisi dans les réglages n'y apparaît jamais.
  if (config.apPasswordGenerated) {
    logLine("net", "Point d'accès de secours: %s, mot de passe %s", config.apSsid.c_str(), config.apPassword.c_str());
  } else if (!config.factoryApPassword.isEmpty() && config.apPassword == config.factoryApPassword) {
    logLine("net", "Point d'accès de secours: %s, mot de passe d'usine (étiquette)", config.apSsid.c_str());
  } else {
    logLine("net", "Point d'accès de secours: %s, mot de passe personnalisé", config.apSsid.c_str());
  }

  // Le Wi-Fi se connecte en arrière-plan: le NFC et l'audio sont prêts sans attendre le réseau.
  network.begin(config, runtime);

  runtime.state = DeviceState::Booting;
  audio.begin(config.currentVolume);
  runtime.sdState = audio.sdState();
  runtime.sdReady = runtime.sdState == SdState::Ready;
  if (!runtime.sdReady)
  {
    logLine("main", "Carte SD %s", runtime.sdState == SdState::Unreadable ? "illisible (pas de FAT32)" : "absente");
    runtime.state = DeviceState::Error;
  }

  runtime.nfcReady = nfc.begin();
  if (!runtime.nfcReady)
  {
    logLine("main", "Lecteur NFC non initialisé");
    runtime.state = DeviceState::Error;
  }

  if (runtime.sdReady && runtime.nfcReady) {
    logLine("main", "Prête");
    runtime.state = DeviceState::Ready;
  }

  logLine("main", "Démarrage de l'interface web");
  static WebAdmin web(configStore, config, bindings, runtime);
  webAdmin = &web;
  // Pendant un envoi, loop() est suspendu: seules les LEDs continuent. Molette et NFC attendent la fin de
  // l'envoi, une carte posée entre-temps est donc lue ensuite (jamais d'accès SD concurrent avec l'envoi).
  webAdmin->setUploadTick([]() { leds.tick(runtime); });
  // Formatage d'une carte illisible demandé depuis la page (bloquant: la requête attend la fin).
  webAdmin->setSdFormatter([]() {
    leds.fill(CRGB::Yellow, totobox::kLedCount);
    audio.formatSd();
    applySdState(audio.sdState());
    return runtime.sdReady;
  });
  webAdmin->begin();

  // Le délai avant la mise en veille part de la fin du démarrage.
  power.noteActivity(runtime);
}

void loop() {
  if (runtime.factoryResetAtMs != 0 && static_cast<int32_t>(millis() - runtime.factoryResetAtMs) >= 0) {
    audio.stop();
    factory_reset::eraseAndRestart(leds);
  }
  if (audio.consumePlaybackFinished() && !runtime.activeCardUid.isEmpty()) {
    logLine("main", "Lecture terminée pour %s", runtime.activeCardUid.c_str());
    CardBinding* binding = findBindingByUid(runtime.activeCardUid);
    if (binding != nullptr) {
      binding->resumeTrackIndex = 0;
      binding->resumeMs = 0;
      binding->resumeByte = 0;
      persistBindings();
    }
    runtime.state = restingState();
    runtime.activePositionMs = 0;
  }

  syncSdState();
  handleVolume();
  saveVolumeIfIdle();
  updateVolumeOverlay();
  pollNfc();

  // Carte inconnue toujours posée qui vient d'être associée depuis l'interface web: on lance la lecture.
  if (runtime.state == DeviceState::UnknownCard && !runtime.activeCardUid.isEmpty()) {
    CardBinding* binding = findBindingByUid(runtime.activeCardUid);
    if (binding != nullptr) {
      handleKnownCard(*binding);
    }
  }

  applyVolumeChanges();

  runtime.activeMediaPath = audio.currentPath();
  runtime.activeTrackIndex = audio.currentTrackIndex();
  runtime.activeTrackCount = audio.trackCount();
  runtime.activePositionMs = audio.currentPositionMs();
  runtime.audioPlaying = audio.isPlaying();
  if (runtime.audioPlaying) {
    power.noteActivity(runtime);
  } else if (power.isIdle(runtime)) {
    goToSleep();
  }

  // Réglages réseau modifiés depuis l'interface: appliqués sans redémarrage.
  if (runtime.networkConfigChanged) {
    runtime.networkConfigChanged = false;
    network.requestReconfigure();
  }
  // Portail captif et mDNS suivent les changements de réseau (connexion, point d'accès démarré ou arrêté).
  if (network.tick() && webAdmin != nullptr) {
    webAdmin->onNetworkChanged();
  }

  // Pendant la lecture, WebAdmin ne sert que les requêtes légères (statut, réglages, associations).
  if (webAdmin != nullptr) {
    webAdmin->loop();
  }

  if (!audio.isPlaying() && runtime.state == DeviceState::Playing && runtime.activeCardUid.isEmpty()) {
    runtime.state = restingState();
  }

  // if (!audio.isPlaying())
  // {
  leds.tick(runtime);
  // }
}
