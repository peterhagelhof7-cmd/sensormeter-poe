#include "SensorManager.h"

#include <DHT.h>
#include "TimeUtils.h"
#include "pins.h"

static const unsigned long READ_INTERVAL_MS = 60UL * 1000UL;  // 1x je Minute (Lastenheft)

// Plausibilitaetsgrenzen laut DHT-22-Datenblatt - gilt hier fuer BEIDE
// Sensorpositionen (anders als beim Sensormeter-Projekt, siehe pins.h).
static bool plausibleDht22(float t, float h) {
  return t >= -40.0f && t <= 80.0f && h >= 0.0f && h <= 100.0f;
}

static DHT dhtInternal(PIN_DHT_INTERNAL, DHT22);
static DHT dhtExternal(PIN_DHT_EXTERNAL, DHT22);

SensorManager::SensorManager(DataManager& dataManager, ConfigManager& configManager)
    : _data(dataManager), _config(configManager) {}

void SensorManager::begin() {
  dhtInternal.begin();
  dhtExternal.begin();
  Serial.println("[SENSOR] DHT-22 (intern) initialisiert, DHT-22 (extern) bereit");
}

void SensorManager::readInternalSensor() {
  float humidity = dhtInternal.readHumidity();
  float temperature = dhtInternal.readTemperature();

  SensorReading reading;
  reading.lastReadMillis = millis();

  if (isnan(humidity) || isnan(temperature)) {
    _data.pushLogEntry("Sensor intern: Fehler beim Lesen des DHT-22", 3);
    _data.setSensor1(reading);  // reading.valid bleibt false
    return;
  }
  if (!plausibleDht22(temperature, humidity)) {
    _data.pushLogEntry("Sensor intern: unplausibler Wert verworfen", 3);
    _data.setSensor1(reading);
    return;
  }

  // Kalibrierkorrektur erst NACH der Plausibilitaetspruefung auf den
  // Rohmesswert anwenden - sie ist eine kleine Kalibrierkonstante, kein
  // Mittel zur Fehlerkompensation, und soll den Garbage-Filter nicht
  // verfaelschen. Wirkt dadurch auf Anzeige, SNMP und Stundenwerte/CSV
  // gleichermassen, da alle denselben DataManager-Wert lesen.
  const DeviceConfig& cfg = _config.getConfig();
  temperature += cfg.sensor1TempOffset;
  humidity += cfg.sensor1HumOffset;
  if (humidity < 0.0f) humidity = 0.0f;
  if (humidity > 100.0f) humidity = 100.0f;

  reading.temperature = temperature;
  reading.humidity = humidity;
  reading.valid = true;
  _data.setSensor1(reading);

  // Stuendliche Ringpuffer-Speicherung - nur wenn Zeit per NTP synchronisiert
  // ist.
  if (!isTimeSynced()) return;

  time_t now = time(nullptr);
  long hourIndex = now / 3600;
  if (hourIndex != _lastRecordedHour) {
    HourValue hv;
    hv.timestamp = now;
    hv.temperature = temperature;
    hv.humidity = humidity;
    _data.pushHourValue(hv);
    _lastRecordedHour = hourIndex;
  }
}

void SensorManager::readExternalSensorIfEnabled() {
  const DeviceConfig& cfg = _config.getConfig();
  if (!cfg.sensor2Enabled) return;
  if (cfg.pin5Mode != "sensor") return;  // Pin 5 als Kontakt-Eingang belegt

  float humidity = dhtExternal.readHumidity();
  float temperature = dhtExternal.readTemperature();

  SensorReading reading;
  reading.lastReadMillis = millis();

  if (isnan(humidity) || isnan(temperature)) {
    _data.pushLogEntry("Sensor extern: Fehler beim Lesen des DHT-22", 3);
    _data.setSensor2(reading);
    return;
  }
  if (!plausibleDht22(temperature, humidity)) {
    _data.pushLogEntry("Sensor extern: unplausibler Wert verworfen", 3);
    _data.setSensor2(reading);
    return;
  }

  // Siehe Kommentar in readInternalSensor() - Korrektur erst nach der
  // Plausibilitaetspruefung auf den Rohmesswert.
  temperature += cfg.sensor2TempOffset;
  humidity += cfg.sensor2HumOffset;
  if (humidity < 0.0f) humidity = 0.0f;
  if (humidity > 100.0f) humidity = 100.0f;

  reading.temperature = temperature;
  reading.humidity = humidity;
  reading.valid = true;
  _data.setSensor2(reading);
}

void SensorManager::loop() {
  // Erster Durchlauf (_lastReadMillis == 0): sofort lesen statt 60s zu warten.
  if (_lastReadMillis != 0 && millis() - _lastReadMillis < READ_INTERVAL_MS) return;
  _lastReadMillis = millis();

  readInternalSensor();
  readExternalSensorIfEnabled();
}
