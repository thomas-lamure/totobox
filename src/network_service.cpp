// SPDX-License-Identifier: GPL-3.0-or-later
#include "network_service.h"

#include <WiFi.h>

#include <algorithm>

#include "logger.h"
#include "retry_delay.h"
#include "totobox_config.h"

namespace {

// Dernière raison de déconnexion de la station (écrite par la tâche d'événements Wi-Fi).
volatile uint8_t lastDisconnectReason = 0;

// Délai avant de retenter un essai refusé, laissé au framework pour sa propre reprise.
constexpr uint32_t kTrialRetryAfterMs = 4000;

// Raison d'échec compréhensible pour l'interface. Constaté sur la carte: AUTH_FAIL/AUTH_EXPIRE apparaissent
// aussi lors de connexions qui finissent par réussir; on ne conclut donc qu'à l'expiration du délai.
const char* describeFailure(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
      return "network_not_found";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "wrong_password";
    default:
      return "connection_failed";
  }
}

}  // namespace

void NetworkService::begin(const DeviceConfig& config, RuntimeState& runtime) {
  config_ = &config;
  runtime_ = &runtime;
  runtime_->wifiConnected = false;

  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { lastDisconnectReason = info.wifi_sta_disconnected.reason; },
               ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  if (config.wifiSsid.isEmpty()) {
    logLine("net", "Aucun Wi-Fi maison configuré");
    startAccessPoint();
    return;
  }
  connectStation();
}

void NetworkService::requestReconfigure() {
  reconfigurePending_ = true;
  reconfigureAtMs_ = millis() + totobox::kNetworkApplyDelayMs;
}

bool NetworkService::tick() {
  if (runtime_ == nullptr) {
    return false;
  }
  uint32_t now = millis();

  if (reconfigurePending_ && static_cast<int32_t>(now - reconfigureAtMs_) >= 0) {
    reconfigurePending_ = false;
    applyReconfigure();
    // startedAtMs_ vient d'être relevé après `now`: sans relecture, `now - startedAtMs_` déborde et l'essai
    // est déclaré expiré dès ce tour.
    now = millis();
  }

  switch (phase_) {
    case Phase::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        phase_ = Phase::Station;
        publishStation();
        logLine("net", "Wi-Fi connecté en %lu ms, IP %s", static_cast<unsigned long>(now - startedAtMs_),
                runtime_->wifiIp.c_str());
        if (runtime_->networkAttempt == "trying") {
          runtime_->networkAttemptIp = runtime_->wifiIp;
          reportAttempt("connected");
        }
        changed_ = true;
      } else if (now - startedAtMs_ >= totobox::kWifiConnectTimeoutMs) {
        const char* failure = describeFailure(lastDisconnectReason);
        logLine("net", "Wi-Fi %s injoignable après %lu s (%s)", config_->wifiSsid.c_str(),
                static_cast<unsigned long>(totobox::kWifiConnectTimeoutMs / 1000), failure);
        if (runtime_->networkAttempt == "trying") {
          reportAttempt("failed", failure);
        }
        startAccessPoint();
      }
      break;

    case Phase::Station:
      tickStation(now);
      break;

    case Phase::Trial:
      tickTrial(now);
      break;

    case Phase::AccessPoint:
      break;
  }

  const bool changed = changed_;
  changed_ = false;
  return changed;
}

// Reconnexion gérée ici plutôt que par le framework Arduino: il ne retente qu'une fois par coupure, et
// abandonne si cette tentative échoue avec une raison jugée définitive (AUTH_FAIL). Constaté sur la carte:
// la box refuse souvent la première tentative (AUTH_FAIL puis succès), et la boîte restait déconnectée
// après la première coupure (~2 min). On réessaie donc tant que le Wi-Fi n'est pas revenu, en espaçant.
void NetworkService::tickStation(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!runtime_->wifiConnected) {
      publishStation();
      logLine("net", "Wi-Fi reconnecté après %lu s, IP %s", static_cast<unsigned long>((now - lostAtMs_) / 1000),
              runtime_->wifiIp.c_str());
      changed_ = true;
    }
    retryDelayMs_ = totobox::kWifiRetryInitialMs;
    return;
  }

  if (runtime_->wifiConnected) {
    runtime_->wifiConnected = false;
    lostAtMs_ = now;
    retryDelayMs_ = totobox::kWifiRetryInitialMs;
    nextRetryAtMs_ = now + retryDelayMs_;
    const uint8_t reason = lastDisconnectReason;
    logLine("net", "Wi-Fi perdu (raison %u: %s), nouvelle tentative dans %lu s", reason,
            WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)),
            static_cast<unsigned long>(retryDelayMs_ / 1000));
    return;
  }

  if (static_cast<int32_t>(now - nextRetryAtMs_) < 0) {
    return;
  }
  logLine("net", "Reconnexion au Wi-Fi %s (déconnecté depuis %lu s, dernière raison %u)", config_->wifiSsid.c_str(),
          static_cast<unsigned long>((now - lostAtMs_) / 1000), lastDisconnectReason);
  WiFi.disconnect();
  WiFi.begin(config_->wifiSsid.c_str(), config_->wifiPassword.c_str());
  retryDelayMs_ = retry_delay::next(retryDelayMs_, totobox::kWifiRetryMaxMs);
  nextRetryAtMs_ = now + retryDelayMs_;
}

// Essai du Wi-Fi maison depuis le point d'accès: le point d'accès reste actif pendant l'essai.
void NetworkService::tickTrial(uint32_t now) {
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (runtime_->apCloseAtMs == 0) {
    if (connected) {
      runtime_->networkAttemptIp = WiFi.localIP().toString();
      runtime_->apCloseAtMs = now + totobox::kApCloseDelayMs;
      reportAttempt("connected");
      logLine("net", "Wi-Fi %s rejoint, IP %s: point d'accès coupé dans %lu s", config_->wifiSsid.c_str(),
              runtime_->networkAttemptIp.c_str(), static_cast<unsigned long>(totobox::kApCloseDelayMs / 1000));
      changed_ = true;
    } else if (!trialRetried_ && lastDisconnectReason != 0 && now - startedAtMs_ >= kTrialRetryAfterMs) {
      // La box refuse souvent la première tentative (AUTH_FAIL) et le framework n'insiste pas: un second essai.
      trialRetried_ = true;
      logLine("net", "Essai du Wi-Fi %s refusé (raison %u), nouvel essai", config_->wifiSsid.c_str(),
              lastDisconnectReason);
      lastDisconnectReason = 0;
      WiFi.disconnect();
      WiFi.begin(config_->wifiSsid.c_str(), config_->wifiPassword.c_str());
    } else if (now - startedAtMs_ >= totobox::kWifiConnectTimeoutMs) {
      const char* failure = describeFailure(lastDisconnectReason);
      logLine("net", "Essai du Wi-Fi %s en échec (%s): la boîte reste en point d'accès", config_->wifiSsid.c_str(),
              failure);
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      phase_ = Phase::AccessPoint;
      reportAttempt("failed", failure);
    }
    return;
  }

  // Connexion réussie: on laisse au téléphone le temps de lire le résultat, puis on coupe le point d'accès.
  if (static_cast<int32_t>(now - runtime_->apCloseAtMs) < 0) {
    return;
  }
  runtime_->apCloseAtMs = 0;
  if (connected) {
    WiFi.mode(WIFI_STA);
    phase_ = Phase::Station;
    publishStation();
    logLine("net", "Point d'accès coupé, Wi-Fi maison %s, IP %s", config_->wifiSsid.c_str(), runtime_->wifiIp.c_str());
  } else {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    phase_ = Phase::AccessPoint;
    reportAttempt("failed", "connection_failed");
    logLine("net", "Wi-Fi %s perdu avant la coupure du point d'accès: on le garde", config_->wifiSsid.c_str());
  }
  changed_ = true;
}

// Ne touche qu'à ce qui a changé: un réglage de volume enregistré avec les réglages ne reconnecte rien.
void NetworkService::applyReconfigure() {
  const bool stationChanged = config_->wifiSsid != appliedSsid_ || config_->wifiPassword != appliedPassword_;
  const bool apChanged = config_->apSsid != appliedApSsid_ || config_->apPassword != appliedApPassword_;
  const bool wantsStation = !config_->wifiSsid.isEmpty();

  if (phase_ == Phase::AccessPoint || phase_ == Phase::Trial) {
    if (apChanged) {
      logLine("net", "Nouveau point d'accès %s", config_->apSsid.c_str());
      startAccessPoint();
    }
    // Depuis le point d'accès, tout enregistrement avec un Wi-Fi maison relance un essai (nouvel essai
    // après un mot de passe corrigé, par exemple).
    if (wantsStation) {
      startTrial();
    } else {
      // Wi-Fi maison oublié: un essai en cours (ou un point d'accès en sursis) est abandonné.
      if (phase_ == Phase::Trial) {
        logLine("net", "Wi-Fi maison oublié pendant l'essai: retour au point d'accès seul");
        WiFi.disconnect();
        startAccessPoint();
      }
      appliedSsid_ = "";
      appliedPassword_ = "";
      reportAttempt("");
    }
    return;
  }

  if (!stationChanged) {
    return;
  }
  if (wantsStation) {
    logLine("net", "Nouveau Wi-Fi maison %s", config_->wifiSsid.c_str());
    runtime_->networkAttemptSsid = config_->wifiSsid;
    reportAttempt("trying");
    WiFi.disconnect();
    connectStation();
  } else {
    logLine("net", "Wi-Fi maison oublié: passage en point d'accès");
    WiFi.disconnect();
    appliedSsid_ = "";
    appliedPassword_ = "";
    startAccessPoint();
  }
}

void NetworkService::connectStation() {
  // Le nom DHCP n'est pris en compte qu'au démarrage de l'interface station.
  WiFi.mode(WIFI_OFF);
  WiFi.setHostname(config_->hostname.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  // La MAC n'est lisible qu'une fois le Wi-Fi activé.
  runtime_->wifiMac = WiFi.macAddress();
  lastDisconnectReason = 0;
  WiFi.begin(config_->wifiSsid.c_str(), config_->wifiPassword.c_str());
  appliedSsid_ = config_->wifiSsid;
  appliedPassword_ = config_->wifiPassword;
  phase_ = Phase::Connecting;
  startedAtMs_ = millis();
  runtime_->wifiConnected = false;
  runtime_->wifiMode = "connecting";
  runtime_->wifiIp = "";
  logLine("net", "Connexion au Wi-Fi %s en arrière-plan", config_->wifiSsid.c_str());
}

void NetworkService::startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(config_->apSsid.c_str(), config_->apPassword.c_str());
  WiFi.setSleep(false);
  appliedApSsid_ = config_->apSsid;
  appliedApPassword_ = config_->apPassword;
  runtime_->wifiMac = WiFi.macAddress();
  runtime_->apCloseAtMs = 0;
  phase_ = Phase::AccessPoint;
  runtime_->wifiConnected = true;
  runtime_->wifiMode = "ap";
  runtime_->wifiIp = WiFi.softAPIP().toString();
  logLine("net", "Point d'accès %s démarré (mot de passe %s), IP %s", config_->apSsid.c_str(),
          config_->apPasswordGenerated                 ? config_->apPassword.c_str()
          : config_->apPassword == config_->factoryApPassword ? "d'usine"
                                                               : "personnalisé",
          runtime_->wifiIp.c_str());
  changed_ = true;
}

void NetworkService::startTrial() {
  WiFi.setHostname(config_->hostname.c_str());
  WiFi.mode(WIFI_AP_STA);
  lastDisconnectReason = 0;
  WiFi.begin(config_->wifiSsid.c_str(), config_->wifiPassword.c_str());
  appliedSsid_ = config_->wifiSsid;
  appliedPassword_ = config_->wifiPassword;
  phase_ = Phase::Trial;
  startedAtMs_ = millis();
  trialRetried_ = false;
  runtime_->apCloseAtMs = 0;
  runtime_->networkAttemptSsid = config_->wifiSsid;
  runtime_->networkAttemptIp = "";
  reportAttempt("trying");
  // Longueur seulement: le mot de passe ne doit jamais apparaître dans les logs.
  logLine("net", "Essai du Wi-Fi %s (mot de passe de %u caractères), point d'accès maintenu",
          config_->wifiSsid.c_str(), static_cast<unsigned>(config_->wifiPassword.length()));
}

void NetworkService::publishStation() {
  runtime_->wifiConnected = true;
  runtime_->wifiMode = "sta";
  runtime_->wifiIp = WiFi.localIP().toString();
}

void NetworkService::reportAttempt(const char* state, const char* error) {
  if (strcmp(state, "trying") == 0) {
    ++runtime_->networkAttemptId;
  }
  runtime_->networkAttempt = state;
  runtime_->networkError = error;
}
