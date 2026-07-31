#include "SensorManager.h"

#include <Adafruit_AHTX0.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <BH1750.h>
#include <DHT.h>
#include <ScioSense_ENS160.h>
#include <Wire.h>
#include "TimeUtils.h"
#include "pins.h"

static const unsigned long READ_INTERVAL_MS = 60UL * 1000UL;  // 1x je Minute (Lastenheft)

// Plausibilitaetsgrenzen laut DHT-22-Datenblatt - gilt fuer den internen
// Sensor (fest verbaut, siehe pins.h - anders als beim Sensormeter-Projekt,
// wo intern ein DHT11 sitzt).
static bool plausibleDht22(float t, float h) {
  return t >= -40.0f && t <= 80.0f && h >= 0.0f && h <= 100.0f;
}
// Plausibilitaetsgrenzen fuer den externen RJ45-Anschluss - dort steckt
// wahlweise ein DHT11- oder DHT21-Modul (siehe ConfigManager::pin5DhtType),
// beide mit eigenem, engerem Datenblattbereich als der interne DHT22.
static bool plausibleDht11(float t, float h) {
  return t >= 0.0f && t <= 50.0f && h >= 20.0f && h <= 95.0f;
}
static bool plausibleDht21(float t, float h) {
  return t >= -40.0f && t <= 80.0f && h >= 0.0f && h <= 100.0f;
}
// BME280/AHT20/AHT21 haben laut Datenblatt beide denselben nutzbaren
// Bereich (-40..85 C, 0..100 %rH) - eine gemeinsame Grenze reicht.
static bool plausibleI2cTempHum(float t, float h) {
  return t >= -40.0f && t <= 85.0f && h >= 0.0f && h <= 100.0f;
}
// BMP280-Datenblatt: 300-1100 hPa nutzbarer Druckbereich.
static bool plausiblePressure(float hpa) {
  return hpa >= 300.0f && hpa <= 1100.0f;
}
// BH1750 liefert laut Bibliothek bei Fehler -1.0/-2.0, gueltige Werte sind
// immer >= 0 (0 lx = voellige Dunkelheit ist ein gueltiger Messwert).
static bool plausibleLux(float lux) {
  return lux >= 0.0f;
}
// ENS160-Datenblatt: eCO2 400-65000 ppm nutzbarer Bereich.
static bool plausibleEco2(float ppm) {
  return ppm >= 400.0f && ppm <= 65000.0f;
}

static DHT dhtInternal(PIN_DHT_INTERNAL, DHT22);
// Zwei DHT-Objekte auf demselben externen Pin statt einem - der Adafruit-
// DHT-Treiber bindet den Bibliothekstyp am Konstruktor, laesst sich also
// nicht zur Laufzeit umschalten. Ungenutzt bleibt das jeweils andere
// Objekt harmlos (reines GPIO-Bit-Banging, kein dauerhaft belegter Zustand
// zwischen Lesungen) - siehe ConfigManager::pin5DhtType.
static DHT dhtExternalDht11(PIN_DHT_EXTERNAL, DHT11);
static DHT dhtExternalDht21(PIN_DHT_EXTERNAL, DHT21);
static Adafruit_BME280 bme280External;
static Adafruit_AHTX0 aht20External;
static Adafruit_BMP280 bmp280External(&Wire);
static BH1750 bh1750External;

SensorManager::SensorManager(DataManager& dataManager, ConfigManager& configManager, SensorDetector& sensorDetector)
    : _data(dataManager), _config(configManager), _detector(sensorDetector) {}

void SensorManager::begin() {
  // Wire.begin() ist auf dem ESP32 idempotent - unproblematisch, dass
  // SensorDetector/DisplayManager denselben Bus mit denselben Pins
  // ebenfalls starten, unabhaengig von der Aufrufreihenfolge.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  dhtInternal.begin();
  dhtExternalDht11.begin();
  dhtExternalDht21.begin();
  Serial.println("[SENSOR] DHT-22 (intern) initialisiert, DHT11/DHT21 (extern) bereit");

  // _lastRecordedHour lebt nur im RAM und startet bei jedem Boot auf -1 -
  // ohne diesen Abgleich wuerde der erste Sensor-Read nach JEDEM Neustart
  // (geplanter Reboot, WLAN-Watchdog, OTA) einen Ringpuffer-Eintrag fuer
  // eine Stunde erzeugen, die schon vor dem Neustart aufgezeichnet wurde -
  // ein Duplikat mit neu gemessenen Werten statt eines echten
  // Stundenwechsels. loadRingbuffer() laeuft in main.cpp vor begin(), der
  // zuletzt gespeicherte Eintrag ist hier also schon verfuegbar.
  HourValue letzte[DataManager::RINGBUFFER_SIZE];
  size_t anzahl = _data.getRingbuffer(letzte, DataManager::RINGBUFFER_SIZE);
  if (anzahl > 0) {
    _lastRecordedHour = letzte[anzahl - 1].timestamp / 3600;
  }
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
    uint8_t addr = _detector.detectedI2cAddress();
    if (chip == "BME280") {
      readExternalBme280(addr);
      return;
    }
    if (chip == "BMP280") {
      readExternalBmp280(addr);
      return;
    }
    if (chip == "AHT20/AHT21") {
      readExternalAht20();
      return;
    }
    if (chip == "BH1750") {
      readExternalBh1750(addr);
      return;
    }
    if (chip == "ENS160") {
      readExternalEns160(addr);
      return;
    }
    // SHT30/31/35: erkannt, aber kein I2C-Lesepfad - kein Familienmodul
    // dafuer entworfen (siehe sensormeter-family/repo/module-design/).
    // Bewusst kein Log-Eintrag - kein Fehler, sondern dokumentierte Grenze.
    return;
  }
  if (_detector.detectedType() == ModuleType::I2C_SENSOR_UNKNOWN) return;

  if (cfg.pin5Mode != "sensor") return;  // Pin 5 als Kontakt-Eingang belegt
  readExternalDht();
}

void SensorManager::readExternalDht() {
  const DeviceConfig& cfg = _config.getConfig();
  bool useDht21 = (cfg.pin5DhtType == "DHT21");
  DHT& dht = useDht21 ? dhtExternalDht21 : dhtExternalDht11;

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  SensorReading reading;
  reading.lastReadMillis = millis();

  if (isnan(humidity) || isnan(temperature)) {
    _data.pushLogEntry("Sensor extern: Fehler beim Lesen des " + cfg.pin5DhtType, 3);
    _data.setSensor2(reading);
    return;
  }
  bool plausible = useDht21 ? plausibleDht21(temperature, humidity) : plausibleDht11(temperature, humidity);
  if (!plausible) {
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

// Liest NUR den Luftdruck - kein Temperatur/Feuchte-Wert im Sensor-2-Sinn
// (BMP280 misst zwar auch Temperatur, aber nur als interne Kompensationsgroesse,
// nicht kalibriert genug fuer die Anzeige; siehe module-design/bmp280-modul.md).
// Adafruit_BMP280::begin() prueft die Chip-ID selbst (Default 0x58) und
// schlaegt für ein tatsaechliches BME280 (0x60) sauber fehl - SensorDetector
// hat die beiden Chips vorher bereits per Chip-ID-Register unterschieden
// (siehe SensorDetector.cpp), diese Pruefung hier ist die zweite,
// unabhaengige Absicherung.
void SensorManager::readExternalBmp280(uint8_t address) {
  SensorReading reading;
  reading.lastReadMillis = millis();

  if (!bmp280External.begin(address)) {
    _data.pushLogEntry("Sensor extern: BMP280 (0x" + String(address, HEX) + ") nicht erreichbar", 3);
    _data.setSensor2(reading);
    return;
  }

  float pressurePa = bmp280External.readPressure();
  if (isnan(pressurePa)) {
    _data.pushLogEntry("Sensor extern: Fehler beim Lesen des BMP280", 3);
    _data.setSensor2(reading);
    return;
  }
  float pressureHpa = pressurePa / 100.0f;
  if (!plausiblePressure(pressureHpa)) {
    _data.pushLogEntry("Sensor extern: unplausibler Druckwert verworfen", 3);
    _data.setSensor2(reading);
    return;
  }

  // Keine Kalibrier-Offset-Anwendung wie bei Temperatur/Feuchte (siehe
  // sensor2TempOffset/-HumOffset) - fuer Luftdruck waere eine Korrektur
  // typischerweise hoehenabhaengig (Normalnull-Bezug), nicht nur ein
  // fester Chip-Offset; bewusst nicht Teil dieser ersten Umsetzung.
  reading.pressureHpa = pressureHpa;
  reading.valid = true;
  _data.setSensor2(reading);
}

// Liest NUR die Helligkeit - BH1750 hat keinen Temperatur/Feuchte-Ausgang.
void SensorManager::readExternalBh1750(uint8_t address) {
  SensorReading reading;
  reading.lastReadMillis = millis();

  if (!bh1750External.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, address, &Wire)) {
    _data.pushLogEntry("Sensor extern: BH1750 (0x" + String(address, HEX) + ") nicht erreichbar", 3);
    _data.setSensor2(reading);
    return;
  }

  float lux = bh1750External.readLightLevel();
  if (!plausibleLux(lux)) {
    _data.pushLogEntry("Sensor extern: Fehler beim Lesen des BH1750", 3);
    _data.setSensor2(reading);
    return;
  }

  reading.lux = lux;
  reading.valid = true;
  _data.setSensor2(reading);
}

// Liest NUR die Luftguete (eCO2) - ENS160 hat keinen Temperatur/Feuchte-
// Ausgang im hier genutzten Sinn. Bewusste Vereinfachung: begin()+setMode()
// werden wie bei den anderen I2C-Sensoren bei jedem 60s-Zyklus neu
// aufgerufen (Konsistenz mit dem Rest dieser Datei) - ein Metalloxid-
// Gassensor braucht eigentlich eine laengere, durchgehende Aufwaermphase
// fuer eine stabile Basislinie; ob das wiederholte Neu-Initialisieren die
// Messqualitaet gegenueber einem dauerhaft laufenden Sensor verschlechtert,
// ist mangels echter Hardware noch nicht verifiziert.
void SensorManager::readExternalEns160(uint8_t address) {
  SensorReading reading;
  reading.lastReadMillis = millis();

  ScioSense_ENS160 ens160(&Wire, address);
  if (!ens160.begin() || !ens160.available() || !ens160.setMode(ENS160_OPMODE_STD)) {
    _data.pushLogEntry("Sensor extern: ENS160 (0x" + String(address, HEX) + ") nicht erreichbar", 3);
    _data.setSensor2(reading);
    return;
  }

  if (!ens160.measure(true)) {
    _data.pushLogEntry("Sensor extern: Fehler beim Lesen des ENS160", 3);
    _data.setSensor2(reading);
    return;
  }

  float eco2Ppm = ens160.geteCO2();
  if (!plausibleEco2(eco2Ppm)) {
    _data.pushLogEntry("Sensor extern: unplausibler Luftguete-Wert verworfen", 3);
    _data.setSensor2(reading);
    return;
  }

  reading.eco2Ppm = eco2Ppm;
  reading.valid = true;
  _data.setSensor2(reading);
}

void SensorManager::maybeRecordHourValue() {
  // Stuendliche Ringpuffer-Speicherung - nur wenn Zeit per NTP synchronisiert ist.
  if (!isTimeSynced()) return;

  time_t now = time(nullptr);
  long hourIndex = now / 3600;
  if (hourIndex == _lastRecordedHour) return;
  _lastRecordedHour = hourIndex;

  SensorReading s1 = _data.getSensor1();
  SensorReading s2 = _data.getSensor2();

  HourValue hv;
  hv.timestamp = now;
  if (s1.valid) {
    hv.sensor1Temperature = s1.temperature;
    hv.sensor1Humidity = s1.humidity;
  }
  if (s2.valid) {
    hv.sensor2Temperature = s2.temperature;
    hv.sensor2Humidity = s2.humidity;
    hv.sensor2PressureHpa = s2.pressureHpa;
    hv.sensor2Lux = s2.lux;
    hv.sensor2Eco2Ppm = s2.eco2Ppm;
  }
  _data.pushHourValue(hv);
}

void SensorManager::loop() {
  // Erster Durchlauf (_lastReadMillis == 0): sofort lesen statt 60s zu warten.
  if (_lastReadMillis != 0 && millis() - _lastReadMillis < READ_INTERVAL_MS) return;
  _lastReadMillis = millis();

  readInternalSensor();
  readExternalSensorIfEnabled();
  maybeRecordHourValue();
}
