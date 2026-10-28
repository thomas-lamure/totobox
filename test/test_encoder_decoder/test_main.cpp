// SPDX-License-Identifier: GPL-3.0-or-later
// Tests du décodage de l'encodeur KY-040 (séquence de quadrature, rebonds, fronts manqués).

#include <unity.h>

#include <initializer_list>

#include "encoder_decoder.h"

namespace {

// États successifs lus sur les broches (CLK << 1 | DT): somme des crans comptés.
int feed(encoder_decoder::Decoder& decoder, std::initializer_list<uint8_t> states) {
  int detents = 0;
  for (uint8_t state : states) {
    detents += decoder.update(state);
  }
  return detents;
}

constexpr uint8_t S00 = 0, S01 = 1, S10 = 2, S11 = 3;

}  // namespace

void setUp() {}
void tearDown() {}

void test_one_detent_each_way() {
  encoder_decoder::Decoder decoder;
  TEST_ASSERT_EQUAL(1, feed(decoder, {S01, S00, S10, S11}));   // horaire: CLK descend le premier
  TEST_ASSERT_EQUAL(-1, feed(decoder, {S10, S00, S01, S11}));  // antihoraire: DT descend le premier
}

void test_several_detents_in_a_row() {
  encoder_decoder::Decoder decoder;
  TEST_ASSERT_EQUAL(3, feed(decoder, {S01, S00, S10, S11, S01, S00, S10, S11, S01, S00, S10, S11}));
  TEST_ASSERT_EQUAL(-2, feed(decoder, {S10, S00, S01, S11, S10, S00, S01, S11}));
}

void test_count_only_at_rest() {
  encoder_decoder::Decoder decoder;
  TEST_ASSERT_EQUAL(0, feed(decoder, {S01, S00, S10}));  // cran pas terminé
  TEST_ASSERT_EQUAL(1, feed(decoder, {S11}));
}

void test_contact_bounce_is_ignored() {
  encoder_decoder::Decoder decoder;
  // Rebonds sur chaque front: allers-retours entre états voisins, puis le cran se termine.
  TEST_ASSERT_EQUAL(1, feed(decoder, {S01, S11, S01, S00, S01, S00, S10, S00, S10, S11}));
  // Rebond au départ de la position de repos: aucun cran.
  TEST_ASSERT_EQUAL(0, feed(decoder, {S01, S11, S01, S11, S10, S11}));
}

void test_same_state_read_twice_is_ignored() {
  encoder_decoder::Decoder decoder;
  TEST_ASSERT_EQUAL(1, feed(decoder, {S11, S01, S01, S00, S00, S10, S10, S11, S11}));
}

void test_missed_edge_is_absorbed() {
  encoder_decoder::Decoder decoder;
  // 01 -> 10: les deux signaux ont changé entre deux lectures (front manqué), le cran compte quand même.
  TEST_ASSERT_EQUAL(1, feed(decoder, {S01, S10, S11}));
  TEST_ASSERT_EQUAL(-1, feed(decoder, {S10, S01, S11}));
}

void test_half_turn_then_back_counts_nothing() {
  encoder_decoder::Decoder decoder;
  TEST_ASSERT_EQUAL(0, feed(decoder, {S01, S00, S01, S11}));  // on s'arrête à mi-cran et on revient
  TEST_ASSERT_EQUAL(0, feed(decoder, {S10, S11}));
}

void test_reversal_between_detents() {
  encoder_decoder::Decoder decoder;
  TEST_ASSERT_EQUAL(0, feed(decoder, {S01, S00, S10, S11, S10, S00, S01, S11}));  // +1 puis -1
}

void test_start_state_out_of_rest() {
  // Démarrage avec l'encodeur arrêté entre deux crans: le premier retour au repos ne compte que si le
  // parcours suffit.
  encoder_decoder::Decoder decoder;
  decoder.state = S00;
  TEST_ASSERT_EQUAL(1, feed(decoder, {S10, S11}));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_one_detent_each_way);
  RUN_TEST(test_several_detents_in_a_row);
  RUN_TEST(test_count_only_at_rest);
  RUN_TEST(test_contact_bounce_is_ignored);
  RUN_TEST(test_same_state_read_twice_is_ignored);
  RUN_TEST(test_missed_edge_is_absorbed);
  RUN_TEST(test_half_turn_then_back_counts_nothing);
  RUN_TEST(test_reversal_between_detents);
  RUN_TEST(test_start_state_out_of_rest);
  return UNITY_END();
}
