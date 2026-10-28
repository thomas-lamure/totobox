// SPDX-License-Identifier: GPL-3.0-or-later
// Tests du suivi des cartes: carte maîtresse, inconnue ou associée, retrait (y compris échange rapide), signets,
// fin d'histoire, retrait et retour de la carte SD.

#include <unity.h>

#include "access_lock.h"
#include "card_flow.h"

using namespace card_flow;

namespace {

constexpr uint32_t kGraceMs = 250;

RuntimeState runtime;
DeviceConfig config;
std::vector<CardBinding> bindings;

CardBinding binding(const char* uid, const char* path) {
  CardBinding item;
  item.uid = uid;
  item.mediaPath = path;
  return item;
}

Bookmark bookmark(uint16_t track, uint32_t ms, uint32_t byte) {
  Bookmark value;
  value.track = track;
  value.ms = ms;
  value.byte = byte;
  return value;
}

// Carte posée par le lecteur, comme pollNfc(): renvoie ce que main.cpp doit faire.
NfcStep seen(const char* uid, uint32_t now) {
  return onNfcEvent(runtime, true, uid, now, kGraceMs);
}

NfcStep noCard(uint32_t now) {
  return onNfcEvent(runtime, false, "", now, kGraceMs);
}

}  // namespace

void setUp() {
  runtime = RuntimeState();
  runtime.sdReady = true;
  runtime.nfcReady = true;
  runtime.state = DeviceState::Ready;
  config = DeviceConfig();
  bindings = {binding("04A1B2C3", "/content/stories/petit-prince"), binding("04D4E5F6", "/content/music")};
}
void tearDown() {}

void test_new_card_is_presented_once() {
  NfcStep step = seen("04A1B2C3", 1000);
  TEST_ASSERT_TRUE(step.present);
  TEST_ASSERT_FALSE(step.stopCurrent);
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  // Toujours la même carte aux recherches suivantes: rien à faire.
  step = seen("04A1B2C3", 1100);
  TEST_ASSERT_FALSE(step.present);
  TEST_ASSERT_FALSE(step.stopCurrent);
}

void test_removal_is_confirmed_after_the_grace_delay() {
  seen("04A1B2C3", 1000);
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  TEST_ASSERT_FALSE(noCard(1100).stopCurrent);  // une recherche manquée ne coupe pas l'histoire
  TEST_ASSERT_FALSE(noCard(1249).stopCurrent);
  TEST_ASSERT_TRUE(noCard(1250).stopCurrent);
}

void test_no_card_without_active_card_does_nothing() {
  TEST_ASSERT_FALSE(noCard(10000).stopCurrent);
}

void test_quick_swap_stops_the_previous_card_first() {
  seen("04A1B2C3", 1000);
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  // Autre carte posée avant la confirmation du retrait de la première.
  const NfcStep step = seen("04D4E5F6", 1150);
  TEST_ASSERT_TRUE(step.stopCurrent);
  TEST_ASSERT_TRUE(step.present);
}

void test_known_card_asks_for_playback() {
  TEST_ASSERT_TRUE(onCardPresented(runtime, config, bindings, "04A1B2C3") == Presented::Play);
  TEST_ASSERT_EQUAL_STRING("04A1B2C3", runtime.activeCardUid.c_str());
}

void test_unknown_card_is_remembered_for_the_page() {
  TEST_ASSERT_TRUE(onCardPresented(runtime, config, bindings, "0A0B0C0D") == Presented::Unknown);
  TEST_ASSERT_TRUE(runtime.state == DeviceState::UnknownCard);
  TEST_ASSERT_EQUAL_STRING("0A0B0C0D", runtime.lastUnknownCardUid.c_str());
  // Retirée: elle reste proposée à l'association.
  onCardRemoved(runtime, nullptr, nullptr);
  TEST_ASSERT_TRUE(runtime.state == DeviceState::Ready);
  TEST_ASSERT_EQUAL_STRING("0A0B0C0D", runtime.lastUnknownCardUid.c_str());
}

void test_master_card_unlocks_without_playing() {
  MasterCard master;
  master.uid = "AABBCCDD";
  access_lock::enable(config, master, "123456");
  TEST_ASSERT_TRUE(onCardPresented(runtime, config, bindings, "AABBCCDD") == Presented::Unlock);
  TEST_ASSERT_TRUE(runtime.unlocked);
  TEST_ASSERT_TRUE(runtime.state == DeviceState::Ready);
  TEST_ASSERT_TRUE(runtime.lastUnknownCardUid.isEmpty());
}

void test_removal_while_playing_keeps_the_bookmark() {
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  runtime.state = DeviceState::Playing;
  const Bookmark stoppedAt = bookmark(1, 83000, 1327000);
  TEST_ASSERT_TRUE(onCardRemoved(runtime, findBinding(bindings, "04A1B2C3"), &stoppedAt));
  const CardBinding* story = findBinding(bindings, "04A1B2C3");
  TEST_ASSERT_EQUAL_UINT16(1, story->resumeTrackIndex);
  TEST_ASSERT_EQUAL_UINT32(83000, story->resumeMs);
  TEST_ASSERT_EQUAL_UINT32(1327000, story->resumeByte);
  TEST_ASSERT_TRUE(runtime.activeCardUid.isEmpty());
  TEST_ASSERT_TRUE(runtime.state == DeviceState::Ready);
}

void test_removal_without_playback_keeps_the_old_bookmark() {
  bindings[0].resumeMs = 42000;
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  TEST_ASSERT_FALSE(onCardRemoved(runtime, findBinding(bindings, "04A1B2C3"), nullptr));
  TEST_ASSERT_EQUAL_UINT32(42000, bindings[0].resumeMs);
}

void test_finished_story_starts_over_next_time() {
  bindings[0].resumeTrackIndex = 2;
  bindings[0].resumeMs = 600000;
  bindings[0].resumeByte = 9000000;
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  runtime.state = DeviceState::Playing;
  TEST_ASSERT_TRUE(onStoryFinished(runtime, findBinding(bindings, "04A1B2C3")));
  TEST_ASSERT_EQUAL_UINT16(0, bindings[0].resumeTrackIndex);
  TEST_ASSERT_EQUAL_UINT32(0, bindings[0].resumeMs);
  TEST_ASSERT_EQUAL_UINT32(0, bindings[0].resumeByte);
  TEST_ASSERT_TRUE(runtime.state == DeviceState::Ready);
}

void test_hardware_error_survives_card_events() {
  runtime.sdReady = false;
  runtime.state = DeviceState::Error;
  onCardPresented(runtime, config, bindings, "0A0B0C0D");
  onCardRemoved(runtime, nullptr, nullptr);
  TEST_ASSERT_TRUE(runtime.state == DeviceState::Error);
  runtime.sdReady = true;
  runtime.nfcReady = false;
  TEST_ASSERT_TRUE(restingState(runtime) == DeviceState::Error);
}

void test_story_resumes_when_the_sd_card_comes_back() {
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  TEST_ASSERT_TRUE(onSdLost(runtime, findBinding(bindings, "04A1B2C3"), bookmark(1, 90000, 1400000)));
  TEST_ASSERT_EQUAL_UINT32(1400000, bindings[0].resumeByte);
  runtime.sdReady = false;
  TEST_ASSERT_FALSE(shouldResumeAfterSd(runtime, false));  // toujours absente
  runtime.sdReady = true;
  TEST_ASSERT_TRUE(shouldResumeAfterSd(runtime, false));
  TEST_ASSERT_FALSE(shouldResumeAfterSd(runtime, false));  // une seule fois
}

void test_no_resume_if_the_card_was_removed_meanwhile() {
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  onSdLost(runtime, findBinding(bindings, "04A1B2C3"), bookmark(1, 90000, 1400000));
  runtime.sdReady = false;
  onCardRemoved(runtime, findBinding(bindings, "04A1B2C3"), nullptr);
  runtime.sdReady = true;
  TEST_ASSERT_FALSE(shouldResumeAfterSd(runtime, false));
  TEST_ASSERT_EQUAL_UINT32(1400000, bindings[0].resumeByte);  // signet gardé pour la prochaine fois
}

void test_sd_lost_without_card_keeps_nothing() {
  TEST_ASSERT_FALSE(onSdLost(runtime, nullptr, bookmark(1, 90000, 1400000)));
  TEST_ASSERT_FALSE(runtime.resumeAfterSd);
}

void test_unknown_card_bound_from_the_page_starts_playing() {
  onCardPresented(runtime, config, bindings, "0A0B0C0D");
  TEST_ASSERT_FALSE(shouldPlayNewlyBound(runtime, bindings));
  bindings.push_back(binding("0A0B0C0D", "/content/music/berceuse.mp3"));
  TEST_ASSERT_TRUE(shouldPlayNewlyBound(runtime, bindings));
  // Retirée avant l'association: rien à lire.
  onCardRemoved(runtime, nullptr, nullptr);
  TEST_ASSERT_FALSE(shouldPlayNewlyBound(runtime, bindings));
}

void test_uid_is_formatted_in_uppercase_hex() {
  const uint8_t uid[] = {0x04, 0xA1, 0x0B, 0xFF};
  TEST_ASSERT_EQUAL_STRING("04A10BFF", formatUid(uid, sizeof(uid)).c_str());
  TEST_ASSERT_EQUAL_STRING("", formatUid(uid, 0).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_new_card_is_presented_once);
  RUN_TEST(test_removal_is_confirmed_after_the_grace_delay);
  RUN_TEST(test_no_card_without_active_card_does_nothing);
  RUN_TEST(test_quick_swap_stops_the_previous_card_first);
  RUN_TEST(test_known_card_asks_for_playback);
  RUN_TEST(test_unknown_card_is_remembered_for_the_page);
  RUN_TEST(test_master_card_unlocks_without_playing);
  RUN_TEST(test_removal_while_playing_keeps_the_bookmark);
  RUN_TEST(test_removal_without_playback_keeps_the_old_bookmark);
  RUN_TEST(test_finished_story_starts_over_next_time);
  RUN_TEST(test_hardware_error_survives_card_events);
  RUN_TEST(test_story_resumes_when_the_sd_card_comes_back);
  RUN_TEST(test_no_resume_if_the_card_was_removed_meanwhile);
  RUN_TEST(test_sd_lost_without_card_keeps_nothing);
  RUN_TEST(test_unknown_card_bound_from_the_page_starts_playing);
  RUN_TEST(test_uid_is_formatted_in_uppercase_hex);
  return UNITY_END();
}
