// SPDX-License-Identifier: GPL-3.0-or-later
#include "zip_stream.h"

namespace zip_stream {

namespace {

// Bit 3: CRC et tailles répétés dans le descripteur; bit 11: noms en UTF-8.
constexpr uint16_t kFlags = 0x0008 | 0x0800;
constexpr uint16_t kVersionNeeded = 20;
constexpr uint32_t kLocalHeaderBytes = 30;
constexpr uint32_t kDescriptorBytes = 16;
constexpr uint32_t kCentralHeaderBytes = 46;
constexpr uint32_t kEndBytes = 22;
constexpr char kRootArchiveName[] = "totobox";

void put16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(value & 0xFF);
  out.push_back(value >> 8);
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back((value >> shift) & 0xFF);
  }
}

void putName(std::vector<uint8_t>& out, const String& name) {
  for (size_t index = 0; index < name.length(); ++index) {
    out.push_back(static_cast<uint8_t>(name[index]));
  }
}

String lastSegment(const String& path) {
  const int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

String folderLabel(const String& folderPath) {
  return folderPath == "/content" ? String(kRootArchiveName) : lastSegment(folderPath);
}

}  // namespace

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  crc = ~crc;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

std::vector<uint8_t> localHeader(const Entry& entry) {
  std::vector<uint8_t> out;
  out.reserve(kLocalHeaderBytes + entry.name.length());
  put32(out, 0x04034b50);
  put16(out, kVersionNeeded);
  put16(out, kFlags);
  put16(out, 0);  // stocké
  put16(out, entry.dosTime);
  put16(out, entry.dosDate);
  put32(out, 0);  // CRC: dans le descripteur
  put32(out, entry.size);
  put32(out, entry.size);
  put16(out, static_cast<uint16_t>(entry.name.length()));
  put16(out, 0);
  putName(out, entry.name);
  return out;
}

std::vector<uint8_t> dataDescriptor(const Entry& entry) {
  std::vector<uint8_t> out;
  out.reserve(kDescriptorBytes);
  put32(out, 0x08074b50);
  put32(out, entry.crc);
  put32(out, entry.size);
  put32(out, entry.size);
  return out;
}

std::vector<uint8_t> centralHeader(const Entry& entry) {
  std::vector<uint8_t> out;
  out.reserve(kCentralHeaderBytes + entry.name.length());
  put32(out, 0x02014b50);
  put16(out, 0x0300 | kVersionNeeded);  // créé sous Unix: droits lus dans les attributs externes
  put16(out, kVersionNeeded);
  put16(out, kFlags);
  put16(out, 0);
  put16(out, entry.dosTime);
  put16(out, entry.dosDate);
  put32(out, entry.crc);
  put32(out, entry.size);
  put32(out, entry.size);
  put16(out, static_cast<uint16_t>(entry.name.length()));
  put16(out, 0);  // extra
  put16(out, 0);  // commentaire
  put16(out, 0);  // disque
  put16(out, 0);  // attributs internes
  put32(out, 0100644u << 16);  // fichier ordinaire, rw-r--r--
  put32(out, entry.offset);
  putName(out, entry.name);
  return out;
}

std::vector<uint8_t> endOfCentralDirectory(uint16_t count, uint32_t centralSize, uint32_t centralOffset) {
  std::vector<uint8_t> out;
  out.reserve(kEndBytes);
  put32(out, 0x06054b50);
  put16(out, 0);
  put16(out, 0);
  put16(out, count);
  put16(out, count);
  put32(out, centralSize);
  put32(out, centralOffset);
  put16(out, 0);
  return out;
}

uint32_t archiveSize(const std::vector<Entry>& entries) {
  if (entries.empty() || entries.size() > 0xFFFF) {
    return 0;
  }
  uint64_t total = kEndBytes;
  for (const Entry& entry : entries) {
    if (entry.name.isEmpty() || entry.name.length() > 0xFFFF) {
      return 0;
    }
    total += kLocalHeaderBytes + kDescriptorBytes + kCentralHeaderBytes + 2ull * entry.name.length() + entry.size;
  }
  return total < 0xFFFFFFFFull ? static_cast<uint32_t>(total) : 0;
}

void dosDateTime(int year, int month, int day, int hour, int minute, int second, uint16_t& date, uint16_t& time) {
  if (year < 1980 || year > 2107 || month < 1 || month > 12 || day < 1 || day > 31) {
    date = 0x21;
    time = 0;
    return;
  }
  date = static_cast<uint16_t>(((year - 1980) << 9) | (month << 5) | day);
  time = static_cast<uint16_t>((hour << 11) | (minute << 5) | (second / 2));
}

String entryName(const String& folderPath, const String& filePath) {
  return folderLabel(folderPath) + filePath.substring(folderPath.length());
}

String archiveName(const String& folderPath) {
  return folderLabel(folderPath) + ".zip";
}

}  // namespace zip_stream
