// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_paths.h"

namespace library_paths {

namespace {

constexpr uint32_t kEstimatedBitrateBitsPerSec = 128000;
constexpr uint16_t kMaxRenameSuffix = 1000;

// Lettres et chiffres ASCII seulement: un octet d'un caractère accentué (UTF-8) devient "_".
bool isAsciiAlphaNumeric(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

String sanitizeName(const String& name, bool allowDot) {
  String out;
  out.reserve(name.length());
  for (size_t index = 0; index < name.length(); ++index) {
    const char c = name[index];
    const bool allowed = isAsciiAlphaNumeric(c) || c == '_' || c == '-' || c == ' ' || (allowDot && c == '.');
    out += allowed ? c : '_';
  }
  out.trim();
  while (out.endsWith(".")) {
    out.remove(out.length() - 1);
  }
  return isReservedName(out) ? String() : out;
}

}  // namespace

bool isAudioFile(const String& path) {
  String lower = path;
  lower.toLowerCase();
  return lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".flac") || lower.endsWith(".aac") ||
         lower.endsWith(".m4a") || lower.endsWith(".ogg") || lower.endsWith(".opus");
}

uint32_t estimateDurationSec(uint32_t sizeBytes) {
  return static_cast<uint32_t>(static_cast<uint64_t>(sizeBytes) * 8 / kEstimatedBitrateBitsPerSec);
}

String normalizeLibraryPath(const String& rawPath) {
  String path = rawPath;
  path.trim();
  path.replace('\\', '/');
  if (path.isEmpty()) {
    path = kLibraryRoot;
  }
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  while (path.endsWith("/") && path.length() > 1) {
    path.remove(path.length() - 1);
  }
  if (path.indexOf("..") >= 0) {
    return "";
  }
  if (!(path == kLibraryRoot || path.startsWith(String(kLibraryRoot) + "/"))) {
    return "";
  }
  return path;
}

String sanitizeFileName(const String& rawName) {
  String name = rawName;
  name.replace('\\', '/');
  const int slashIndex = name.lastIndexOf('/');
  if (slashIndex >= 0) {
    name = name.substring(slashIndex + 1);
  }
  return sanitizeName(name, true);
}

String sanitizeFolderName(const String& rawName) {
  return sanitizeName(rawName, false);
}

bool isReservedName(const String& name) {
  if (name.isEmpty() || name == "." || name == "..") {
    return true;
  }
  String lower = name;
  lower.toLowerCase();
  return lower == ".trash" || lower == "trash" || lower == "content";
}

bool isInTrash(const String& path) {
  return path == kTrashRoot || path.startsWith(String(kTrashRoot) + "/");
}

String normalizeConflictMode(const String& rawMode) {
  String mode = rawMode;
  mode.toLowerCase();
  return mode == "replace" || mode == "reject" ? mode : String("rename");
}

String resolveConflict(const String& dir, const String& name, const String& mode,
                       const std::function<bool(const String&)>& exists) {
  String basePath = dir;
  if (!basePath.endsWith("/")) {
    basePath += "/";
  }
  const String fullPath = basePath + name;
  if (mode == "replace" || !exists(fullPath)) {
    return fullPath;
  }
  if (mode == "reject") {
    return "";
  }

  const int dotIndex = name.lastIndexOf('.');
  const String stem = dotIndex > 0 ? name.substring(0, dotIndex) : name;
  const String extension = dotIndex > 0 ? name.substring(dotIndex) : String();
  for (uint16_t counter = 1; counter < kMaxRenameSuffix; ++counter) {
    const String candidate = basePath + stem + "-" + String(counter) + extension;
    if (!exists(candidate)) {
      return candidate;
    }
  }
  return "";
}

bool trashTargetFor(const String& libraryPath, TrashTarget& target) {
  if (libraryPath.isEmpty() || libraryPath == kLibraryRoot || isInTrash(libraryPath) ||
      !libraryPath.startsWith(String(kLibraryRoot) + "/")) {
    return false;
  }
  const String relative = libraryPath.substring(String(kLibraryRoot).length() + 1);
  const int slashIndex = relative.lastIndexOf('/');
  target.dir = slashIndex >= 0 ? String(kTrashRoot) + "/" + relative.substring(0, slashIndex) : String(kTrashRoot);
  target.name = slashIndex >= 0 ? relative.substring(slashIndex + 1) : relative;
  return true;
}

String originalPathFromTrash(const String& trashPath) {
  if (!isInTrash(trashPath)) {
    return "";
  }
  String relative = trashPath.substring(String(kTrashRoot).length());
  if (relative.isEmpty()) {
    return "";
  }
  if (!relative.startsWith("/")) {
    relative = "/" + relative;
  }
  return String(kLibraryRoot) + relative;
}

const char* moveError(const String& sourcePath, const String& targetDir) {
  if (sourcePath.isEmpty() || sourcePath == kLibraryRoot || isInTrash(sourcePath) || targetDir.isEmpty() ||
      isInTrash(targetDir)) {
    return "invalid_path";
  }
  if (targetDir == sourcePath || targetDir.startsWith(sourcePath + "/")) {
    return "invalid_move_target";
  }
  return nullptr;
}

}  // namespace library_paths
