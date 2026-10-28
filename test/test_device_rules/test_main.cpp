// SPDX-License-Identifier: GPL-3.0-or-later
// Tests des petites règles de la boîte: volume à la molette, jauge et chenillard des LEDs, reconnexion Wi-Fi.

#include <unity.h>

#include "led_layout.h"
#include "retry_delay.h"
#include "volume_rules.h"

void setUp() {}
void tearDown() {}

void test_detents_move_the_volume_by_steps() {
  TEST_ASSERT_EQUAL_UINT8(55, volume_rules::afterDetents(50, 1, 20, 85, 5));
  TEST_ASSERT_EQUAL_UINT8(35, volume_rules::afterDetents(50, -3, 20, 85, 5));
  TEST_ASSERT_EQUAL_UINT8(50, volume_rules::afterDetents(50, 0, 20, 85, 5));
}

void test_volume_stays_within_the_parents_limits() {
  TEST_ASSERT_EQUAL_UINT8(85, volume_rules::afterDetents(80, 3, 20, 85, 5));
  TEST_ASSERT_EQUAL_UINT8(20, volume_rules::afterDetents(22, -1, 20, 85, 5));
  TEST_ASSERT_EQUAL_UINT8(100, volume_rules::afterDetents(100, 127, 0, 100, 5));  // sans débordement
  TEST_ASSERT_EQUAL_UINT8(0, volume_rules::afterDetents(0, -128, 0, 100, 5));
  // Volume enregistré hors des limites (limites resserrées depuis): ramené dedans au premier cran.
  TEST_ASSERT_EQUAL_UINT8(85, volume_rules::afterDetents(95, 1, 20, 85, 5));
}

void test_gauge_shows_the_volume_between_the_limits() {
  TEST_ASSERT_EQUAL_UINT8(0, volume_rules::gaugePercent(20, 20, 80));
  TEST_ASSERT_EQUAL_UINT8(50, volume_rules::gaugePercent(50, 20, 80));
  TEST_ASSERT_EQUAL_UINT8(100, volume_rules::gaugePercent(80, 20, 80));
  TEST_ASSERT_EQUAL_UINT8(0, volume_rules::gaugePercent(10, 20, 80));
  TEST_ASSERT_EQUAL_UINT8(100, volume_rules::gaugePercent(90, 20, 80));
  // Limites égales: le volume ne peut pas bouger, jauge pleine.
  TEST_ASSERT_EQUAL_UINT8(100, volume_rules::gaugePercent(50, 50, 50));
}

void test_reversed_strip_starts_at_the_last_led() {
  TEST_ASSERT_EQUAL_UINT8(7, led_layout::physicalIndex(0, 8, true));
  TEST_ASSERT_EQUAL_UINT8(0, led_layout::physicalIndex(7, 8, true));
  TEST_ASSERT_EQUAL_UINT8(3, led_layout::physicalIndex(3, 8, false));
}

void test_gauge_lights_the_nearest_number_of_leds() {
  TEST_ASSERT_EQUAL_UINT8(0, led_layout::litCount(0, 8));
  TEST_ASSERT_EQUAL_UINT8(1, led_layout::litCount(1, 8));  // au moins une LED dès que la valeur n'est pas nulle
  TEST_ASSERT_EQUAL_UINT8(4, led_layout::litCount(50, 8));
  TEST_ASSERT_EQUAL_UINT8(5, led_layout::litCount(57, 8));  // 4,56 arrondi à 5
  TEST_ASSERT_EQUAL_UINT8(8, led_layout::litCount(100, 8));
  TEST_ASSERT_EQUAL_UINT8(8, led_layout::litCount(200, 8));
}

void test_forward_chase_loops_in_the_forward_direction() {
  led_layout::Chase chase;
  chase.index = 1;
  // Bande à l'envers: "aller" vu de face = indices décroissants, puis retour au bout.
  chase = led_layout::nextChase(chase, 8, false, -1);
  TEST_ASSERT_EQUAL_UINT8(0, chase.index);
  chase = led_layout::nextChase(chase, 8, false, -1);
  TEST_ASSERT_EQUAL_UINT8(7, chase.index);
}

void test_bouncing_chase_turns_back_at_both_ends() {
  led_layout::Chase chase;
  chase.index = 6;
  chase.direction = 1;
  chase = led_layout::nextChase(chase, 8, true, -1);
  TEST_ASSERT_EQUAL_UINT8(7, chase.index);
  chase = led_layout::nextChase(chase, 8, true, -1);
  TEST_ASSERT_EQUAL_UINT8(6, chase.index);
  TEST_ASSERT_EQUAL_INT8(-1, chase.direction);
  for (int i = 0; i < 6; ++i) {
    chase = led_layout::nextChase(chase, 8, true, -1);
  }
  TEST_ASSERT_EQUAL_UINT8(0, chase.index);
  chase = led_layout::nextChase(chase, 8, true, -1);
  TEST_ASSERT_EQUAL_UINT8(1, chase.index);
}

void test_wifi_retry_delay_doubles_up_to_one_minute() {
  uint32_t delay = 10000;
  delay = retry_delay::next(delay, 60000);
  TEST_ASSERT_EQUAL_UINT32(20000, delay);
  delay = retry_delay::next(delay, 60000);
  TEST_ASSERT_EQUAL_UINT32(40000, delay);
  delay = retry_delay::next(delay, 60000);
  TEST_ASSERT_EQUAL_UINT32(60000, delay);
  delay = retry_delay::next(delay, 60000);
  TEST_ASSERT_EQUAL_UINT32(60000, delay);
  TEST_ASSERT_EQUAL_UINT32(4000000000u, retry_delay::next(3000000000u, 4000000000u));  // sans débordement
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_detents_move_the_volume_by_steps);
  RUN_TEST(test_volume_stays_within_the_parents_limits);
  RUN_TEST(test_gauge_shows_the_volume_between_the_limits);
  RUN_TEST(test_reversed_strip_starts_at_the_last_led);
  RUN_TEST(test_gauge_lights_the_nearest_number_of_leds);
  RUN_TEST(test_forward_chase_loops_in_the_forward_direction);
  RUN_TEST(test_bouncing_chase_turns_back_at_both_ends);
  RUN_TEST(test_wifi_retry_delay_doubles_up_to_one_minute);
  return UNITY_END();
}
