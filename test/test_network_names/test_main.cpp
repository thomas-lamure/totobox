// SPDX-License-Identifier: GPL-3.0-or-later
// Tests de la validation des réglages réseau (nom de la boîte, SSID, mots de passe).

#include <unity.h>

#include "network_names.h"

void setUp() {}
void tearDown() {}

void test_hostname_is_normalized() {
  TEST_ASSERT_EQUAL_STRING("totobox-salon", network_names::normalizeHostname("  TotoBox-Salon ").c_str());
}

void test_valid_hostnames() {
  TEST_ASSERT_TRUE(network_names::isValidHostname("totobox"));
  TEST_ASSERT_TRUE(network_names::isValidHostname("totobox-2"));
  TEST_ASSERT_TRUE(network_names::isValidHostname("a"));
  TEST_ASSERT_TRUE(network_names::isValidHostname("12345678901234567890123456789012"));  // 32 caractères
}

void test_invalid_hostnames() {
  TEST_ASSERT_FALSE(network_names::isValidHostname(""));
  TEST_ASSERT_FALSE(network_names::isValidHostname("-totobox"));
  TEST_ASSERT_FALSE(network_names::isValidHostname("totobox-"));
  TEST_ASSERT_FALSE(network_names::isValidHostname("toto box"));
  TEST_ASSERT_FALSE(network_names::isValidHostname("toto_box"));
  TEST_ASSERT_FALSE(network_names::isValidHostname("TotoBox"));  // à normaliser d'abord
  TEST_ASSERT_FALSE(network_names::isValidHostname("totobox.local"));
  TEST_ASSERT_FALSE(network_names::isValidHostname("chambre-é"));
  TEST_ASSERT_FALSE(network_names::isValidHostname("123456789012345678901234567890123"));  // 33 caractères
}

void test_ssid_length() {
  TEST_ASSERT_TRUE(network_names::isValidSsid("Maison"));
  TEST_ASSERT_TRUE(network_names::isValidSsid("12345678901234567890123456789012"));
  TEST_ASSERT_FALSE(network_names::isValidSsid(""));
  TEST_ASSERT_FALSE(network_names::isValidSsid("123456789012345678901234567890123"));
}

void test_wifi_password_open_or_wpa2() {
  TEST_ASSERT_TRUE(network_names::isValidWifiPassword(""));  // réseau ouvert
  TEST_ASSERT_TRUE(network_names::isValidWifiPassword("12345678"));
  TEST_ASSERT_FALSE(network_names::isValidWifiPassword("1234567"));
  String longest;
  for (int index = 0; index < 63; ++index) {
    longest += 'x';
  }
  TEST_ASSERT_TRUE(network_names::isValidWifiPassword(longest));
  longest += 'x';
  TEST_ASSERT_FALSE(network_names::isValidWifiPassword(longest));
}

void test_access_point_password_requires_wpa2() {
  TEST_ASSERT_FALSE(network_names::isValidApPassword(""));
  TEST_ASSERT_FALSE(network_names::isValidApPassword("1234567"));
  TEST_ASSERT_TRUE(network_names::isValidApPassword("totobox123"));
}

void test_default_ap_ssid_uses_mac_suffix() {
  const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0xAB, 0x0C, 0xD3};
  TEST_ASSERT_EQUAL_STRING("TotoBox-0CD3", network_names::defaultApSsid(mac).c_str());
}

void test_generated_ap_password_format() {
  for (int round = 0; round < 50; ++round) {
    const String password = network_names::generateApPassword();
    TEST_ASSERT_EQUAL_UINT32(14, password.length());
    TEST_ASSERT_TRUE(network_names::isValidApPassword(password));
    for (size_t index = 0; index < password.length(); ++index) {
      const char c = password[index];
      if (index == 4 || index == 9) {
        TEST_ASSERT_EQUAL_CHAR('-', c);
      } else {
        // Ni majuscules ni caractères ambigus.
        TEST_ASSERT_TRUE((c >= 'a' && c <= 'z') || (c >= '2' && c <= '9'));
        TEST_ASSERT_TRUE(c != 'o' && c != 'l' && c != 'i');
      }
    }
  }
  TEST_ASSERT_TRUE(network_names::generateApPassword() != network_names::generateApPassword());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hostname_is_normalized);
  RUN_TEST(test_valid_hostnames);
  RUN_TEST(test_invalid_hostnames);
  RUN_TEST(test_ssid_length);
  RUN_TEST(test_wifi_password_open_or_wpa2);
  RUN_TEST(test_access_point_password_requires_wpa2);
  RUN_TEST(test_default_ap_ssid_uses_mac_suffix);
  RUN_TEST(test_generated_ap_password_format);
  return UNITY_END();
}
