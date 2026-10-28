// SPDX-License-Identifier: GPL-3.0-or-later
#include "web_admin.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_mac.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <mdns.h>

#include "card_backup_sd.h"
#include "library_paths.h"
#include "update_rules.h"
#include "version.h"
#include "logger.h"
#include "network_names.h"
#include "request_guard.h"
#include "secret_channel.h"
#include "totobox_config.h"

#include <algorithm>
#include <vector>

namespace {

using library_paths::kLibraryRoot;
using library_paths::kTrashRoot;
constexpr uint8_t kLibraryMaxDepth = 3;
constexpr uint16_t kDnsPort = 53;

// URL de test de connectivité des téléphones et ordinateurs. Rediriger ces requêtes vers la boîte
// déclenche l'ouverture automatique de la fenêtre "Se connecter au réseau".
constexpr const char* kCaptiveProbePaths[] = {
    "/generate_204",               // Android, Chrome OS
    "/gen_204",                    // Android
    "/hotspot-detect.html",        // iOS, macOS
    "/library/test/success.html",  // iOS (ancien)
    "/connecttest.txt",            // Windows 10+
    "/ncsi.txt",                   // Windows (ancien)
    "/redirect",                   // Windows
    "/canonical.html",             // Firefox
    "/success.txt",                // Firefox
};

struct SdEntryInfo {
  String path;
  bool isDirectory = false;
  uint32_t size = 0;
};

}  // namespace

WebAdmin::WebAdmin(ConfigStore& store, DeviceConfig& config, std::vector<CardBinding>& bindings, RuntimeState& runtime)
    : store_(store), config_(config), bindings_(bindings), runtime_(runtime), server_(80) {}

void WebAdmin::begin() {
  LittleFS.begin(true);
  // Lue une fois: un nouvel envoi de l'image LittleFS (uploadfs) redémarre la boîte.
  File version = LittleFS.open("/ui_version.txt", FILE_READ);
  if (version) {
    uiVersion_ = version.readString();
    uiVersion_.trim();
    version.close();
  }
  logLine("web", "Version de l'interface: %s", uiVersion_.isEmpty() ? "inconnue" : uiVersion_.c_str());
  registerRoutes();
  // Le serveur écoute sur toutes les interfaces: il peut démarrer avant que le réseau soit connecté.
  server_.begin();
}

void WebAdmin::onNetworkChanged() {
  // En point d'accès (y compris pendant l'essai d'un Wi-Fi maison), tout nom de domaine résout vers la boîte
  // pour que le téléphone ouvre l'interface; hors point d'accès, le portail captif s'arrête.
  const bool accessPoint = runtime_.wifiMode == "ap";
  if (accessPoint && !captivePortal_) {
    const IPAddress apIp = WiFi.softAPIP();
    portalHost_ = apIp.toString();
    dns_.setErrorReplyCode(DNSReplyCode::NoError);
    captivePortal_ = dns_.start(kDnsPort, "*", apIp);
  } else if (!accessPoint && captivePortal_) {
    dns_.stop();
    captivePortal_ = false;
  }
  restartMdns();
}

// Constaté sur la carte: la box ne relayait plus les requêtes mDNS vers la boîte (réponse à une requête
// arrivée une minute plus tard), et <nom>.local cessait d'être résolu ~2 min après l'ouverture de la page.
// Les annonces émises par la boîte, elles, passent: on se réannonce avant l'expiration des 120 s pour que
// les appareils du réseau gardent le nom en cache. mdns_hostname_set() avec le même nom relance l'annonce
// sans recréer le service.
void WebAdmin::announceMdnsPeriodically() {
  if (!mdnsActive_ || !runtime_.wifiConnected || runtime_.wifiMode != "sta") {
    return;
  }
  const uint32_t now = millis();
  if (now - lastMdnsAnnounceMs_ < totobox::kMdnsAnnounceMs) {
    return;
  }
  lastMdnsAnnounceMs_ = now;
  mdns_hostname_set(config_.hostname.c_str());
}

// mDNS: http://<nom>.local depuis le réseau domestique comme depuis le point d'accès. Relancé à chaque
// changement de réseau ou de nom.
void WebAdmin::restartMdns() {
  if (mdnsActive_) {
    MDNS.end();
  }
  mdnsActive_ = MDNS.begin(config_.hostname.c_str());
  lastMdnsAnnounceMs_ = millis();
  if (mdnsActive_) {
    MDNS.addService("http", "tcp", 80);
    logLine("web", "Interface web: http://%s.local", config_.hostname.c_str());
  } else {
    logLine("web", "mDNS indisponible");
  }
}

void WebAdmin::loop() {
  // Serial.print("-w");
  if (captivePortal_) {
    dns_.processNextRequest();
  }
  announceMdnsPeriodically();
  server_.handleClient();
  // Une requête d'envoi est traitée en entier dans handleClient(): à son retour, l'envoi est terminé (ou abandonné).
  runtime_.uploading = false;
}

void WebAdmin::noteActivity() {
  runtime_.lastActivityMs = millis();
}

void WebAdmin::redirectToPortal() {
  server_.sendHeader("Location", "http://" + portalHost_ + "/", true);
  server_.send(302, "text/plain", "");
}

void WebAdmin::handleNotFound() {
  // Requête destinée à un autre site (détection de portail, navigation): on renvoie vers l'interface.
  if (captivePortal_ && server_.hostHeader() != portalHost_) {
    redirectToPortal();
    return;
  }
  server_.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
}

void WebAdmin::registerRoutes() {
  static const char* kGuardHeaders[] = {"Origin", "Sec-Fetch-Site", "Content-Type", "Content-Length"};
  server_.collectHeaders(kGuardHeaders, sizeof(kGuardHeaders) / sizeof(kGuardHeaders[0]));

  // Toute requête compte comme une interaction et repousse la mise en veille, sauf le statut interrogé
  // chaque seconde par une page restée ouverte, et les tests de connectivité du portail captif.
  server_.on("/", HTTP_GET, [this]() {
    noteActivity();
    // Page compressée par scripts/build_webfs.py (le serveur ajoute Content-Encoding: gzip pour un .gz);
    // repli sur la version non compressée si l'image LittleFS a été construite autrement.
    for (const char* path : {"/index.html.gz", "/index.html"}) {
      if (!LittleFS.exists(path)) {
        continue;
      }
      File file = LittleFS.open(path, FILE_READ);
      // Toujours revalider: après une mise à jour, le rechargement proposé doit obtenir la nouvelle page.
      server_.sendHeader("Cache-Control", "no-cache");
      server_.streamFile(file, "text/html; charset=utf-8");
      file.close();
      return;
    }
    server_.send(200, "text/plain", "TotoBox admin UI missing from LittleFS");
  });

  for (const char* path : kCaptiveProbePaths) {
    server_.on(path, HTTP_ANY, [this]() {
      if (captivePortal_) {
        redirectToPortal();
      } else {
        handleNotFound();
      }
    });
  }
  server_.onNotFound([this]() { handleNotFound(); });

  onApi("/api/status", HTTP_GET, [this]() { handleStatus(); });
  onApi("/api/config", HTTP_GET, [this]() { noteActivity(); handleGetConfig(); });
  onApi("/api/config", HTTP_POST, [this]() { noteActivity(); handlePostConfig(); });
  onApi("/api/volume", HTTP_POST, [this]() { noteActivity(); handlePostVolume(); });
  onApi("/api/unlock", HTTP_POST, [this]() { noteActivity(); handleUnlock(); });
  onApi("/api/lock", HTTP_POST, [this]() { noteActivity(); handleLock(); });
  onApi("/api/lock/setup", HTTP_POST, [this]() { noteActivity(); handleLockSetup(); });
  onApi("/api/lock/pin", HTTP_POST, [this]() { noteActivity(); handleLockPin(); });
  onApi("/api/lock/cards", HTTP_POST, [this]() { noteActivity(); handleLockCards(); });
  onApi("/api/bindings", HTTP_POST, [this]() { noteActivity(); handlePostBindings(); });
  onApi("/api/library", HTTP_GET, [this]() { noteActivity(); handleLibrary(); });
  server_.on(
      "/api/upload", HTTP_POST,
      [this]() {
        // Envoi multipart: l'origine est contrôlée, pas le JSON.
        if (rejectIfForeign(true)) {
          return;
        }
        noteActivity();
        handleUpload();
      },
      [this]() {
        noteActivity();
        handleUploadData();
        if (uploadTick_) {
          uploadTick_();
        }
      });
  onApi("/api/folder", HTTP_POST, [this]() { noteActivity(); handleCreateFolder(); });
  onApi("/api/delete", HTTP_POST, [this]() { noteActivity(); handleDelete(); });
  onApi("/api/trash", HTTP_GET, [this]() { noteActivity(); handleTrash(); });
  onApi("/api/restore", HTTP_POST, [this]() { noteActivity(); handleRestore(); });
  onApi("/api/rename", HTTP_POST, [this]() { noteActivity(); handleRename(); });
  onApi("/api/move", HTTP_POST, [this]() { noteActivity(); handleMove(); });
  onApi("/api/factory-reset", HTTP_POST, [this]() { noteActivity(); handleFactoryReset(); });
  onApi("/api/cards/backup", HTTP_GET, [this]() { noteActivity(); handleBackupInfo(); });
  onApi("/api/cards/backup", HTTP_POST, [this]() { noteActivity(); handleBackupSave(); });
  onApi("/api/cards/restore", HTTP_POST, [this]() { noteActivity(); handleBackupRestore(); });
  onApi("/api/setup/complete", HTTP_POST, [this]() { noteActivity(); handleSetupComplete(); });
  onApi("/api/sd/format", HTTP_POST, [this]() { noteActivity(); handleSdFormat(); });
  onApi("/api/trash/purge", HTTP_POST, [this]() { noteActivity(); handlePurgeTrash(); });
  onApi("/api/update", HTTP_POST, [this]() { noteActivity(); handleUpdate(); });
}

void WebAdmin::onApi(const char* path, HTTPMethod method, std::function<void()> handler) {
  server_.on(path, method, [this, method, handler]() {
    if (rejectIfForeign(method != HTTP_GET)) {
      return;
    }
    handler();
  });
}

std::vector<String> WebAdmin::allowedHosts() const {
  std::vector<String> hosts = {config_.hostname + ".local"};
  if (!previousHostname_.isEmpty()) {
    hosts.push_back(previousHostname_ + ".local");
  }
  if (WiFi.getMode() & WIFI_STA) {
    hosts.push_back(WiFi.localIP().toString());
  }
  if (WiFi.getMode() & WIFI_AP) {
    hosts.push_back(WiFi.softAPIP().toString());
  }
  return hosts;
}

const char* WebAdmin::foreignRequestError(bool mutating, bool jsonBody) {
  request_guard::Request request;
  request.host = server_.hostHeader();
  request.origin = server_.header("Origin");
  request.secFetchSite = server_.header("Sec-Fetch-Site");
  request.contentType = server_.header("Content-Type");
  request.mutating = mutating;
  request.hasBody = jsonBody && server_.header("Content-Length").toInt() > 0;
  return request_guard::check(request, allowedHosts());
}

bool WebAdmin::rejectIfSealedInvalid(JsonDocument& request) {
  const char* error = secret_channel::unseal(request);
  if (error == nullptr) {
    return false;
  }
  logLine("web", "Secrets chiffrés refusés (%s): %s", error, server_.uri().c_str());
  const int code = strcmp(error, "stale_challenge") == 0 ? 409 : 400;
  server_.send(code, "application/json", String("{\"ok\":false,\"error\":\"") + error + "\"}");
  return true;
}

bool WebAdmin::rejectIfForeign(bool mutating) {
  const char* error = foreignRequestError(mutating, true);
  if (error == nullptr) {
    return false;
  }
  logLine("web", "Requête refusée (%s): %s, hôte %s, origine %s", error, server_.uri().c_str(),
          server_.hostHeader().c_str(), server_.header("Origin").c_str());
  const int code = strcmp(error, "json_required") == 0 ? 415 : 403;
  server_.send(code, "application/json", String("{\"ok\":false,\"error\":\"") + error + "\"}");
  return true;
}

void WebAdmin::handleStatus() {
  JsonDocument doc;
  doc["state"] = deviceStateToString(runtime_.state);
  doc["firmwareVersion"] = totobox::kFirmwareVersion;
  doc["firmwareBuild"] = totobox::kFirmwareBuild;
  // Mise à jour en ligne: dépôt où la page cherche les versions, et avancement d'une mise à jour lancée.
  doc["updateRepo"] = totobox::kUpdateRepo;
  // Réglages des mises à jour, visibles même verrouillée: la page ne cherche les versions que si updateNotify.
  doc["updateNotify"] = config_.updateNotify;
  doc["autoUpdate"] = config_.autoUpdate;
  if (updater_ != nullptr) {
    const UpdateService::Status update = updater_->status();
    JsonObject progress = doc["update"].to<JsonObject>();
    progress["phase"] = UpdateService::phaseName(update.phase);
    progress["version"] = update.version;
    progress["done"] = update.done;
    progress["total"] = update.total;
    progress["error"] = update.error;
  }
  doc["uiVersion"] = uiVersion_;
  doc["setupDone"] = config_.setupDone;
  // Pour chiffrer les secrets envoyés par la page (secret_channel).
  doc["boxKey"] = secret_channel::publicKeyBase64();
  doc["challenge"] = secret_channel::challengeBase64();
  doc["sdReady"] = runtime_.sdReady;
  doc["sdState"] = sdStateToString(runtime_.sdState);
  doc["nfcReady"] = runtime_.nfcReady;
  doc["wifiConnected"] = runtime_.wifiConnected;
  doc["wifiMode"] = runtime_.wifiMode;
  doc["wifiIp"] = runtime_.wifiIp;
  doc["hostname"] = mdnsActive_ ? config_.hostname + ".local" : "";
  doc["wifiSsid"] = config_.wifiSsid;
  // Connexion lancée depuis l'interface: la page en affiche le résultat.
  JsonObject attempt = doc["networkAttempt"].to<JsonObject>();
  attempt["id"] = runtime_.networkAttemptId;
  attempt["state"] = runtime_.networkAttempt;
  attempt["ssid"] = runtime_.networkAttemptSsid;
  attempt["ip"] = runtime_.networkAttemptIp;
  attempt["error"] = runtime_.networkError;
  attempt["apClosesInSec"] =
      runtime_.apCloseAtMs != 0 && static_cast<int32_t>(runtime_.apCloseAtMs - millis()) > 0
          ? (runtime_.apCloseAtMs - millis() + 999) / 1000
          : 0;
  doc["activeCardUid"] = runtime_.activeCardUid;
  doc["activeMediaPath"] = runtime_.activeMediaPath;
  doc["activeTrackIndex"] = runtime_.activeTrackIndex;
  doc["activePositionMs"] = runtime_.activePositionMs;
  doc["activeTrackCount"] = runtime_.activeTrackCount;
  doc["audioPlaying"] = runtime_.audioPlaying;
  const bool locked = access_lock::isLocked(config_, runtime_);
  doc["lockEnabled"] = config_.lockEnabled;
  doc["locked"] = locked;
  // Verrouillée: seuls la lecture en cours et le volume restent visibles.
  doc["lastUnknownCardUid"] = locked ? "" : runtime_.lastUnknownCardUid;
  for (const CardBinding& binding : bindings_) {
    if (binding.uid == runtime_.activeCardUid) {
      doc["activeCardLabel"] = binding.label;
      break;
    }
  }
  doc["currentVolume"] = config_.currentVolume;
  doc["volumeMin"] = config_.volumeMin;
  doc["volumeMax"] = config_.volumeMax;
  doc["bindingsCount"] = bindings_.size();

  // Pas d'accès SD pendant la lecture: on renvoie les dernières valeurs connues.
  if (!runtime_.audioPlaying && !sdStatsValid_) {
    refreshSdStats();
  }
  doc["sdTotalBytes"] = sdTotalCache_;
  doc["sdUsedBytes"] = sdUsedCache_;
  doc["sdFreeBytes"] = sdTotalCache_ >= sdUsedCache_ ? (sdTotalCache_ - sdUsedCache_) : 0;

  String payload;
  serializeJson(doc, payload);
  server_.send(200, "application/json", payload);
}

void WebAdmin::handleGetConfig() {
  if (rejectIfLocked()) {
    return;
  }
  sendJsonConfig();
}

void WebAdmin::handlePostConfig() {
  if (rejectIfLocked()) {
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok) {
    server_.send(400, "application/json", "{\"error\":\"invalid_json\"}");
    return;
  }
  if (rejectIfSealedInvalid(doc)) {
    return;
  }

  // Champ absent = inchangé (la page n'envoie jamais les mots de passe qu'elle ne connaît pas).
  const bool hasWifiPassword = doc["wifiPassword"].is<const char*>();
  const bool hasApPassword = doc["apPassword"].is<const char*>();
  String wifiSsid = String(static_cast<const char*>(doc["wifiSsid"] | config_.wifiSsid.c_str()));
  wifiSsid.trim();
  String wifiPassword = String(static_cast<const char*>(doc["wifiPassword"] | config_.wifiPassword.c_str()));
  // Nouveau réseau sans mot de passe saisi: réseau ouvert, l'ancien mot de passe ne le concerne pas.
  // Sans réseau maison, aucun mot de passe n'est gardé.
  if ((wifiSsid != config_.wifiSsid && !hasWifiPassword) || wifiSsid.isEmpty()) {
    wifiPassword = "";
  }
  String apSsid = String(static_cast<const char*>(doc["apSsid"] | config_.apSsid.c_str()));
  apSsid.trim();
  const String apPassword = String(static_cast<const char*>(doc["apPassword"] | config_.apPassword.c_str()));
  const String hostname =
      network_names::normalizeHostname(String(static_cast<const char*>(doc["hostname"] | config_.hostname.c_str())));

  // Validation avant toute modification: un réglage refusé ne doit rien changer. Seuls les champs modifiés
  // sont contrôlés: une valeur déjà enregistrée (ancienne version, règles plus souples) ne bloque pas les
  // autres réglages.
  const char* error = nullptr;
  if (wifiSsid != config_.wifiSsid && !wifiSsid.isEmpty() && !network_names::isValidSsid(wifiSsid)) {
    error = "invalid_wifi_ssid";
  } else if (hasWifiPassword && !wifiSsid.isEmpty() && !network_names::isValidWifiPassword(wifiPassword)) {
    error = "invalid_wifi_password";
  } else if (apSsid != config_.apSsid && !network_names::isValidSsid(apSsid)) {
    error = "invalid_ap_ssid";
  } else if (hasApPassword && !network_names::isValidApPassword(apPassword)) {
    error = "invalid_ap_password";
  } else if (hostname != config_.hostname && !network_names::isValidHostname(hostname)) {
    error = "invalid_hostname";
  }
  if (error != nullptr) {
    logLine("web", "Réglages refusés: %s", error);
    JsonDocument response;
    response["ok"] = false;
    response["error"] = error;
    sendJson(400, response);
    return;
  }

  const bool networkChanged = wifiSsid != config_.wifiSsid || wifiPassword != config_.wifiPassword ||
                              apSsid != config_.apSsid || apPassword != config_.apPassword;
  const bool hostnameChanged = hostname != config_.hostname;
  if (hostnameChanged) {
    previousHostname_ = config_.hostname;
  }

  config_.volumeMin = constrain(doc["volumeMin"] | config_.volumeMin, 0, 100);
  config_.volumeMax = constrain(doc["volumeMax"] | config_.volumeMax, config_.volumeMin, 100);
  config_.currentVolume = constrain(doc["currentVolume"] | config_.currentVolume, config_.volumeMin, config_.volumeMax);
  config_.wifiSsid = wifiSsid;
  config_.wifiPassword = wifiPassword;
  config_.apSsid = apSsid;
  if (apPassword != config_.apPassword) {
    config_.apPasswordGenerated = false;  // choisi dans les réglages: plus jamais affiché dans la console
  }
  config_.apPassword = apPassword;
  config_.hostname = hostname;
  config_.updateNotify = doc["updateNotify"] | config_.updateNotify;
  config_.autoUpdate = doc["autoUpdate"] | config_.autoUpdate;
  store_.saveConfig(config_);

  // Appliqué sans redémarrage: le réseau par NetworkService (après l'envoi de cette réponse), le nom tout de
  // suite pour le mDNS (le nom DHCP suit à la prochaine connexion).
  if (networkChanged) {
    runtime_.networkConfigChanged = true;
  }
  if (hostnameChanged) {
    restartMdns();
  }
  sendJsonConfig();
}

// Réglage du volume en direct (curseur de l'interface): appliqué par loop(), écrit en NVS une fois stable.
void WebAdmin::handlePostVolume() {
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok || !request["volume"].is<int>()) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_volume\"}");
    return;
  }

  config_.currentVolume = constrain(request["volume"].as<int>(), config_.volumeMin, config_.volumeMax);
  runtime_.volumeSaveDue = true;
  runtime_.lastVolumeChangeMs = millis();

  JsonDocument response;
  response["ok"] = true;
  response["currentVolume"] = config_.currentVolume;
  response["volumeMin"] = config_.volumeMin;
  response["volumeMax"] = config_.volumeMax;
  String payload;
  serializeJson(response, payload);
  server_.send(200, "application/json", payload);
}

void WebAdmin::handlePostBindings() {
  if (rejectIfLocked()) {
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok || !doc.is<JsonArray>()) {
    server_.send(400, "application/json", "{\"error\":\"invalid_bindings\"}");
    return;
  }

  std::vector<CardBinding> next;
  for (JsonObject item : doc.as<JsonArray>()) {
    CardBinding binding;
    binding.uid = String(static_cast<const char*>(item["uid"] | ""));
    binding.label = String(static_cast<const char*>(item["label"] | ""));
    binding.mediaPath = String(static_cast<const char*>(item["mediaPath"] | ""));
    binding.mode = playbackModeFromString(String(static_cast<const char*>(item["mode"] | "sd_local")));
    binding.resumeTrackIndex = item["resumeTrackIndex"] | 0;
    binding.resumeMs = item["resumeMs"] | 0;
    binding.resumeByte = item["resumeByte"] | 0;
    // Marque-page remis au début (ou page plus ancienne qui ignore l'octet): pas d'octet orphelin.
    if (binding.resumeMs == 0 && binding.resumeTrackIndex == 0) {
      binding.resumeByte = 0;
    }
    if (access_lock::isLockCard(config_, binding.uid)) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"lock_card\"}");
      return;
    }
    if (!binding.uid.isEmpty()) {
      next.push_back(binding);
    }
  }
  bindings_ = next;

  store_.saveBindings(bindings_);
  for (const CardBinding& binding : bindings_) {
    if (binding.uid == runtime_.lastUnknownCardUid) {
      runtime_.lastUnknownCardUid = "";
      break;
    }
  }
  sendJsonConfig();
}

void WebAdmin::handleLibrary() {
  if (rejectIfLocked()) {
    return;
  }
  if (rejectIfBusy()) {
    return;
  }
  JsonDocument doc;
  doc["sdReady"] = runtime_.sdReady;
  doc["rootPath"] = kLibraryRoot;
  doc["trashPath"] = kTrashRoot;
  doc["sort"] = "type_then_name";
  JsonArray entries = doc["entries"].to<JsonArray>();

  if (runtime_.sdReady && SD.exists(kLibraryRoot)) {
    LibrarySummary summary;
    buildTreeAtPath(entries, kLibraryRoot, false, 0, &summary);
    doc["rootAvailable"] = true;
    doc["trackCount"] = summary.trackCount;
    doc["durationEstimateSec"] = summary.durationEstimateSec;
    doc["totalSize"] = summary.totalSize;
    refreshSdStats();
    doc["sdTotalBytes"] = sdTotalCache_;
    doc["sdUsedBytes"] = sdUsedCache_;
    doc["sdFreeBytes"] = sdTotalCache_ >= sdUsedCache_ ? (sdTotalCache_ - sdUsedCache_) : 0;
  } else {
    doc["rootAvailable"] = false;
  }

  String payload;
  serializeJson(doc, payload);
  server_.send(runtime_.sdReady ? 200 : 503, "application/json", payload);
}

void WebAdmin::sendJson(int code, JsonDocument& doc) {
  String payload;
  serializeJson(doc, payload);
  server_.send(code, "application/json", payload);
}

bool WebAdmin::rejectIfLocked() {
  if (!access_lock::isLocked(config_, runtime_)) {
    return false;
  }
  server_.send(403, "application/json", "{\"ok\":false,\"error\":\"locked\"}");
  return true;
}

void WebAdmin::handleUnlock() {
  JsonDocument response;
  if (!access_lock::isLocked(config_, runtime_)) {
    response["ok"] = true;
    sendJson(200, response);
    return;
  }

  const uint32_t retryInSec = pinLimiter_.retryInSec(millis());
  if (retryInSec > 0) {
    response["ok"] = false;
    response["error"] = "too_many_attempts";
    response["retryInSec"] = retryInSec;
    sendJson(429, response);
    return;
  }

  JsonDocument request;
  deserializeJson(request, server_.arg("plain"));
  // Un message chiffré illisible ou au défi périmé ne compte pas comme un mauvais code.
  if (rejectIfSealedInvalid(request)) {
    return;
  }
  const String pin = String(static_cast<const char*>(request["pin"] | ""));
  if (!access_lock::checkPin(config_, pin)) {
    const uint8_t attemptsLeft = pinLimiter_.recordFailure(millis());
    response["ok"] = false;
    response["error"] = attemptsLeft == 0 ? "too_many_attempts" : "wrong_pin";
    response["attemptsLeft"] = attemptsLeft;
    response["retryInSec"] = pinLimiter_.retryInSec(millis());
    sendJson(attemptsLeft == 0 ? 429 : 403, response);
    return;
  }

  pinLimiter_.reset();
  runtime_.unlocked = true;
  logLine("web", "Réglages déverrouillés par code");
  response["ok"] = true;
  sendJson(200, response);
}

void WebAdmin::handleLock() {
  runtime_.unlocked = false;
  logLine("web", "Réglages verrouillés");
  JsonDocument response;
  response["ok"] = true;
  response["locked"] = access_lock::isLocked(config_, runtime_);
  sendJson(200, response);
}

// Vérifie qu'une carte peut devenir carte maîtresse. Renvoie un code d'erreur, ou nullptr si elle convient.
const char* WebAdmin::validateMasterCard(const String& uid) const {
  if (uid.isEmpty()) {
    return "invalid_card";
  }
  for (const CardBinding& binding : bindings_) {
    if (binding.uid == uid) {
      return "card_in_use";
    }
  }
  if (access_lock::findCard(config_, uid) != nullptr) {
    return "card_already_master";
  }
  return nullptr;
}

// Une carte maîtresse posée pendant la configuration est vue comme "inconnue": on l'oublie comme telle.
void WebAdmin::forgetUnknownCard(const String& uid) {
  if (runtime_.lastUnknownCardUid == uid) {
    runtime_.lastUnknownCardUid = "";
  }
  if (runtime_.activeCardUid == uid && runtime_.state == DeviceState::UnknownCard) {
    runtime_.state = DeviceState::Ready;
  }
}

MasterCard WebAdmin::masterCardFromRequest(JsonDocument& request) const {
  MasterCard card;
  card.uid = String(static_cast<const char*>(request["cardUid"] | ""));
  card.uid.trim();
  card.uid.toUpperCase();
  card.label = String(static_cast<const char*>(request["cardLabel"] | ""));
  card.label.trim();
  return card;
}

void WebAdmin::sendLockConfig() {
  JsonDocument response;
  response["ok"] = true;
  response["lockEnabled"] = config_.lockEnabled;
  JsonArray cards = response["lockCards"].to<JsonArray>();
  for (const MasterCard& card : config_.lockCards) {
    JsonObject item = cards.add<JsonObject>();
    item["uid"] = card.uid;
    item["label"] = card.label;
  }
  sendJson(200, response);
}

void WebAdmin::sendLockError(const char* error) {
  JsonDocument response;
  response["ok"] = false;
  response["error"] = error;
  sendJson(400, response);
}

// {enabled:true, cardUid, cardLabel, pin}: active avec une première carte. {enabled:false}: désactive tout.
void WebAdmin::handleLockSetup() {
  if (rejectIfLocked()) {
    return;
  }

  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok) {
    sendLockError("invalid_json");
    return;
  }
  if (rejectIfSealedInvalid(request)) {
    return;
  }

  if (!(request["enabled"] | false)) {
    access_lock::disable(config_);
    store_.saveConfig(config_);
    logLine("web", "Verrouillage désactivé");
    sendLockConfig();
    return;
  }

  const MasterCard card = masterCardFromRequest(request);
  const String pin = String(static_cast<const char*>(request["pin"] | ""));
  const char* error = access_lock::isValidPin(pin) ? validateMasterCard(card.uid) : "invalid_pin";
  if (error != nullptr) {
    sendLockError(error);
    return;
  }

  access_lock::enable(config_, card, pin);
  store_.saveConfig(config_);
  // La personne qui vient de configurer reste déverrouillée jusqu'à "Verrouiller".
  runtime_.unlocked = true;
  pinLimiter_.reset();
  forgetUnknownCard(card.uid);
  logLine("web", "Verrouillage activé avec la carte %s", card.uid.c_str());
  sendLockConfig();
}

// {pin}: change le code de secours.
void WebAdmin::handleLockPin() {
  if (rejectIfLocked()) {
    return;
  }
  JsonDocument request;
  deserializeJson(request, server_.arg("plain"));
  if (rejectIfSealedInvalid(request)) {
    return;
  }
  const String pin = String(static_cast<const char*>(request["pin"] | ""));
  if (!config_.lockEnabled) {
    sendLockError("lock_disabled");
    return;
  }
  if (!access_lock::isValidPin(pin)) {
    sendLockError("invalid_pin");
    return;
  }
  access_lock::setPin(config_, pin);
  store_.saveConfig(config_);
  pinLimiter_.reset();
  logLine("web", "Code de secours modifié");
  sendLockConfig();
}

// {action:"add", cardUid, cardLabel} ou {action:"remove", cardUid}.
void WebAdmin::handleLockCards() {
  if (rejectIfLocked()) {
    return;
  }
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok) {
    sendLockError("invalid_json");
    return;
  }
  if (!config_.lockEnabled) {
    sendLockError("lock_disabled");
    return;
  }

  const String action = String(static_cast<const char*>(request["action"] | ""));
  const MasterCard card = masterCardFromRequest(request);
  if (action == "add") {
    const char* error = validateMasterCard(card.uid);
    if (error == nullptr && !access_lock::addCard(config_, card)) {
      error = "too_many_cards";
    }
    if (error != nullptr) {
      sendLockError(error);
      return;
    }
    forgetUnknownCard(card.uid);
    logLine("web", "Carte maîtresse ajoutée: %s", card.uid.c_str());
  } else if (action == "remove") {
    if (!access_lock::removeCard(config_, card.uid)) {
      sendLockError(config_.lockCards.size() <= 1 ? "last_card" : "invalid_card");
      return;
    }
    logLine("web", "Carte maîtresse retirée: %s", card.uid.c_str());
  } else {
    sendLockError("invalid_action");
    return;
  }

  store_.saveConfig(config_);
  sendLockConfig();
}

bool WebAdmin::rejectIfBusy() {
  if (!runtime_.audioPlaying) {
    return false;
  }
  server_.send(503, "application/json", "{\"ok\":false,\"error\":\"busy_playing\"}");
  return true;
}

void WebAdmin::refreshSdStats() {
  sdTotalCache_ = sdTotalBytes();
  sdUsedCache_ = sdUsedBytes();
  sdStatsValid_ = runtime_.sdReady;
}

void WebAdmin::sendJsonConfig() {
  JsonDocument doc;
  doc["volumeMin"] = config_.volumeMin;
  doc["volumeMax"] = config_.volumeMax;
  doc["currentVolume"] = config_.currentVolume;
  doc["wifiSsid"] = config_.wifiSsid;
  doc["apSsid"] = config_.apSsid;
  doc["hostname"] = config_.hostname;
  doc["updateNotify"] = config_.updateNotify;
  doc["autoUpdate"] = config_.autoUpdate;
  doc["lastUpdateCheck"] = config_.lastUpdateCheck;
  doc["lockEnabled"] = config_.lockEnabled;
  JsonArray lockCards = doc["lockCards"].to<JsonArray>();
  for (const MasterCard& card : config_.lockCards) {
    JsonObject item = lockCards.add<JsonObject>();
    item["uid"] = card.uid;
    item["label"] = card.label;
  }

  JsonArray bindings = doc["bindings"].to<JsonArray>();
  for (const CardBinding& binding : bindings_) {
    JsonObject item = bindings.add<JsonObject>();
    item["uid"] = binding.uid;
    item["label"] = binding.label;
    item["mediaPath"] = binding.mediaPath;
    item["mode"] = playbackModeToString(binding.mode);
    item["resumeTrackIndex"] = binding.resumeTrackIndex;
    item["resumeMs"] = binding.resumeMs;
    item["resumeByte"] = binding.resumeByte;
  }

  String payload;
  serializeJson(doc, payload);
  server_.send(200, "application/json", payload);
}

void WebAdmin::handleUpload() {
  JsonDocument doc;
  if (!runtime_.sdReady) {
    doc["ok"] = false;
    doc["error"] = "sd_unavailable";
    String payload;
    serializeJson(doc, payload);
    server_.send(503, "application/json", payload);
    return;
  }

  if (!uploadError_.isEmpty()) {
    doc["ok"] = false;
    doc["error"] = uploadError_;
    doc["targetPath"] = uploadTargetDir_;
    doc["conflictMode"] = uploadConflictMode_;
    doc["lastResolvedPath"] = lastResolvedPath_;
    doc["bytesWritten"] = uploadBytesWritten_;
    doc["metadataBytesStripped"] = uploadStrippedBytes_;
    doc["filesUploaded"] = uploadFileCount_;
    doc["filesSkipped"] = uploadSkippedCount_;
    JsonArray uploadedPaths = doc["uploadedPaths"].to<JsonArray>();
    for (const String& path : uploadedPaths_) {
      uploadedPaths.add(path);
    }
    uploadError_ = "";
    uploadBytesWritten_ = 0;
    uploadStrippedBytes_ = 0;
    uploadFileCount_ = 0;
    uploadSkippedCount_ = 0;
    uploadConflictMode_ = "";
    uploadedPaths_.clear();
    lastResolvedPath_ = "";
    String payload;
    serializeJson(doc, payload);
    server_.send(400, "application/json", payload);
    return;
  }

  doc["ok"] = true;
  doc["targetPath"] = uploadTargetDir_;
  doc["conflictMode"] = uploadConflictMode_;
  doc["lastResolvedPath"] = lastResolvedPath_;
  doc["bytesWritten"] = uploadBytesWritten_;
  doc["metadataBytesStripped"] = uploadStrippedBytes_;
  doc["filesUploaded"] = uploadFileCount_;
  doc["filesSkipped"] = uploadSkippedCount_;
  JsonArray uploadedPaths = doc["uploadedPaths"].to<JsonArray>();
  for (const String& path : uploadedPaths_) {
    uploadedPaths.add(path);
  }
  String payload;
  serializeJson(doc, payload);
  uploadBytesWritten_ = 0;
  uploadStrippedBytes_ = 0;
  uploadFileCount_ = 0;
  uploadSkippedCount_ = 0;
  uploadTargetDir_ = "";
  uploadConflictMode_ = "";
  uploadedPaths_.clear();
  lastResolvedPath_ = "";
  server_.send(200, "application/json", payload);
}

void WebAdmin::handleUploadData() {
  HTTPUpload& upload = server_.upload();

  if (upload.status == UPLOAD_FILE_START) {
    if (upload.totalSize == 0) {
      uploadError_ = "";
      uploadBytesWritten_ = 0;
      uploadStrippedBytes_ = 0;
      uploadFileCount_ = 0;
      uploadSkippedCount_ = 0;
      uploadedPaths_.clear();
      lastResolvedPath_ = "";
    }

    if (!runtime_.sdReady) {
      uploadError_ = "sd_unavailable";
      return;
    }

    // Avant toute écriture: la réponse finale (403) part après la réception de tout l'envoi.
    if (const char* error = foreignRequestError(true, false)) {
      uploadError_ = error;
      return;
    }

    if (access_lock::isLocked(config_, runtime_)) {
      uploadError_ = "locked";
      return;
    }

    if (runtime_.audioPlaying) {
      uploadError_ = "busy_playing";
      return;
    }

    const String targetArg = server_.hasArg("targetPath") ? server_.arg("targetPath") : String(kLibraryRoot);
    uploadTargetDir_ = library_paths::normalizeLibraryPath(targetArg);
    uploadConflictMode_ = library_paths::normalizeConflictMode(server_.hasArg("conflictMode") ? server_.arg("conflictMode") : "rename");

    if (uploadTargetDir_.isEmpty()) {
      uploadError_ = "invalid_target_path";
      return;
    }

    if (!ensureDirectoryExists(uploadTargetDir_)) {
      uploadError_ = "mkdir_failed";
      return;
    }

    String safeName = library_paths::sanitizeFileName(upload.filename);
    if (safeName.isEmpty() || !library_paths::isAudioFile(safeName)) {
      uploadError_ = "invalid_file_name";
      return;
    }

    String fullPath = resolveUploadPath(uploadTargetDir_, safeName, uploadConflictMode_);
    if (fullPath.isEmpty()) {
      if (uploadConflictMode_ == "reject") {
        ++uploadSkippedCount_;
        uploadError_ = "file_exists";
      } else {
        uploadError_ = "invalid_target_path";
      }
      return;
    }

    if (uploadConflictMode_ == "replace" && SD.exists(fullPath)) {
      SD.remove(fullPath);
    }

    uploadedPaths_.push_back(fullPath);
    lastResolvedPath_ = fullPath;

    uploadFile_ = SD.open(fullPath.c_str(), FILE_WRITE);
    runtime_.uploading = true;
    if (!uploadFile_) {
      uploadError_ = "open_failed";
      uploadedPaths_.pop_back();
      return;
    }

    // Les tags ID3 (pochette, titre...) sont inutiles sur la boîte: on les retire pendant l'écriture.
    String lowerName = safeName;
    lowerName.toLowerCase();
    uploadStripper_.reset(lowerName.endsWith(".mp3"));
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if ((!uploadError_.isEmpty() && uploadError_ != "file_exists") || !uploadFile_) {
      return;
    }

    if (!uploadStripper_.feed(upload.buf, upload.currentSize, [this](const uint8_t* data, size_t length) {
          return writeUploadChunk(data, length);
        })) {
      uploadError_ = "write_failed";
      uploadFile_.close();
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile_) {
      if (uploadError_.isEmpty() &&
          !uploadStripper_.finish([this](const uint8_t* data, size_t length) { return writeUploadChunk(data, length); })) {
        uploadError_ = "write_failed";
      }
      uploadStrippedBytes_ += uploadStripper_.strippedBytes();
      sdStatsValid_ = false;
      uploadFile_.flush();
      uploadFile_.close();
      if (uploadError_.isEmpty()) {
        ++uploadFileCount_;
      }
    } else if (uploadError_ == "file_exists") {
      uploadError_ = "";
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile_) {
      uploadFile_.close();
    }
    uploadError_ = "upload_aborted";
  }
}

bool WebAdmin::writeUploadChunk(const uint8_t* data, size_t length) {
  const size_t written = uploadFile_.write(data, length);
  uploadBytesWritten_ += written;
  return written == length;
}

void WebAdmin::handleCreateFolder() {
  if (rejectIfLocked()) {
    return;
  }
  if (rejectIfBusy()) {
    return;
  }
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  const String parentPath = library_paths::normalizeLibraryPath(String(static_cast<const char*>(request["parentPath"] | kLibraryRoot)));
  const String folderName = library_paths::sanitizeFolderName(String(static_cast<const char*>(request["folderName"] | "")));
  if (parentPath.isEmpty() || folderName.isEmpty()) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_folder_request\"}");
    return;
  }

  if (!runtime_.sdReady) {
    server_.send(503, "application/json", "{\"ok\":false,\"error\":\"sd_unavailable\"}");
    return;
  }

  if (!ensureDirectoryExists(parentPath)) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"parent_missing\"}");
    return;
  }

  const String newPath = resolveUploadPath(parentPath, folderName, "rename");
  if (newPath.isEmpty() || !SD.mkdir(newPath)) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"mkdir_failed\"}");
    return;
  }

  JsonDocument response;
  response["ok"] = true;
  response["path"] = newPath;
  String payload;
  serializeJson(response, payload);
  server_.send(200, "application/json", payload);
}

void WebAdmin::handleDelete() {
  if (rejectIfLocked()) {
    return;
  }
  if (rejectIfBusy()) {
    return;
  }
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  const String path = library_paths::normalizeLibraryPath(String(static_cast<const char*>(request["path"] | "")));
  if (path.isEmpty()) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_path\"}");
    return;
  }

  if (!runtime_.sdReady) {
    server_.send(503, "application/json", "{\"ok\":false,\"error\":\"sd_unavailable\"}");
    return;
  }

  JsonDocument response;
  if (!SD.exists(path)) {
    response["ok"] = false;
    response["error"] = "not_found";
  } else {
    const String trashedPath = movePathToTrash(path);
    if (trashedPath.isEmpty()) {
      response["ok"] = false;
      response["error"] = "trash_failed";
      response["path"] = path;
    } else {
      response["ok"] = true;
      response["path"] = path;
      response["trashedPath"] = trashedPath;
    }
  }

  String payload;
  serializeJson(response, payload);
  server_.send(response["ok"] | false ? 200 : 400, "application/json", payload);
}

void WebAdmin::handleTrash() {
  if (rejectIfLocked()) {
    return;
  }
  if (rejectIfBusy()) {
    return;
  }
  JsonDocument doc;
  doc["sdReady"] = runtime_.sdReady;
  doc["trashPath"] = kTrashRoot;
  JsonArray entries = doc["entries"].to<JsonArray>();

  if (runtime_.sdReady && SD.exists(kTrashRoot)) {
    LibrarySummary summary;
    buildTreeAtPath(entries, kTrashRoot, true, 0, &summary);
    doc["trashAvailable"] = true;
    doc["trackCount"] = summary.trackCount;
    doc["durationEstimateSec"] = summary.durationEstimateSec;
    doc["totalSize"] = summary.totalSize;
  } else {
    doc["trashAvailable"] = false;
  }

  String payload;
  serializeJson(doc, payload);
  server_.send(runtime_.sdReady ? 200 : 503, "application/json", payload);
}

void WebAdmin::handleRestore() {
  if (rejectIfLocked()) {
    return;
  }
  if (rejectIfBusy()) {
    return;
  }
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  const String sourcePath = library_paths::normalizeLibraryPath(String(static_cast<const char*>(request["path"] | "")));
  if (!library_paths::isInTrash(sourcePath)) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_trash_path\"}");
    return;
  }

  const String originalPath = library_paths::originalPathFromTrash(sourcePath);
  if (originalPath.isEmpty()) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_restore_path\"}");
    return;
  }

  const int slashIndex = originalPath.lastIndexOf('/');
  const String parentPath = slashIndex > 0 ? originalPath.substring(0, slashIndex) : String(kLibraryRoot);
  if (!ensureDirectoryExists(parentPath)) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"restore_parent_failed\"}");
    return;
  }

  const String baseName = slashIndex >= 0 ? originalPath.substring(slashIndex + 1) : originalPath;
  const String resolvedPath = resolveUploadPath(parentPath, baseName, "rename");
  JsonDocument response;
  if (resolvedPath.isEmpty() || !SD.rename(sourcePath, resolvedPath)) {
    response["ok"] = false;
    response["error"] = "restore_failed";
    response["path"] = sourcePath;
  } else {
    response["ok"] = true;
    response["path"] = sourcePath;
    response["restoredPath"] = resolvedPath;
  }

  String payload;
  serializeJson(response, payload);
  server_.send(response["ok"] | false ? 200 : 400, "application/json", payload);
}

void WebAdmin::handleRename() {
  if (rejectIfLocked()) {
    return;
  }
  if (rejectIfBusy()) {
    return;
  }
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  const String sourcePath = library_paths::normalizeLibraryPath(String(static_cast<const char*>(request["path"] | "")));
  const String desiredName = String(static_cast<const char*>(request["name"] | ""));
  const String conflictMode = String(static_cast<const char*>(request["conflictMode"] | "rename"));
  JsonDocument response;

  if (sourcePath.isEmpty()) {
    response["ok"] = false;
    response["error"] = "invalid_path";
  } else {
    const String renamedPath = renamePath(sourcePath, desiredName, conflictMode);
    if (renamedPath.isEmpty()) {
      response["ok"] = false;
      response["error"] = "rename_failed";
      response["path"] = sourcePath;
    } else {
      response["ok"] = true;
      response["path"] = sourcePath;
      response["renamedPath"] = renamedPath;
    }
  }

  String payload;
  serializeJson(response, payload);
  server_.send(response["ok"] | false ? 200 : 400, "application/json", payload);
}

void WebAdmin::handleFactoryReset() {
  if (rejectIfLocked()) {
    return;
  }
  // La sauvegarde automatique écrit sur la carte SD: pas pendant la lecture.
  if (rejectIfBusy()) {
    return;
  }
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }
  // Mot tapé par l'utilisateur dans la page: un appel accidentel ne suffit pas.
  if (String(static_cast<const char*>(request["confirm"] | "")) != "EFFACER") {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"confirm_required\"}");
    return;
  }

  // Sauvegarde automatique des cartes avant l'effacement. Rien à sauvegarder: on garde l'éventuelle sauvegarde
  // précédente plutôt que de l'écraser par une liste vide.
  bool backupSaved = false;
  if (!bindings_.empty() && runtime_.sdReady) {
    const String savedAt = String(static_cast<const char*>(request["savedAt"] | ""));
    if (!card_backup::save(SD, bindings_, savedAt)) {
      server_.send(500, "application/json", "{\"ok\":false,\"error\":\"backup_failed\"}");
      return;
    }
    backupSaved = true;
  }

  // Point d'accès après la remise à zéro: nom par défaut, mot de passe de l'étiquette s'il y en a une.
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  JsonDocument response;
  response["ok"] = true;
  response["apSsid"] = network_names::defaultApSsid(mac);
  response["factoryPassword"] = !config_.factoryApPassword.isEmpty();
  response["backupSaved"] = backupSaved;
  response["backupCount"] = backupSaved ? bindings_.size() : 0;
  sendJson(200, response);

  logLine("web", "Remise à zéro demandée");
  runtime_.factoryResetAtMs = millis() + totobox::kFactoryResetDelayMs;
  if (runtime_.factoryResetAtMs == 0) {
    runtime_.factoryResetAtMs = 1;
  }
}

// Carte présente mais illisible (exFAT, NTFS, vierge): formatage en FAT, sur demande explicite seulement.
void WebAdmin::handleSdFormat() {
  if (rejectIfLocked()) {
    return;
  }
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }
  // Mot tapé dans la page: tout le contenu de la carte est effacé.
  if (String(static_cast<const char*>(request["confirm"] | "")) != "FORMATER") {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"confirm_required\"}");
    return;
  }
  // Jamais une carte lisible: elle contient peut-être des histoires.
  if (runtime_.sdState != SdState::Unreadable || !sdFormatter_) {
    server_.send(409, "application/json", "{\"ok\":false,\"error\":\"sd_not_unreadable\"}");
    return;
  }
  const bool ready = sdFormatter_();
  sdStatsValid_ = false;
  if (!ready) {
    server_.send(500, "application/json", "{\"ok\":false,\"error\":\"format_failed\"}");
    return;
  }
  server_.send(200, "application/json", "{\"ok\":true}");
}

// Assistant de première connexion terminé ou remis à plus tard: il ne s'affiche plus.
void WebAdmin::handleSetupComplete() {
  if (rejectIfLocked()) {
    return;
  }
  if (!config_.setupDone) {
    config_.setupDone = true;
    store_.saveConfig(config_);
    logLine("web", "Assistant de première connexion terminé");
  }
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WebAdmin::handleBackupInfo() {
  if (rejectIfLocked() || rejectIfBusy()) {
    return;
  }
  JsonDocument response;
  response["ok"] = true;
  card_backup::Backup backup;
  const bool exists = runtime_.sdReady && card_backup::load(SD, backup);
  response["exists"] = exists;
  response["count"] = exists ? backup.bindings.size() : 0;
  response["savedAt"] = backup.savedAt;
  sendJson(200, response);
}

void WebAdmin::handleBackupSave() {
  if (rejectIfLocked() || rejectIfBusy()) {
    return;
  }
  JsonDocument request;
  deserializeJson(request, server_.arg("plain"));
  const String savedAt = String(static_cast<const char*>(request["savedAt"] | ""));
  if (!runtime_.sdReady) {
    server_.send(503, "application/json", "{\"ok\":false,\"error\":\"sd_unavailable\"}");
    return;
  }
  // Une liste vide écraserait la sauvegarde précédente sans rien apporter.
  if (bindings_.empty()) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"nothing_to_save\"}");
    return;
  }
  if (!card_backup::save(SD, bindings_, savedAt)) {
    server_.send(500, "application/json", "{\"ok\":false,\"error\":\"backup_failed\"}");
    return;
  }
  sdStatsValid_ = false;
  JsonDocument response;
  response["ok"] = true;
  response["exists"] = true;
  response["count"] = bindings_.size();
  response["savedAt"] = savedAt;
  sendJson(200, response);
}

void WebAdmin::handleBackupRestore() {
  if (rejectIfLocked() || rejectIfBusy()) {
    return;
  }
  if (!runtime_.sdReady) {
    server_.send(503, "application/json", "{\"ok\":false,\"error\":\"sd_unavailable\"}");
    return;
  }
  card_backup::Backup backup;
  if (!card_backup::load(SD, backup)) {
    server_.send(404, "application/json", "{\"ok\":false,\"error\":\"no_backup\"}");
    return;
  }
  const size_t skipped = card_backup::dropLockCards(backup.bindings, config_);
  // Les associations actuelles sont remplacées par celles de la sauvegarde.
  bindings_ = backup.bindings;
  store_.saveBindings(bindings_);
  for (const CardBinding& binding : bindings_) {
    if (binding.uid == runtime_.lastUnknownCardUid) {
      runtime_.lastUnknownCardUid = "";
      break;
    }
  }
  logLine("web", "%u cartes restaurées depuis la carte SD (%u cartes maîtresses ignorées)",
          static_cast<unsigned>(bindings_.size()), static_cast<unsigned>(skipped));
  JsonDocument response;
  response["ok"] = true;
  response["count"] = bindings_.size();
  response["skipped"] = skipped;
  sendJson(200, response);
}

void WebAdmin::handleMove() {
  if (rejectIfLocked()) {
    return;
  }
  if (rejectIfBusy()) {
    return;
  }
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  const String sourcePath = library_paths::normalizeLibraryPath(String(static_cast<const char*>(request["path"] | "")));
  const String targetDir = library_paths::normalizeLibraryPath(String(static_cast<const char*>(request["targetDir"] | "")));
  const char* error = nullptr;
  const String movedPath = movePath(sourcePath, targetDir, error);

  JsonDocument response;
  response["path"] = sourcePath;
  if (movedPath.isEmpty()) {
    response["ok"] = false;
    response["error"] = error;
  } else {
    sdStatsValid_ = false;
    response["ok"] = true;
    response["movedPath"] = movedPath;
  }
  String payload;
  serializeJson(response, payload);
  server_.send(movedPath.isEmpty() ? 400 : 200, "application/json", payload);
}

// Déplace un fichier ou un dossier dans un autre dossier de la bibliothèque, sous le même nom (suffixe -1, -2...
// si ce nom est déjà pris). Renvoie le nouveau chemin, ou "" avec `error` renseigné.
String WebAdmin::movePath(const String& sourcePath, const String& targetDir, const char*& error) {
  // Un dossier ne peut pas aller dans lui-même ni dans l'un de ses sous-dossiers; rien n'entre ni ne sort de la
  // corbeille par ici.
  error = library_paths::moveError(sourcePath, targetDir);
  if (error != nullptr) {
    return "";
  }
  if (!runtime_.sdReady) {
    error = "sd_unavailable";
    return "";
  }
  if (!SD.exists(sourcePath)) {
    error = "not_found";
    return "";
  }
  File target = SD.open(targetDir.c_str());
  const bool targetIsDir = target && target.isDirectory();
  if (target) {
    target.close();
  }
  if (!targetIsDir) {
    error = "not_found";
    return "";
  }

  const int slashIndex = sourcePath.lastIndexOf('/');
  if (sourcePath.substring(0, slashIndex) == targetDir) {
    return sourcePath;  // déjà dans ce dossier
  }
  const String movedPath = resolveUploadPath(targetDir, sourcePath.substring(slashIndex + 1), "rename");
  if (movedPath.isEmpty() || !SD.rename(sourcePath, movedPath)) {
    logLine("web", "Déplacement de %s vers %s impossible (errno %d)", sourcePath.c_str(), targetDir.c_str(), errno);
    error = "move_failed";
    return "";
  }
  return movedPath;
}

void WebAdmin::handlePurgeTrash() {
  if (rejectIfLocked()) {
    return;
  }
  if (rejectIfBusy()) {
    return;
  }
  JsonDocument request;
  String targetPath = kTrashRoot;
  if (!server_.arg("plain").isEmpty()) {
    if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
      return;
    }
    targetPath = library_paths::normalizeLibraryPath(String(static_cast<const char*>(request["path"] | kTrashRoot)));
  }

  if (!library_paths::isInTrash(targetPath)) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_trash_path\"}");
    return;
  }

  JsonDocument response;
  if (!SD.exists(targetPath)) {
    response["ok"] = false;
    response["error"] = "not_found";
    response["path"] = targetPath;
  } else if (!deletePathRecursive(targetPath)) {
    response["ok"] = false;
    response["error"] = "purge_failed";
    response["path"] = targetPath;
  } else {
    if (targetPath == kTrashRoot) {
      ensureDirectoryExists(kTrashRoot);
    }
    sdStatsValid_ = false;
    response["ok"] = true;
    response["path"] = targetPath;
  }

  String payload;
  serializeJson(response, payload);
  server_.send(response["ok"] | false ? 200 : 400, "application/json", payload);
}

bool WebAdmin::ensureDirectoryExists(const String& path) {
  String normalized = library_paths::normalizeLibraryPath(path);
  if (normalized.isEmpty()) {
    return false;
  }
  if (SD.exists(normalized)) {
    File existing = SD.open(normalized.c_str());
    const bool isDir = existing && existing.isDirectory();
    if (existing) {
      existing.close();
    }
    if (!isDir) {
      logLine("web", "%s existe mais n'est pas un dossier", normalized.c_str());
    }
    return isDir;
  }

  // Chaque dossier intermédiaire, sans barre finale ("/content", puis "/content/a"...).
  int slash = normalized.indexOf('/', 1);
  while (true) {
    const String partial = slash < 0 ? normalized : normalized.substring(0, slash);
    if (!SD.exists(partial) && !SD.mkdir(partial)) {
      logLine("web", "Création du dossier %s impossible (errno %d)", partial.c_str(), errno);
      return false;
    }
    if (slash < 0) {
      return true;
    }
    slash = normalized.indexOf('/', slash + 1);
  }
}

String WebAdmin::resolveUploadPath(const String& targetDir, const String& safeName, const String& conflictMode) const {
  return library_paths::resolveConflict(targetDir, safeName, conflictMode, [](const String& path) { return SD.exists(path); });
}

bool WebAdmin::deletePathRecursive(const String& path) {
  const String normalized = library_paths::normalizeLibraryPath(path);
  if (normalized.isEmpty()) {
    return false;
  }

  File node = SD.open(normalized.c_str());
  if (!node) {
    return false;
  }

  if (!node.isDirectory()) {
    node.close();
    return SD.remove(normalized);
  }

  File child = node.openNextFile();
  while (child) {
    String childPath = child.name();
    if (!childPath.startsWith("/")) {
      childPath = normalized + "/" + childPath;
    }
    const bool deleted = child.isDirectory() ? deletePathRecursive(childPath) : SD.remove(childPath);
    child.close();
    if (!deleted) {
      node.close();
      return false;
    }
    child = node.openNextFile();
  }

  node.close();
  return SD.rmdir(normalized);
}

String WebAdmin::movePathToTrash(const String& path) {
  const String normalized = library_paths::normalizeLibraryPath(path);
  library_paths::TrashTarget target;
  if (!library_paths::trashTargetFor(normalized, target)) {
    logLine("web", "Corbeille: chemin refusé %s", path.c_str());
    return "";
  }

  if (!ensureDirectoryExists(target.dir)) {
    logLine("web", "Corbeille: création de %s impossible", target.dir.c_str());
    return "";
  }

  const String trashedPath = resolveUploadPath(target.dir, target.name, "rename");
  if (trashedPath.isEmpty()) {
    logLine("web", "Corbeille: aucun nom libre pour %s dans %s", target.name.c_str(), target.dir.c_str());
    return "";
  }

  if (!SD.rename(normalized, trashedPath)) {
    logLine("web", "Corbeille: déplacement de %s vers %s impossible (errno %d)", normalized.c_str(),
            trashedPath.c_str(), errno);
    return "";
  }
  return trashedPath;
}

bool WebAdmin::buildTreeAtPath(JsonArray& out, const String& rootPath, bool includeTrash, uint8_t depth, LibrarySummary* summary) {
  if (depth > kLibraryMaxDepth) {
    return false;
  }

  File dir = SD.open(rootPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return false;
  }

  std::vector<SdEntryInfo> entries;
  File entry = dir.openNextFile();
  while (entry) {
    String name = entry.name();
    if (!name.startsWith("/")) {
      name = rootPath == "/" ? "/" + name : rootPath + "/" + name;
    }

    if (!includeTrash && library_paths::isInTrash(name)) {
      entry.close();
      entry = dir.openNextFile();
      continue;
    }

    const bool entryIsDirectory = entry.isDirectory();
    if (entryIsDirectory || library_paths::isAudioFile(name)) {
      SdEntryInfo info;
      info.path = name;
      info.isDirectory = entryIsDirectory;
      info.size = static_cast<uint32_t>(entry.size());
      entries.push_back(info);
    }

    entry.close();
    entry = dir.openNextFile();
  }

  std::sort(entries.begin(), entries.end(), [](const SdEntryInfo& left, const SdEntryInfo& right) {
    if (left.isDirectory != right.isDirectory) {
      return left.isDirectory > right.isDirectory;
    }
    return strcasecmp(left.path.c_str(), right.path.c_str()) < 0;
  });

  for (const SdEntryInfo& info : entries) {
    if (info.isDirectory) {
      JsonObject item = out.add<JsonObject>();
      item["path"] = info.path;
      item["type"] = "directory";
      JsonArray children = item["children"].to<JsonArray>();
      LibrarySummary childSummary;
      buildTreeAtPath(children, info.path, includeTrash, depth + 1, &childSummary);
      item["trackCount"] = childSummary.trackCount;
      item["durationEstimateSec"] = childSummary.durationEstimateSec;
      item["totalSize"] = childSummary.totalSize;
      if (summary != nullptr) {
        summary->trackCount += childSummary.trackCount;
        summary->durationEstimateSec += childSummary.durationEstimateSec;
        summary->totalSize += childSummary.totalSize;
      }
    } else {
      JsonObject item = out.add<JsonObject>();
      item["path"] = info.path;
      item["type"] = "file";
      item["trackCount"] = 1;
      item["durationEstimateSec"] = library_paths::estimateDurationSec(info.size);
      item["totalSize"] = info.size;
      if (summary != nullptr) {
        summary->trackCount += 1;
        summary->durationEstimateSec += library_paths::estimateDurationSec(info.size);
        summary->totalSize += info.size;
      }
    }
  }

  dir.close();
  return true;
}

String WebAdmin::renamePath(const String& sourcePath, const String& desiredName, const String& rawConflictMode) {
  if (!runtime_.sdReady || !SD.exists(sourcePath)) {
    return "";
  }

  File source = SD.open(sourcePath.c_str());
  if (!source) {
    return "";
  }
  const bool isDir = source.isDirectory();
  source.close();

  const String conflictMode = library_paths::normalizeConflictMode(rawConflictMode);

  String safeName = isDir ? library_paths::sanitizeFolderName(desiredName) : library_paths::sanitizeFileName(desiredName);
  if (safeName.isEmpty()) {
    return "";
  }
  if (!isDir && !library_paths::isAudioFile(safeName)) {
    return "";
  }

  const int slashIndex = sourcePath.lastIndexOf('/');
  const String parentPath = slashIndex > 0 ? sourcePath.substring(0, slashIndex) : String(kLibraryRoot);
  String targetPath = resolveUploadPath(parentPath, safeName, conflictMode);
  if (targetPath.isEmpty()) {
    return "";
  }

  if (targetPath == sourcePath) {
    return sourcePath;
  }

  if (conflictMode == "replace" && SD.exists(targetPath)) {
    File target = SD.open(targetPath.c_str());
    const bool targetIsDir = target && target.isDirectory();
    if (target) {
      target.close();
    }
    if (targetIsDir != isDir) {
      return "";
    }
    if (targetIsDir) {
      if (!deletePathRecursive(targetPath)) {
        return "";
      }
    } else if (!SD.remove(targetPath)) {
      return "";
    }
  }

  return SD.rename(sourcePath, targetPath) ? targetPath : "";
}

uint64_t WebAdmin::sdTotalBytes() const {
  return runtime_.sdReady ? SD.totalBytes() : 0;
}

uint64_t WebAdmin::sdUsedBytes() const {
  return runtime_.sdReady ? SD.usedBytes() : 0;
}

// {version, uiVersion, firmware: {url, sha256, size}, page: {url, sha256, size}}: télécharge et installe une
// version publiée sur GitHub (UpdateService), puis redémarre. La page suit l'avancement dans /api/status.
void WebAdmin::handleUpdate() {
  if (rejectIfLocked() || rejectIfBusy()) {
    return;
  }
  JsonDocument request;
  if (deserializeJson(request, server_.arg("plain")) != DeserializationError::Ok) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }
  update_rules::Request update;
  update.version = String(static_cast<const char*>(request["version"] | ""));
  update.uiVersion = String(static_cast<const char*>(request["uiVersion"] | ""));
  for (const char* key : {"firmware", "page"}) {
    update_rules::Asset& asset = strcmp(key, "firmware") == 0 ? update.firmware : update.page;
    asset.url = String(static_cast<const char*>(request[key]["url"] | ""));
    asset.sha256 = String(static_cast<const char*>(request[key]["sha256"] | ""));
    asset.size = request[key]["size"] | 0;
  }
  const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
  const char* error = update_rules::check(update, totobox::kFirmwareVersion, totobox::kUpdateRepo,
                                          slot != nullptr ? slot->size : 0, totobox::kUpdateMaxPageBytes);
  if (error == nullptr && !runtime_.wifiConnected) {
    error = "no_internet";  // point d'accès seul: pas d'accès à GitHub
  }
  if (error == nullptr && (updater_ == nullptr || !updater_->start(update))) {
    error = "update_running";
  }
  JsonDocument response;
  response["ok"] = error == nullptr;
  if (error != nullptr) {
    logLine("web", "Mise à jour refusée: %s", error);
    response["error"] = error;
  }
  sendJson(error == nullptr ? 200 : 409, response);
}
