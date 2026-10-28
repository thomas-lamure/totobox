// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <functional>

#include "update_rules.h"

// Mise à jour en ligne, demandée depuis la page (POST /api/update) une fois la nouvelle version trouvée sur
// GitHub, ou cherchée par la boîte elle-même une fois par jour en mode automatique (startCheck). Tâche de fond
// sur le cœur 0 (téléchargement HTTPS de plusieurs secondes): loop() continue (page, LEDs).
// Ordre choisi pour ne jamais laisser la boîte sans page ni sans firmware:
//  1. la page est téléchargée à côté de l'actuelle (/index.html.gz.new) et son empreinte vérifiée;
//  2. le firmware est écrit dans la partition OTA inactive (Update) et son empreinte vérifiée: un échec ou une
//     coupure de courant laisse l'ancien firmware démarrer;
//  3. seulement alors la page remplace l'ancienne, la partition du nouveau firmware devient celle du démarrage,
//     et la boîte redémarre.
// Les certificats des serveurs de GitHub sont vérifiés avec les autorités racines de Mozilla
// (src/certs/x509_crt_bundle.bin, scripts/gen_ca_bundle.py).
class UpdateService {
 public:
  enum class Phase : uint8_t { Idle, Checking, Page, Firmware, Restarting, Failed };

  // Issue d'une recherche automatique (startCheck), relevée une fois par loop().
  enum class CheckResult : uint8_t {
    None,         // pas de recherche terminée depuis le dernier relevé
    UpToDate,     // déjà la dernière version
    Installing,   // nouvelle version trouvée: installation lancée (la boîte va redémarrer)
    Unusable,     // nouvelle version qui ne s'installe pas en ligne (USB): rien à faire avant la suivante
    Deferred,     // nouvelle version, mais une histoire a commencé entre-temps: réessayer plus tard
    Unreachable,  // GitHub injoignable: réessayer plus tard
  };

  struct Status {
    Phase phase = Phase::Idle;
    String version;
    uint32_t done = 0;   // octets reçus du fichier en cours
    uint32_t total = 0;  // taille du fichier en cours
    String error;        // Failed: code d'erreur (download_failed, hash_mismatch, write_failed...)
  };

  bool begin();
  // Lance la mise à jour (demande déjà validée par update_rules::check); faux si une est déjà en cours.
  bool start(const update_rules::Request& request);
  // Cherche la dernière version du dépôt `repo` et l'installe si elle est plus récente que `currentVersion`;
  // faux si une recherche ou une mise à jour est déjà en cours.
  bool startCheck(const String& repo, const String& currentVersion);
  CheckResult consumeCheckResult();
  // Mis à jour par loop(): vrai pendant une lecture ou avec une carte posée. Une version trouvée pendant ce
  // temps n'est pas installée (Deferred).
  void setHold(bool hold) { hold_ = hold; }
  Status status() const;
  // Téléchargement ou redémarrage en cours: pas de lecture, pas de mise en veille.
  bool busy() const;
  // Recherche ou mise à jour en cours (pas de nouvelle recherche, pas de mise en veille).
  bool active() const;
  // Nouvelle version installée: redémarrer (après le délai laissé à la page pour voir la fin).
  bool restartDue() const;

  static const char* phaseName(Phase phase);

 private:
  static void taskEntry(void* arg);
  void run();
  void install();
  CheckResult check();
  bool download(const update_rules::Asset& asset, const std::function<bool(const uint8_t*, size_t)>& sink);
  void setPhase(Phase phase, uint32_t total);
  void setProgress(uint32_t done);
  void fail(const char* error);

  SemaphoreHandle_t mutex_ = nullptr;
  update_rules::Request request_;
  bool checkMode_ = false;
  String checkRepo_;
  String checkCurrentVersion_;
  volatile bool hold_ = false;
  CheckResult checkResult_ = CheckResult::None;
  Status status_;
  uint32_t restartAtMs_ = 0;
};
