// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Remplacement minimal d'Arduino pour les tests sur ordinateur (pio test -e native).
// Ne couvre que ce qu'utilisent les modules testés (access_lock, config_store, app_types, request_guard, ArduinoJson).

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

class String {
 public:
  String() = default;
  String(const char* text) : value_(text != nullptr ? text : "") {}
  String(const std::string& text) : value_(text) {}
  explicit String(char c) : value_(1, c) {}

  String& operator=(const char* text) {
    value_ = text != nullptr ? text : "";
    return *this;
  }

  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.length(); }
  bool isEmpty() const { return value_.empty(); }
  void reserve(size_t size) { value_.reserve(size); }

  char operator[](size_t index) const { return index < value_.size() ? value_[index] : '\0'; }
  char& operator[](size_t index) { return value_[index]; }

  bool concat(const char* text) {
    if (text != nullptr) {
      value_ += text;
    }
    return true;
  }
  bool concat(char c) {
    value_ += c;
    return true;
  }

  String& operator+=(const String& other) {
    value_ += other.value_;
    return *this;
  }
  String& operator+=(const char* text) {
    concat(text);
    return *this;
  }
  String& operator+=(char c) {
    value_ += c;
    return *this;
  }

  void trim() {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value_.erase(value_.begin(), std::find_if(value_.begin(), value_.end(), notSpace));
    value_.erase(std::find_if(value_.rbegin(), value_.rend(), notSpace).base(), value_.end());
  }
  void toUpperCase() {
    std::transform(value_.begin(), value_.end(), value_.begin(), [](unsigned char c) { return std::toupper(c); });
  }
  void toLowerCase() {
    std::transform(value_.begin(), value_.end(), value_.begin(), [](unsigned char c) { return std::tolower(c); });
  }
  bool startsWith(const String& prefix) const { return value_.rfind(prefix.value_, 0) == 0; }
  bool endsWith(const String& suffix) const {
    return value_.size() >= suffix.value_.size() &&
           value_.compare(value_.size() - suffix.value_.size(), suffix.value_.size(), suffix.value_) == 0;
  }

  int indexOf(char c, size_t from = 0) const {
    const size_t found = value_.find(c, from);
    return found == std::string::npos ? -1 : static_cast<int>(found);
  }
  int indexOf(const char* text, size_t from = 0) const {
    const size_t found = value_.find(text, from);
    return found == std::string::npos ? -1 : static_cast<int>(found);
  }
  String substring(size_t from) const { return from < value_.size() ? String(value_.substr(from)) : String(); }
  String substring(size_t from, size_t to) const {
    if (to > value_.size()) to = value_.size();
    return from < to ? String(value_.substr(from, to - from)) : String();
  }
  void remove(size_t index) {
    if (index < value_.size()) value_.erase(index);
  }
  void remove(size_t index, size_t count) {
    if (index < value_.size()) value_.erase(index, count);
  }

  friend bool operator==(const String& left, const String& right) { return left.value_ == right.value_; }
  friend bool operator==(const String& left, const char* right) { return left.value_ == (right ? right : ""); }
  friend bool operator!=(const String& left, const String& right) { return !(left == right); }
  friend bool operator!=(const String& left, const char* right) { return !(left == right); }
  friend String operator+(String left, const String& right) { return left += right; }
  friend String operator+(String left, const char* right) { return left += right; }
  friend String operator+(const char* left, const String& right) { return String(left) += right; }

 private:
  std::string value_;
};

inline bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

// Horloge simulée: les tests la règlent avec nativeTestSetMillis().
inline unsigned long& nativeTestMillis() {
  static unsigned long now = 0;
  return now;
}
inline void nativeTestSetMillis(unsigned long now) {
  nativeTestMillis() = now;
}
inline unsigned long millis() {
  return nativeTestMillis();
}
