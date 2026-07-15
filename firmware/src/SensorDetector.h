#pragma once

#include <Arduino.h>
#include "ConfigManager.h"
#include "DataManager.h"

// Automatische Erkennung des RJ45-Steckmoduls (Lastenheft Abschnitt 15,
// Pflichtenheft 3.8). Laeuft einmal beim Boot (waehrend des Countdowns,
// parallel zur Netzwerk-Wartezeit - siehe main.cpp) sowie erneut auf
// Anfrage ueber die Einstellungsseite ("Erkennung neu starten"). Erkennt
// NUR die Modul-KATEGORIE (DHT-Sensor / I2C-Sensor inkl. konkretem Chip bei
// bekannter Adresse / kein Sensor) - AUSDRUECKLICH NICHT DHT-11 vs. DHT-22
// (unzuverlaessig, siehe Lastenheft 15.4) und NICHT das Vorhandensein eines
// Relais-Moduls (kein auswertbares Rueck-Signal, siehe Lastenheft 15.3).

enum class ModuleType {
  NONE,
  DHT_SENSOR,
  I2C_SENSOR_KNOWN,
  I2C_SENSOR_UNKNOWN,
};

// Ein einzelner I2C-Treffer aus einem vollstaendigen Bus-Sweep (siehe
// scanI2cBus()) - Adresse plus Chipname, falls bekannt ("" sonst).
struct I2cHit {
  uint8_t address = 0;
  String chipName;
};

class SensorDetector {
 public:
  SensorDetector(DataManager& dataManager, ConfigManager& configManager);

  void begin();

  // Fuehrt den eigentlichen Scan durch (I2C-Scan ueber den GESAMTEN
  // Adressraum 0x08-0x77, bei Fehlschlag DHT-Leseversuch - siehe Lastenheft
  // Abschnitt 15.1). Setzt bei einem Treffer automatisch
  // cfg.sensor2Enabled=true (falls noch nicht gesetzt); setzt es NICHT
  // automatisch auf false, wenn nichts gefunden wird - ein manuell
  // aktivierter Schalter soll durch einen fehlgeschlagenen/verpassten Scan
  // nicht stillschweigend wieder deaktiviert werden (siehe Abschnitt 15.4).
  void runDetection();

  // Periodischer I2C-Rescan (alle 60s, aus loop() aufzurufen) - unabhaengig
  // vom 60s-Lesezyklus von SensorManager getaktet (eigener Timer, kein
  // gemeinsamer Zaehler). Wiederholt NUR den I2C-Sweep aus runDetection(),
  // NICHT den DHT-Leseversuch (Lastenheft 15.1, Schritt 2): dhtProbe teilt
  // sich den Pin mit SensorManager's dhtExternal, ein zusaetzlicher
  // Leseversuch jede Minute wuerde dessen eigentlichen Lesezyklus stoeren.
  // Bei mindestens einem I2C-Treffer wird der Erkennungsstatus aktualisiert
  // (auch bei Modulwechsel auf eine andere bekannte/unbekannte Adresse);
  // bleibt der Scan ergebnislos, wird der zuletzt bekannte Status NICHT
  // zurueckgesetzt (ein momentaner I2C-Aussetzer oder ein aktives DHT-/
  // Kontakt-Modul auf Pin 5, das hier nie "gefunden" wird, soll den Status
  // nicht faelschlich auf "Kein Sensor" zuruecksetzen).
  void loop();

  ModuleType detectedType() const { return _detectedType; }
  // Menschenlesbare Beschreibung fuer Einstellungsseite/Log, z.B.
  // "I2C-Sensor (BME280)", "I2C-Sensor (unbekannt, 0x42)", "DHT-Sensor",
  // "Kein Sensor / Relais" - haengt bei mehr als einem I2C-Treffer einen
  // Hinweis auf die weiteren gefundenen Adressen an (siehe
  // detectedI2cDeviceCount()).
  String detectedDescription() const;
  // Roher Chipname aus KNOWN_CHIPS (z.B. "BME280", "AHT20/AHT21"), nur bei
  // I2C_SENSOR_KNOWN gueltig, sonst "" - fuer SensorManager, um den
  // passenden I2C-Lesepfad zu waehlen (siehe readExternalSensorIfEnabled()).
  // Bezieht sich auf das PRIMAERE Geraet (niedrigste gefundene Adresse,
  // siehe scanI2cBus()) - SensorManager liest weiterhin nur ein einzelnes
  // I2C-Geraet gleichzeitig aus, echte Mehrfach-Nutzung mehrerer
  // gleichzeitig erkannter Module ist eine spaetere Erweiterung.
  String detectedChipName() const { return _detectedChipName; }
  // Gefundene I2C-Adresse des PRIMAEREN Geraets, nur bei
  // I2C_SENSOR_KNOWN/_UNKNOWN gueltig - fuer SensorManager, da z.B. BME280
  // zwei moegliche Adressen hat (0x76/0x77, siehe SDO-Pin).
  uint8_t detectedI2cAddress() const { return _detectedI2cAddress; }

  // Alle beim letzten Scan gefundenen I2C-Geraete (voller Sweep, nicht nur
  // das primaere) - Anzahl und Einzelzugriff, z.B. fuer eine spaetere
  // Anzeige auf der Einstellungsseite. Auf MAX_LOGGED_I2C_HITS begrenzt;
  // realistisch treten in dieser Familie hoechstens 2 Treffer gleichzeitig
  // auf (Kombimodule wie AHT20+BMP280).
  static const uint8_t MAX_LOGGED_I2C_HITS = 8;
  uint8_t detectedI2cDeviceCount() const { return _i2cHitCount; }
  const I2cHit& detectedI2cDeviceAt(uint8_t index) const { return _i2cHits[index]; }

 private:
  DataManager& _data;
  ConfigManager& _config;

  ModuleType _detectedType = ModuleType::NONE;
  String _detectedChipName;     // nur bei I2C_SENSOR_KNOWN
  uint8_t _detectedI2cAddress = 0;  // nur bei I2C_SENSOR_KNOWN/_UNKNOWN

  // Liefert bei bekannter Adresse den Chipnamen, sonst "" (Lastenheft
  // Abschnitt 15.2, erweiterbare Tabelle).
  static String knownChipName(uint8_t address);

  // Reiner I2C-Sweep (Schritt 1 aus runDetection(), fuer den periodischen
  // Rescan in loop() wiederverwendet). Durchlaeuft den GESAMTEN
  // Adressraum 0x08-0x77 (bricht NICHT beim ersten Treffer ab) und wertet
  // jede gefundene Adresse aus - Ergebnisse landen in _i2cHits[]/
  // _i2cHitCount. Das primaere Geraet (niedrigste gefundene Adresse, siehe
  // Klassenkommentar zu detectedChipName()) wird zusaetzlich in
  // _detectedType/_detectedChipName/_detectedI2cAddress gespiegelt, damit
  // SensorManager unveraendert nur dieses eine liest. Liefert true, wenn
  // mindestens ein Geraet gefunden wurde; laesst bei keinem Treffer alle
  // Felder unveraendert.
  bool scanI2cBus();

  I2cHit _i2cHits[MAX_LOGGED_I2C_HITS];
  uint8_t _i2cHitCount = 0;

  unsigned long _lastRescanMillis = 0;
};
