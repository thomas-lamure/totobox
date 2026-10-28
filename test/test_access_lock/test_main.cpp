// SPDX-License-Identifier: GPL-3.0-or-later
// Tests du verrouillage des réglages: code de secours, cartes maîtresses, limitation des essais.

#include <unity.h>

#include <cstring>

#include "access_lock.h"

namespace {

MasterCard card(const char* uid, const char* label = "") {
  MasterCard result;
  result.uid = uid;
  result.label = label;
  return result;
}

DeviceConfig lockedConfig() {
  DeviceConfig config;
  access_lock::enable(config, card("04AAAA0001", "Maman"), "123456");
  return config;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_pin_must_be_six_digits() {
  TEST_ASSERT_TRUE(access_lock::isValidPin("123456"));
  TEST_ASSERT_TRUE(access_lock::isValidPin("000000"));
  TEST_ASSERT_FALSE(access_lock::isValidPin(""));
  TEST_ASSERT_FALSE(access_lock::isValidPin("12345"));
  TEST_ASSERT_FALSE(access_lock::isValidPin("1234567"));
  TEST_ASSERT_FALSE(access_lock::isValidPin("12a456"));
  TEST_ASSERT_FALSE(access_lock::isValidPin("12 456"));
}

void test_enable_stores_hash_not_pin() {
  const DeviceConfig config = lockedConfig();
  TEST_ASSERT_TRUE(config.lockEnabled);
  TEST_ASSERT_EQUAL_UINT32(1, config.lockCards.size());
  TEST_ASSERT_EQUAL_STRING("04AAAA0001", config.lockCards[0].uid.c_str());
  TEST_ASSERT_EQUAL_UINT32(32, config.lockPinSalt.length());  // 16 octets en hexadécimal
  TEST_ASSERT_EQUAL_UINT32(64, config.lockPinHash.length());  // SHA-256 en hexadécimal
  TEST_ASSERT_NULL(std::strstr(config.lockPinHash.c_str(), "123456"));
}

void test_check_pin() {
  const DeviceConfig config = lockedConfig();
  TEST_ASSERT_TRUE(access_lock::checkPin(config, "123456"));
  TEST_ASSERT_FALSE(access_lock::checkPin(config, "654321"));
  TEST_ASSERT_FALSE(access_lock::checkPin(config, "12345"));
  TEST_ASSERT_FALSE(access_lock::checkPin(config, ""));
}

void test_same_pin_gets_different_salt_and_hash() {
  const DeviceConfig first = lockedConfig();
  const DeviceConfig second = lockedConfig();
  TEST_ASSERT_FALSE(first.lockPinSalt == second.lockPinSalt);
  TEST_ASSERT_FALSE(first.lockPinHash == second.lockPinHash);
}

void test_set_pin_replaces_old_pin() {
  DeviceConfig config = lockedConfig();
  access_lock::setPin(config, "999999");
  TEST_ASSERT_TRUE(access_lock::checkPin(config, "999999"));
  TEST_ASSERT_FALSE(access_lock::checkPin(config, "123456"));
}

void test_lock_state_follows_runtime_unlock() {
  const DeviceConfig config = lockedConfig();
  RuntimeState runtime;
  TEST_ASSERT_TRUE(access_lock::isLocked(config, runtime));
  runtime.unlocked = true;
  TEST_ASSERT_FALSE(access_lock::isLocked(config, runtime));

  DeviceConfig open;
  runtime.unlocked = false;
  TEST_ASSERT_FALSE(access_lock::isLocked(open, runtime));
}

void test_master_cards_only_count_when_enabled() {
  DeviceConfig config = lockedConfig();
  TEST_ASSERT_TRUE(access_lock::isLockCard(config, "04AAAA0001"));
  TEST_ASSERT_FALSE(access_lock::isLockCard(config, "04BBBB0002"));
  TEST_ASSERT_FALSE(access_lock::isLockCard(config, ""));
  config.lockEnabled = false;
  TEST_ASSERT_FALSE(access_lock::isLockCard(config, "04AAAA0001"));
}

void test_add_card_rejects_duplicates_empty_and_overflow() {
  DeviceConfig config = lockedConfig();
  TEST_ASSERT_TRUE(access_lock::addCard(config, card("04BBBB0002", "Papa")));
  TEST_ASSERT_FALSE(access_lock::addCard(config, card("04BBBB0002")));
  TEST_ASSERT_FALSE(access_lock::addCard(config, card("")));
  for (int index = 3; index <= static_cast<int>(access_lock::kMaxCards); ++index) {
    String uid = "04CC";
    uid += static_cast<char>('0' + index);
    TEST_ASSERT_TRUE(access_lock::addCard(config, card(uid.c_str())));
  }
  TEST_ASSERT_EQUAL_UINT32(access_lock::kMaxCards, config.lockCards.size());
  TEST_ASSERT_FALSE(access_lock::addCard(config, card("04DDDD0009")));
  const MasterCard* papa = access_lock::findCard(config, "04BBBB0002");
  TEST_ASSERT_NOT_NULL(papa);
  TEST_ASSERT_EQUAL_STRING("Papa", papa->label.c_str());
}

void test_last_card_cannot_be_removed() {
  DeviceConfig config = lockedConfig();
  TEST_ASSERT_FALSE(access_lock::removeCard(config, "04AAAA0001"));
  TEST_ASSERT_TRUE(access_lock::addCard(config, card("04BBBB0002")));
  TEST_ASSERT_FALSE(access_lock::removeCard(config, "04FFFF0000"));
  TEST_ASSERT_TRUE(access_lock::removeCard(config, "04AAAA0001"));
  TEST_ASSERT_EQUAL_UINT32(1, config.lockCards.size());
  TEST_ASSERT_EQUAL_STRING("04BBBB0002", config.lockCards[0].uid.c_str());
}

void test_disable_clears_everything() {
  DeviceConfig config = lockedConfig();
  access_lock::disable(config);
  TEST_ASSERT_FALSE(config.lockEnabled);
  TEST_ASSERT_EQUAL_UINT32(0, config.lockCards.size());
  TEST_ASSERT_TRUE(config.lockPinHash.isEmpty());
  TEST_ASSERT_FALSE(access_lock::checkPin(config, "123456"));
}

void test_attempt_limiter_blocks_after_five_failures() {
  access_lock::PinAttemptLimiter limiter;
  const uint32_t start = 1000;
  TEST_ASSERT_EQUAL_UINT32(0, limiter.retryInSec(start));
  TEST_ASSERT_EQUAL_UINT8(4, limiter.recordFailure(start));
  TEST_ASSERT_EQUAL_UINT8(3, limiter.recordFailure(start));
  TEST_ASSERT_EQUAL_UINT8(2, limiter.recordFailure(start));
  TEST_ASSERT_EQUAL_UINT8(1, limiter.recordFailure(start));
  TEST_ASSERT_EQUAL_UINT32(0, limiter.retryInSec(start));
  TEST_ASSERT_EQUAL_UINT8(0, limiter.recordFailure(start));

  TEST_ASSERT_EQUAL_UINT32(60, limiter.retryInSec(start));
  TEST_ASSERT_EQUAL_UINT32(1, limiter.retryInSec(start + 59500));
  TEST_ASSERT_EQUAL_UINT32(0, limiter.retryInSec(start + 60000));

  // Après le blocage, nouvelle série de 5 essais.
  TEST_ASSERT_EQUAL_UINT8(4, limiter.recordFailure(start + 60000));
}

void test_attempt_limiter_reset() {
  access_lock::PinAttemptLimiter limiter;
  for (int attempt = 0; attempt < 5; ++attempt) {
    limiter.recordFailure(0);
  }
  TEST_ASSERT_TRUE(limiter.retryInSec(0) > 0);
  limiter.reset();
  TEST_ASSERT_EQUAL_UINT32(0, limiter.retryInSec(0));
  TEST_ASSERT_EQUAL_UINT8(4, limiter.recordFailure(0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pin_must_be_six_digits);
  RUN_TEST(test_enable_stores_hash_not_pin);
  RUN_TEST(test_check_pin);
  RUN_TEST(test_same_pin_gets_different_salt_and_hash);
  RUN_TEST(test_set_pin_replaces_old_pin);
  RUN_TEST(test_lock_state_follows_runtime_unlock);
  RUN_TEST(test_master_cards_only_count_when_enabled);
  RUN_TEST(test_add_card_rejects_duplicates_empty_and_overflow);
  RUN_TEST(test_last_card_cannot_be_removed);
  RUN_TEST(test_disable_clears_everything);
  RUN_TEST(test_attempt_limiter_blocks_after_five_failures);
  RUN_TEST(test_attempt_limiter_reset);
  return UNITY_END();
}
