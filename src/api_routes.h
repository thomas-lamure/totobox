// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include <stddef.h>

// Table des routes /api et de ce qu'elles refusent avant même d'être traitées (sans dépendance matérielle:
// testée sur ordinateur). WebAdmin applique ces refus à toutes les routes au même endroit (onApi()), au lieu de
// les répéter en tête de chaque gestionnaire. scripts/test_api_routes.py vérifie que docs/api.md et le faux
// serveur (scripts/mock-server.mjs) disent la même chose.
namespace api_routes {

enum class Method : uint8_t { Get, Post };

struct Route {
  Method method;
  const char* path;
  // Réglages verrouillés: 403 locked (gestion de la boîte). Restent servies la lecture en cours, le volume et
  // le déverrouillage.
  bool refusedWhenLocked;
  // Accès à la carte SD: 503 busy_playing pendant une histoire (la carte est à la tâche audio) et
  // 503 busy_downloading pendant un téléchargement (pas de renommage d'un fichier en cours d'envoi).
  bool refusedWhileBusy;
};

// Toutes les routes, dans l'ordre de la documentation.
const Route* all(size_t& count);

// Route enregistrée pour cette méthode et ce chemin exact, ou nullptr.
const Route* find(Method method, const String& path);

// Refus à renvoyer avant de traiter la route, ou nullptr: "locked" (403), "busy_playing" ou "busy_downloading"
// (503). Le verrouillage passe en premier: une boîte verrouillée ne dit rien de ce qu'elle fait.
const char* refusal(const Route& route, bool locked, bool playing, bool downloading);

}  // namespace api_routes
