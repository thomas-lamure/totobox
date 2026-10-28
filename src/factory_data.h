// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

// Données d'usine écrites une fois par boîte sur la ligne de production (scripts/provision.py), dans la
// partition "mfg" (partitions.csv). La boîte ne l'écrit jamais: un effacement de la configuration (NVS)
// ramène aux valeurs imprimées sur l'étiquette.
//
// Format (petit-boutiste): "TBXF", version (1 octet), 3 octets réservés, longueur du JSON (4 octets),
// CRC32 du JSON (4 octets, IEEE comme zlib.crc32), puis le JSON, par exemple {"apPassword":"k7mp-3xqa-9fzt"}.
// Le reste de la partition est à 0xFF.
namespace factory_data {

constexpr size_t kHeaderSize = 16;
constexpr uint8_t kVersion = 1;

struct FactoryData {
  String apPassword;
};

uint32_t crc32(const uint8_t* data, size_t length);
// Renvoie false si la zone est vierge, abîmée ou d'un format inconnu (`out` n'est alors pas modifié).
bool parse(const uint8_t* data, size_t size, FactoryData& out);

}  // namespace factory_data
