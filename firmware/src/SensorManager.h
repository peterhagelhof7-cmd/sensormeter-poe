#pragma once

#include <Arduino.h>
#include "ConfigManager.h"
#include "DataManager.h"
#include "SensorDetector.h"

// Sensor-Auslesung (Pflichtenheft-Task "SensorTask"). Anders als beim
// Sensormeter-Projekt (WT32-ETH01, dort DHT11 intern) nutzen hier BEIDE
// Sensorpositionen DHT-22 (siehe docs/lastenheft.txt Abschnitt 2,
// "Abweichungen gegenueber Sensormeter"). Sensor 1 (intern) ist immer aktiv,
// Sensor 2 (extern, RJ45) nur wenn per ConfigManager aktiviert (manuell oder
// per SensorDetector-Autoerkennung). Takt: 60s. Stuendliche Werte des
// internen Sensors wandern zusaetzlich in den 7-Tage-Ringpuffer des
// DataManager (Web-Graph).
//
// "Sensor 2" (extern) liest je nach von SensorDetector erkanntem Chiptyp
// einen von drei Wegen: DHT22 auf Pin 5 (Default/Fallback, wie bisher),
// BME280 oder AHT20/AHT21 per I2C - siehe docs/entscheidungen.md
// "I2C-Lesepfad fuer Sensor 2". BH1750 (Lux) und CCS811 (eCO2/TVOC) werden
// von SensorDetector zwar erkannt, aber NICHT gelesen - beide passen nicht
// in das Temperatur/Feuchte-Datenmodell von "Sensor 2" (siehe
// sensormeter-family/repo/module-design/README.md, Abschnitt
// "Firmware-Luecke").

class SensorManager {
 public:
  SensorManager(DataManager& dataManager, ConfigManager& configManager, SensorDetector& sensorDetector);

  void begin();
  void loop();

 private:
  DataManager& _data;
  ConfigManager& _config;
  SensorDetector& _detector;

  unsigned long _lastReadMillis = 0;
  long _lastRecordedHour = -1;

  void readInternalSensor();
  void readExternalSensorIfEnabled();
  void readExternalDht();
  void readExternalBme280(uint8_t address);
  void readExternalAht20();
};
