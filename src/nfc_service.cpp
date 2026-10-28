// SPDX-License-Identifier: GPL-3.0-or-later
#include "nfc_service.h"

#include "logger.h"
#include "totobox_config.h"

namespace {

constexpr uint8_t kSpiDataWrite = 0x01;  // PN532_SPI_DATAWRITE
// Trame ACK envoyée par l'hôte: le PN532 abandonne la commande en cours (manuel PN532, §6.2.1.3).
constexpr uint8_t kAbortFrame[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
constexpr uint8_t kSearchRetriesForever = 0xFF;
// UID ISO14443A: 4, 7 ou 10 octets.
constexpr uint8_t kMaxUidLength = 10;
// readDetectedPassiveTargetID() copie autant d'octets que l'annonce la trame lue (jusqu'à 255), même quand on
// vide une réponse qui n'est pas une détection: tampon dimensionné pour le pire cas.
constexpr size_t kUidScratchSize = 256;

}  // namespace

NFCService::NFCService()
    : spiNfc_(HSPI), nfc_(totobox::kPn532Ss, &spiNfc_) {}

bool NFCService::begin() {
  spiNfc_.begin(totobox::kPn532Sck, totobox::kPn532Miso, totobox::kPn532Mosi, totobox::kPn532Ss);
  pinMode(totobox::kPn532Irq, INPUT);
  nfc_.begin();

  // Au réveil, la recherche sans fin armée avant la veille peut encore tourner, ou sa réponse attendre.
  abortCommand();
  drainPending(10);

  uint32_t version = 0;
  for (int attempt = 0; attempt < 3 && !version; ++attempt) {
    version = nfc_.getFirmwareVersion();
  }
  if (!version) {
    ready_ = false;
    return false;
  }

  // SAMConfig() lit sa réponse sans attendre qu'elle soit prête: on vide ce qui peut rester.
  nfc_.SAMConfig();
  drainPending(20);
  if (!setSearchRetries(totobox::kNfcSearchRetries)) {
    logLine("nfc", "Réglage du nombre d'essais refusé par le PN532");
  }

  phase_ = Phase::Idle;
  consecutiveFailures_ = 0;
  ready_ = true;
  return true;
}

NFCService::Event NFCService::tick() {
  if (!ready_) {
    tagPresent_ = false;
    return Event::None;
  }

  const uint32_t now = millis();
  if (phase_ == Phase::Idle) {
    if (now - lastStartMs_ < totobox::kNfcPollIntervalMs) {
      return Event::None;
    }
    lastStartMs_ = now;
    if (startDetection()) {
      phase_ = Phase::Waiting;
    } else {
      resync();
    }
    return Event::None;
  }

  // Recherche en cours: on ne lit la réponse que lorsque le PN532 la signale.
  if (!irqLow()) {
    if (now - lastStartMs_ >= totobox::kNfcResponseTimeoutMs) {
      phase_ = Phase::Idle;
      resync();
    }
    return Event::None;
  }
  phase_ = Phase::Idle;
  return readResult();
}

bool NFCService::detectOnce(String& uid, uint32_t timeoutMs) {
  if (!ready_) {
    return false;
  }
  delay(totobox::kNfcSettleMs);  // appels rapprochés: laisser le PN532 accepter la commande suivante
  if (!startDetection()) {
    return false;
  }
  const uint32_t startedAt = millis();
  while (!irqLow() && millis() - startedAt < timeoutMs) {
    delay(1);
  }
  phase_ = Phase::Idle;
  if (!irqLow()) {
    abortCommand();
    return false;
  }
  if (readResult() != Event::CardPresent) {
    return false;
  }
  uid = lastUid_;
  return true;
}

bool NFCService::isTagPresent() const {
  return tagPresent_;
}

String NFCService::lastUid() const {
  return lastUid_;
}

bool NFCService::armWakeOnCard() {
  if (!ready_) {
    return false;
  }

  // Avec un nombre d'essais fini, la recherche en cours se termine en quelques ms: on attend sa réponse pour
  // trouver le PN532 au repos. Constaté sur la carte: une commande envoyée juste après une trame d'abandon
  // n'est pas acquittée, d'où l'abandon en dernier recours seulement, suivi d'une resynchronisation.
  if (phase_ == Phase::Waiting && !drainPending(totobox::kNfcResponseTimeoutMs)) {
    abortCommand();
    nfc_.getFirmwareVersion();
  }
  phase_ = Phase::Idle;
  delay(totobox::kNfcSettleMs);

  // 1. InAutoPoll sans fin, une recherche toutes les 2 x 150 ms, cartes Mifare/ISO14443A (type 0x10):
  //    champ RF coupé entre deux recherches, plus économe.
  const char* mode = nullptr;
  uint8_t autoPoll[] = {PN532_COMMAND_INAUTOPOLL, 0xFF, 0x02, 0x10};
  const bool autoPollAck = nfc_.sendCommandCheckAck(autoPoll, sizeof(autoPoll), 100);
  bool retriesAck = false;
  bool detectionAck = false;
  if (autoPollAck) {
    mode = "InAutoPoll";
  } else {
    // 2. Repli: recherche classique sans fin (champ RF permanent), après resynchronisation.
    nfc_.getFirmwareVersion();
    delay(totobox::kNfcSettleMs);
    retriesAck = setSearchRetries(kSearchRetriesForever);
    detectionAck = retriesAck && startDetection();
    if (detectionAck) {
      mode = "InListPassiveTarget sans fin";
    }
  }

  // IRQ basse: une réponse attend (carte arrivée à l'instant ?), le réveil serait immédiat: on ne l'arme pas.
  const uint32_t startedAt = millis();
  while (irqLow() && millis() - startedAt < 50) {
    delay(1);
  }
  const bool irqHigh = !irqLow();
  // Constaté sur la carte: l'ACK d'InAutoPoll n'est pas lu correctement, mais la commande est exécutée.
  // Le PN532 cherche alors seul et ignore les commandes radio suivantes (la recherche de repli), tout en
  // répondant aux autres (réglage des essais). Ce profil signe une recherche autonome active.
  if (mode == nullptr && retriesAck && !detectionAck) {
    mode = "InAutoPoll probablement actif (ACK non lu)";
  }
  logLine("nfc", "Réveil par carte: %s (InAutoPoll %s, essais %s, recherche %s), IRQ %s",
          mode != nullptr ? mode : "état du PN532 inconnu", autoPollAck ? "ACK" : "KO",
          autoPollAck ? "-" : (retriesAck ? "ACK" : "KO"), autoPollAck ? "-" : (detectionAck ? "ACK" : "KO"),
          irqHigh ? "haute" : "basse");
  // IRQ haute: on arme le réveil par carte même sans ACK confirmé. Si le PN532 cherche une carte, il baissera
  // l'IRQ à son arrivée; sinon rien ne change (l'encodeur réveille toujours).
  return irqHigh;
}

bool NFCService::startDetection() {
  return nfc_.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);
}

NFCService::Event NFCService::readResult() {
  uint8_t uid[kUidScratchSize] = {0};
  uint8_t length = 0;
  if (nfc_.readDetectedPassiveTargetID(uid, &length) && length > 0 && length <= kMaxUidLength) {
    lastUid_ = formatUid(uid, length);
    tagPresent_ = true;
    consecutiveFailures_ = 0;
    return Event::CardPresent;
  }
  tagPresent_ = false;
  consecutiveFailures_ = 0;
  return Event::NoCard;
}

void NFCService::abortCommand() {
  spiNfc_.beginTransaction(SPISettings(1000000, SPI_LSBFIRST, SPI_MODE0));
  digitalWrite(totobox::kPn532Ss, LOW);
  delay(2);  // le PN532 peut être en veille interne: comme Adafruit_PN532::wakeup()
  spiNfc_.transfer(kSpiDataWrite);
  for (const uint8_t byte : kAbortFrame) {
    spiNfc_.transfer(byte);
  }
  digitalWrite(totobox::kPn532Ss, HIGH);
  spiNfc_.endTransaction();
  delay(1);
}

// Lit et jette une réponse signalée par l'IRQ (readDetectedPassiveTargetID lit la trame sans l'interpréter
// au-delà de son en-tête).
bool NFCService::drainPending(uint32_t timeoutMs) {
  const uint32_t startedAt = millis();
  while (!irqLow() && millis() - startedAt < timeoutMs) {
    delay(1);
  }
  if (!irqLow()) {
    return false;
  }
  uint8_t scratch[kUidScratchSize];
  uint8_t length = 0;
  nfc_.readDetectedPassiveTargetID(scratch, &length);
  delay(totobox::kNfcSettleMs);
  return true;
}

// setPassiveActivationRetries() de la bibliothèque ne lit pas la réponse du PN532: on la vide ici, sinon
// la commande suivante lirait cette réponse à la place de son accusé de réception.
bool NFCService::setSearchRetries(uint8_t retries) {
  if (!nfc_.setPassiveActivationRetries(retries)) {
    return false;
  }
  drainPending(50);
  return true;
}

void NFCService::resync() {
  if (consecutiveFailures_ < 255) {
    ++consecutiveFailures_;
  }
  if (consecutiveFailures_ == 3) {
    logLine("nfc", "Le PN532 ne répond plus: réinitialisation de l'échange");
  }
  abortCommand();
  drainPending(10);
  nfc_.getFirmwareVersion();  // resynchronise l'échange après l'abandon
  delay(totobox::kNfcSettleMs);
  setSearchRetries(totobox::kNfcSearchRetries);
}

bool NFCService::irqLow() const {
  return digitalRead(totobox::kPn532Irq) == LOW;
}

String NFCService::formatUid(const uint8_t* uid, uint8_t length) const {
  String result;
  for (uint8_t index = 0; index < length; ++index) {
    if (uid[index] < 0x10) {
      result += '0';
    }
    result += String(uid[index], HEX);
  }
  result.toUpperCase();
  return result;
}
