// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <FS.h>

#include "card_backup.h"

// Lecture et écriture du fichier de sauvegarde sur la carte SD (séparé de card_backup, testé sur ordinateur).
namespace card_backup {

// Écrit d'abord un fichier temporaire: une écriture interrompue ne détruit pas la sauvegarde précédente.
bool save(fs::FS& fs, const std::vector<CardBinding>& bindings, const String& savedAt);
// false si absente ou illisible.
bool load(fs::FS& fs, Backup& out);

}  // namespace card_backup
