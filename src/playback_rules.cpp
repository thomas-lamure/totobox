// SPDX-License-Identifier: GPL-3.0-or-later
#include "playback_rules.h"

#include <algorithm>
#include <initializer_list>
#include <strings.h>

namespace playback_rules {

namespace {

constexpr uint8_t kLibVolumeSteps = 21;

bool hasExtension(const String& path, std::initializer_list<const char*> extensions) {
  String lower = path;
  lower.toLowerCase();
  for (const char* extension : extensions) {
    if (lower.endsWith(extension)) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool supportsByteResume(const String& path) {
  return hasExtension(path, {".mp3", ".wav", ".m4a", ".flac", ".aac"});
}

bool supportsSecondResume(const String& path) {
  return hasExtension(path, {".mp3", ".wav", ".flac", ".aac"});
}

ResumePlan planResume(const String& path, uint32_t resumeMs, uint32_t resumeByte, uint32_t fileSize) {
  ResumePlan plan;
  if (resumeByte > 0 && resumeByte >= fileSize) {
    plan.beyondEnd = true;
    return plan;
  }
  if (resumeByte > 0 && supportsByteResume(path)) {
    plan.kind = ResumeKind::ByByte;
    plan.startByte = resumeByte;
    plan.baseMs = resumeMs;
  } else if (resumeMs >= 1000UL && supportsSecondResume(path)) {
    plan.kind = ResumeKind::BySeconds;
    plan.seconds = static_cast<uint16_t>(std::min<uint32_t>(resumeMs / 1000UL, UINT16_MAX));
  } else if (resumeMs >= 1000UL || resumeByte > 0) {
    plan.unsupported = true;
  }
  return plan;
}

uint32_t positionMs(const PlaybackClock& clock, uint32_t libTimeSec) {
  if (!clock.byteResumed) {
    return libTimeSec * 1000UL;
  }
  if (clock.anchorPending) {
    return clock.baseMs;
  }
  return clock.baseMs + (libTimeSec > clock.anchorSec ? libTimeSec - clock.anchorSec : 0) * 1000UL;
}

uint32_t positionByte(uint32_t filePos, uint32_t buffered, uint32_t fileSize) {
  if (filePos > fileSize) {
    return 0;
  }
  return filePos > buffered ? filePos - buffered : 0;
}

void sortPlaylist(std::vector<String>& playlist) {
  std::sort(playlist.begin(), playlist.end(),
            [](const String& left, const String& right) { return strcasecmp(left.c_str(), right.c_str()) < 0; });
}

uint8_t libVolume(uint8_t percent) {
  return static_cast<uint8_t>(std::min<uint8_t>(percent, 100) * kLibVolumeSteps / 100);
}

}  // namespace playback_rules
