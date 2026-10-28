// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include "app_types.h"

// Connexion réseau non bloquante: WiFi.begin() rend la main tout de suite (la connexion se fait dans les
// tâches réseau d'ESP-IDF), tick() suit l'avancement depuis loop() et bascule en point d'accès si le Wi-Fi
// maison ne répond pas dans le délai. Le NFC et l'audio sont ainsi utilisables dès le démarrage.
//
// Les réglages réseau modifiés depuis l'interface s'appliquent sans redémarrage (requestReconfigure()).
// Depuis le point d'accès, la boîte essaie le Wi-Fi maison en gardant son point d'accès (mode AP+STA): le
// téléphone reste connecté et la page affiche le résultat; en cas de succès, le point d'accès se coupe
// après kApCloseDelayMs, en cas d'échec rien n'est perdu.
class NetworkService {
 public:
  void begin(const DeviceConfig& config, RuntimeState& runtime);
  // Applique les nouveaux réglages après un court délai, le temps que la page reçoive sa réponse.
  void requestReconfigure();
  // Renvoie true au tour où le réseau change (station connectée, point d'accès démarré ou arrêté):
  // portail captif et mDNS sont alors remis à jour.
  bool tick();

 private:
  enum class Phase { Connecting, Station, AccessPoint, Trial };

  void connectStation();
  void startAccessPoint();
  void startTrial();
  void applyReconfigure();
  void tickStation(uint32_t now);
  void tickTrial(uint32_t now);
  void publishStation();
  void reportAttempt(const char* state, const char* error = "");

  const DeviceConfig* config_ = nullptr;
  RuntimeState* runtime_ = nullptr;
  Phase phase_ = Phase::Connecting;
  uint32_t startedAtMs_ = 0;
  bool changed_ = false;
  bool reconfigurePending_ = false;
  // Reconnexion après une coupure du Wi-Fi maison.
  uint32_t lostAtMs_ = 0;
  uint32_t nextRetryAtMs_ = 0;
  uint32_t retryDelayMs_ = 0;
  uint32_t reconfigureAtMs_ = 0;
  // Un seul nouvel essai par tentative depuis le point d'accès.
  bool trialRetried_ = false;
  // Réglages appliqués, pour ne reconnecter que ce qui a réellement changé.
  String appliedSsid_;
  String appliedPassword_;
  String appliedApSsid_;
  String appliedApPassword_;
};
