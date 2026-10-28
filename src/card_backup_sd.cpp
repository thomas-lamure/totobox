// SPDX-License-Identifier: GPL-3.0-or-later
#include "card_backup_sd.h"

#include "logger.h"

namespace card_backup {

namespace {

constexpr char kTempPath[] = "/totobox-cartes.json.tmp";

}  // namespace

bool save(fs::FS& fs, const std::vector<CardBinding>& bindings, const String& savedAt) {
  const String payload = serialize(bindings, savedAt);
  File file = fs.open(kTempPath, FILE_WRITE);
  if (!file) {
    logLine("backup", "Impossible de créer %s", kTempPath);
    return false;
  }
  const size_t written = file.print(payload);
  file.close();
  if (written != payload.length()) {
    logLine("backup", "Écriture incomplète de la sauvegarde (%u/%u octets)", static_cast<unsigned>(written),
            static_cast<unsigned>(payload.length()));
    fs.remove(kTempPath);
    return false;
  }
  // FAT ne renomme pas par-dessus un fichier existant.
  if (fs.exists(kPath) && !fs.remove(kPath)) {
    logLine("backup", "Impossible de remplacer %s", kPath);
    return false;
  }
  if (!fs.rename(kTempPath, kPath)) {
    logLine("backup", "Impossible de renommer la sauvegarde");
    return false;
  }
  logLine("backup", "%u cartes sauvegardées dans %s", static_cast<unsigned>(bindings.size()), kPath);
  return true;
}

bool load(fs::FS& fs, Backup& out) {
  if (!fs.exists(kPath)) {
    return false;
  }
  File file = fs.open(kPath, FILE_READ);
  if (!file) {
    return false;
  }
  const String payload = file.readString();
  file.close();
  if (!parse(payload, out)) {
    logLine("backup", "Sauvegarde %s illisible", kPath);
    return false;
  }
  return true;
}

}  // namespace card_backup
