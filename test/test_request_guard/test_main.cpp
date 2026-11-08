// SPDX-License-Identifier: GPL-3.0-or-later
// Tests de la protection des routes /api contre les autres sites (CSRF, rebinding DNS).

#include <unity.h>

#include "request_guard.h"

namespace {

const std::vector<String> kHosts = {"totobox.local", "10.0.0.236", "192.168.4.1"};

request_guard::Request post(const char* host, const char* origin = "", const char* contentType = "application/json",
                            const char* path = "/api/config") {
  request_guard::Request request;
  request.mutating = true;
  request.path = path;
  request.host = host;
  request.origin = origin;
  request.contentType = contentType;
  request.contentLength = 42;
  return request;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_host_is_normalized() {
  TEST_ASSERT_EQUAL_STRING("totobox.local", request_guard::normalizeHost(" TotoBox.local:80 ").c_str());
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", request_guard::normalizeHost("http://10.0.0.5").c_str());
  TEST_ASSERT_EQUAL_STRING("totobox.local", request_guard::normalizeHost("http://totobox.local./x").c_str());
}

void test_page_of_the_box_is_accepted() {
  TEST_ASSERT_NULL(request_guard::check(post("totobox.local", "http://totobox.local"), kHosts));
  TEST_ASSERT_NULL(request_guard::check(post("10.0.0.236", "http://10.0.0.236"), kHosts));
  TEST_ASSERT_NULL(request_guard::check(post("192.168.4.1:80", "http://192.168.4.1"), kHosts));
  request_guard::Request sameOrigin = post("totobox.local", "http://totobox.local");
  sameOrigin.secFetchSite = "same-origin";
  TEST_ASSERT_NULL(request_guard::check(sameOrigin, kHosts));
}

void test_other_site_is_refused() {
  TEST_ASSERT_EQUAL_STRING("forbidden_origin", request_guard::check(post("totobox.local", "http://evil.example"), kHosts));
  TEST_ASSERT_EQUAL_STRING("forbidden_origin", request_guard::check(post("totobox.local", "null"), kHosts));
  request_guard::Request crossSite = post("totobox.local");
  crossSite.secFetchSite = "cross-site";
  TEST_ASSERT_EQUAL_STRING("forbidden_origin", request_guard::check(crossSite, kHosts));
}

void test_simple_requests_without_json_are_refused() {
  // Formulaire ou fetch text/plain sans Origin (anciens navigateurs): ne passent pas faute de JSON.
  TEST_ASSERT_EQUAL_STRING("json_required", request_guard::check(post("totobox.local", "", "text/plain"), kHosts));
  TEST_ASSERT_EQUAL_STRING("json_required",
                           request_guard::check(post("totobox.local", "", "application/x-www-form-urlencoded"), kHosts));
  TEST_ASSERT_NULL(request_guard::check(post("totobox.local", "", "application/json; charset=utf-8"), kHosts));
  // POST sans corps (verrouiller, restaurer...): seule l'origine compte.
  request_guard::Request empty = post("totobox.local", "http://totobox.local", "", "/api/lock");
  empty.contentLength = 0;
  TEST_ASSERT_NULL(request_guard::check(empty, kHosts));
}

void test_body_expected_by_each_route() {
  using request_guard::Body;
  using request_guard::expectedBody;
  TEST_ASSERT_TRUE(expectedBody(false, "/api/status") == Body::None);
  TEST_ASSERT_TRUE(expectedBody(false, "/api/download") == Body::None);
  TEST_ASSERT_TRUE(expectedBody(true, "/api/config") == Body::Json);
  TEST_ASSERT_TRUE(expectedBody(true, "/api/upload") == Body::Multipart);
  // Nom voisin: pas la route d'envoi.
  TEST_ASSERT_TRUE(expectedBody(true, "/api/uploads") == Body::Json);
  TEST_ASSERT_TRUE(expectedBody(true, "/api/upload/x") == Body::Json);
}

void test_file_upload_checks_origin_not_json() {
  // POST /api/upload: corps multipart, accepté sans JSON, mais seulement depuis la page de la boîte.
  request_guard::Request upload =
      post("totobox.local", "http://totobox.local", "multipart/form-data; boundary=x", "/api/upload");
  TEST_ASSERT_NULL(request_guard::check(upload, kHosts));
  upload.origin = "http://evil.example";
  TEST_ASSERT_EQUAL_STRING("forbidden_origin", request_guard::check(upload, kHosts));
  upload.origin = "";
  upload.secFetchSite = "cross-site";
  TEST_ASSERT_EQUAL_STRING("forbidden_origin", request_guard::check(upload, kHosts));
  // Le même corps vers une autre route est refusé.
  upload.secFetchSite = "same-origin";
  upload.path = "/api/folder";
  TEST_ASSERT_EQUAL_STRING("json_required", request_guard::check(upload, kHosts));
}

void test_reads_ignore_the_body() {
  // GET avec un corps (rare, mais permis par HTTP): rien à vérifier au-delà de l'hôte.
  request_guard::Request get = post("totobox.local", "http://evil.example", "text/plain", "/api/status");
  get.mutating = false;
  TEST_ASSERT_NULL(request_guard::check(get, kHosts));
}

void test_dns_rebinding_is_refused() {
  request_guard::Request get;
  get.host = "evil.example";
  TEST_ASSERT_EQUAL_STRING("forbidden_host", request_guard::check(get, kHosts));
  get.host = "";
  TEST_ASSERT_EQUAL_STRING("forbidden_host", request_guard::check(get, kHosts));
  get.host = "totobox.local";
  TEST_ASSERT_NULL(request_guard::check(get, kHosts));
}

void test_scripts_without_browser_headers_are_accepted() {
  // curl -H "Content-Type: application/json": ni Origin ni Sec-Fetch-Site.
  TEST_ASSERT_NULL(request_guard::check(post("10.0.0.236"), kHosts));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_host_is_normalized);
  RUN_TEST(test_page_of_the_box_is_accepted);
  RUN_TEST(test_other_site_is_refused);
  RUN_TEST(test_simple_requests_without_json_are_refused);
  RUN_TEST(test_body_expected_by_each_route);
  RUN_TEST(test_file_upload_checks_origin_not_json);
  RUN_TEST(test_reads_ignore_the_body);
  RUN_TEST(test_dns_rebinding_is_refused);
  RUN_TEST(test_scripts_without_browser_headers_are_accepted);
  return UNITY_END();
}
