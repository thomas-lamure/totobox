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

// Le lecteur rend l'une puis l'autre de deux cartes posées ensemble, comme pollNfc() avec main.cpp.
void startPlaying(const char* uid, uint32_t now) {
  seen(uid, now);
  onCardPresented(runtime, config, bindings, uid);
  runtime.state = DeviceState::Playing;
}

// Ce que pollNfc() fait d'une étape: retrait (signet de `stoppedAt`) puis carte posée.
void apply(const NfcStep& step, const char* uid) {
  if (step.stopCurrent) {
    onCardRemoved(runtime, findBinding(bindings, runtime.activeCardUid), nullptr);
  }
  if (step.present) {
    if (onCardPresented(runtime, config, bindings, uid) == Presented::Play) {
      runtime.state = DeviceState::Playing;
    }
  }
}

void test_second_card_stops_playback_and_shows_two_cards() {
  startPlaying("04A1B2C3", 1000);
  // Autre carte alors que la première répond encore.
  NfcStep step = seen("04D4E5F6", 1100);
  TEST_ASSERT_TRUE(step.stopCurrent);
  TEST_ASSERT_FALSE(step.present);
  TEST_ASSERT_TRUE(runtime.twoCards);
  const Bookmark stoppedAt = bookmark(1, 83000, 1327000);
  TEST_ASSERT_TRUE(onCardRemoved(runtime, findBinding(bindings, "04A1B2C3"), &stoppedAt));
  TEST_ASSERT_EQUAL_UINT32(1327000, bindings[0].resumeByte);
  TEST_ASSERT_TRUE(runtime.state == DeviceState::TwoCards);
}

void test_cards_alternating_with_empty_searches_never_play() {
  // Comme sur la boîte: une carte, puis ~250 ms sans carte (retrait confirmé), puis l'autre, toutes les ~280 ms.
  startPlaying("04A1B2C3", 1000);
  uint32_t plays = 0;
  uint32_t stops = 0;
  const char* uids[] = {"04A1B2C3", "04D4E5F6"};
  for (uint32_t cycle = 0; cycle < 20; ++cycle) {
    const uint32_t base = 1000 + cycle * 280;
    for (uint32_t now = base + 100; now < base + 280; now += 100) {
      const NfcStep empty = noCard(now);
      stops += empty.stopCurrent ? 1 : 0;
      apply(empty, "");
    }
    const char* uid = uids[(cycle + 1) % 2];
    const NfcStep step = seen(uid, base + 280);
    plays += step.present ? 1 : 0;
    stops += step.stopCurrent ? 1 : 0;
    apply(step, uid);
  }
  TEST_ASSERT_EQUAL_UINT32(0, plays);  // aucune histoire relancée
  TEST_ASSERT_TRUE(stops <= 1);        // la première arrêtée une seule fois
  TEST_ASSERT_TRUE(runtime.twoCards);
  TEST_ASSERT_TRUE(runtime.state == DeviceState::TwoCards);
}

void test_remaining_card_plays_once_alone_long_enough() {
  startPlaying("04A1B2C3", 1000);
  apply(seen("04D4E5F6", 1100), "04D4E5F6");
  seen("04A1B2C3", 1400);
  // La deuxième carte est retirée: seule la première répond.
  uint32_t now = 1500;
  for (; now < 1400 + kTwoCardsClearMs; now += 100) {
    TEST_ASSERT_FALSE(seen("04A1B2C3", now).present);
  }
  const NfcStep step = seen("04A1B2C3", now);
  TEST_ASSERT_TRUE(step.present);
  TEST_ASSERT_FALSE(step.stopCurrent);
  TEST_ASSERT_FALSE(runtime.twoCards);
  TEST_ASSERT_TRUE(onCardPresented(runtime, config, bindings, "04A1B2C3") == Presented::Play);
}

void test_both_cards_removed_ends_the_two_cards_state() {
  startPlaying("04A1B2C3", 1000);
  apply(seen("04D4E5F6", 1100), "04D4E5F6");
  TEST_ASSERT_FALSE(noCard(1100 + kTwoCardsWindowMs - 1).present);
  TEST_ASSERT_TRUE(runtime.twoCards);
  noCard(1100 + kTwoCardsWindowMs);
  TEST_ASSERT_FALSE(runtime.twoCards);
  TEST_ASSERT_TRUE(runtime.state == DeviceState::Ready);
  // Une carte posée ensuite se lit normalement.
  TEST_ASSERT_TRUE(seen("04D4E5F6", 5000).present);
}

void test_card_swapped_later_is_not_two_cards() {
  startPlaying("04A1B2C3", 1000);
  TEST_ASSERT_TRUE(noCard(1000 + kGraceMs).stopCurrent);
  onCardRemoved(runtime, findBinding(bindings, "04A1B2C3"), nullptr);
  // Autre carte posée plus d'une seconde après: lue tout de suite.
  const NfcStep step = seen("04D4E5F6", 1000 + kTwoCardsWindowMs);
  TEST_ASSERT_TRUE(step.present);
  TEST_ASSERT_FALSE(runtime.twoCards);
}

void test_quick_swap_waits_before_playing_the_new_card() {
  startPlaying("04A1B2C3", 1000);
  apply(noCard(1000 + kGraceMs), "");
  // Autre carte moins d'une seconde après: traitée comme deux cartes tant qu'elle n'est pas seule assez longtemps.
  NfcStep step = seen("04D4E5F6", 1600);
  TEST_ASSERT_FALSE(step.present);
  TEST_ASSERT_FALSE(step.stopCurrent);  // plus rien ne jouait
  TEST_ASSERT_TRUE(runtime.state == DeviceState::TwoCards);
  uint32_t now = 1700;
  for (; now < 1600 + kTwoCardsClearMs; now += 100) {
    TEST_ASSERT_FALSE(seen("04D4E5F6", now).present);
  }
  TEST_ASSERT_TRUE(seen("04D4E5F6", now).present);
}

void test_same_card_put_back_resumes_at_once() {
  startPlaying("04A1B2C3", 1000);
  apply(noCard(1000 + kGraceMs), "");
  TEST_ASSERT_TRUE(seen("04A1B2C3", 1500).present);
  TEST_ASSERT_FALSE(runtime.twoCards);
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

void test_remote_stop_keeps_the_bookmark_and_does_not_restart() {
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  runtime.state = DeviceState::Playing;
  const Bookmark stoppedAt = bookmark(1, 83000, 1327000);
  TEST_ASSERT_TRUE(onRemoteStop(runtime, findBinding(bindings, "04A1B2C3"), &stoppedAt));
  TEST_ASSERT_EQUAL_UINT32(1327000, bindings[0].resumeByte);
  TEST_ASSERT_TRUE(runtime.state == DeviceState::Paused);
  TEST_ASSERT_TRUE(runtime.stoppedRemotely);
  TEST_ASSERT_EQUAL_STRING("04A1B2C3", runtime.activeCardUid.c_str());

  // La carte reste posée: le lecteur la revoit sans rien relancer.
  const NfcStep step = seen("04A1B2C3", 1000);
  TEST_ASSERT_FALSE(step.present);
  TEST_ASSERT_FALSE(step.stopCurrent);

  // Retirée (rien ne joue: signet inchangé), puis reposée: la lecture reprend au signet.
  TEST_ASSERT_TRUE(noCard(1000 + kGraceMs).stopCurrent);
  TEST_ASSERT_FALSE(onCardRemoved(runtime, findBinding(bindings, "04A1B2C3"), nullptr));
  TEST_ASSERT_FALSE(runtime.stoppedRemotely);
  TEST_ASSERT_TRUE(runtime.state == DeviceState::Ready);
  TEST_ASSERT_EQUAL_UINT32(1327000, bindings[0].resumeByte);
  TEST_ASSERT_TRUE(seen("04A1B2C3", 2000).present);
  TEST_ASSERT_TRUE(onCardPresented(runtime, config, bindings, "04A1B2C3") == Presented::Play);
  TEST_ASSERT_FALSE(runtime.stoppedRemotely);
}

void test_remote_stop_cancels_the_resume_after_the_sd_card() {
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  onSdLost(runtime, findBinding(bindings, "04A1B2C3"), bookmark(1, 90000, 1400000));
  runtime.sdReady = false;
  runtime.state = DeviceState::Error;
  TEST_ASSERT_TRUE(onRemoteStop(runtime, findBinding(bindings, "04A1B2C3"), nullptr));
  TEST_ASSERT_EQUAL_UINT32(1400000, bindings[0].resumeByte);  // signet de la carte SD gardé
  TEST_ASSERT_TRUE(runtime.state == DeviceState::Error);      // la carte SD manque toujours
  runtime.sdReady = true;
  TEST_ASSERT_FALSE(shouldResumeAfterSd(runtime, false));
}

void test_remote_stop_without_playback_does_nothing() {
  // Aucune carte.
  TEST_ASSERT_FALSE(onRemoteStop(runtime, nullptr, nullptr));
  // Carte associée posée, mais rien ne joue (lecture finie).
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  runtime.state = DeviceState::Ready;
  TEST_ASSERT_FALSE(onRemoteStop(runtime, findBinding(bindings, "04A1B2C3"), nullptr));
  TEST_ASSERT_FALSE(runtime.stoppedRemotely);
  TEST_ASSERT_TRUE(runtime.state == DeviceState::Ready);
  // Carte inconnue posée.
  onCardPresented(runtime, config, bindings, "0A0B0C0D");
  const Bookmark stoppedAt = bookmark(0, 1000, 2000);
  TEST_ASSERT_FALSE(onRemoteStop(runtime, nullptr, &stoppedAt));
  // Signet d'une autre carte que l'active: refusé.
  TEST_ASSERT_FALSE(onRemoteStop(runtime, findBinding(bindings, "04D4E5F6"), &stoppedAt));
  TEST_ASSERT_EQUAL_UINT32(0, bindings[1].resumeByte);
}

void test_search_pauses_while_the_knob_turns_without_card() {
  constexpr uint32_t kPause = 400;
  // Molette jamais tournée: recherche normale.
  TEST_ASSERT_TRUE(shouldSearch(runtime, 1000, 0, kPause));
  // Sans carte: pas de recherche pendant qu'on tourne, reprise après la pause.
  TEST_ASSERT_FALSE(shouldSearch(runtime, 5000, 4900, kPause));
  TEST_ASSERT_TRUE(shouldSearch(runtime, 5300, 4900, kPause));
  // Carte posée (ou deux): toujours suivie, son retrait doit arrêter l'histoire.
  onCardPresented(runtime, config, bindings, "04A1B2C3");
  TEST_ASSERT_TRUE(shouldSearch(runtime, 5000, 4900, kPause));
  onCardRemoved(runtime, nullptr, nullptr);
  runtime.twoCards = true;
  TEST_ASSERT_TRUE(shouldSearch(runtime, 5000, 4900, kPause));
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
  RUN_TEST(test_second_card_stops_playback_and_shows_two_cards);
  RUN_TEST(test_cards_alternating_with_empty_searches_never_play);
  RUN_TEST(test_remaining_card_plays_once_alone_long_enough);
  RUN_TEST(test_both_cards_removed_ends_the_two_cards_state);
  RUN_TEST(test_card_swapped_later_is_not_two_cards);
  RUN_TEST(test_quick_swap_waits_before_playing_the_new_card);
  RUN_TEST(test_same_card_put_back_resumes_at_once);
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
  RUN_TEST(test_remote_stop_keeps_the_bookmark_and_does_not_restart);
  RUN_TEST(test_remote_stop_cancels_the_resume_after_the_sd_card);
  RUN_TEST(test_remote_stop_without_playback_does_nothing);
  RUN_TEST(test_search_pauses_while_the_knob_turns_without_card);
  RUN_TEST(test_uid_is_formatted_in_uppercase_hex);
  return UNITY_END();
}
