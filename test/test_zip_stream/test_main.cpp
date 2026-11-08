// SPDX-License-Identifier: GPL-3.0-or-later
// Tests de l'archive ZIP envoyée au fil de l'eau (téléchargement d'un dossier).

#include <unity.h>

#include <string>
#include <vector>

#include "zip_stream.h"

using namespace zip_stream;

namespace {

uint32_t read32(const std::vector<uint8_t>& bytes, size_t at) {
  return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
}

uint16_t read16(const std::vector<uint8_t>& bytes, size_t at) {
  return bytes[at] | (bytes[at + 1] << 8);
}

void append(std::vector<uint8_t>& out, const std::vector<uint8_t>& part) {
  out.insert(out.end(), part.begin(), part.end());
}

// Archive complète, assemblée dans l'ordre où la boîte l'envoie.
std::vector<uint8_t> build(std::vector<Entry>& entries, const std::vector<std::string>& contents) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < entries.size(); ++i) {
    entries[i].offset = out.size();
    append(out, localHeader(entries[i]));
    const auto* data = reinterpret_cast<const uint8_t*>(contents[i].data());
    out.insert(out.end(), data, data + contents[i].size());
    entries[i].crc = crc32Update(0, data, contents[i].size());
    append(out, dataDescriptor(entries[i]));
  }
  const uint32_t centralOffset = out.size();
  for (const Entry& entry : entries) {
    append(out, centralHeader(entry));
  }
  append(out, endOfCentralDirectory(entries.size(), out.size() - centralOffset, centralOffset));
  return out;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_crc32_matches_the_reference() {
  const std::string check = "123456789";
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32Update(0, reinterpret_cast<const uint8_t*>(check.data()), check.size()));
  // Par morceaux: même résultat.
  uint32_t crc = crc32Update(0, reinterpret_cast<const uint8_t*>(check.data()), 4);
  crc = crc32Update(crc, reinterpret_cast<const uint8_t*>(check.data()) + 4, 5);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc);
  TEST_ASSERT_EQUAL_HEX32(0, crc32Update(0, nullptr, 0));
}

void test_archive_size_is_known_before_sending() {
  std::vector<Entry> entries(2);
  entries[0].name = "music/a.mp3";
  entries[0].size = 5;
  entries[1].name = "music/comptines/b.mp3";
  entries[1].size = 3;
  const std::vector<uint8_t> archive = build(entries, {"hello", "abc"});
  TEST_ASSERT_EQUAL(archive.size(), archiveSize(entries));

  // En-tête local du 2e fichier: signature, drapeaux (descripteur + UTF-8), stocké, tailles, nom.
  const size_t second = entries[1].offset;
  TEST_ASSERT_EQUAL_HEX32(0x04034b50, read32(archive, second));
  TEST_ASSERT_EQUAL_HEX16(0x0808, read16(archive, second + 6));
  TEST_ASSERT_EQUAL(0, read16(archive, second + 8));
  TEST_ASSERT_EQUAL(3, read32(archive, second + 18));
  TEST_ASSERT_EQUAL(21, read16(archive, second + 26));
  // Fin d'archive: 2 fichiers, répertoire central juste avant.
  const size_t end = archive.size() - 22;
  TEST_ASSERT_EQUAL_HEX32(0x06054b50, read32(archive, end));
  TEST_ASSERT_EQUAL(2, read16(archive, end + 10));
  const uint32_t centralOffset = read32(archive, end + 16);
  TEST_ASSERT_EQUAL_HEX32(0x02014b50, read32(archive, centralOffset));
  TEST_ASSERT_EQUAL(entries[0].crc, read32(archive, centralOffset + 16));
  TEST_ASSERT_EQUAL(0, read32(archive, centralOffset + 42));  // position du 1er en-tête local
}

void test_archive_limits_without_zip64() {
  std::vector<Entry> entries(1);
  TEST_ASSERT_EQUAL(0, archiveSize({}));
  entries[0].name = "";
  TEST_ASSERT_EQUAL(0, archiveSize(entries));
  entries[0].name = "a.mp3";
  entries[0].size = 0xFFFFFFF0u;  // fichier seul sous 4 Go, archive au-delà
  TEST_ASSERT_EQUAL(0, archiveSize(entries));
  entries[0].size = 1000;
  TEST_ASSERT_EQUAL(30 + 16 + 46 + 10 + 1000 + 22, archiveSize(entries));
}

void test_dos_date_and_time() {
  uint16_t date = 0;
  uint16_t time = 0;
  dosDateTime(2026, 11, 8, 20, 30, 59, date, time);
  TEST_ASSERT_EQUAL_HEX16(((2026 - 1980) << 9) | (11 << 5) | 8, date);
  TEST_ASSERT_EQUAL_HEX16((20 << 11) | (30 << 5) | 29, time);
  // Horloge jamais réglée (1970): 1er janvier 1980.
  dosDateTime(1970, 1, 1, 0, 0, 0, date, time);
  TEST_ASSERT_EQUAL_HEX16(0x21, date);
  TEST_ASSERT_EQUAL(0, time);
}

void test_names_keep_the_folder_tree() {
  TEST_ASSERT_EQUAL_STRING("music/comptines/a.mp3", entryName("/content/music", "/content/music/comptines/a.mp3").c_str());
  TEST_ASSERT_EQUAL_STRING("totobox/stories/b.mp3", entryName("/content", "/content/stories/b.mp3").c_str());
  TEST_ASSERT_EQUAL_STRING("comptines.zip", archiveName("/content/music/comptines").c_str());
  TEST_ASSERT_EQUAL_STRING("totobox.zip", archiveName("/content").c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_matches_the_reference);
  RUN_TEST(test_archive_size_is_known_before_sending);
  RUN_TEST(test_archive_limits_without_zip64);
  RUN_TEST(test_dos_date_and_time);
  RUN_TEST(test_names_keep_the_folder_tree);
  return UNITY_END();
}
