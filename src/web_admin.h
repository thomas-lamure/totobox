// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <vector>

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <WebServer.h>

#include "access_lock.h"
#include "app_types.h"
#include "config_store.h"
#include "id3_stripper.h"

class WebAdmin {
 public:
  WebAdmin(ConfigStore& store, DeviceConfig& config, std::vector<CardBinding>& bindings, RuntimeState& runtime);

  void begin();
  // Réseau changé (connecté, point d'accès démarré ou arrêté): portail captif et mDNS remis à jour.
  void onNetworkChanged();
  void loop();
  // Appelé à chaque morceau reçu pendant un envoi: le serveur web garde la main jusqu'à la fin de la requête,
  // loop() ne tourne plus, et ce rappel garde les LEDs vivantes.
  void setUploadTick(std::function<void()> tick) { uploadTick_ = std::move(tick); }
  // Formate une carte SD illisible (AudioService): renvoie true si elle est prête ensuite.
  void setSdFormatter(std::function<bool()> formatter) { sdFormatter_ = std::move(formatter); }

 private:
  struct LibrarySummary {
    uint16_t trackCount = 0;
    uint32_t totalSize = 0;
    uint32_t durationEstimateSec = 0;
  };

  void registerRoutes();
  // Route /api protégée par request_guard (hôte, et pour une modification origine et corps JSON).
  void onApi(const char* path, HTTPMethod method, std::function<void()> handler);
  // Code d'erreur si la requête vient d'un autre site ou vise un autre nom que la boîte, sinon nullptr.
  const char* foreignRequestError(bool mutating, bool jsonBody);
  bool rejectIfForeign(bool mutating);
  // Déchiffre les secrets envoyés par la page (secret_channel); répond 400/409 et renvoie true en cas d'échec.
  bool rejectIfSealedInvalid(JsonDocument& request);
  std::vector<String> allowedHosts() const;
  void noteActivity();
  void restartMdns();
  void announceMdnsPeriodically();
  void redirectToPortal();
  void handleNotFound();
  bool rejectIfBusy();
  bool rejectIfLocked();
  void handleUnlock();
  void handleLock();
  void handleLockSetup();
  void handleLockPin();
  void handleLockCards();
  const char* validateMasterCard(const String& uid) const;
  void forgetUnknownCard(const String& uid);
  MasterCard masterCardFromRequest(JsonDocument& request) const;
  void sendLockConfig();
  void sendLockError(const char* error);
  void sendJson(int code, JsonDocument& doc);
  void refreshSdStats();
  void handleStatus();
  void handleGetConfig();
  void handlePostConfig();
  void handlePostVolume();
  void handlePostBindings();
  void handleLibrary();
  void handleUpload();
  void handleUploadData();
  bool writeUploadChunk(const uint8_t* data, size_t length);
  void handleCreateFolder();
  void handleDelete();
  void handleTrash();
  void handleRestore();
  void handleRename();
  void handleMove();
  void handleFactoryReset();
  void handleBackupInfo();
  void handleSetupComplete();
  void handleSdFormat();
  void handleBackupSave();
  void handleBackupRestore();
  void handlePurgeTrash();
  void sendJsonConfig();
  bool ensureDirectoryExists(const String& path);
  String resolveUploadPath(const String& targetDir, const String& safeName, const String& conflictMode) const;
  bool deletePathRecursive(const String& path);
  String movePathToTrash(const String& path);
  bool buildTreeAtPath(JsonArray& out, const String& rootPath, bool includeTrash, uint8_t depth, LibrarySummary* summary = nullptr);
  String renamePath(const String& sourcePath, const String& desiredName, const String& conflictMode);
  String movePath(const String& sourcePath, const String& targetDir, const char*& error);
  uint64_t sdTotalBytes() const;
  uint64_t sdUsedBytes() const;

  ConfigStore& store_;
  DeviceConfig& config_;
  std::vector<CardBinding>& bindings_;
  RuntimeState& runtime_;
  WebServer server_;
  // Portail captif, actif uniquement en mode point d'accès.
  DNSServer dns_;
  bool captivePortal_ = false;
  // Nom précédent après un changement depuis la page: elle reste servie sous l'ancien nom jusqu'au rechargement.
  String previousHostname_;
  // Version de la page présente dans LittleFS (ui_version.txt), renvoyée dans le statut.
  String uiVersion_;
  bool mdnsActive_ = false;
  uint32_t lastMdnsAnnounceMs_ = 0;
  access_lock::PinAttemptLimiter pinLimiter_;
  String portalHost_;
  File uploadFile_;
  Id3Stripper uploadStripper_;
  String uploadTargetDir_;
  String uploadConflictMode_;
  String uploadError_;
  uint32_t uploadBytesWritten_ = 0;
  uint32_t uploadStrippedBytes_ = 0;
  uint16_t uploadFileCount_ = 0;
  uint16_t uploadSkippedCount_ = 0;
  std::vector<String> uploadedPaths_;
  String lastResolvedPath_;
  uint64_t sdTotalCache_ = 0;
  uint64_t sdUsedCache_ = 0;
  bool sdStatsValid_ = false;
  std::function<void()> uploadTick_;
  std::function<bool()> sdFormatter_;
};
