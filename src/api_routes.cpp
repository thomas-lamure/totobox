// SPDX-License-Identifier: GPL-3.0-or-later
#include "api_routes.h"

namespace api_routes {

namespace {

// Méthode, chemin, refusée si verrouillée, refusée pendant une lecture ou un téléchargement.
constexpr Route kRoutes[] = {
    {Method::Get, "/api/status", false, false},
    {Method::Post, "/api/volume", false, false},
    {Method::Post, "/api/unlock", false, false},
    {Method::Post, "/api/lock", false, false},
    {Method::Get, "/api/config", true, false},
    {Method::Post, "/api/config", true, false},
    {Method::Post, "/api/bindings", true, false},
    {Method::Post, "/api/lock/setup", true, false},
    {Method::Post, "/api/lock/pin", true, false},
    {Method::Post, "/api/lock/cards", true, false},
    {Method::Get, "/api/library", true, true},
    {Method::Post, "/api/upload", true, true},
    {Method::Get, "/api/download", true, true},
    {Method::Post, "/api/folder", true, true},
    {Method::Post, "/api/rename", true, true},
    {Method::Post, "/api/move", true, true},
    {Method::Post, "/api/delete", true, true},
    {Method::Get, "/api/trash", true, true},
    {Method::Post, "/api/restore", true, true},
    {Method::Post, "/api/trash/purge", true, true},
    {Method::Get, "/api/cards/backup", true, true},
    {Method::Post, "/api/cards/backup", true, true},
    {Method::Post, "/api/cards/restore", true, true},
    {Method::Post, "/api/setup/complete", true, false},
    {Method::Post, "/api/factory-reset", true, true},
    {Method::Post, "/api/update", true, true},
    // Seulement pour une carte illisible, donc sans histoire en cours.
    {Method::Post, "/api/sd/format", true, false},
};

}  // namespace

const Route* all(size_t& count) {
  count = sizeof(kRoutes) / sizeof(kRoutes[0]);
  return kRoutes;
}

const Route* find(Method method, const String& path) {
  for (const Route& route : kRoutes) {
    if (route.method == method && path == route.path) {
      return &route;
    }
  }
  return nullptr;
}

const char* refusal(const Route& route, bool locked, bool playing, bool downloading) {
  if (route.refusedWhenLocked && locked) {
    return "locked";
  }
  if (route.refusedWhileBusy && playing) {
    return "busy_playing";
  }
  if (route.refusedWhileBusy && downloading) {
    return "busy_downloading";
  }
  return nullptr;
}

}  // namespace api_routes
