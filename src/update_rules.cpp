// SPDX-License-Identifier: GPL-3.0-or-later
#include "update_rules.h"

namespace update_rules {

namespace {

bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Caractères permis dans le nom d'un fichier de Release: pas de "/", "?", "#", "%", espace...
bool isSafeFileName(const String& name) {
  if (name.isEmpty() || name.indexOf("..") >= 0) {
    return false;
  }
  for (size_t index = 0; index < name.length(); ++index) {
    const char c = name[index];
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                         c == '-' || c == '_';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool parseVersion(const String& text, uint16_t parts[3]) {
  size_t index = 0;
  for (int part = 0; part < 3; ++part) {
    if (index >= text.length() || !isDigit(text[index])) {
      return false;
    }
    // Pas de zéro en tête (versionnage sémantique), et pas plus de 5 chiffres.
    if (text[index] == '0' && index + 1 < text.length() && isDigit(text[index + 1])) {
      return false;
    }
    uint32_t value = 0;
    size_t digits = 0;
    while (index < text.length() && isDigit(text[index])) {
      value = value * 10 + (text[index] - '0');
      ++index;
      if (++digits > 5 || value > 65535) {
        return false;
      }
    }
    parts[part] = static_cast<uint16_t>(value);
    if (part < 2) {
      if (index >= text.length() || text[index] != '.') {
        return false;
      }
      ++index;
    }
  }
  return index == text.length();
}

bool isNewer(const String& current, const String& candidate) {
  uint16_t now[3];
  uint16_t next[3];
  if (!parseVersion(current, now) || !parseVersion(candidate, next)) {
    return false;
  }
  for (int part = 0; part < 3; ++part) {
    if (next[part] != now[part]) {
      return next[part] > now[part];
    }
  }
  return false;
}

bool isReleaseAsset(const String& url, const String& repo, const String& version) {
  const String prefix = "https://github.com/" + repo + "/releases/download/v" + version + "/";
  return url.startsWith(prefix) && isSafeFileName(url.substring(prefix.length()));
}

bool isSha256Hex(const String& value) {
  if (value.length() != 64) {
    return false;
  }
  for (size_t index = 0; index < value.length(); ++index) {
    if (!isHexDigit(value[index])) {
      return false;
    }
  }
  return true;
}

const char* check(const Request& request, const String& currentVersion, const String& repo,
                  uint32_t maxFirmwareBytes, uint32_t maxPageBytes) {
  uint16_t parts[3];
  if (!parseVersion(request.version, parts)) {
    return "invalid_version";
  }
  if (!isNewer(currentVersion, request.version)) {
    return "not_newer";
  }
  if (!isReleaseAsset(request.firmware.url, repo, request.version) ||
      !isReleaseAsset(request.page.url, repo, request.version)) {
    return "invalid_url";
  }
  if (!isSha256Hex(request.firmware.sha256) || !isSha256Hex(request.page.sha256)) {
    return "invalid_hash";
  }
  if (request.firmware.size == 0 || request.firmware.size > maxFirmwareBytes || request.page.size == 0 ||
      request.page.size > maxPageBytes) {
    return "invalid_size";
  }
  if (!isSafeFileName(request.uiVersion) || request.uiVersion.length() > 32) {
    return "invalid_ui_version";
  }
  return nullptr;
}

String toHex(const uint8_t* digest, size_t length) {
  static const char kHex[] = "0123456789abcdef";
  String out;
  out.reserve(length * 2);
  for (size_t index = 0; index < length; ++index) {
    out += kHex[digest[index] >> 4];
    out += kHex[digest[index] & 0x0F];
  }
  return out;
}

namespace {

constexpr char kUpdateBlock[] = "<!-- totobox-update ";

bool findAsset(JsonVariantConst release, JsonVariantConst file, Asset& asset) {
  const String name = String(static_cast<const char*>(file["name"] | ""));
  const uint32_t size = file["size"] | 0;
  for (JsonVariantConst item : release["assets"].as<JsonArrayConst>()) {
    if (name.length() > 0 && name == String(static_cast<const char*>(item["name"] | "")) && size == (item["size"] | 0)) {
      asset.url = String(static_cast<const char*>(item["browser_download_url"] | ""));
      asset.sha256 = String(static_cast<const char*>(file["sha256"] | ""));
      asset.size = size;
      return asset.url.length() > 0;
    }
  }
  return false;
}

}  // namespace

bool parseRelease(JsonVariantConst release, Request& request) {
  String version = String(static_cast<const char*>(release["tag_name"] | ""));
  if (version.startsWith("v")) {
    version = version.substring(1);
  }
  const String body = String(static_cast<const char*>(release["body"] | ""));
  const int start = body.indexOf(kUpdateBlock);
  if (start < 0) {
    return false;
  }
  const int jsonStart = start + static_cast<int>(strlen(kUpdateBlock));
  const int end = body.indexOf("-->", jsonStart);
  if (end < 0) {
    return false;
  }
  JsonDocument info;
  if (deserializeJson(info, body.substring(jsonStart, end).c_str()) != DeserializationError::Ok ||
      version != String(static_cast<const char*>(info["version"] | ""))) {
    return false;
  }
  request.version = version;
  request.uiVersion = String(static_cast<const char*>(info["uiVersion"] | ""));
  return findAsset(release, info["firmware"], request.firmware) && findAsset(release, info["page"], request.page);
}

bool shouldAutoCheck(const AutoCheck& state) {
  if (!state.enabled || !state.online || state.busy || state.retryWaiting || state.nowEpoch == 0 ||
      state.quietMs < kAutoCheckQuietMs) {
    return false;
  }
  // Horloge revenue en arrière (dernière recherche "dans le futur"): on ne reste pas bloqué.
  return state.nowEpoch < state.lastCheckEpoch || state.nowEpoch - state.lastCheckEpoch >= kAutoCheckIntervalSec;
}

}  // namespace update_rules
