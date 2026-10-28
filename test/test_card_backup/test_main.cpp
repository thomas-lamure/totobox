// SPDX-License-Identifier: GPL-3.0-or-later
// Tests du format de sauvegarde des cartes sur la carte SD.

#include <unity.h>

#include "card_backup.h"

namespace {

std::vector<CardBinding> sampleBindings() {
  std::vector<CardBinding> bindings(2);
  bindings[0].uid = "CACB3C7F";
  bindings[0].label = "Popi";
  bindings[0].mediaPath = "/content/Popi";
  bindings[0].resumeTrackIndex = 2;
  bindings[0].resumeMs = 76000;
  bindings[0].resumeByte = 1234567;
  bindings[1].uid = "04AAAA0001";
  bindings[1].label = "Chanson";
  bindings[1].mediaPath = "/content/chanson.mp3";
  return bindings;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_round_trip_keeps_bindings_and_bookmarks() {
  const String payload = card_backup::serialize(sampleBindings(), "2026-10-23T10:00:00.000Z");
  card_backup::Backup backup;
  TEST_ASSERT_TRUE(card_backup::parse(payload, backup));
  TEST_ASSERT_EQUAL_STRING("2026-10-23T10:00:00.000Z", backup.savedAt.c_str());
  TEST_ASSERT_EQUAL_UINT32(2, backup.bindings.size());
  TEST_ASSERT_EQUAL_STRING("Popi", backup.bindings[0].label.c_str());
  TEST_ASSERT_EQUAL_STRING("/content/Popi", backup.bindings[0].mediaPath.c_str());
  TEST_ASSERT_EQUAL_UINT16(2, backup.bindings[0].resumeTrackIndex);
  TEST_ASSERT_EQUAL_UINT32(76000, backup.bindings[0].resumeMs);
  TEST_ASSERT_EQUAL_UINT32(1234567, backup.bindings[0].resumeByte);
  TEST_ASSERT_EQUAL_STRING("04AAAA0001", backup.bindings[1].uid.c_str());
}

void test_foreign_or_broken_files_are_rejected() {
  card_backup::Backup backup;
  TEST_ASSERT_FALSE(card_backup::parse("pas du json", backup));
  TEST_ASSERT_FALSE(card_backup::parse(R"({"format":"autre","version":1,"bindings":[]})", backup));
  TEST_ASSERT_FALSE(card_backup::parse(R"({"format":"totobox-cards","version":2,"bindings":[]})", backup));
  TEST_ASSERT_FALSE(card_backup::parse(R"({"format":"totobox-cards","version":1})", backup));
  // Liste vide mais bien formée: lisible.
  TEST_ASSERT_TRUE(card_backup::parse(R"({"format":"totobox-cards","version":1,"bindings":[]})", backup));
  TEST_ASSERT_EQUAL_UINT32(0, backup.bindings.size());
}

void test_entries_without_uid_are_skipped() {
  card_backup::Backup backup;
  TEST_ASSERT_TRUE(card_backup::parse(
      R"({"format":"totobox-cards","version":1,"bindings":[{"uid":"","mediaPath":"/content/a"},{"uid":"AB12","mediaPath":"/content/b"}]})",
      backup));
  TEST_ASSERT_EQUAL_UINT32(1, backup.bindings.size());
  TEST_ASSERT_EQUAL_STRING("AB12", backup.bindings[0].uid.c_str());
}

void test_master_cards_are_dropped_on_restore() {
  std::vector<CardBinding> bindings = sampleBindings();
  DeviceConfig config;
  config.lockEnabled = true;
  config.lockCards.push_back({"04AAAA0001", "Maman"});
  TEST_ASSERT_EQUAL_UINT32(1, card_backup::dropLockCards(bindings, config));
  TEST_ASSERT_EQUAL_UINT32(1, bindings.size());
  TEST_ASSERT_EQUAL_STRING("CACB3C7F", bindings[0].uid.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip_keeps_bindings_and_bookmarks);
  RUN_TEST(test_foreign_or_broken_files_are_rejected);
  RUN_TEST(test_entries_without_uid_are_skipped);
  RUN_TEST(test_master_cards_are_dropped_on_restore);
  return UNITY_END();
}
