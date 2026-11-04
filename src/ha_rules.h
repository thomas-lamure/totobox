// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include <vector>

#include "app_types.h"

// Règles de la liaison avec Home Assistant par MQTT (sans dépendance matérielle: testées sur ordinateur):
// adresse du broker, sujets, messages de découverte, état publié, commandes reçues. Conception: docs/home-assistant.md.
// La boîte apparaît comme un appareil: état, histoire, volume (borné aux limites de la boîte), bouton d'arrêt,
// cartes posées (étiquettes Home Assistant) et retirées (déclencheur), diagnostic.
namespace ha_rules {

constexpr uint16_t kDefaultPort = 1883;
constexpr char kDiscoveryPrefix[] = "homeassistant";
// Tampon du client MQTT: en-tête, sujet et contenu du plus long message de découverte.
constexpr size_t kMqttBufferBytes = 1280;
// Utilisateur et mot de passe du broker: longueur maximale acceptée par la page.
constexpr size_t kMaxCredentialLength = 64;
// Pendant la lecture, l'état (position) est republié à cet intervalle; sinon, seulement quand il change.
constexpr uint32_t kStateRefreshMs = 15000;
// Carte posée avant la connexion (réveil par une carte): publiée si elle l'a été il y a moins que ça.
constexpr uint32_t kTagMaxAgeMs = 60000;

struct Broker {
  String host;
  uint16_t port = kDefaultPort;
};

// Adresse saisie dans la page: nom ou IP, éventuellement avec un port MQTT ("192.168.1.20:1884",
// "mqtt://broker:1884"), ou adresse de la page Home Assistant ("http://192.168.1.10:8123/": le port web est
// ignoré, le broker étant sur la même machine). nullptr si l'adresse est lisible, sinon le code d'erreur:
// "invalid_address" ou "tls_unsupported" (mqtts://, pas encore pris en charge).
const char* parseAddress(const String& input, Broker& broker);

// Nom à résoudre par mDNS (la résolution DNS du framework ne le fait pas).
bool isLocalName(const String& host);

// Identifiant stable de la boîte (adresse MAC Wi-Fi), indépendant du nom réseau: "totobox_a1b2c3d4e5f6".
String deviceId(const uint8_t mac[6]);

// Sujet de la boîte: "totobox/<id>/<leaf>".
String topic(const String& id, const char* leaf);

constexpr char kAvailabilityLeaf[] = "availability";
constexpr char kStateLeaf[] = "state";
constexpr char kTagLeaf[] = "tag";
constexpr char kCardRemovedLeaf[] = "card_removed";
constexpr char kVolumeSetLeaf[] = "volume/set";
constexpr char kStopLeaf[] = "stop";
constexpr char kOnline[] = "online";
constexpr char kOffline[] = "offline";

struct DeviceInfo {
  String id;       // deviceId()
  String name;     // nom réseau de la boîte
  String version;  // version du firmware
  String url;      // page de la boîte ("http://totobox.local/")
  uint8_t volumeMin = 0;
  uint8_t volumeMax = 100;
};

struct Message {
  String topic;
  String payload;
};

// Messages de découverte (à publier conservés). Contenu vide à la place de chacun pour retirer l'appareil de
// Home Assistant (liaison désactivée).
std::vector<Message> discoveryMessages(const DeviceInfo& device);

struct State {
  String state;  // deviceStateToString(), ou "stopped" après un arrêt depuis Home Assistant
  String story;  // nom de l'histoire (carte associée), vide sinon
  String card;   // UID de la carte posée, vide sinon (jamais une carte maîtresse)
  uint16_t track = 0;   // piste en cours, à partir de 1 (0: aucune)
  uint16_t tracks = 0;
  uint32_t positionSec = 0;
  uint8_t volume = 0;
  uint8_t volumeMin = 0;
  uint8_t volumeMax = 100;
  String sd;     // sdStateToString()
  bool locked = false;
  int8_t rssi = 0;  // dBm, 0 si inconnu
};

// Valeur de « state »: deviceStateToString(), sauf "stopped" pour une lecture arrêtée depuis Home Assistant.
const char* stateName(const RuntimeState& runtime);

String stateJson(const State& state);

// Vrai si l'état doit être publié: changement (hors position), ou rafraîchissement de la position pendant la
// lecture. `sinceLastMs`: temps écoulé depuis la dernière publication.
bool shouldPublishState(const State& previous, const State& current, uint32_t sinceLastMs);

// Cartes publiées: connues et inconnues, jamais les cartes maîtresses, verrouillage actif ou non (leur UID
// suffit à en copier une).
bool shouldPublishCard(const DeviceConfig& config, const String& uid);

// Carte posée avant la connexion: encore utile à Home Assistant si elle date de moins de kTagMaxAgeMs.
inline bool isTagFresh(uint32_t ageMs) {
  return ageMs <= kTagMaxAgeMs;
}

enum class CommandType {
  None,
  Volume,
  Stop
};

struct Command {
  CommandType type = CommandType::None;
  uint8_t volume = 0;  // déjà borné aux limites de la boîte
};

// Sujets écoutés par la boîte.
std::vector<String> commandTopics(const String& id);

// Commande reçue sur `receivedTopic`. Volume: nombre (Home Assistant envoie "55" ou "55.0"), arrondi et borné
// à [volumeMin, volumeMax]. Contenu illisible ou sujet inconnu: CommandType::None.
Command parseCommand(const String& id, const String& receivedTopic, const String& payload, uint8_t volumeMin,
                     uint8_t volumeMax);

}  // namespace ha_rules
