// SPDX-License-Identifier: GPL-3.0-or-later
#include "secret_channel.h"

#include <sodium.h>

#include <cstring>

#include "logger.h"

namespace secret_channel {

namespace {

constexpr size_t kChallengeBytes = 16;
// Message chiffré: défi et quelques secrets courts, largement en dessous.
constexpr size_t kMaxSealedBytes = 512;

uint8_t secretKey[crypto_box_SECRETKEYBYTES];
uint8_t publicKey[crypto_box_PUBLICKEYBYTES];
uint8_t challenge[kChallengeBytes];
bool ready = false;

String toBase64(const uint8_t* data, size_t length) {
  char text[sodium_base64_ENCODED_LEN(kMaxSealedBytes, sodium_base64_VARIANT_ORIGINAL)];
  if (sodium_base64_ENCODED_LEN(length, sodium_base64_VARIANT_ORIGINAL) > sizeof(text)) {
    return "";
  }
  sodium_bin2base64(text, sizeof(text), data, length, sodium_base64_VARIANT_ORIGINAL);
  return String(text);
}

// Renvoie le nombre d'octets décodés, ou -1 (invalide ou plus grand que `capacity`).
int fromBase64(const char* text, uint8_t* out, size_t capacity) {
  if (text == nullptr) {
    return -1;
  }
  size_t length = 0;
  if (sodium_base642bin(out, capacity, text, strlen(text), nullptr, &length, nullptr,
                        sodium_base64_VARIANT_ORIGINAL) != 0) {
    return -1;
  }
  return static_cast<int>(length);
}

void rotateChallenge() {
  randombytes_buf(challenge, sizeof(challenge));
}

}  // namespace

bool begin(DeviceConfig& config) {
  if (sodium_init() < 0) {
    logLine("secret", "libsodium indisponible: secrets acceptés en clair seulement");
    return false;
  }
  bool changed = false;
  if (sodium_hex2bin(secretKey, sizeof(secretKey), config.channelSecretKey.c_str(), config.channelSecretKey.length(),
                     nullptr, nullptr, nullptr) != 0 ||
      config.channelSecretKey.length() != 2 * sizeof(secretKey)) {
    crypto_box_keypair(publicKey, secretKey);
    char hex[2 * sizeof(secretKey) + 1];
    sodium_bin2hex(hex, sizeof(hex), secretKey, sizeof(secretKey));
    config.channelSecretKey = hex;
    sodium_memzero(hex, sizeof(hex));
    changed = true;
    logLine("secret", "Nouvelle paire de clés pour les secrets de la page");
  } else {
    crypto_scalarmult_base(publicKey, secretKey);
  }
  rotateChallenge();
  ready = true;
  return changed;
}

String publicKeyBase64() {
  return ready ? toBase64(publicKey, sizeof(publicKey)) : String();
}

String challengeBase64() {
  return ready ? toBase64(challenge, sizeof(challenge)) : String();
}

const char* unseal(JsonDocument& doc) {
  if (!doc["sealed"].is<JsonObjectConst>()) {
    return nullptr;  // rien de chiffré
  }
  if (!ready) {
    return "sealed_invalid";
  }
  JsonObjectConst sealed = doc["sealed"].as<JsonObjectConst>();
  uint8_t ephemeralKey[crypto_box_PUBLICKEYBYTES];
  uint8_t nonce[crypto_box_NONCEBYTES];
  uint8_t box[kMaxSealedBytes + crypto_box_MACBYTES];
  const int keyLength = fromBase64(sealed["epk"] | static_cast<const char*>(nullptr), ephemeralKey, sizeof(ephemeralKey));
  const int nonceLength = fromBase64(sealed["nonce"] | static_cast<const char*>(nullptr), nonce, sizeof(nonce));
  const int boxLength = fromBase64(sealed["box"] | static_cast<const char*>(nullptr), box, sizeof(box));
  if (keyLength != static_cast<int>(sizeof(ephemeralKey)) || nonceLength != static_cast<int>(sizeof(nonce)) ||
      boxLength < static_cast<int>(crypto_box_MACBYTES)) {
    return "sealed_invalid";
  }

  char message[kMaxSealedBytes + 1];
  const size_t messageLength = boxLength - crypto_box_MACBYTES;
  if (crypto_box_open_easy(reinterpret_cast<uint8_t*>(message), box, boxLength, nonce, ephemeralKey, secretKey) != 0) {
    return "sealed_invalid";
  }
  message[messageLength] = '\0';

  JsonDocument secrets;
  const DeserializationError parsed = deserializeJson(secrets, message, messageLength);
  sodium_memzero(message, sizeof(message));
  if (parsed != DeserializationError::Ok || !secrets.is<JsonObjectConst>()) {
    return "sealed_invalid";
  }

  // Défi: celui en cours seulement, puis on en tire un autre (une requête enregistrée ne peut pas être rejouée).
  uint8_t received[kChallengeBytes];
  if (fromBase64(secrets["challenge"] | static_cast<const char*>(nullptr), received, sizeof(received)) !=
          static_cast<int>(sizeof(received)) ||
      sodium_memcmp(received, challenge, sizeof(challenge)) != 0) {
    return "stale_challenge";
  }
  rotateChallenge();

  for (JsonPairConst field : secrets.as<JsonObjectConst>()) {
    if (strcmp(field.key().c_str(), "challenge") != 0) {
      // Copie (String): `secrets` disparaît à la fin de la fonction. Les secrets sont des textes.
      doc[String(field.key().c_str())] = String(field.value() | "");
    }
  }
  doc.remove("sealed");
  return nullptr;
}

}  // namespace secret_channel
