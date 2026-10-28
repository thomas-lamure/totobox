// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

// Logs série partagés entre loop() et la tâche audio: chaque ligne est formatée en entier puis écrite
// d'un seul bloc sous mutex, pour que les lignes des deux tâches ne s'entremêlent pas.
// Format: "[   12.345] [tag] message".
void logBegin();
void logLine(const char* tag, const char* format, ...) __attribute__((format(printf, 2, 3)));
