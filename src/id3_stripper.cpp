// SPDX-License-Identifier: GPL-3.0-or-later
#include "id3_stripper.h"

#include <algorithm>
#include <cstring>

void Id3Stripper::reset(bool enabled) {
  enabled_ = enabled;
  phase_ = enabled ? Phase::Header : Phase::Body;
  headerLength_ = 0;
  skipRemaining_ = 0;
  tailLength_ = 0;
  strippedBytes_ = 0;
}

bool Id3Stripper::feed(const uint8_t* data, size_t length, const Sink& sink) {
  while (length > 0) {
    switch (phase_) {
      case Phase::Header: {
        const size_t count = std::min(kHeaderSize - headerLength_, length);
        memcpy(header_ + headerLength_, data, count);
        headerLength_ += count;
        data += count;
        length -= count;
        if (headerLength_ < kHeaderSize) {
          break;
        }

        // En-tête ID3v2: "ID3", version, révision, flags, taille sur 4 octets "syncsafe" (7 bits utiles).
        const bool isId3v2 = header_[0] == 'I' && header_[1] == 'D' && header_[2] == '3' &&
                             header_[6] < 0x80 && header_[7] < 0x80 && header_[8] < 0x80 && header_[9] < 0x80;
        headerLength_ = 0;
        if (!isId3v2) {
          phase_ = Phase::Body;
          if (!emit(header_, kHeaderSize, sink)) {
            return false;
          }
          break;
        }

        const uint32_t tagSize = (static_cast<uint32_t>(header_[6]) << 21) | (static_cast<uint32_t>(header_[7]) << 14) |
                                 (static_cast<uint32_t>(header_[8]) << 7) | header_[9];
        const bool hasFooter = (header_[5] & 0x10) != 0;
        skipRemaining_ = tagSize + (hasFooter ? kHeaderSize : 0);
        strippedBytes_ += kHeaderSize + skipRemaining_;
        // Un fichier peut enchaîner plusieurs tags: on revérifie l'en-tête après chacun.
        phase_ = skipRemaining_ > 0 ? Phase::Skip : Phase::Header;
        break;
      }

      case Phase::Skip: {
        const size_t count = std::min<size_t>(skipRemaining_, length);
        skipRemaining_ -= count;
        data += count;
        length -= count;
        if (skipRemaining_ == 0) {
          phase_ = Phase::Header;
        }
        break;
      }

      case Phase::Body:
        return emit(data, length, sink);
    }
  }
  return true;
}

bool Id3Stripper::finish(const Sink& sink) {
  // Fichier plus court qu'un en-tête: on le garde tel quel.
  if (phase_ == Phase::Header && headerLength_ > 0) {
    phase_ = Phase::Body;
    if (!emit(header_, headerLength_, sink)) {
      return false;
    }
    headerLength_ = 0;
  }

  if (tailLength_ == kId3v1Size && tail_[0] == 'T' && tail_[1] == 'A' && tail_[2] == 'G') {
    strippedBytes_ += kId3v1Size;
    tailLength_ = 0;
    return true;
  }

  const bool ok = tailLength_ == 0 || sink(tail_, tailLength_);
  tailLength_ = 0;
  return ok;
}

uint32_t Id3Stripper::strippedBytes() const {
  return strippedBytes_;
}

bool Id3Stripper::emit(const uint8_t* data, size_t length, const Sink& sink) {
  if (!enabled_) {
    return sink(data, length);
  }

  // On retient toujours les 128 derniers octets: ils peuvent être un tag ID3v1, connu seulement en fin de flux.
  const size_t total = tailLength_ + length;
  if (total <= kId3v1Size) {
    memcpy(tail_ + tailLength_, data, length);
    tailLength_ = total;
    return true;
  }

  const size_t outCount = total - kId3v1Size;
  const size_t fromTail = std::min(outCount, tailLength_);
  const size_t fromData = outCount - fromTail;
  if (fromTail > 0 && !sink(tail_, fromTail)) {
    return false;
  }
  if (fromData > 0 && !sink(data, fromData)) {
    return false;
  }

  memmove(tail_, tail_ + fromTail, tailLength_ - fromTail);
  tailLength_ -= fromTail;
  memcpy(tail_ + tailLength_, data + fromData, length - fromData);
  tailLength_ += length - fromData;
  return true;
}
