#include "SensorManager.h"

#include <Adafruit_AHTX0.h>
#include <Adafruit_BME280.h>
#include <DHT.h>
#include <Wire.h>
#include "TimeUtils.h"
#include "pins.h"

static const unsigned long READ_INTERVAL_MS = 60UL * 1000UL;  // 1x je Minute (Lastenheft)

// Plausibilitaetsgrenzen laut DHT-22-Datenblatt - gilt hier fuer BEIDE
// Sensorpositionen (anders als beim Sensormeter-Projekt, siehe pins.h).
static bool plausibleDht22(float t, float h) {
  return t >= -40.0f && t <= 80.0f && h >= 0.0f && h <= 100.0f;
}
// BME280/AHT20/AHT21 haben laut Datenblatt beide denselben nutzbaren
// Bereich (-40..85 C, 0..100 %rH) - eine gemeinsame Grenze reicht.
static bool plausibleI2cTempHum(float t, float h) {
  return t >= -40.0f && t <= 85.0f && h >= 0.0f && h <= 100.0f;
}

static DHT dhtInternal(PIN_DHT_INTERNAL, DHT22);
static DHT dhtExternal(PIN_DHT_EXTERNAL, DHT22);
static Adafruit_BME280 bme280External;
static Adafruit_AHTX0 aht20External;

SensorManager::SensorManager(DataManager& dataManager, ConfigManager& configManager, SensorDetector& sensorDetector)
    : _data(dataManager), _config(configManager), _detector(sensorDetector) {}

void SensorManager::begin() {
  // Wire.begin() ist auf dem ESP32 idempotent - unproblematisch, dass
  // SensorDetector/DisplayManager denselben Bus mit denselben Pins
  // ebenfalls starten, unabhaengig von der Aufrufreihenfolge.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
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

  // I2C-Zweige zuerst und UNABHAENGIG von cfg.pin5Mode - ein I2C-Sensor
  // (SCL/SDA) und ein Kontakt-Modul auf Pin 5 koennen gleichzeitig gesteckt
  // sein (unterschiedliche Pins, siehe module-design/README.md). Nur der
  // DHT-Zweig unten braucht Pin 5 exklusiv und wird entsprechend geprueft.
  String chip = _detector.detectedChipName();
  if (_detector.detectedType() == ModuleType::I2C_SENSOR_KNOWN) {
    if (chip == "BME280") {
      readExternalBme280(_detector.detectedI2cAddress());
      return;
    }
    if (chip == "AHT20/AHT21") {
      readExternalAht20();
      return;
    }
    // BH1750, SHT30/31/35, CCS811: erkannt, aber kein I2C-Lesepfad - passen
    // nicht ins Temperatur/Feuchte-Datenmodell von "Sensor 2" (siehe
    // Klassenkommentar). Bewusst kein Log-Eintrag - kein Fehler, sondern
    // dokumentierte Grenze.
    return;
  }
  if (_detector.detectedType() == ModuleType::I2C_SENSOR_UNKNOWN) return;

  if (cfg.pin5Mode != "sensor") return;  // Pin 5 als Kontakt-Eingang belegt
  readExternalDht();
}

void SensorManager::readExternalDht() {
  const DeviceConfig& cfg = _config.getConfig();
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

void SensorManager::readExternalBme280(uint8_t address) {
  const DeviceConfig& cfg = _config.getConfig();
  SensorReading reading;
  reading.lastReadMillis = millis();

  // begin() erneut aufzurufen kostet bei 60s-Takt keine spuerbare Zeit
  // (einzelner I2C-Chip-ID-Check + Konfigschreiben) und macht einen
  // zwischenzeitlichen Adresswechsel (z.B. nach erneuter Erkennung mit
  // anderem SDO-Pin-Stand) ohne zusaetzlichen Zustand robust.
  if (!bme280External.begin(address, &Wire)) {
    _data.pushLogEntry("Sensor extern: BME280 (0x" + String(address, HEX) + ") nicht erreichbar", 3);
    _data.setSensor2(reading);
    return;
  }

  float temperature = bme280External.readTemperature();
  float humidity = bme280External.readHumidity();
  if (isnan(temperature) || isnan(humidity)) {
    _data.pushLogEntry("Sensor extern: Fehler beim Lesen des BME280", 3);
    _data.setSensor2(reading);
    return;
  }
  if (!plausibleI2cTempHum(temperature, humidity)) {
    _data.pushLogEntry("Sensor extern: unplausibler Wert verworfen", 3);
    _data.setSensor2(reading);
    return;
  }

  temperature += cfg.sensor2TempOffset;
  humidity += cfg.sensor2HumOffset;
  if (humidity < 0.0f) humidity = 0.0f;
  if (humidity > 100.0f) humidity = 100.0f;

  reading.temperature = temperature;
  reading.humidity = humidity;
  reading.valid = true;
  _data.setSensor2(reading);
}

void SensorManager::readExternalAht20() {
  const DeviceConfig& cfg = _config.getConfig();
  SensorReading reading;
  reading.lastReadMillis = millis();

  if (!aht20External.begin(&Wire)) {
    _data.pushLogEntry("Sensor extern: AHT20/AHT21 nicht erreichbar", 3);
    _data.setSensor2(reading);
    return;
  }

  sensors_event_t humidityEvent, temperatureEvent;
  aht20External.getEvent(&humidityEvent, &temperatureEvent);
  float temperature = temperatureEvent.temperature;
  float humidity = humidityEvent.relative_humidity;
  if (isnan(temperature) || isnan(humidity)) {
    _data.pushLogEntry("Sensor extern: Fehler beim Lesen des AHT20/AHT21", 3);
    _data.setSensor2(reading);
    return;
  }
  if (!plausibleI2cTempHum(temperature, humidity)) {
    _data.pushLogEntry("Sensor extern: unplausibler Wert verworfen", 3);
    _data.setSensor2(reading);
    return;
  }

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
