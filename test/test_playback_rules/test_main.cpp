// SPDX-License-Identifier: GPL-3.0-or-later
// Tests des décisions de lecture: reprise au signet selon le format, position enregistrée, ordre des pistes,
// niveau de volume de la lib.

#include <unity.h>

#include "playback_rules.h"

using namespace playback_rules;

void setUp() {}
void tearDown() {}

void test_byte_resume_formats() {
  TEST_ASSERT_TRUE(supportsByteResume("/content/a.MP3"));
  TEST_ASSERT_TRUE(supportsByteResume("a.m4a"));
  TEST_ASSERT_TRUE(supportsByteResume("a.flac"));
  TEST_ASSERT_FALSE(supportsByteResume("a.ogg"));
  TEST_ASSERT_FALSE(supportsByteResume("a.opus"));
  TEST_ASSERT_TRUE(supportsSecondResume("a.wav"));
  TEST_ASSERT_FALSE(supportsSecondResume("a.m4a"));  // setAudioPlayPosition refusé en M4A
}

void test_bookmark_with_byte_resumes_at_that_byte() {
  const ResumePlan plan = planResume("/content/a.mp3", 83000, 1327000, 5000000);
  TEST_ASSERT_TRUE(plan.kind == ResumeKind::ByByte);
  TEST_ASSERT_EQUAL_UINT32(1327000, plan.startByte);
  TEST_ASSERT_EQUAL_UINT32(83000, plan.baseMs);
  TEST_ASSERT_FALSE(plan.beyondEnd);
}

void test_m4a_resumes_by_byte_only() {
  TEST_ASSERT_TRUE(planResume("a.m4a", 83000, 1327000, 5000000).kind == ResumeKind::ByByte);
  const ResumePlan old = planResume("a.m4a", 83000, 0, 5000000);  // ancien signet sans octet
  TEST_ASSERT_TRUE(old.kind == ResumeKind::FromStart);
  TEST_ASSERT_TRUE(old.unsupported);
}

void test_old_bookmark_without_byte_resumes_in_seconds() {
  const ResumePlan plan = planResume("a.mp3", 83900, 0, 0);
  TEST_ASSERT_TRUE(plan.kind == ResumeKind::BySeconds);
  TEST_ASSERT_EQUAL_UINT16(83, plan.seconds);
  TEST_ASSERT_EQUAL_UINT32(0, plan.startByte);
  // Plafonné à ce que la lib accepte.
  TEST_ASSERT_EQUAL_UINT16(65535, planResume("a.mp3", 4000000000u, 0, 0).seconds);
}

void test_bookmark_beyond_the_file_starts_over() {
  // Fichier remplacé par un plus court, ou signet faussé (ex.: -1 relevé après un retrait de carte SD).
  ResumePlan plan = planResume("a.mp3", 83000, 5000000, 5000000);
  TEST_ASSERT_TRUE(plan.beyondEnd);
  TEST_ASSERT_TRUE(plan.kind == ResumeKind::FromStart);
  TEST_ASSERT_EQUAL_UINT32(0, plan.startByte);
  TEST_ASSERT_EQUAL_UINT16(0, plan.seconds);
  plan = planResume("a.mp3", 83000, 4294961109u, 5000000);
  TEST_ASSERT_TRUE(plan.beyondEnd);
  // Fichier impossible à ouvrir (taille 0).
  TEST_ASSERT_TRUE(planResume("a.mp3", 83000, 1000, 0).beyondEnd);
}

void test_no_bookmark_starts_at_the_beginning() {
  const ResumePlan plan = planResume("a.mp3", 0, 0, 0);
  TEST_ASSERT_TRUE(plan.kind == ResumeKind::FromStart);
  TEST_ASSERT_FALSE(plan.unsupported);
  TEST_ASSERT_FALSE(plan.beyondEnd);
  // Moins d'une seconde: pas de saut.
  TEST_ASSERT_TRUE(planResume("a.mp3", 900, 0, 0).kind == ResumeKind::FromStart);
}

void test_ogg_bookmark_is_unsupported() {
  const ResumePlan plan = planResume("a.ogg", 83000, 1327000, 5000000);
  TEST_ASSERT_TRUE(plan.kind == ResumeKind::FromStart);
  TEST_ASSERT_TRUE(plan.unsupported);
}

void test_position_without_byte_resume_follows_the_library_clock() {
  PlaybackClock clock;
  TEST_ASSERT_EQUAL_UINT32(42000, positionMs(clock, 42));
}

void test_position_after_byte_resume_counts_from_the_bookmark() {
  PlaybackClock clock;
  clock.byteResumed = true;
  clock.anchorPending = true;
  clock.baseMs = 83000;
  // Avant le démarrage de la piste: le signet.
  TEST_ASSERT_EQUAL_UINT32(83000, positionMs(clock, 0));
  // Horloge de la lib relevée à 1 s au démarrage: 10 s plus tard, signet + 10 s.
  clock.anchorPending = false;
  clock.anchorSec = 1;
  TEST_ASSERT_EQUAL_UINT32(93000, positionMs(clock, 11));
  // Horloge de la lib en arrière de l'ancre: jamais avant le signet.
  TEST_ASSERT_EQUAL_UINT32(83000, positionMs(clock, 0));
}

void test_byte_position_subtracts_the_buffer() {
  TEST_ASSERT_EQUAL_UINT32(1300000, positionByte(1327000, 27000, 5000000));
  TEST_ASSERT_EQUAL_UINT32(0, positionByte(1000, 4000, 5000000));
}

void test_byte_position_after_sd_removal_is_ignored() {
  // getFilePos() renvoie -1 (0xFFFFFFFF) quand la carte ne répond plus.
  TEST_ASSERT_EQUAL_UINT32(0, positionByte(0xFFFFFFFFu, 4096, 5000000));
}

void test_playlist_is_sorted_without_case() {
  std::vector<String> playlist = {"/d/b.mp3", "/d/02-x.mp3", "/d/A.mp3", "/d/01-y.mp3"};
  sortPlaylist(playlist);
  TEST_ASSERT_EQUAL_STRING("/d/01-y.mp3", playlist[0].c_str());
  TEST_ASSERT_EQUAL_STRING("/d/02-x.mp3", playlist[1].c_str());
  TEST_ASSERT_EQUAL_STRING("/d/A.mp3", playlist[2].c_str());
  TEST_ASSERT_EQUAL_STRING("/d/b.mp3", playlist[3].c_str());
}

void test_volume_maps_to_22_library_levels() {
  TEST_ASSERT_EQUAL_UINT8(0, libVolume(0));
  TEST_ASSERT_EQUAL_UINT8(1, libVolume(5));  // un cran de molette
  TEST_ASSERT_EQUAL_UINT8(10, libVolume(50));
  TEST_ASSERT_EQUAL_UINT8(21, libVolume(100));
  TEST_ASSERT_EQUAL_UINT8(21, libVolume(255));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_byte_resume_formats);
  RUN_TEST(test_bookmark_with_byte_resumes_at_that_byte);
  RUN_TEST(test_m4a_resumes_by_byte_only);
  RUN_TEST(test_old_bookmark_without_byte_resumes_in_seconds);
  RUN_TEST(test_bookmark_beyond_the_file_starts_over);
  RUN_TEST(test_no_bookmark_starts_at_the_beginning);
  RUN_TEST(test_ogg_bookmark_is_unsupported);
  RUN_TEST(test_position_without_byte_resume_follows_the_library_clock);
  RUN_TEST(test_position_after_byte_resume_counts_from_the_bookmark);
  RUN_TEST(test_byte_position_subtracts_the_buffer);
  RUN_TEST(test_byte_position_after_sd_removal_is_ignored);
  RUN_TEST(test_playlist_is_sorted_without_case);
  RUN_TEST(test_volume_maps_to_22_library_levels);
  return UNITY_END();
}
