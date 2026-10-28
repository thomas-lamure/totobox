// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// Retire à la volée les tags ID3v2 (début de fichier, pochette incluse) et ID3v1
// (128 derniers octets) d'un flux MP3, sans tamponner le fichier entier.
// Usage: reset(), puis feed() pour chaque bloc reçu, puis finish().
class Id3Stripper {
 public:
  using Sink = std::function<bool(const uint8_t* data, size_t length)>;

  void reset(bool enabled);
  bool feed(const uint8_t* data, size_t length, const Sink& sink);
  bool finish(const Sink& sink);
  uint32_t strippedBytes() const;

 private:
  enum class Phase { Header, Skip, Body };

  static constexpr size_t kHeaderSize = 10;
  static constexpr size_t kId3v1Size = 128;

  bool emit(const uint8_t* data, size_t length, const Sink& sink);

  bool enabled_ = false;
  Phase phase_ = Phase::Body;
  uint8_t header_[kHeaderSize] = {0};
  size_t headerLength_ = 0;
  uint32_t skipRemaining_ = 0;
  uint8_t tail_[kId3v1Size] = {0};
  size_t tailLength_ = 0;
  uint32_t strippedBytes_ = 0;
};
