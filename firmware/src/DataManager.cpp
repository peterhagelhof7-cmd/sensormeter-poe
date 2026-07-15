#include "DataManager.h"

#include <LittleFS.h>

namespace {
constexpr const char* kHistoryFile = "/history.csv";

// Leeres Feld -> NAN (String::toFloat() liefert bei leerem/ungueltigem Text
// 0.0, nicht NAN - das wuerde einen echten 0-Messwert von "Modul liefert
// dieses Feld nicht" nicht mehr unterscheidbar machen).
float parseFloatOrNan(const String& s) {
  if (s.length() == 0) return NAN;
  return s.toFloat();
}

// NAN -> leeres Feld, sonst 1 Nachkommastelle (wie bisher).
String formatFloatOrEmpty(float v) {
  if (isnan(v)) return String();
  return String(v, 1);
}
}  // namespace

void DataManager::begin() {
  _mutex = xSemaphoreCreateMutex();
}

SystemState DataManager::getSystemState() {
  SystemState state;
  xSemaphoreTake(_mutex, portMAX_DELAY);
  state = _systemState;
  xSemaphoreGive(_mutex);
  return state;
}

void DataManager::setSystemState(SystemState state) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  bool changed = (_systemState != state);
  _systemState = state;
  xSemaphoreGive(_mutex);
  if (changed) {
    Serial.printf("[STATE] -> %s\n", toString(state));
  }
}

SensorReading DataManager::getSensor1() {
  SensorReading reading;
  xSemaphoreTake(_mutex, portMAX_DELAY);
  reading = _sensor1;
  xSemaphoreGive(_mutex);
  return reading;
}

void DataManager::setSensor1(const SensorReading& reading) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  _sensor1 = reading;
  xSemaphoreGive(_mutex);
}

SensorReading DataManager::getSensor2() {
  SensorReading reading;
  xSemaphoreTake(_mutex, portMAX_DELAY);
  reading = _sensor2;
  xSemaphoreGive(_mutex);
  return reading;
}

void DataManager::setSensor2(const SensorReading& reading) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  _sensor2 = reading;
  xSemaphoreGive(_mutex);
}

void DataManager::pushHourValue(const HourValue& value) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  _ringbuffer[_ringbufferNextIndex] = value;
  _ringbufferNextIndex = (_ringbufferNextIndex + 1) % RINGBUFFER_SIZE;
  if (_ringbufferCount < RINGBUFFER_SIZE) _ringbufferCount++;
  xSemaphoreGive(_mutex);
  saveRingbuffer();
}

// Spaltenreihenfolge, siehe HourValue: timestamp,s1temp,s1hum,s2temp,s2hum,
// s2pressure,s2lux,s2eco2 - 8 Spalten, nicht anwendbare Felder leer statt 0
// (siehe formatFloatOrEmpty/parseFloatOrNan oben). Groessenrechnung (worst
// case ~60-70 Byte/Zeile * 168 Zeilen =~ 10-12 KB) siehe docs/entscheidungen.md.
void DataManager::saveRingbuffer() {
  HourValue buffer[RINGBUFFER_SIZE];
  size_t count = getRingbuffer(buffer, RINGBUFFER_SIZE);

  fs::File f = LittleFS.open(kHistoryFile, "w");
  if (!f) {
    Serial.println("[DATA] history.csv konnte nicht geschrieben werden");
    return;
  }
  for (size_t i = 0; i < count; i++) {
    const HourValue& hv = buffer[i];
    f.printf("%ld,%s,%s,%s,%s,%s,%s,%s\n", static_cast<long>(hv.timestamp),
              formatFloatOrEmpty(hv.sensor1Temperature).c_str(),
              formatFloatOrEmpty(hv.sensor1Humidity).c_str(),
              formatFloatOrEmpty(hv.sensor2Temperature).c_str(),
              formatFloatOrEmpty(hv.sensor2Humidity).c_str(),
              formatFloatOrEmpty(hv.sensor2PressureHpa).c_str(),
              formatFloatOrEmpty(hv.sensor2Lux).c_str(),
              formatFloatOrEmpty(hv.sensor2Eco2Ppm).c_str());
  }
  f.close();
}

void DataManager::loadRingbuffer() {
  fs::File f = LittleFS.open(kHistoryFile, "r");
  if (!f) return;

  xSemaphoreTake(_mutex, portMAX_DELAY);
  _ringbufferCount = 0;
  _ringbufferNextIndex = 0;
  while (f.available() && _ringbufferCount < RINGBUFFER_SIZE) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;

    // In 8 kommagetrennte Felder zerlegen - alte Zeilen mit nur 3 Feldern
    // (Format vor 2026-07-15) werden uebersprungen statt falsch interpretiert.
    String fields[8];
    int fieldCount = 0;
    int start = 0;
    while (fieldCount < 8) {
      int comma = line.indexOf(',', start);
      if (comma < 0) {
        fields[fieldCount++] = line.substring(start);
        break;
      }
      fields[fieldCount++] = line.substring(start, comma);
      start = comma + 1;
    }
    if (fieldCount != 8) continue;

    HourValue hv;
    hv.timestamp = static_cast<time_t>(fields[0].toInt());
    hv.sensor1Temperature = parseFloatOrNan(fields[1]);
    hv.sensor1Humidity = parseFloatOrNan(fields[2]);
    hv.sensor2Temperature = parseFloatOrNan(fields[3]);
    hv.sensor2Humidity = parseFloatOrNan(fields[4]);
    hv.sensor2PressureHpa = parseFloatOrNan(fields[5]);
    hv.sensor2Lux = parseFloatOrNan(fields[6]);
    hv.sensor2Eco2Ppm = parseFloatOrNan(fields[7]);

    _ringbuffer[_ringbufferNextIndex] = hv;
    _ringbufferNextIndex = (_ringbufferNextIndex + 1) % RINGBUFFER_SIZE;
    _ringbufferCount++;
  }
  xSemaphoreGive(_mutex);
  f.close();
  Serial.printf("[DATA] %u Ringpuffer-Eintraege aus history.csv geladen\n",
                static_cast<unsigned>(_ringbufferCount));
}

size_t DataManager::getRingbuffer(HourValue* out, size_t maxCount) {
  size_t count;
  xSemaphoreTake(_mutex, portMAX_DELAY);
  count = min(_ringbufferCount, maxCount);
  size_t startIndex = (_ringbufferNextIndex + RINGBUFFER_SIZE - _ringbufferCount) % RINGBUFFER_SIZE;
  for (size_t i = 0; i < count; i++) {
    out[i] = _ringbuffer[(startIndex + i) % RINGBUFFER_SIZE];
  }
  xSemaphoreGive(_mutex);
  return count;
}

void DataManager::pushLogEntry(const String& message, int severity) {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  _logSequenceCounter++;
  _log[_logNextIndex].timestamp = time(nullptr);
  _log[_logNextIndex].message = message;
  _log[_logNextIndex].severity = severity;
  _log[_logNextIndex].sequence = _logSequenceCounter;
  _logNextIndex = (_logNextIndex + 1) % LOG_CAPACITY;
  if (_logCount < LOG_CAPACITY) _logCount++;
  xSemaphoreGive(_mutex);
  Serial.printf("[LOG] %s\n", message.c_str());
}

size_t DataManager::getLogEntries(LogEntry* out, size_t maxCount) {
  size_t count;
  xSemaphoreTake(_mutex, portMAX_DELAY);
  count = min(_logCount, maxCount);
  // Neueste zuerst: rueckwaerts ab dem zuletzt geschriebenen Eintrag lesen.
  for (size_t i = 0; i < count; i++) {
    size_t index = (_logNextIndex + LOG_CAPACITY - 1 - i) % LOG_CAPACITY;
    out[i] = _log[index];
  }
  xSemaphoreGive(_mutex);
  return count;
}

size_t DataManager::getLogEntriesAfter(unsigned long afterSequence, LogEntry* out, size_t maxCount) {
  size_t count = 0;
  xSemaphoreTake(_mutex, portMAX_DELAY);
  size_t startIndex = (_logNextIndex + LOG_CAPACITY - _logCount) % LOG_CAPACITY;
  // Chronologisch (aeltester zuerst) durchgehen, damit die Reihenfolge beim
  // Versand erhalten bleibt.
  for (size_t i = 0; i < _logCount && count < maxCount; i++) {
    size_t index = (startIndex + i) % LOG_CAPACITY;
    if (_log[index].sequence > afterSequence) {
      out[count++] = _log[index];
    }
  }
  xSemaphoreGive(_mutex);
  return count;
}
