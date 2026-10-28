// SPDX-License-Identifier: GPL-3.0-or-later
#include "logger.h"

#include <cstdarg>
#include <cstdio>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

constexpr size_t kLineMax = 256;
constexpr size_t kSerialTxBufferBytes = 4096;
// Si le moniteur série ne suit pas, on perd des lignes plutôt que de bloquer la tâche audio.
constexpr uint32_t kSerialTxTimeoutMs = 10;
constexpr TickType_t kLockTimeout = pdMS_TO_TICKS(20);

SemaphoreHandle_t mutex = nullptr;

}  // namespace

void logBegin() {
  Serial.setTxBufferSize(kSerialTxBufferBytes);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(kSerialTxTimeoutMs);
  if (mutex == nullptr) {
    mutex = xSemaphoreCreateMutex();
  }
}

void logLine(const char* tag, const char* format, ...) {
  char line[kLineMax];
  const uint32_t now = millis();
  int length = snprintf(line, sizeof(line), "[%6lu.%03lu] [%s] ", static_cast<unsigned long>(now / 1000),
                        static_cast<unsigned long>(now % 1000), tag);

  va_list args;
  va_start(args, format);
  vsnprintf(line + length, sizeof(line) - length, format, args);
  va_end(args);

  // Ligne tronquée si besoin, en gardant la place du retour à la ligne.
  length = strnlen(line, sizeof(line) - 3);
  line[length++] = '\r';
  line[length++] = '\n';

  if (mutex != nullptr && xSemaphoreTake(mutex, kLockTimeout) != pdTRUE) {
    return;
  }
  Serial.write(reinterpret_cast<const uint8_t*>(line), length);
  if (mutex != nullptr) {
    xSemaphoreGive(mutex);
  }
}
