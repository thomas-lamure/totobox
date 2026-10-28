// SPDX-License-Identifier: GPL-3.0-or-later
#include "update_service.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include <algorithm>

#include <ArduinoJson.h>
#include <esp_ota_ops.h>

#include "logger.h"
#include "totobox_config.h"
#include "version.h"

// Autorités racines de Mozilla, intégrées au firmware (board_build.embed_files dans platformio.ini).
extern const uint8_t kCaBundle[] asm("_binary_src_certs_x509_crt_bundle_bin_start");

namespace {

constexpr uint32_t kTaskStackBytes = 12 * 1024;  // TLS (mbedtls) et tampon de lecture
constexpr UBaseType_t kTaskPriority = 1;
constexpr BaseType_t kTaskCore = 0;
constexpr uint32_t kHttpTimeoutMs = 15000;
constexpr uint32_t kStallTimeoutMs = 20000;  // plus aucune donnée reçue
constexpr uint32_t kRestartDelayMs = 3000;   // la page voit la fin avant le redémarrage
constexpr char kPagePath[] = "/index.html.gz";
constexpr char kNewPagePath[] = "/index.html.gz.new";
constexpr char kUiVersionPath[] = "/ui_version.txt";

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTake(mutex_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex_); }

 private:
  SemaphoreHandle_t mutex_;
};

String lowercase(String value) {
  value.toLowerCase();
  return value;
}

}  // namespace

const char* UpdateService::phaseName(Phase phase) {
  switch (phase) {
    case Phase::Idle:
      return "idle";
    case Phase::Checking:
      return "checking";
    case Phase::Page:
      return "page";
    case Phase::Firmware:
      return "firmware";
    case Phase::Restarting:
      return "restarting";
    case Phase::Failed:
      return "failed";
  }
  return "idle";
}

bool UpdateService::begin() {
  mutex_ = xSemaphoreCreateMutex();
  // Reste d'une mise à jour interrompue avant l'étape 3: l'ancienne page est toujours en place.
  if (LittleFS.exists(kNewPagePath)) {
    LittleFS.remove(kNewPagePath);
  }
  return mutex_ != nullptr;
}

bool UpdateService::start(const update_rules::Request& request) {
  {
    Lock lock(mutex_);
    if (status_.phase == Phase::Checking || status_.phase == Phase::Page || status_.phase == Phase::Firmware ||
        status_.phase == Phase::Restarting) {
      return false;
    }
    checkMode_ = false;
    request_ = request;
    status_ = Status();
    status_.phase = Phase::Page;
    status_.version = request.version;
    status_.total = request.page.size;
  }
  logLine("update", "Mise à jour vers %s demandée", request.version.c_str());
  if (xTaskCreatePinnedToCore(taskEntry, "update", kTaskStackBytes, this, kTaskPriority, nullptr, kTaskCore) !=
      pdPASS) {
    fail("task_failed");
    return false;
  }
  return true;
}

bool UpdateService::startCheck(const String& repo, const String& currentVersion) {
  {
    Lock lock(mutex_);
    if (status_.phase == Phase::Checking || status_.phase == Phase::Page || status_.phase == Phase::Firmware ||
        status_.phase == Phase::Restarting) {
      return false;
    }
    checkMode_ = true;
    checkRepo_ = repo;
    checkCurrentVersion_ = currentVersion;
    status_ = Status();
    status_.phase = Phase::Checking;
  }
  if (xTaskCreatePinnedToCore(taskEntry, "update", kTaskStackBytes, this, kTaskPriority, nullptr, kTaskCore) !=
      pdPASS) {
    Lock lock(mutex_);
    status_.phase = Phase::Idle;
    checkResult_ = CheckResult::Unreachable;
    return false;
  }
  return true;
}

UpdateService::CheckResult UpdateService::consumeCheckResult() {
  Lock lock(mutex_);
  const CheckResult result = checkResult_;
  checkResult_ = CheckResult::None;
  return result;
}

UpdateService::Status UpdateService::status() const {
  Lock lock(mutex_);
  return status_;
}

bool UpdateService::busy() const {
  const Phase phase = status().phase;
  return phase == Phase::Page || phase == Phase::Firmware || phase == Phase::Restarting;
}

bool UpdateService::active() const {
  return status().phase == Phase::Checking || busy();
}

bool UpdateService::restartDue() const {
  Lock lock(mutex_);
  return status_.phase == Phase::Restarting && static_cast<int32_t>(millis() - restartAtMs_) >= 0;
}

void UpdateService::taskEntry(void* arg) {
  static_cast<UpdateService*>(arg)->run();
  vTaskDelete(nullptr);
}

void UpdateService::setPhase(Phase phase, uint32_t total) {
  Lock lock(mutex_);
  status_.phase = phase;
  status_.done = 0;
  status_.total = total;
}

void UpdateService::setProgress(uint32_t done) {
  Lock lock(mutex_);
  status_.done = done;
}

void UpdateService::fail(const char* error) {
  logLine("update", "Mise à jour abandonnée: %s", error);
  Lock lock(mutex_);
  status_.phase = Phase::Failed;
  status_.error = error;
}

void UpdateService::run() {
  if (checkMode_) {
    const CheckResult result = check();
    {
      Lock lock(mutex_);
      checkResult_ = result;
      if (result != CheckResult::Installing) {
        status_.phase = Phase::Idle;
      }
    }
    if (result != CheckResult::Installing) {
      return;
    }
  }
  install();
}

// Recherche automatique: dernière Release du dépôt (API GitHub, HTTPS), lue comme par la page.
UpdateService::CheckResult UpdateService::check() {
  WiFiClientSecure client;
  client.setCACertBundle(kCaBundle);
  HTTPClient http;
  http.setUserAgent(String("TotoBox/") + totobox::kFirmwareVersion);
  http.setTimeout(kHttpTimeoutMs);
  const String url = "https://api.github.com/repos/" + checkRepo_ + "/releases/latest";
  if (!http.begin(client, url)) {
    return CheckResult::Unreachable;
  }
  http.addHeader("Accept", "application/vnd.github+json");
  const int code = http.GET();
  if (code == 404) {
    http.end();
    logLine("update", "Aucune version publiée");
    return CheckResult::UpToDate;
  }
  if (code != HTTP_CODE_OK) {
    logLine("update", "Recherche de mise à jour impossible (HTTP %d)", code);
    http.end();
    return CheckResult::Unreachable;
  }
  // Seuls ces champs sont gardés: la réponse complète (notes, auteurs...) prendrait trop de mémoire.
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["body"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["size"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  JsonDocument release;
  const DeserializationError parsed = deserializeJson(release, http.getString(), DeserializationOption::Filter(filter));
  http.end();
  if (parsed != DeserializationError::Ok) {
    logLine("update", "Réponse de GitHub illisible (%s)", parsed.c_str());
    return CheckResult::Unreachable;
  }

  String latest = String(static_cast<const char*>(release["tag_name"] | ""));
  if (latest.startsWith("v")) {
    latest = latest.substring(1);
  }
  if (!update_rules::isNewer(checkCurrentVersion_, latest)) {
    logLine("update", "Version %s à jour (dernière publiée: %s)", checkCurrentVersion_.c_str(), latest.c_str());
    return CheckResult::UpToDate;
  }
  update_rules::Request request;
  const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
  const char* error = update_rules::parseRelease(release, request)
                          ? update_rules::check(request, checkCurrentVersion_, checkRepo_,
                                                slot != nullptr ? slot->size : 0, totobox::kUpdateMaxPageBytes)
                          : "no_online_update";
  if (error != nullptr) {
    logLine("update", "Version %s publiée, pas installable en ligne (%s): à installer par USB", latest.c_str(), error);
    return CheckResult::Unusable;
  }
  if (hold_) {
    logLine("update", "Version %s trouvée: installation reportée (lecture en cours)", latest.c_str());
    return CheckResult::Deferred;
  }
  logLine("update", "Version %s trouvée: installation automatique", latest.c_str());
  Lock lock(mutex_);
  request_ = request;
  status_ = Status();
  status_.phase = Phase::Page;
  status_.version = request.version;
  status_.total = request.page.size;
  return CheckResult::Installing;
}

void UpdateService::install() {
  // 1. Page, à côté de l'actuelle.
  LittleFS.remove(kNewPagePath);
  File page = LittleFS.open(kNewPagePath, FILE_WRITE);
  if (!page) {
    fail("write_failed");
    return;
  }
  const bool pageOk = download(request_.page, [&page](const uint8_t* data, size_t length) {
    return page.write(data, length) == length;
  });
  page.close();
  if (!pageOk) {
    LittleFS.remove(kNewPagePath);
    return;
  }

  // 2. Firmware, dans la partition OTA inactive.
  setPhase(Phase::Firmware, request_.firmware.size);
  if (!Update.begin(request_.firmware.size, U_FLASH)) {
    logLine("update", "Partition OTA indisponible (erreur %u)", Update.getError());
    LittleFS.remove(kNewPagePath);
    fail("no_space");
    return;
  }
  const bool firmwareOk = download(request_.firmware, [](const uint8_t* data, size_t length) {
    return Update.write(const_cast<uint8_t*>(data), length) == length;
  });
  // end(true) vérifie l'image (en-tête, somme de contrôle) et en fait la partition de démarrage.
  if (!firmwareOk || !Update.end(true)) {
    if (firmwareOk) {
      logLine("update", "Image refusée par la partition OTA (erreur %u)", Update.getError());
      fail("invalid_image");
    }
    Update.abort();
    LittleFS.remove(kNewPagePath);
    return;
  }

  // 3. Nouvelle page en place, puis redémarrage sur le nouveau firmware.
  LittleFS.remove(kPagePath);
  LittleFS.remove("/index.html");
  if (!LittleFS.rename(kNewPagePath, kPagePath)) {
    // Le nouveau firmware démarrera quand même; la page pourra être renvoyée par USB (uploadfs).
    logLine("update", "Nouvelle page impossible à mettre en place");
  }
  File version = LittleFS.open(kUiVersionPath, FILE_WRITE);
  if (version) {
    version.print(request_.uiVersion);
    version.close();
  }
  logLine("update", "Version %s installée: redémarrage", request_.version.c_str());
  Lock lock(mutex_);
  status_.phase = Phase::Restarting;
  restartAtMs_ = millis() + kRestartDelayMs;
}

bool UpdateService::download(const update_rules::Asset& asset,
                             const std::function<bool(const uint8_t*, size_t)>& sink) {
  WiFiClientSecure client;
  client.setCACertBundle(kCaBundle);
  HTTPClient http;
  // github.com redirige vers le serveur qui héberge les fichiers des Releases.
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setUserAgent(String("TotoBox/") + totobox::kFirmwareVersion);
  http.setTimeout(kHttpTimeoutMs);
  if (!http.begin(client, asset.url)) {
    fail("download_failed");
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    logLine("update", "Téléchargement de %s refusé (HTTP %d)", asset.url.c_str(), code);
    http.end();
    fail(code < 0 ? "no_internet" : "download_failed");
    return false;
  }
  const int announced = http.getSize();
  if (announced >= 0 && static_cast<uint32_t>(announced) != asset.size) {
    http.end();
    fail("invalid_size");
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);
  WiFiClient* stream = http.getStreamPtr();
  static uint8_t buffer[2048];
  uint32_t received = 0;
  uint32_t lastDataMs = millis();
  const char* error = nullptr;
  while (received < asset.size) {
    const size_t available = stream->available();
    if (available == 0) {
      if (!http.connected() || millis() - lastDataMs > kStallTimeoutMs) {
        error = "download_incomplete";
        break;
      }
      delay(5);
      continue;
    }
    const size_t wanted = std::min<size_t>({available, sizeof(buffer), asset.size - received});
    const int length = stream->readBytes(buffer, wanted);
    if (length <= 0) {
      continue;
    }
    mbedtls_sha256_update_ret(&sha, buffer, length);
    if (!sink(buffer, length)) {
      error = "write_failed";
      break;
    }
    received += length;
    lastDataMs = millis();
    setProgress(received);
  }
  http.end();
  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);

  if (error == nullptr && update_rules::toHex(digest, sizeof(digest)) != lowercase(asset.sha256)) {
    error = "hash_mismatch";
  }
  if (error != nullptr) {
    fail(error);
    return false;
  }
  logLine("update", "%s reçu et vérifié (%lu octets)", asset.url.c_str(), static_cast<unsigned long>(received));
  return true;
}
