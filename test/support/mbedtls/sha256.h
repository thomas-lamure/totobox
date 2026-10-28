// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SHA-256 portable (FIPS 180-4) pour les tests sur ordinateur: même signature que mbedtls_sha256_ret()
// d'ESP-IDF, utilisée par access_lock. Seul le mode SHA-256 (is224 = 0) est pris en charge.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace native_sha256 {

constexpr uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotateRight(uint32_t value, uint32_t bits) {
  return (value >> bits) | (value << (32 - bits));
}

inline void compressBlock(uint32_t state[8], const unsigned char block[64]) {
  uint32_t schedule[64];
  for (int i = 0; i < 16; ++i) {
    schedule[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
                  (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotateRight(schedule[i - 15], 7) ^ rotateRight(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
    const uint32_t s1 = rotateRight(schedule[i - 2], 17) ^ rotateRight(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
    schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
    const uint32_t choice = (e & f) ^ (~e & g);
    const uint32_t temp1 = h + s1 + choice + kRoundConstants[i] + schedule[i];
    const uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

}  // namespace native_sha256

inline int mbedtls_sha256_ret(const unsigned char* input, size_t length, unsigned char output[32], int is224) {
  if (is224 != 0) {
    return -1;
  }
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

  size_t offset = 0;
  for (; offset + 64 <= length; offset += 64) {
    native_sha256::compressBlock(state, input + offset);
  }

  // Bourrage: 0x80, des zéros, puis la longueur en bits sur 64 bits (gros-boutiste).
  unsigned char tail[128] = {0};
  const size_t remaining = length - offset;
  if (remaining > 0) {
    std::memcpy(tail, input + offset, remaining);
  }
  tail[remaining] = 0x80;
  const size_t tailLength = remaining < 56 ? 64 : 128;
  const uint64_t bitLength = static_cast<uint64_t>(length) * 8;
  for (int i = 0; i < 8; ++i) {
    tail[tailLength - 1 - i] = static_cast<unsigned char>(bitLength >> (8 * i));
  }
  for (size_t block = 0; block < tailLength; block += 64) {
    native_sha256::compressBlock(state, tail + block);
  }

  for (int i = 0; i < 8; ++i) {
    output[i * 4] = static_cast<unsigned char>(state[i] >> 24);
    output[i * 4 + 1] = static_cast<unsigned char>(state[i] >> 16);
    output[i * 4 + 2] = static_cast<unsigned char>(state[i] >> 8);
    output[i * 4 + 3] = static_cast<unsigned char>(state[i]);
  }
  return 0;
}
