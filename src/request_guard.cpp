// SPDX-License-Identifier: GPL-3.0-or-later
#include "request_guard.h"

namespace request_guard {

namespace {

bool isAllowed(const String& host, const std::vector<String>& allowedHosts) {
  if (host.isEmpty()) {
    return false;
  }
  for (const String& allowed : allowedHosts) {
    if (!allowed.isEmpty() && host == normalizeHost(allowed)) {
      return true;
    }
  }
  return false;
}

}  // namespace

String normalizeHost(const String& value) {
  String host = value;
  host.trim();
  host.toLowerCase();
  const int scheme = host.indexOf("://");
  if (scheme >= 0) {
    host = host.substring(scheme + 3);
  }
  const int slash = host.indexOf('/');
  if (slash >= 0) {
    host = host.substring(0, slash);
  }
  const int colon = host.indexOf(':');
  if (colon >= 0) {
    host = host.substring(0, colon);
  }
  while (host.endsWith(".")) {
    host.remove(host.length() - 1);  // "totobox.local." est le même nom
  }
  return host;
}

const char* check(const Request& request, const std::vector<String>& allowedHosts) {
  // Requête adressée à un autre nom que la boîte: rebinding DNS.
  if (!isAllowed(normalizeHost(request.host), allowedHosts)) {
    return "forbidden_host";
  }
  if (!request.mutating) {
    return nullptr;
  }
  // Envoyée par une page d'une autre origine.
  String fetchSite = request.secFetchSite;
  fetchSite.toLowerCase();
  if (fetchSite == "cross-site" || fetchSite == "same-site") {
    return "forbidden_origin";
  }
  if (!request.origin.isEmpty() && (request.origin == "null" || !isAllowed(normalizeHost(request.origin), allowedHosts))) {
    return "forbidden_origin";
  }
  // Corps JSON obligatoire: text/plain ou formulaire partiraient sans demande d'autorisation du navigateur.
  if (request.hasBody) {
    String contentType = request.contentType;
    contentType.toLowerCase();
    if (!contentType.startsWith("application/json")) {
      return "json_required";
    }
  }
  return nullptr;
}

}  // namespace request_guard
