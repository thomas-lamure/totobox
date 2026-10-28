// SPDX-License-Identifier: GPL-3.0-or-later
// Tests de Id3Stripper: retrait à la volée des tags ID3v2 (début, pochette) et ID3v1 (128 derniers octets).

#include <unity.h>

#include <cstdint>
#include <random>
#include <vector>

#include "id3_stripper.h"

namespace {

using Bytes = std::vector<uint8_t>;

// Tag ID3v2 de `payload` octets (taille "syncsafe"), avec pied de page optionnel.
Bytes id3v2Tag(uint32_t payload, bool footer) {
  Bytes tag = {'I', 'D', '3', 4, 0, static_cast<uint8_t>(footer ? 0x10 : 0x00),
               static_cast<uint8_t>((payload >> 21) & 0x7F), static_cast<uint8_t>((payload >> 14) & 0x7F),
               static_cast<uint8_t>((payload >> 7) & 0x7F), static_cast<uint8_t>(payload & 0x7F)};
  tag.insert(tag.end(), payload, 0xAB);
  if (footer) {
    const Bytes footerBytes = {'3', 'D', 'I', 4, 0, 0x10, tag[6], tag[7], tag[8], tag[9]};
    tag.insert(tag.end(), footerBytes.begin(), footerBytes.end());
  }
  return tag;
}

Bytes id3v1Tag() {
  Bytes tag(128, ' ');
  tag[0] = 'T';
  tag[1] = 'A';
  tag[2] = 'G';
  return tag;
}

// Données audio qui ne commencent pas par "ID3" ni ne finissent par "TAG".
Bytes audio(size_t length, std::mt19937& random) {
  Bytes data(length);
  for (auto& byte : data) {
    byte = static_cast<uint8_t>(random());
  }
  if (!data.empty()) {
    data[0] = 0xFF;  // en-tête de trame MP3
  }
  return data;
}

Bytes concat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

// Passe `input` dans le filtre par blocs de taille aléatoire (comme les paquets d'un upload HTTP).
Bytes strip(const Bytes& input, bool enabled, std::mt19937& random, uint32_t* stripped = nullptr) {
  Id3Stripper stripper;
  stripper.reset(enabled);
  Bytes out;
  const Id3Stripper::Sink sink = [&out](const uint8_t* data, size_t length) {
    out.insert(out.end(), data, data + length);
    return true;
  };
  size_t offset = 0;
  while (offset < input.size()) {
    const size_t length = std::min<size_t>(input.size() - offset, 1 + random() % 1500);
    TEST_ASSERT_TRUE(stripper.feed(input.data() + offset, length, sink));
    offset += length;
  }
  TEST_ASSERT_TRUE(stripper.finish(sink));
  if (stripped != nullptr) {
    *stripped = stripper.strippedBytes();
  }
  return out;
}

void assertBytesEqual(const Bytes& expected, const Bytes& actual) {
  TEST_ASSERT_EQUAL_UINT32(expected.size(), actual.size());
  if (!expected.empty()) {
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), actual.data(), expected.size());
  }
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_disabled_passes_everything_through() {
  std::mt19937 random(1);
  const Bytes input = concat({id3v2Tag(500, false), audio(3000, random), id3v1Tag()});
  assertBytesEqual(input, strip(input, false, random));
}

void test_removes_id3v2_and_id3v1() {
  std::mt19937 random(2);
  const Bytes sound = audio(4000, random);
  uint32_t stripped = 0;
  const Bytes output = strip(concat({id3v2Tag(2000, false), sound, id3v1Tag()}), true, random, &stripped);
  assertBytesEqual(sound, output);
  TEST_ASSERT_EQUAL_UINT32(10 + 2000 + 128, stripped);
}

void test_removes_tag_with_footer_and_chained_tags() {
  std::mt19937 random(3);
  const Bytes sound = audio(1000, random);
  uint32_t stripped = 0;
  const Bytes output = strip(concat({id3v2Tag(300, true), id3v2Tag(50, false), sound}), true, random, &stripped);
  assertBytesEqual(sound, output);
  TEST_ASSERT_EQUAL_UINT32((10 + 300 + 10) + (10 + 50), stripped);
}

void test_keeps_file_without_tags() {
  std::mt19937 random(4);
  const Bytes sound = audio(2500, random);
  uint32_t stripped = 0;
  assertBytesEqual(sound, strip(sound, true, random, &stripped));
  TEST_ASSERT_EQUAL_UINT32(0, stripped);
}

void test_keeps_files_shorter_than_a_header() {
  std::mt19937 random(5);
  const Bytes tiny = {0xFF, 0xFB, 0x90};
  assertBytesEqual(tiny, strip(tiny, true, random));
}

void test_random_files_and_chunk_sizes() {
  std::mt19937 random(42);
  for (int iteration = 0; iteration < 300; ++iteration) {
    const Bytes sound = audio(random() % 5000, random);
    Bytes input;
    uint32_t expectedStripped = 0;
    for (uint32_t tag = 0, count = random() % 3; tag < count; ++tag) {
      const Bytes header = id3v2Tag(random() % 3000, random() % 2 == 0);
      expectedStripped += header.size();
      input.insert(input.end(), header.begin(), header.end());
    }
    input.insert(input.end(), sound.begin(), sound.end());
    if (random() % 2 == 0) {
      const Bytes trailer = id3v1Tag();
      expectedStripped += trailer.size();
      input.insert(input.end(), trailer.begin(), trailer.end());
    }
    uint32_t stripped = 0;
    assertBytesEqual(sound, strip(input, true, random, &stripped));
    TEST_ASSERT_EQUAL_UINT32(expectedStripped, stripped);
  }
}

void test_sink_failure_is_reported() {
  Id3Stripper stripper;
  stripper.reset(false);
  const uint8_t data[4] = {1, 2, 3, 4};
  const Id3Stripper::Sink failing = [](const uint8_t*, size_t) { return false; };
  TEST_ASSERT_FALSE(stripper.feed(data, sizeof(data), failing));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_disabled_passes_everything_through);
  RUN_TEST(test_removes_id3v2_and_id3v1);
  RUN_TEST(test_removes_tag_with_footer_and_chained_tags);
  RUN_TEST(test_keeps_file_without_tags);
  RUN_TEST(test_keeps_files_shorter_than_a_header);
  RUN_TEST(test_random_files_and_chunk_sizes);
  RUN_TEST(test_sink_failure_is_reported);
  return UNITY_END();
}
