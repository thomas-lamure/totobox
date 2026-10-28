// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

// Validation des réglages réseau saisis dans l'interface web (sans dépendance matérielle: testée sur
// ordinateur, pio test -e native).
namespace network_names {

constexpr size_t kMaxHostnameLength = 32;
constexpr size_t kMaxSsidLength = 32;
constexpr size_t kMinWpaPasswordLength = 8;
constexpr size_t kMaxWpaPasswordLength = 63;

// Nom saisi ramené à la forme attendue: espaces retirés, minuscules.
String normalizeHostname(const String& raw);
// Nom réseau (mDNS http://<nom>.local et DHCP): lettres a-z, chiffres et tirets, 1 à 32 caractères,
// sans tiret au début ni à la fin.
bool isValidHostname(const String& name);
// SSID: 1 à 32 octets.
bool isValidSsid(const String& ssid);
// Mot de passe Wi-Fi maison: vide (réseau ouvert) ou WPA2 (8 à 63 caractères).
bool isValidWifiPassword(const String& password);
// Mot de passe du point d'accès de la boîte: WPA2 obligatoire (8 à 63 caractères).
bool isValidApPassword(const String& password);

// Nom par défaut du point d'accès: préfixe + 2 derniers octets de l'adresse MAC ("TotoBox-1A2B"), propre à
// chaque boîte.
String defaultApSsid(const uint8_t mac[6]);
// Mot de passe aléatoire facile à recopier sur un téléphone: "xxxx-xxxx-xxxx", minuscules et chiffres sans
// caractères ambigus (0/o, 1/l/i), ~60 bits d'entropie.
String generateApPassword();

}  // namespace network_names
