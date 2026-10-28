// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <functional>

#include "update_rules.h"

// Mise à jour en ligne, demandée depuis la page (POST /api/update) une fois la nouvelle version trouvée sur
// GitHub. Tâche de fond sur le cœur 0 (téléchargement HTTPS de plusieurs secondes): loop() continue (page, LEDs).
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
  enum class Phase : uint8_t { Idle, Page, Firmware, Restarting, Failed };

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
  Status status() const;
  // Téléchargement ou redémarrage en cours: pas de lecture, pas de mise en veille.
  bool busy() const;
  // Nouvelle version installée: redémarrer (après le délai laissé à la page pour voir la fin).
  bool restartDue() const;

  static const char* phaseName(Phase phase);

 private:
  static void taskEntry(void* arg);
  void run();
  bool download(const update_rules::Asset& asset, const std::function<bool(const uint8_t*, size_t)>& sink);
  void setPhase(Phase phase, uint32_t total);
  void setProgress(uint32_t done);
  void fail(const char* error);

  SemaphoreHandle_t mutex_ = nullptr;
  update_rules::Request request_;
  Status status_;
  uint32_t restartAtMs_ = 0;
};
