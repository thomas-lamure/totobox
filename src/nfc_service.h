// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Adafruit_PN532.h>
#include <SPI.h>

// Détection NFC pilotée par l'IRQ du PN532 (GPIO21): tick() lance une recherche, puis surveille l'IRQ et ne
// lit la réponse qu'une fois prête. loop() n'attend jamais le PN532.
//
// Le PN532 est réglé pour un nombre d'essais fini (kNfcSearchRetries): il répond "aucune carte" au lieu de
// chercher indéfiniment, ce qui permet de détecter le retrait et le garde toujours disponible pour la commande
// suivante (une recherche sans fin lui fait ignorer toute nouvelle commande de détection).
class NFCService {
 public:
  enum class Event { None, CardPresent, NoCard };

  NFCService();

  bool begin();
  // Non bloquant, à appeler à chaque tour de loop(). CardPresent/NoCard au tour où une recherche aboutit.
  Event tick();
  // Recherche bloquante courte (réveil de vérification d'une carte laissée sur la boîte).
  bool detectOnce(String& uid, uint32_t timeoutMs);
  bool isTagPresent() const;
  String lastUid() const;
  // Avant la veille: le PN532 cherche seul les cartes et baissera son IRQ à l'arrivée de l'une d'elles.
  bool armWakeOnCard();

 private:
  enum class Phase { Idle, Waiting };

  bool startDetection();
  Event readResult();
  void abortCommand();
  bool drainPending(uint32_t timeoutMs);
  bool setSearchRetries(uint8_t retries);
  void resync();
  bool irqLow() const;
  String formatUid(const uint8_t* uid, uint8_t length) const;

  SPIClass spiNfc_;
  Adafruit_PN532 nfc_;
  bool ready_ = false;
  bool tagPresent_ = false;
  String lastUid_;
  Phase phase_ = Phase::Idle;
  uint32_t lastStartMs_ = 0;
  uint8_t consecutiveFailures_ = 0;
};
