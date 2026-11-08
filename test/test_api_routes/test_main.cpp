// SPDX-License-Identifier: GPL-3.0-or-later
// Tests de la table des routes /api: refus quand la boîte est verrouillée, pendant une lecture ou un
// téléchargement. La concordance avec docs/api.md et le faux serveur est vérifiée par scripts/test_api_routes.py.

#include <unity.h>

#include <set>
#include <string>

#include "api_routes.h"

using namespace api_routes;

namespace {

const Route& route(Method method, const char* path) {
  const Route* found = find(method, path);
  TEST_ASSERT_NOT_NULL_MESSAGE(found, path);
  return *found;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_routes_are_unique() {
  size_t count = 0;
  const Route* routes = all(count);
  TEST_ASSERT_GREATER_THAN(20, count);
  std::set<std::string> seen;
  for (size_t i = 0; i < count; ++i) {
    const std::string key = std::string(routes[i].method == Method::Get ? "GET " : "POST ") + routes[i].path;
    TEST_ASSERT_TRUE_MESSAGE(seen.insert(key).second, key.c_str());
    TEST_ASSERT_TRUE(find(routes[i].method, routes[i].path) == &routes[i]);
  }
  TEST_ASSERT_NULL(find(Method::Get, "/api/statu"));
  TEST_ASSERT_NULL(find(Method::Post, "/api/status"));
}

void test_locked_box_only_exposes_playback_and_volume() {
  // AGENTS.md: verrouillée, la boîte n'expose que la lecture en cours et le volume (plus de quoi se déverrouiller).
  size_t count = 0;
  const Route* routes = all(count);
  std::set<std::string> open;
  for (size_t i = 0; i < count; ++i) {
    if (refusal(routes[i], true, false, false) == nullptr) {
      open.insert(routes[i].path);
    }
  }
  const std::set<std::string> expected = {"/api/status", "/api/volume", "/api/unlock", "/api/lock"};
  TEST_ASSERT_TRUE(open == expected);
}

void test_sd_routes_wait_for_the_story() {
  for (const char* path : {"/api/library", "/api/trash", "/api/download", "/api/cards/backup"}) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE("busy_playing", refusal(route(Method::Get, path), false, true, false), path);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("busy_downloading", refusal(route(Method::Get, path), false, false, true), path);
  }
  for (const char* path : {"/api/upload", "/api/rename", "/api/delete", "/api/restore", "/api/factory-reset"}) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE("busy_playing", refusal(route(Method::Post, path), false, true, false), path);
  }
  // Réglages, cartes et volume restent servis pendant une histoire.
  for (const char* path : {"/api/config", "/api/bindings", "/api/volume", "/api/setup/complete"}) {
    TEST_ASSERT_NULL_MESSAGE(refusal(route(Method::Post, path), false, true, true), path);
  }
}

void test_lock_comes_first() {
  TEST_ASSERT_EQUAL_STRING("locked", refusal(route(Method::Get, "/api/library"), true, true, true));
  TEST_ASSERT_NULL(refusal(route(Method::Get, "/api/library"), false, false, false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_routes_are_unique);
  RUN_TEST(test_locked_box_only_exposes_playback_and_volume);
  RUN_TEST(test_sd_routes_wait_for_the_story);
  RUN_TEST(test_lock_comes_first);
  return UNITY_END();
}
