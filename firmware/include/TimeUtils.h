#pragma once

#include <time.h>

// Heuristik "ist die Systemzeit per NTP synchronisiert": Die ESP32-RTC
// startet nahe der Unix-Epoche 0, ein plausibles Datum liegt sicher nach
// dem 1.1.2001. Gemeinsam genutzt von TimeManager (Sync-Erkennung) und
// SensorManager (stuendliche Ringpuffer-Speicherung erfordert echte Zeit).
inline bool isTimeSynced() {
  return time(nullptr) > 978307200;
}
