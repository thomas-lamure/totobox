// SPDX-License-Identifier: GPL-3.0-or-later
#include <Arduino.h>
#include <esp_mac.h>
#include <esp_partition.h>
#include <WiFi.h>
#include <time.h>

#include <vector>

#include "access_lock.h"
#include "app_types.h"
#include "audio_service.h"
#include "card_flow.h"
#include "config_store.h"
#include "factory_data.h"
#include "factory_reset.h"
#include "ha_rules.h"
#include "secret_channel.h"
#include "led_controller.h"
#include "logger.h"
#include "network_service.h"
#include "power_manager.h"
#include "nfc_service.h"
#include "update_rules.h"
#include "totobox_config.h"
#if !TOTOBOX_OFFLINE
#include "ha_service.h"
#include "update_service.h"
#endif
#include "version.h"
#include "volume_knob.h"
#include "volume_rules.h"
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
#if !TOTOBOX_OFFLINE
UpdateService updater;
HomeAssistantService homeAssistant;
#endif
PowerManager power;

CardBinding* findBindingByUid(const String& uid) {
  return card_flow::findBinding(bindings, uid);
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

// Carte SD insérée, remplacée ou retirée après le démarrage: détectée sans redémarrer par la tâche de surveillance
// d'AudioService; loop() ne fait que relever son état (sans attente).
void syncSdState() {
  uint16_t trackIndex = 0;
  AudioService::ResumePoint point;
  if (audio.consumeSdLostPlayback(trackIndex, point)) {
    CardBinding* binding = findBindingByUid(runtime.activeCardUid);
    card_flow::Bookmark stoppedAt;
    stoppedAt.track = trackIndex;
    stoppedAt.ms = point.ms;
    stoppedAt.byte = point.byte;
    if (card_flow::onSdLost(runtime, binding, stoppedAt)) {
      persistBindings();
      logLine("main", "Carte SD retirée pendant la lecture: signet gardé pour %s", binding->uid.c_str());
    }
  }

  const bool wasReady = runtime.sdReady;
  applySdState(audio.sdState());
  if (card_flow::shouldResumeAfterSd(runtime, wasReady)) {
    CardBinding* binding = findBindingByUid(runtime.activeCardUid);
    if (binding != nullptr) {
      logLine("main", "Carte SD revenue: reprise de %s", binding->uid.c_str());
      handleKnownCard(*binding);
    }
  }
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
    // L'histoire passe avant un téléchargement depuis la page: la carte SD n'est lue que par la tâche audio.
    if (webAdmin != nullptr) {
      webAdmin->cancelDownload();
    }
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
#if !TOTOBOX_OFFLINE
  if (ha_rules::shouldPublishCard(config, uid)) {
    homeAssistant.notifyCardPresented(uid);
  }
#endif

  switch (card_flow::onCardPresented(runtime, config, bindings, uid)) {
    case card_flow::Presented::Unlock: {
      const MasterCard* card = access_lock::findCard(config, uid);
      logLine("main", "Carte maîtresse %s: réglages déverrouillés", card->label.isEmpty() ? uid.c_str() : card->label.c_str());
      // Confirmation sur la boîte: barre verte qui s'ouvre en deux.
      runtime.volumeOverlayActive = true;
      runtime.lastVolumeOverlayMs = millis();
      runtime.volumeOverlayDurationMs = totobox::kUnlockOverlayMs;
      leds.showUnlockOverlay();
      return;
    }
    case card_flow::Presented::Unknown:
      logLine("main", "Carte inconnue %s", uid.c_str());
      return;
    case card_flow::Presented::Play:
      handleKnownCard(*findBindingByUid(uid));
      return;
  }
}

// Carte retirée, ou remplacée par une autre: la lecture s'arrête toujours (exigence produit), le signet est gardé.
void handleCardRemoved() {
  power.noteActivity(runtime);
  logLine("main", "Carte retirée %s", runtime.activeCardUid.c_str());
#if !TOTOBOX_OFFLINE
  if (ha_rules::shouldPublishCard(config, runtime.activeCardUid)) {
    homeAssistant.notifyCardRemoved(runtime.activeCardUid);
  }
#endif

  card_flow::Bookmark stoppedAt;
  const bool wasPlaying = audio.isPlaying();
  if (wasPlaying) {
    stoppedAt.track = audio.currentTrackIndex();
    const AudioService::ResumePoint point = audio.pause();
    stoppedAt.ms = point.ms;
    stoppedAt.byte = point.byte;
  }
  CardBinding* binding = findBindingByUid(runtime.activeCardUid);
  if (card_flow::onCardRemoved(runtime, binding, wasPlaying ? &stoppedAt : nullptr)) {
    persistBindings();
  }
}

void showVolumeOverlay();

#if !TOTOBOX_OFFLINE
constexpr uint32_t kHaShutdownMs = 500;
#endif

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
#if !TOTOBOX_OFFLINE
  // Home Assistant voit l'appareil indisponible tout de suite (sinon après l'expiration de la connexion).
  homeAssistant.shutdown(kHaShutdownMs);
#endif
  // Deux cartes posées: l'une d'elles est confiée au PowerManager, sans quoi elles réveilleraient la boîte aussitôt.
  power.sleep(nfc, runtime.twoCards ? runtime.twoCardsUid : runtime.activeCardUid);
}

// Dernier cran de molette (0 = aucun): la recherche de carte attend qu'on la lâche (card_flow::shouldSearch).
uint32_t lastKnobTurnMs = 0;

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
  lastKnobTurnMs = millis();

  config.currentVolume = volume_rules::afterDetents(config.currentVolume, delta, config.volumeMin, config.volumeMax,
                                                    totobox::kVolumeStepPercent);
  runtime.volumeSaveDue = true;
  runtime.lastVolumeChangeMs = millis();
  // Affiché même en butée, pour montrer que la molette est prise en compte.
  showVolumeOverlay();
}

// La jauge LED représente la position du volume entre les limites autorisées:
// vide au minimum, pleine au maximum.
void showVolumeOverlay() {
  runtime.volumeOverlayActive = true;
  runtime.lastVolumeOverlayMs = millis();
  runtime.volumeOverlayDurationMs = totobox::kVolumeOverlayMs;
  leds.showPercentOverlay(volume_rules::gaugePercent(config.currentVolume, config.volumeMin, config.volumeMax));
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

#if !TOTOBOX_OFFLINE
// ---------- Home Assistant ----------

// L'état est confié au service MQTT à ce rythme (il publie lui-même ce qui a changé).
constexpr uint32_t kHaUpdateMs = 250;

// Arrêt demandé depuis Home Assistant: comme un retrait de carte, mais la carte reste posée (pas de relance).
void stopFromRemote() {
  card_flow::Bookmark stoppedAt;
  const bool wasPlaying = audio.isPlaying();
  if (wasPlaying) {
    stoppedAt.track = audio.currentTrackIndex();
    const AudioService::ResumePoint point = audio.pause();
    stoppedAt.ms = point.ms;
    stoppedAt.byte = point.byte;
  }
  if (card_flow::onRemoteStop(runtime, findBindingByUid(runtime.activeCardUid), wasPlaying ? &stoppedAt : nullptr)) {
    persistBindings();
    logLine("ha", "Lecture arrêtée depuis Home Assistant (%s)", runtime.activeCardUid.c_str());
  }
}

ha_rules::State haState() {
  ha_rules::State state;
  state.state = ha_rules::stateName(runtime);
  // Carte maîtresse posée: ni son UID ni son nom.
  if (ha_rules::shouldPublishCard(config, runtime.activeCardUid)) {
    state.card = runtime.activeCardUid;
    const CardBinding* binding = findBindingByUid(runtime.activeCardUid);
    if (binding != nullptr) {
      state.story = binding->label.isEmpty() ? binding->mediaPath : binding->label;
    }
  }
  if (!state.story.isEmpty() && runtime.activeTrackCount > 0) {
    state.track = runtime.activeTrackIndex + 1;
    state.tracks = runtime.activeTrackCount;
    state.positionSec = runtime.activePositionMs / 1000;
  }
  state.volume = config.currentVolume;
  state.volumeMin = config.volumeMin;
  state.volumeMax = config.volumeMax;
  state.sd = sdStateToString(runtime.sdState);
  state.locked = access_lock::isLocked(config, runtime);
  state.rssi = runtime.wifiConnected ? static_cast<int8_t>(WiFi.RSSI()) : 0;
  return state;
}

void homeAssistantStep() {
  if (runtime.haConfigChanged) {
    runtime.haConfigChanged = false;
    homeAssistant.configure(config);
  }

  static uint32_t lastUpdateMs = 0;
  if (millis() - lastUpdateMs >= kHaUpdateMs) {
    lastUpdateMs = millis();
    ha_rules::DeviceInfo device;
    device.name = config.hostname;
    device.version = totobox::kFirmwareVersion;
    device.url = "http://" + config.hostname + ".local/";
    device.volumeMin = config.volumeMin;
    device.volumeMax = config.volumeMax;
    homeAssistant.update(runtime.wifiConnected, device, haState());
    const HomeAssistantService::Status status = homeAssistant.status();
    runtime.haState = status.state;
    runtime.haError = status.error;
  }

  ha_rules::Command command;
  while (homeAssistant.takeCommand(command)) {
    // Pendant une mise à jour, rien ne joue et rien ne doit démarrer.
    if (runtime.updating) {
      continue;
    }
    power.noteActivity(runtime);
    switch (command.type) {
      case ha_rules::CommandType::Volume:
        // Bornes relues ici: elles ont pu changer depuis la réception.
        config.currentVolume = constrain(command.volume, config.volumeMin, config.volumeMax);
        runtime.volumeSaveDue = true;
        runtime.lastVolumeChangeMs = millis();
        logLine("ha", "Volume %u %% depuis Home Assistant", config.currentVolume);
        break;
      case ha_rules::CommandType::Stop:
        stopFromRemote();
        break;
      case ha_rules::CommandType::None:
        break;
    }
  }
}

// ---------- Mise à jour automatique ----------

// Fin de la dernière lecture ou du dernier passage d'une carte (0 = démarrage ou réveil): la recherche automatique
// attend une minute de calme, un enfant réveillant souvent la boîte pour écouter tout de suite.
uint32_t lastBusyMs = 0;
// GitHub injoignable: nouvel essai une heure plus tard (en mémoire: un réveil réessaie, ce qui est voulu).
constexpr uint32_t kAutoCheckRetryMs = 60UL * 60UL * 1000UL;
uint32_t autoCheckRetryAtMs = 0;

// Heure NTP en secondes depuis 1970, 0 tant qu'elle n'est pas connue (pas d'horloge à pile; l'heure est gardée
// pendant la veille profonde).
uint32_t epochNow() {
  const time_t now = time(nullptr);
  return now > 1700000000 ? static_cast<uint32_t>(now) : 0;
}

void autoUpdateStep() {
  const bool listening = runtime.audioPlaying || !runtime.activeCardUid.isEmpty();
  if (listening) {
    lastBusyMs = millis();
  }
  updater.setHold(listening);

  switch (updater.consumeCheckResult()) {
    case UpdateService::CheckResult::UpToDate:
    case UpdateService::CheckResult::Unusable:
    case UpdateService::CheckResult::Installing:
      // Recherche aboutie: la suivante dans 24 h (enregistré avant un éventuel redémarrage).
      config.lastUpdateCheck = epochNow();
      configStore.saveConfig(config);
      break;
    case UpdateService::CheckResult::Unreachable:
      autoCheckRetryAtMs = millis() + kAutoCheckRetryMs;
      break;
    case UpdateService::CheckResult::Deferred:
    case UpdateService::CheckResult::None:
      break;
  }

  update_rules::AutoCheck state;
  state.enabled = config.autoUpdate;
  state.online = runtime.wifiConnected;
  state.busy = updater.active();
  state.retryWaiting = autoCheckRetryAtMs != 0 && static_cast<int32_t>(millis() - autoCheckRetryAtMs) < 0;
  state.nowEpoch = epochNow();
  state.lastCheckEpoch = config.lastUpdateCheck;
  state.quietMs = millis() - lastBusyMs;
  if (update_rules::shouldAutoCheck(state)) {
    logLine("update", "Recherche automatique d'une nouvelle version");
    updater.startCheck(totobox::kUpdateRepo, totobox::kFirmwareVersion);
  }
}

#endif  // !TOTOBOX_OFFLINE

void updateVolumeOverlay() {
  // Serial.print("-o");
  if (!runtime.volumeOverlayActive) {
    return;
  }

  if (millis() - runtime.lastVolumeOverlayMs >= runtime.volumeOverlayDurationMs) {
    runtime.volumeOverlayActive = false;
  }
}

void pollNfc() {
  // Non bloquant: tick() ne fait que lancer une recherche ou lire une réponse signalée par l'IRQ du PN532.
  const NFCService::Event event = nfc.tick();
  if (event == NFCService::Event::None) {
    return;
  }
  const String uid = event == NFCService::Event::CardPresent ? nfc.lastUid() : String();
  const bool hadTwoCards = runtime.twoCards;
  const card_flow::NfcStep step = card_flow::onNfcEvent(runtime, event == NFCService::Event::CardPresent, uid, millis(),
                                                        totobox::kTagRemovalGraceMs);
  if (runtime.twoCards && !hadTwoCards) {
    logLine("main", "Deux cartes posées (%s et %s): lecture arrêtée", runtime.activeCardUid.c_str(), uid.c_str());
  } else if (hadTwoCards && !runtime.twoCards) {
    logLine("main", "Plus qu'une carte posée");
  }
  if (step.stopCurrent) {
    handleCardRemoved();
  }
  if (step.present) {
    handleCardPresented(uid);
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
#if TOTOBOX_OFFLINE
  logLine("main", "Variante hors ligne: ni mise à jour en ligne ni Home Assistant");
#else
  // Home Assistant (MQTT) dans sa tâche du cœur 0; ne fait rien tant que la liaison est désactivée.
  uint8_t staMac[6] = {};
  esp_read_mac(staMac, ESP_MAC_WIFI_STA);
  homeAssistant.begin(staMac);
  homeAssistant.configure(config);
  // Heure par NTP (une fois le Wi-Fi de la maison rejoint): date de la dernière recherche de mise à jour.
  configTime(0, 0, "pool.ntp.org", "time.google.com");
#endif

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
#if !TOTOBOX_OFFLINE
  // Après le montage de LittleFS par l'interface web (la mise à jour y remplace la page).
  updater.begin();
  webAdmin->setUpdateService(&updater);
#endif

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
    if (card_flow::onStoryFinished(runtime, findBindingByUid(runtime.activeCardUid))) {
      persistBindings();
    }
  }

#if !TOTOBOX_OFFLINE
  // Mise à jour en ligne installée: redémarrage sur le nouveau firmware.
  if (updater.restartDue()) {
    audio.stop();
    homeAssistant.shutdown(kHaShutdownMs);
    ESP.restart();
  }
  runtime.updating = updater.busy();
  autoUpdateStep();
  homeAssistantStep();
  const bool updateActive = updater.active();
#else
  const bool updateActive = false;
#endif

  syncSdState();
  handleVolume();
  saveVolumeIfIdle();
  updateVolumeOverlay();
  // Pendant une mise à jour, les cartes posées sont ignorées (pas de lecture pendant l'écriture de la flash).
  // Ni pendant une mise à jour, ni (sans carte posée) pendant qu'on tourne la molette.
  if (!runtime.updating &&
      card_flow::shouldSearch(runtime, millis(), lastKnobTurnMs, totobox::kNfcPauseAfterKnobMs)) {
    pollNfc();
  }

  // Carte inconnue toujours posée qui vient d'être associée depuis l'interface web: on lance la lecture.
  if (card_flow::shouldPlayNewlyBound(runtime, bindings)) {
    handleKnownCard(*findBindingByUid(runtime.activeCardUid));
  }

  applyVolumeChanges();

  runtime.activeMediaPath = audio.currentPath();
  runtime.activeTrackIndex = audio.currentTrackIndex();
  runtime.activeTrackCount = audio.trackCount();
  runtime.activePositionMs = audio.currentPositionMs();
  runtime.activeDurationMs = audio.currentDurationMs();
  runtime.audioPlaying = audio.isPlaying();
  if (runtime.audioPlaying || updateActive) {
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
    runtime.state = card_flow::restingState(runtime);
  }

  // if (!audio.isPlaying())
  // {
  leds.tick(runtime);
  // }
}
