#include "DataManager.h"

#include <LittleFS.h>

namespace {
constexpr const char* kHistoryFile = "/history.csv";
}

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

void DataManager::saveRingbuffer() {
  HourValue buffer[RINGBUFFER_SIZE];
  size_t count = getRingbuffer(buffer, RINGBUFFER_SIZE);

  fs::File f = LittleFS.open(kHistoryFile, "w");
  if (!f) {
    Serial.println("[DATA] history.csv konnte nicht geschrieben werden");
    return;
  }
  for (size_t i = 0; i < count; i++) {
    f.printf("%ld,%.2f,%.2f\n", static_cast<long>(buffer[i].timestamp), buffer[i].temperature,
              buffer[i].humidity);
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
    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);
    if (firstComma < 0 || secondComma < 0) continue;
    HourValue hv;
    hv.timestamp = static_cast<time_t>(line.substring(0, firstComma).toInt());
    hv.temperature = line.substring(firstComma + 1, secondComma).toFloat();
    hv.humidity = line.substring(secondComma + 1).toFloat();
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
