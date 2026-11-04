// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "app_types.h"
#include "ha_rules.h"

// Liaison avec Home Assistant par MQTT (docs/home-assistant.md): la boîte s'y annonce comme un appareil (état,
// volume, arrêt, cartes posées et retirées), découverte automatique comprise. Les décisions (sujets, messages,
// bornes, quand publier) sont dans ha_rules.
//
// Le client (PubSubClient) vit dans une tâche du cœur 0, comme UpdateService: connexion, résolution du nom,
// attentes réseau et reconnexions ne prennent jamais de temps au cœur 1 (audio, loop()). PubSubClient n'étant pas
// prévu pour plusieurs tâches, seule cette tâche l'utilise: loop() lui confie réglages et état (sous mutex) et
// les cartes posées ou retirées (file), et relève les commandes reçues (autre file). Rien n'y attend le réseau,
// sauf shutdown() avant la veille.
class HomeAssistantService {
 public:
  struct Status {
    String state = "off";  // "off", "connecting", "connected" ou "error"
    String error;          // "invalid_address", "no_wifi", "dns", "unreachable", "auth"
  };

  // `mac`: adresse MAC Wi-Fi, base de l'identifiant de l'appareil dans Home Assistant.
  bool begin(const uint8_t mac[6]);
  // Réglages (au démarrage et après une modification depuis la page): reconnexion avec les nouveaux, ou retrait
  // de l'appareil de Home Assistant si la liaison est désactivée.
  void configure(const DeviceConfig& config);
  // État courant, appelé régulièrement par loop(): `online` = connectée au Wi-Fi de la maison. La tâche publie ce
  // qui a changé, et republie la découverte si l'appareil a changé (nom, limites du volume).
  void update(bool online, const ha_rules::DeviceInfo& device, const ha_rules::State& state);
  // Carte posée ou retirée (jamais une carte maîtresse: ha_rules::shouldPublishCard). Gardée jusqu'à la
  // connexion, publiée si elle date de moins de ha_rules::kTagMaxAgeMs (réveil par une carte).
  void notifyCardPresented(const String& uid);
  void notifyCardRemoved(const String& uid);
  // Commande reçue de Home Assistant (volume déjà borné); faux s'il n'y en a pas.
  bool takeCommand(ha_rules::Command& command);
  Status status() const;
  // Avant la veille ou un redémarrage: publie « offline » et se déconnecte, en `timeoutMs` au plus.
  void shutdown(uint32_t timeoutMs);

 private:
  enum class EventType : uint8_t { Presented, Removed };
  struct CardEvent {
    EventType type;
    uint32_t atMs;
    char uid[24];
  };

  static void taskEntry(void* arg);
  void run();
  void step();
  bool connect();
  void disconnect(bool removeDevice);
  void publishDiscovery(bool remove);
  void publishPending();
  void onMessage(char* topic, uint8_t* payload, unsigned int length);
  void setStatus(const char* state, const char* error);
  void queueCard(EventType type, const String& uid);

  SemaphoreHandle_t mutex_ = nullptr;
  QueueHandle_t cardEvents_ = nullptr;
  QueueHandle_t commands_ = nullptr;

  // Partagé avec loop(), sous mutex.
  bool enabled_ = false;
  String address_;
  String user_;
  String password_;
  bool reconfigure_ = false;
  bool online_ = false;
  bool haveState_ = false;
  ha_rules::DeviceInfo device_;
  ha_rules::State state_;
  Status status_;
  volatile bool shutdownRequested_ = false;
  volatile bool shutdownDone_ = false;

  // Propre à la tâche.
  String id_;
  bool connected_ = false;
  bool discoveryPublished_ = false;
  String discoveryKey_;
  ha_rules::State published_;
  bool statePublished_ = false;
  uint32_t lastStateMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint32_t retryDelayMs_ = 0;
  bool addressInvalid_ = false;
};
