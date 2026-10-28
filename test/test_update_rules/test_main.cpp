// SPDX-License-Identifier: GPL-3.0-or-later
// Tests des règles de la mise à jour en ligne: versions, fichiers du dépôt officiel seulement, empreintes.

#include <unity.h>

#include "update_rules.h"

using namespace update_rules;

namespace {

constexpr char kRepo[] = "thomas-lamure/totobox";
constexpr char kHash[] = "8f434346648f6b96df89dda901c5176b10a6d83961dd3c1ac88b59b2dc327aa4";

Request validRequest() {
  Request request;
  request.version = "1.1.0";
  request.uiVersion = "8291aeae5293";
  request.firmware.url = "https://github.com/thomas-lamure/totobox/releases/download/v1.1.0/totobox-1.1.0-firmware.bin";
  request.firmware.sha256 = kHash;
  request.firmware.size = 1500000;
  request.page.url = "https://github.com/thomas-lamure/totobox/releases/download/v1.1.0/totobox-1.1.0-page.html.gz";
  request.page.sha256 = kHash;
  request.page.size = 47000;
  return request;
}

const char* checkRequest(const Request& request) {
  return check(request, "1.0.1", kRepo, 1966080, 80 * 1024);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_versions_are_parsed() {
  uint16_t parts[3];
  TEST_ASSERT_TRUE(parseVersion("1.10.0", parts));
  TEST_ASSERT_EQUAL_UINT16(10, parts[1]);
  TEST_ASSERT_TRUE(parseVersion("0.0.0", parts));
  for (const char* bad : {"", "1.0", "v1.0.0", "1.0.0-rc1", "01.0.0", "1..0", "1.0.0.0", "1.0.65536", "1.0.a"}) {
    TEST_ASSERT_FALSE_MESSAGE(parseVersion(bad, parts), bad);
  }
}

void test_only_newer_versions_are_newer() {
  TEST_ASSERT_TRUE(isNewer("1.0.1", "1.0.2"));
  TEST_ASSERT_TRUE(isNewer("1.0.9", "1.0.10"));  // comparaison numérique, pas alphabétique
  TEST_ASSERT_TRUE(isNewer("1.9.9", "2.0.0"));
  TEST_ASSERT_FALSE(isNewer("1.0.1", "1.0.1"));
  TEST_ASSERT_FALSE(isNewer("1.1.0", "1.0.9"));
  TEST_ASSERT_FALSE(isNewer("1.0.1", "garbage"));
}

void test_valid_request_is_accepted() {
  TEST_ASSERT_NULL(checkRequest(validRequest()));
}

void test_older_or_same_version_is_refused() {
  Request request = validRequest();
  request.version = "1.0.1";
  TEST_ASSERT_EQUAL_STRING("not_newer", checkRequest(request));
  request.version = "1.0.0";
  TEST_ASSERT_EQUAL_STRING("not_newer", checkRequest(request));
  request.version = "latest";
  TEST_ASSERT_EQUAL_STRING("invalid_version", checkRequest(request));
}

void test_files_must_come_from_the_official_releases() {
  const char* bad[] = {
      "https://github.com/someone/totobox/releases/download/v1.1.0/totobox-1.1.0-firmware.bin",  // autre dépôt
      "http://github.com/thomas-lamure/totobox/releases/download/v1.1.0/totobox-1.1.0-firmware.bin",  // sans TLS
      "https://github.com/thomas-lamure/totobox/releases/download/v1.0.0/totobox-1.0.0-firmware.bin",  // autre version
      "https://github.com/thomas-lamure/totobox/releases/download/v1.1.0/../../../x.bin",
      "https://github.com/thomas-lamure/totobox/releases/download/v1.1.0/sub/x.bin",
      "https://github.com/thomas-lamure/totobox/releases/download/v1.1.0/x.bin?redirect=evil",
      "https://github.com.evil.example/thomas-lamure/totobox/releases/download/v1.1.0/x.bin",
      "https://github.com/thomas-lamure/totobox/releases/download/v1.1.0/",
  };
  for (const char* url : bad) {
    Request request = validRequest();
    request.firmware.url = url;
    TEST_ASSERT_EQUAL_STRING_MESSAGE("invalid_url", checkRequest(request), url);
  }
  Request request = validRequest();
  request.page.url = bad[0];
  TEST_ASSERT_EQUAL_STRING("invalid_url", checkRequest(request));
}

void test_hashes_must_be_sha256() {
  Request request = validRequest();
  request.firmware.sha256 = "abc";
  TEST_ASSERT_EQUAL_STRING("invalid_hash", checkRequest(request));
  request = validRequest();
  request.page.sha256 = String(kHash).substring(0, 63) + "z";
  TEST_ASSERT_EQUAL_STRING("invalid_hash", checkRequest(request));
  TEST_ASSERT_TRUE(isSha256Hex("8F434346648F6B96DF89DDA901C5176B10A6D83961DD3C1AC88B59B2DC327AA4"));
}

void test_sizes_must_fit_the_partitions() {
  Request request = validRequest();
  request.firmware.size = 0;
  TEST_ASSERT_EQUAL_STRING("invalid_size", checkRequest(request));
  request.firmware.size = 1966081;
  TEST_ASSERT_EQUAL_STRING("invalid_size", checkRequest(request));
  request = validRequest();
  request.page.size = 80 * 1024 + 1;
  TEST_ASSERT_EQUAL_STRING("invalid_size", checkRequest(request));
}

void test_ui_version_is_a_plain_name() {
  Request request = validRequest();
  request.uiVersion = "../../etc";
  TEST_ASSERT_EQUAL_STRING("invalid_ui_version", checkRequest(request));
  request.uiVersion = "";
  TEST_ASSERT_EQUAL_STRING("invalid_ui_version", checkRequest(request));
}

void test_digest_is_written_in_lowercase_hex() {
  const uint8_t digest[] = {0x00, 0x8f, 0xA0, 0xff};
  TEST_ASSERT_EQUAL_STRING("008fa0ff", toHex(digest, sizeof(digest)).c_str());
}

// Réponse de l'API GitHub /releases/latest (champs gardés par le filtre de UpdateService::check).
const char* kRelease = R"({
  "tag_name": "v1.1.0",
  "body": "### Firmware\n\n- Nouveau.\n\n<!-- totobox-update {\"version\":\"1.1.0\",\"uiVersion\":\"8291aeae5293\",\"firmware\":{\"name\":\"totobox-1.1.0-firmware.bin\",\"size\":1500000,\"sha256\":\"8f434346648f6b96df89dda901c5176b10a6d83961dd3c1ac88b59b2dc327aa4\"},\"page\":{\"name\":\"totobox-1.1.0-page.html.gz\",\"size\":47000,\"sha256\":\"8f434346648f6b96df89dda901c5176b10a6d83961dd3c1ac88b59b2dc327aa4\"}} -->",
  "assets": [
    {"name": "totobox-1.1.0-complet.bin", "size": 4194304, "browser_download_url": "https://github.com/thomas-lamure/totobox/releases/download/v1.1.0/totobox-1.1.0-complet.bin"},
    {"name": "totobox-1.1.0-firmware.bin", "size": 1500000, "browser_download_url": "https://github.com/thomas-lamure/totobox/releases/download/v1.1.0/totobox-1.1.0-firmware.bin"},
    {"name": "totobox-1.1.0-page.html.gz", "size": 47000, "browser_download_url": "https://github.com/thomas-lamure/totobox/releases/download/v1.1.0/totobox-1.1.0-page.html.gz"}
  ]
})";

bool parse(const String& json, Request& request) {
  JsonDocument release;
  TEST_ASSERT_TRUE(deserializeJson(release, json.c_str()) == DeserializationError::Ok);
  return parseRelease(release, request);
}

String replaced(String text, const char* from, const char* to) {
  const int index = text.indexOf(from);
  TEST_ASSERT_TRUE(index >= 0);
  return text.substring(0, index) + to + text.substring(index + strlen(from));
}

void test_release_gives_an_acceptable_request() {
  Request request;
  TEST_ASSERT_TRUE(parse(kRelease, request));
  TEST_ASSERT_EQUAL_STRING("1.1.0", request.version.c_str());
  TEST_ASSERT_EQUAL_STRING("8291aeae5293", request.uiVersion.c_str());
  TEST_ASSERT_EQUAL_STRING(
      "https://github.com/thomas-lamure/totobox/releases/download/v1.1.0/totobox-1.1.0-firmware.bin",
      request.firmware.url.c_str());
  TEST_ASSERT_EQUAL_UINT32(47000, request.page.size);
  TEST_ASSERT_NULL(checkRequest(request));
}

void test_release_without_online_update() {
  Request request;
  TEST_ASSERT_FALSE(parse(replaced(kRelease, "<!-- totobox-update", "<!-- autre"), request));  // pas de bloc
  TEST_ASSERT_FALSE(parse(replaced(kRelease, "\"size\": 47000", "\"size\": 1"), request));  // taille différente
  TEST_ASSERT_FALSE(parse(replaced(kRelease, "\\\"version\\\":\\\"1.1.0", "\\\"version\\\":\\\"1.0.9"), request));
  TEST_ASSERT_FALSE(parse(replaced(kRelease, "{\\\"version", "{pas du json"), request));
  TEST_ASSERT_FALSE(parse(R"({"tag_name":"v1.1.0"})", request));
}

void test_auto_check_waits_for_a_quiet_minute_once_a_day() {
  AutoCheck state;
  state.enabled = true;
  state.online = true;
  state.nowEpoch = 1793180000;
  state.lastCheckEpoch = state.nowEpoch - kAutoCheckIntervalSec;
  state.quietMs = kAutoCheckQuietMs;
  TEST_ASSERT_TRUE(shouldAutoCheck(state));

  AutoCheck early = state;
  early.lastCheckEpoch = state.nowEpoch - kAutoCheckIntervalSec + 1;  // moins de 24 h
  TEST_ASSERT_FALSE(shouldAutoCheck(early));
  AutoCheck playing = state;
  playing.quietMs = kAutoCheckQuietMs - 1;  // histoire finie (ou boîte réveillée) il y a moins d'une minute
  TEST_ASSERT_FALSE(shouldAutoCheck(playing));
  AutoCheck noTime = state;
  noTime.nowEpoch = 0;  // heure NTP pas encore reçue
  TEST_ASSERT_FALSE(shouldAutoCheck(noTime));
  AutoCheck accessPoint = state;
  accessPoint.online = false;
  TEST_ASSERT_FALSE(shouldAutoCheck(accessPoint));
  AutoCheck disabled = state;
  disabled.enabled = false;
  TEST_ASSERT_FALSE(shouldAutoCheck(disabled));
  AutoCheck busy = state;
  busy.busy = true;
  TEST_ASSERT_FALSE(shouldAutoCheck(busy));
  AutoCheck retry = state;
  retry.retryWaiting = true;
  TEST_ASSERT_FALSE(shouldAutoCheck(retry));
}

void test_auto_check_runs_on_a_new_box_and_after_a_clock_reset() {
  AutoCheck state;
  state.enabled = true;
  state.online = true;
  state.quietMs = kAutoCheckQuietMs;
  state.nowEpoch = 1793180000;
  state.lastCheckEpoch = 0;  // jamais cherché
  TEST_ASSERT_TRUE(shouldAutoCheck(state));
  state.lastCheckEpoch = state.nowEpoch + 3600;  // dernière recherche "dans le futur"
  TEST_ASSERT_TRUE(shouldAutoCheck(state));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_versions_are_parsed);
  RUN_TEST(test_only_newer_versions_are_newer);
  RUN_TEST(test_valid_request_is_accepted);
  RUN_TEST(test_older_or_same_version_is_refused);
  RUN_TEST(test_files_must_come_from_the_official_releases);
  RUN_TEST(test_hashes_must_be_sha256);
  RUN_TEST(test_sizes_must_fit_the_partitions);
  RUN_TEST(test_ui_version_is_a_plain_name);
  RUN_TEST(test_digest_is_written_in_lowercase_hex);
  RUN_TEST(test_release_gives_an_acceptable_request);
  RUN_TEST(test_release_without_online_update);
  RUN_TEST(test_auto_check_waits_for_a_quiet_minute_once_a_day);
  RUN_TEST(test_auto_check_runs_on_a_new_box_and_after_a_clock_reset);
  return UNITY_END();
}
