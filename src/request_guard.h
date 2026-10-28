// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include <vector>

// Protection des routes /api contre les autres sites ouverts dans un navigateur du réseau local (sans
// dépendance matérielle: testée sur ordinateur).
//  - CSRF: une page malveillante peut envoyer un POST "simple" (text/plain, formulaire) vers la boîte sans
//    autorisation préalable du navigateur. On refuse toute modification venant d'une autre origine (en-têtes
//    Origin et Sec-Fetch-Site), et on exige un corps application/json, que le navigateur ne laisse partir
//    vers une autre origine qu'après une demande d'autorisation (à laquelle la boîte ne répond pas).
//  - Rebinding DNS: un site peut faire pointer son propre nom vers l'IP de la boîte pour lire ses réponses.
//    On n'accepte que les requêtes adressées à la boîte elle-même (en-tête Host: son nom .local ou son IP).
// Les clients hors navigateur (curl, scripts) n'envoient ni Origin ni Sec-Fetch-Site: ils restent acceptés.
namespace request_guard {

struct Request {
  String host;          // en-tête Host, éventuellement avec ":port"
  String origin;        // en-tête Origin, vide si absent
  String secFetchSite;  // en-tête Sec-Fetch-Site, vide si absent
  String contentType;   // en-tête Content-Type, vide si absent
  bool mutating = false;  // route qui modifie quelque chose (POST)
  bool hasBody = false;   // corps non vide à interpréter en JSON
};

// Renvoie nullptr si la requête est acceptée, sinon le code d'erreur à renvoyer:
// "forbidden_host" (403), "forbidden_origin" (403) ou "json_required" (415).
// `allowedHosts`: noms et adresses de la boîte, sans port (ex. "totobox.local", "10.0.0.236", "192.168.4.1").
const char* check(const Request& request, const std::vector<String>& allowedHosts);

// "Totobox.local:80" -> "totobox.local"; "http://10.0.0.5" -> "10.0.0.5".
String normalizeHost(const String& value);

}  // namespace request_guard
