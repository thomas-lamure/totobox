// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

#include <vector>

#include "app_types.h"

// Sauvegarde des associations de cartes (avec leurs marque-pages) dans un fichier JSON à la racine de la carte SD,
// hors de la bibliothèque /content. Une seule sauvegarde: la nouvelle remplace l'ancienne. Faite aussi
// automatiquement avant une remise à zéro. Les cartes maîtresses n'y figurent pas (verrouillage non sauvegardé).
namespace card_backup {

constexpr char kPath[] = "/totobox-cartes.json";
constexpr char kFormat[] = "totobox-cards";
constexpr int kVersion = 1;

struct Backup {
  std::vector<CardBinding> bindings;
  // Date fournie par la page au moment de la sauvegarde (ISO 8601); vide si inconnue (remise à zéro d'atelier).
  String savedAt;
};

String serialize(const std::vector<CardBinding>& bindings, const String& savedAt);
// Renvoie false si le contenu n'est pas une sauvegarde TotoBox lisible.
bool parse(const String& payload, Backup& out);
// Retire les cartes devenues cartes maîtresses (une carte ne peut pas être les deux); renvoie leur nombre.
size_t dropLockCards(std::vector<CardBinding>& bindings, const DeviceConfig& config);

}  // namespace card_backup
