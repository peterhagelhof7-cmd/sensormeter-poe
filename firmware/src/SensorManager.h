#pragma once

#include <Arduino.h>
#include "ConfigManager.h"
#include "DataManager.h"

// Sensor-Auslesung (Pflichtenheft-Task "SensorTask"). Anders als beim
// Sensormeter-Projekt (WT32-ETH01, dort DHT11 intern) nutzen hier BEIDE
// Sensorpositionen DHT-22 (siehe docs/lastenheft.txt Abschnitt 2,
// "Abweichungen gegenueber Sensormeter"). Sensor 1 (intern) ist immer aktiv,
// Sensor 2 (extern, RJ45) nur wenn per ConfigManager aktiviert (manuell oder
// per SensorDetector-Autoerkennung). Takt: 60s. Stuendliche Werte des
// internen Sensors wandern zusaetzlich in den 7-Tage-Ringpuffer des
// DataManager (Web-Graph).

class SensorManager {
 public:
  SensorManager(DataManager& dataManager, ConfigManager& configManager);

  void begin();
  void loop();

 private:
  DataManager& _data;
  ConfigManager& _config;

  unsigned long _lastReadMillis = 0;
  long _lastRecordedHour = -1;

  void readInternalSensor();
  void readExternalSensorIfEnabled();
};
