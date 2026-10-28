// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Version du firmware: renvoyée par /api/status, affichée dans les réglages et au démarrage dans la console.
// kFirmwareVersion suit CHANGELOG.md et n'est modifié que par make release (scripts/release.py);
// TOTOBOX_BUILD est le commit compilé, écrit par scripts/build_version.py.
#include "build_info.h"

namespace totobox {

constexpr const char* kFirmwareVersion = "1.2.1";
constexpr const char* kFirmwareBuild = TOTOBOX_BUILD;

}  // namespace totobox
