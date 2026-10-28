// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "app_types.h"

// Chiffrement des secrets saisis dans la page (mots de passe Wi-Fi, code de secours) pour qu'ils ne circulent pas
// en clair sur le réseau local: l'interface est en HTTP. Ne protège que d'une écoute passive: quelqu'un qui
// s'interpose (MITM) peut remplacer la clé publique, ou la page elle-même.
//
// La boîte a une paire de clés X25519 (libsodium crypto_box), tirée au premier démarrage et enregistrée en NVS
// (effacée par la remise à zéro). /api/status donne la clé publique et un défi aléatoire. La page chiffre les
// secrets avec une paire de clés éphémère (tweetnacl, nacl.box: X25519 + XSalsa20-Poly1305) et les envoie dans
// "sealed": {"epk", "nonce", "box"} (base64). Le message chiffré est un objet JSON qui contient le défi en cours
// et les champs secrets; le défi change après chaque déchiffrement réussi, ce qui empêche de rejouer une requête
// enregistrée (déverrouillage notamment).
namespace secret_channel {

// Prépare libsodium et la paire de clés; en tire une si la configuration n'en a pas. Renvoie true si la
// configuration a changé (à enregistrer).
bool begin(DeviceConfig& config);

// Clé publique et défi en cours, en base64 (renvoyés dans /api/status).
String publicKeyBase64();
String challengeBase64();

// Si `doc` contient "sealed", le déchiffre, vérifie le défi et recopie les champs secrets dans `doc` (une
// requête en clair reste acceptée: scripts, ancienne page). Renvoie nullptr si tout va bien, sinon
// "sealed_invalid" (400) ou "stale_challenge" (409: la page relit le statut et renvoie).
const char* unseal(JsonDocument& doc);

}  // namespace secret_channel
