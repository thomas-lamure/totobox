// SPDX-License-Identifier: GPL-3.0-or-later
#include "card_flow.h"

#include "access_lock.h"

namespace card_flow {

namespace {

void storeBookmark(CardBinding& binding, const Bookmark& bookmark) {
  binding.resumeTrackIndex = bookmark.track;
  binding.resumeMs = bookmark.ms;
  binding.resumeByte = bookmark.byte;
}

void clearActiveCard(RuntimeState& runtime) {
  runtime.activeCardUid = "";
  runtime.activeMediaPath = "";
  runtime.activeTrackIndex = 0;
  runtime.activePositionMs = 0;
}

}  // namespace

NfcStep onNfcEvent(RuntimeState& runtime, bool cardPresent, const String& uid, uint32_t now, uint32_t graceMs) {
  NfcStep step;
  if (cardPresent) {
    runtime.lastCardSeenMs = now;
    if (uid != runtime.activeCardUid) {
      step.stopCurrent = !runtime.activeCardUid.isEmpty();
      step.present = true;
    }
    return step;
  }
  step.stopCurrent = !runtime.activeCardUid.isEmpty() && now - runtime.lastCardSeenMs >= graceMs;
  return step;
}

Presented onCardPresented(RuntimeState& runtime, const DeviceConfig& config,
                          const std::vector<CardBinding>& bindings, const String& uid) {
  clearActiveCard(runtime);
  runtime.activeCardUid = uid;
  if (access_lock::isLockCard(config, uid)) {
    runtime.unlocked = true;
    runtime.state = restingState(runtime);
    return Presented::Unlock;
  }
  if (findBinding(bindings, uid) == nullptr) {
    runtime.lastUnknownCardUid = uid;
    runtime.state = DeviceState::UnknownCard;
    return Presented::Unknown;
  }
  return Presented::Play;
}

bool onCardRemoved(RuntimeState& runtime, CardBinding* binding, const Bookmark* stoppedAt) {
  runtime.resumeAfterSd = false;
  const bool saved = binding != nullptr && stoppedAt != nullptr;
  if (saved) {
    storeBookmark(*binding, *stoppedAt);
  }
  clearActiveCard(runtime);
  runtime.state = restingState(runtime);
  return saved;
}

bool onStoryFinished(RuntimeState& runtime, CardBinding* binding) {
  runtime.state = restingState(runtime);
  runtime.activePositionMs = 0;
  if (binding == nullptr) {
    return false;
  }
  storeBookmark(*binding, Bookmark());
  return true;
}

bool onSdLost(RuntimeState& runtime, CardBinding* binding, const Bookmark& stoppedAt) {
  if (binding == nullptr || runtime.activeCardUid.isEmpty()) {
    return false;
  }
  storeBookmark(*binding, stoppedAt);
  runtime.resumeAfterSd = true;
  return true;
}

bool shouldResumeAfterSd(RuntimeState& runtime, bool wasReady) {
  if (wasReady || !runtime.sdReady || !runtime.resumeAfterSd) {
    return false;
  }
  runtime.resumeAfterSd = false;
  return !runtime.activeCardUid.isEmpty();
}

bool shouldPlayNewlyBound(const RuntimeState& runtime, const std::vector<CardBinding>& bindings) {
  return runtime.state == DeviceState::UnknownCard && !runtime.activeCardUid.isEmpty() &&
         findBinding(bindings, runtime.activeCardUid) != nullptr;
}

DeviceState restingState(const RuntimeState& runtime) {
  return runtime.sdReady && runtime.nfcReady ? DeviceState::Ready : DeviceState::Error;
}

CardBinding* findBinding(std::vector<CardBinding>& bindings, const String& uid) {
  for (CardBinding& binding : bindings) {
    if (binding.uid == uid) {
      return &binding;
    }
  }
  return nullptr;
}

const CardBinding* findBinding(const std::vector<CardBinding>& bindings, const String& uid) {
  for (const CardBinding& binding : bindings) {
    if (binding.uid == uid) {
      return &binding;
    }
  }
  return nullptr;
}

String formatUid(const uint8_t* uid, uint8_t length) {
  static const char kHex[] = "0123456789ABCDEF";
  String result;
  result.reserve(length * 2);
  for (uint8_t index = 0; index < length; ++index) {
    result += kHex[uid[index] >> 4];
    result += kHex[uid[index] & 0x0F];
  }
  return result;
}

}  // namespace card_flow
