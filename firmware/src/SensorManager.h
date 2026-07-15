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
// einen von fuenf Wegen: DHT11/DHT21 auf Pin 5 (Typ ueber
// ConfigManager::pin5DhtType, siehe dortigen Kommentar - Auto-Erkennung
// DHT11 vs. DHT21 ist unzuverlaessig; betrifft NUR den externen RJ45-
// Anschluss, der interne DHT-22 oben bleibt fest verbaut/unveraendert),
// BME280/AHT20/AHT21/BMP280/BH1750/ENS160 per I2C - siehe
// docs/entscheidungen.md "Sensor-2-Datenmodell erweitert". Liefert IMMER
// hoechstens EIN Sensor-2-Ergebnis pro Zyklus (das von SensorDetector als
// "primaer" gemeldete Geraet, niedrigste I2C-Adresse bei mehreren
// Treffern) - echtes gleichzeitiges Lesen mehrerer gesteckter Module ist
// weiterhin nicht umgesetzt (siehe SensorDetector-Klassenkommentar).

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
  void readExternalBmp280(uint8_t address);
  void readExternalBh1750(uint8_t address);
  void readExternalEns160(uint8_t address);

  // Baut aus dem aktuellen Stand von DataManager::getSensor1()/getSensor2()
  // einen HourValue und speichert ihn 1x/Stunde in den Ringpuffer - erst
  // NACH readInternalSensor() UND readExternalSensorIfEnabled() aus loop()
  // aufgerufen, damit beide Sensoren denselben Zyklus abbilden (vorher war
  // das an readInternalSensor() drangehaengt und haette Sensor 2 immer eine
  // Lesung "hinterher gehinkt").
  void maybeRecordHourValue();
};
