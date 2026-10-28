// SPDX-License-Identifier: GPL-3.0-or-later
// Tests de la lecture de la partition d'usine (format écrit par scripts/provision.py).

#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "factory_data.h"

namespace {

void putLe32(std::vector<uint8_t>& image, size_t offset, uint32_t value) {
  for (int index = 0; index < 4; ++index) {
    image[offset + index] = static_cast<uint8_t>(value >> (8 * index));
  }
}

// Même construction que build_image() dans scripts/provision.py.
std::vector<uint8_t> buildImage(const std::string& json, size_t size = 4096) {
  std::vector<uint8_t> image(size, 0xFF);
  memcpy(image.data(), "TBXF", 4);
  image[4] = factory_data::kVersion;
  image[5] = image[6] = image[7] = 0;
  putLe32(image, 8, json.size());
  putLe32(image, 12, factory_data::crc32(reinterpret_cast<const uint8_t*>(json.data()), json.size()));
  memcpy(image.data() + factory_data::kHeaderSize, json.data(), json.size());
  return image;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_crc32_matches_zlib() {
  // zlib.crc32(b"123456789") == 0xCBF43926 (valeur de contrôle du CRC-32 IEEE).
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, factory_data::crc32(reinterpret_cast<const uint8_t*>("123456789"), 9));
}

void test_valid_image_is_read() {
  const std::vector<uint8_t> image = buildImage(R"({"apPassword":"k7mp-3xqa-9fzt"})");
  factory_data::FactoryData data;
  TEST_ASSERT_TRUE(factory_data::parse(image.data(), image.size(), data));
  TEST_ASSERT_EQUAL_STRING("k7mp-3xqa-9fzt", data.apPassword.c_str());
}

void test_erased_partition_is_ignored() {
  const std::vector<uint8_t> image(4096, 0xFF);
  factory_data::FactoryData data;
  TEST_ASSERT_FALSE(factory_data::parse(image.data(), image.size(), data));
}

void test_corrupted_image_is_ignored() {
  std::vector<uint8_t> image = buildImage(R"({"apPassword":"k7mp-3xqa-9fzt"})");
  image[factory_data::kHeaderSize + 5] ^= 0x01;  // un bit du JSON: CRC faux
  factory_data::FactoryData data;
  TEST_ASSERT_FALSE(factory_data::parse(image.data(), image.size(), data));

  std::vector<uint8_t> badVersion = buildImage(R"({"apPassword":"k7mp-3xqa-9fzt"})");
  badVersion[4] = 2;
  TEST_ASSERT_FALSE(factory_data::parse(badVersion.data(), badVersion.size(), data));

  std::vector<uint8_t> badLength = buildImage(R"({"apPassword":"k7mp-3xqa-9fzt"})");
  putLe32(badLength, 8, 5000);
  TEST_ASSERT_FALSE(factory_data::parse(badLength.data(), badLength.size(), data));
}

void test_unusable_password_is_ignored() {
  const std::vector<uint8_t> image = buildImage(R"({"apPassword":"court"})");
  factory_data::FactoryData data;
  TEST_ASSERT_FALSE(factory_data::parse(image.data(), image.size(), data));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_matches_zlib);
  RUN_TEST(test_valid_image_is_read);
  RUN_TEST(test_erased_partition_is_ignored);
  RUN_TEST(test_corrupted_image_is_ignored);
  RUN_TEST(test_unusable_password_is_ignored);
  return UNITY_END();
}
