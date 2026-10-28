// SPDX-License-Identifier: GPL-3.0-or-later
// Tests des règles de chemins de la bibliothèque: rien ne sort de /content, noms nettoyés, doublons, corbeille.

#include <unity.h>

#include <set>
#include <string>

#include "library_paths.h"

using namespace library_paths;

namespace {

// Carte SD simulée: ensemble de chemins existants.
std::set<std::string> existing;

bool exists(const String& path) {
  return existing.count(path.c_str()) > 0;
}

void assertPath(const char* expected, const String& actual) {
  TEST_ASSERT_EQUAL_STRING(expected, actual.c_str());
}

}  // namespace

void setUp() {
  existing.clear();
}
void tearDown() {}

void test_paths_inside_the_library_are_normalized() {
  assertPath("/content", normalizeLibraryPath(""));
  assertPath("/content", normalizeLibraryPath("  /content/  "));
  assertPath("/content/music/a.mp3", normalizeLibraryPath("content/music/a.mp3"));
  assertPath("/content/music", normalizeLibraryPath("\\content\\music\\"));
  assertPath("/content/.trash/x", normalizeLibraryPath("/content/.trash/x"));
}

void test_paths_outside_the_library_are_refused() {
  assertPath("", normalizeLibraryPath("/"));
  assertPath("", normalizeLibraryPath("/totobox-cartes.json"));
  assertPath("", normalizeLibraryPath("/contents/x.mp3"));  // préfixe sans séparateur
  assertPath("", normalizeLibraryPath("/content/../totobox-cartes.json"));
  assertPath("", normalizeLibraryPath("/content/music/..\\..\\x"));
  assertPath("", normalizeLibraryPath("/content/a..b.mp3"));  // ".." refusé partout, même dans un nom
}

void test_file_names_are_sanitized() {
  assertPath("01-intro.mp3", sanitizeFileName("01-intro.mp3"));
  assertPath("chanson.mp3", sanitizeFileName("C:\\Musique\\chanson.mp3"));  // dossier de l'ordinateur ignoré
  assertPath("chanson.mp3", sanitizeFileName("../../chanson.mp3"));
  assertPath("a_b_c.mp3", sanitizeFileName("a:b*c.mp3"));
  assertPath("__t__.mp3", sanitizeFileName("Été.mp3"));  // 2 octets UTF-8 par accent (la page les retire avant)
  assertPath("nom", sanitizeFileName("  nom...  "));
  assertPath("", sanitizeFileName("   "));
  assertPath("", sanitizeFileName(".."));
}

void test_folder_names_have_no_dot() {
  assertPath("Petit Prince", sanitizeFolderName(" Petit Prince "));
  assertPath("v1_2", sanitizeFolderName("v1.2"));
  assertPath("a_b", sanitizeFolderName("a/b"));
  assertPath("_trash", sanitizeFolderName(".trash"));
}

void test_reserved_names_are_refused() {
  TEST_ASSERT_TRUE(isReservedName(""));
  TEST_ASSERT_TRUE(isReservedName("."));
  TEST_ASSERT_TRUE(isReservedName(".."));
  TEST_ASSERT_TRUE(isReservedName("Content"));
  TEST_ASSERT_TRUE(isReservedName("TRASH"));
  TEST_ASSERT_TRUE(isReservedName(".Trash"));
  TEST_ASSERT_FALSE(isReservedName("contenu"));
  assertPath("", sanitizeFolderName("trash"));
  assertPath("", sanitizeFileName("content"));
}

void test_audio_files_are_recognized_whatever_the_case() {
  TEST_ASSERT_TRUE(isAudioFile("/content/a.MP3"));
  TEST_ASSERT_TRUE(isAudioFile("b.m4a"));
  TEST_ASSERT_TRUE(isAudioFile("c.opus"));
  TEST_ASSERT_FALSE(isAudioFile("pochette.jpg"));
  TEST_ASSERT_FALSE(isAudioFile("mp3"));
  TEST_ASSERT_FALSE(isAudioFile("/content/dossier.mp3/"));
}

void test_duration_is_estimated_at_128_kbits() {
  TEST_ASSERT_EQUAL_UINT32(0, estimateDurationSec(0));
  TEST_ASSERT_EQUAL_UINT32(60, estimateDurationSec(960000));
  TEST_ASSERT_EQUAL_UINT32(268435, estimateDurationSec(4294967295u));  // sans débordement
}

void test_conflict_mode_defaults_to_rename() {
  assertPath("replace", normalizeConflictMode("REPLACE"));
  assertPath("reject", normalizeConflictMode("reject"));
  assertPath("rename", normalizeConflictMode(""));
  assertPath("rename", normalizeConflictMode("overwrite"));
}

void test_free_name_is_kept() {
  assertPath("/content/music/a.mp3", resolveConflict("/content/music", "a.mp3", "rename", exists));
  assertPath("/content/music/a.mp3", resolveConflict("/content/music/", "a.mp3", "reject", exists));
}

void test_taken_name_gets_a_suffix() {
  existing = {"/content/music/a.mp3", "/content/music/a-1.mp3"};
  assertPath("/content/music/a-2.mp3", resolveConflict("/content/music", "a.mp3", "rename", exists));
  existing = {"/content/stories"};
  assertPath("/content/stories-1", resolveConflict("/content", "stories", "rename", exists));
  existing = {"/content/.cache"};  // point en tête: pas une extension
  assertPath("/content/.cache-1", resolveConflict("/content", ".cache", "rename", exists));
}

void test_taken_name_in_replace_and_reject_modes() {
  existing = {"/content/a.mp3"};
  assertPath("/content/a.mp3", resolveConflict("/content", "a.mp3", "replace", exists));
  assertPath("", resolveConflict("/content", "a.mp3", "reject", exists));
}

void test_no_free_name_after_999_suffixes() {
  existing = {"/content/a.mp3"};
  for (int i = 1; i < 1000; ++i) {
    existing.insert("/content/a-" + std::to_string(i) + ".mp3");
  }
  assertPath("", resolveConflict("/content", "a.mp3", "rename", exists));
}

void test_trash_keeps_the_library_tree() {
  TrashTarget target;
  TEST_ASSERT_TRUE(trashTargetFor("/content/music/comptines/a.mp3", target));
  assertPath("/content/.trash/music/comptines", target.dir);
  assertPath("a.mp3", target.name);
  TEST_ASSERT_TRUE(trashTargetFor("/content/berceuse.mp3", target));
  assertPath("/content/.trash", target.dir);
  assertPath("berceuse.mp3", target.name);
}

void test_root_and_trash_cannot_be_trashed() {
  TrashTarget target;
  TEST_ASSERT_FALSE(trashTargetFor("/content", target));
  TEST_ASSERT_FALSE(trashTargetFor("", target));
  TEST_ASSERT_FALSE(trashTargetFor("/content/.trash", target));
  TEST_ASSERT_FALSE(trashTargetFor("/content/.trash/music/a.mp3", target));
  TEST_ASSERT_FALSE(trashTargetFor("/autre/a.mp3", target));
}

void test_trash_items_go_back_where_they_were() {
  assertPath("/content/music/comptines", originalPathFromTrash("/content/.trash/music/comptines"));
  assertPath("", originalPathFromTrash("/content/.trash"));
  assertPath("", originalPathFromTrash("/content/music/a.mp3"));
  assertPath("", originalPathFromTrash("/content/.trashed/a.mp3"));
  TEST_ASSERT_TRUE(isInTrash("/content/.trash"));
  TEST_ASSERT_FALSE(isInTrash("/content/.trashed"));
}

void test_moves_stay_in_the_library() {
  TEST_ASSERT_NULL(moveError("/content/music/a.mp3", "/content/stories"));
  TEST_ASSERT_NULL(moveError("/content/music", "/content/musique"));  // nom voisin, pas un sous-dossier
  TEST_ASSERT_EQUAL_STRING("invalid_path", moveError("", "/content"));
  TEST_ASSERT_EQUAL_STRING("invalid_path", moveError("/content", "/content/music"));
  TEST_ASSERT_EQUAL_STRING("invalid_path", moveError("/content/.trash/a.mp3", "/content"));
  TEST_ASSERT_EQUAL_STRING("invalid_path", moveError("/content/a.mp3", "/content/.trash"));
  TEST_ASSERT_EQUAL_STRING("invalid_path", moveError("/content/a.mp3", ""));
  TEST_ASSERT_EQUAL_STRING("invalid_move_target", moveError("/content/music", "/content/music"));
  TEST_ASSERT_EQUAL_STRING("invalid_move_target", moveError("/content/music", "/content/music/comptines"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_paths_inside_the_library_are_normalized);
  RUN_TEST(test_paths_outside_the_library_are_refused);
  RUN_TEST(test_file_names_are_sanitized);
  RUN_TEST(test_folder_names_have_no_dot);
  RUN_TEST(test_reserved_names_are_refused);
  RUN_TEST(test_audio_files_are_recognized_whatever_the_case);
  RUN_TEST(test_duration_is_estimated_at_128_kbits);
  RUN_TEST(test_conflict_mode_defaults_to_rename);
  RUN_TEST(test_free_name_is_kept);
  RUN_TEST(test_taken_name_gets_a_suffix);
  RUN_TEST(test_taken_name_in_replace_and_reject_modes);
  RUN_TEST(test_no_free_name_after_999_suffixes);
  RUN_TEST(test_trash_keeps_the_library_tree);
  RUN_TEST(test_root_and_trash_cannot_be_trashed);
  RUN_TEST(test_trash_items_go_back_where_they_were);
  RUN_TEST(test_moves_stay_in_the_library);
  return UNITY_END();
}
