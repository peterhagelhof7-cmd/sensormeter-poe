#include "SensorDetector.h"

#include <DHT.h>
#include <Wire.h>
#include "pins.h"

namespace {
// Display-Adressen - beim I2C-Scan ausgenommen (Lastenheft Abschnitt 15.1).
// 0x3C: internes SSD1306 (siehe DisplayManager.cpp). 0x3D: optionales
// externes SH1107-Display-Steckmodul (siehe ExternalDisplayManager.cpp
// und sensormeter-family/repo/module-design/sh1107-display-modul.md) -
// ohne diese zweite Ausnahme wuerde ein gestecktes externes Display als
// unbekannter I2C-Sensor erkannt UND (da 0x3D vor den meisten bekannten
// Sensor-Adressen liegt) den Scan abbrechen, bevor ein dahinter
// angeschlossener echter Sensor gefunden wird.
const uint8_t DISPLAY_I2C_ADDRESS = 0x3C;
const uint8_t EXTERNAL_DISPLAY_I2C_ADDRESS = 0x3D;

// Bekannte I2C-Sensor-Adressen (Lastenheft Abschnitt 15.2, erweiterbare
// Tabelle).
struct KnownChip {
  uint8_t address;
  const char* name;
};
const KnownChip KNOWN_CHIPS[] = {
    {0x76, "BME280"}, {0x77, "BME280"}, {0x23, "BH1750"},  {0x5C, "BH1750"},
    {0x44, "SHT30/31/35"}, {0x45, "SHT30/31/35"}, {0x38, "AHT20/AHT21"},
};

// Eigenes DHT-Objekt fuer den Erkennungs-Leseversuch (Lastenheft 15.1,
// Schritt 2) - unabhaengig von SensorManager's dhtExternal, da hier nur ein
// einzelner Testleseversuch zum Erkennungszeitpunkt noetig ist, kein
// periodisches Polling.
DHT dhtProbe(PIN_DHT_EXTERNAL, DHT22);
}  // namespace

SensorDetector::SensorDetector(DataManager& dataManager, ConfigManager& configManager)
    : _data(dataManager), _config(configManager) {}

void SensorDetector::begin() {
  // Wire.begin() ist auf dem ESP32 idempotent (reine Neukonfiguration) -
  // unproblematisch, dass auch DisplayManager::begin() denselben Bus mit
  // denselben Pins startet, unabhaengig von der Aufrufreihenfolge.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  dhtProbe.begin();
  Serial.println("[DETECT] Grundgeruest bereit");
}

String SensorDetector::knownChipName(uint8_t address) {
  for (const KnownChip& chip : KNOWN_CHIPS) {
    if (chip.address == address) return chip.name;
  }
  return "";
}

bool SensorDetector::scanI2cBus() {
  // Voller 7-Bit-Adressraum 0x08-0x77, Display-Adressen ausgenommen - laeuft
  // immer komplett durch (KEIN break beim ersten Treffer mehr), damit auch
  // Kombimodule mit zwei I2C-Chips (z.B. AHT20+BMP280) beide gemeldet
  // werden, nicht nur der erste. _i2cHits[] wird bei jedem Aufruf neu
  // aufgebaut - spiegelt exakt den aktuellen Scan, nicht historische
  // Treffer. Das PRIMAERE Geraet (niedrigste gefundene Adresse) wird
  // zusaetzlich in _detectedType/_detectedChipName/_detectedI2cAddress
  // gespiegelt, damit SensorManager unveraendert genau eines liest -
  // dieser Teil bleibt bei einem ergebnislosen Scan UNVERAENDERT (siehe
  // Aufrufer runDetection()/loop() fuer den Unterschied).
  _i2cHitCount = 0;
  bool primarySet = false;

  for (uint8_t address = 0x08; address <= 0x77; address++) {
    if (address == DISPLAY_I2C_ADDRESS || address == EXTERNAL_DISPLAY_I2C_ADDRESS) continue;

    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error != 0) continue;

    String chipName = knownChipName(address);
    if (chipName.length() > 0) {
      Serial.printf("[DETECT] I2C-Sensor erkannt: %s (0x%02X)\n", chipName.c_str(), address);
    } else {
      Serial.printf("[DETECT] I2C-Geraet mit unbekannter Adresse 0x%02X gefunden\n", address);
    }

    if (_i2cHitCount < MAX_LOGGED_I2C_HITS) {
      _i2cHits[_i2cHitCount].address = address;
      _i2cHits[_i2cHitCount].chipName = chipName;
      _i2cHitCount++;
    }

    if (!primarySet) {
      _detectedI2cAddress = address;
      _detectedType = chipName.length() > 0 ? ModuleType::I2C_SENSOR_KNOWN : ModuleType::I2C_SENSOR_UNKNOWN;
      _detectedChipName = chipName;
      primarySet = true;
    }
  }

  return _i2cHitCount > 0;
}

void SensorDetector::runDetection() {
  _detectedType = ModuleType::NONE;
  _detectedChipName = "";
  _detectedI2cAddress = 0;

  scanI2cBus();

  // Schritt 2: kein I2C-Geraet -> DHT-Leseversuch auf RJ45 Pin 5 - nur, wenn
  // der Pin ueberhaupt als Sensor-Eingang konfiguriert ist (nicht als
  // Kontakt-Eingang, siehe ConfigManager::pin5Mode). Ein DHT-Leseversuch auf
  // einem gesteckten Kontaktmodul wuerde ohnehin nur fehlschlagen, aber
  // unnoetig Zeit kosten.
  if (_detectedType == ModuleType::NONE && _config.getConfig().pin5Mode == "sensor") {
    float humidity = dhtProbe.readHumidity();
    float temperature = dhtProbe.readTemperature();
    if (!isnan(humidity) && !isnan(temperature)) {
      _detectedType = ModuleType::DHT_SENSOR;
      Serial.println("[DETECT] DHT-Sensor erkannt");
    }
  }

  if (_detectedType == ModuleType::NONE) {
    Serial.println("[DETECT] Kein Sensor-Modul erkannt (oder Relais gesteckt, siehe Abschnitt 15.3)");
    return;
  }

  // Sensor 2 automatisch aktivieren, aber ein bereits deaktivierter
  // manueller Override wird durch einen NICHT-Treffer nie stillschweigend
  // ueberschrieben (siehe Klassenkommentar/Lastenheft 15.4).
  DeviceConfig cfg = _config.getConfig();
  if (!cfg.sensor2Enabled) {
    cfg.sensor2Enabled = true;
    _config.setConfig(cfg);
    _data.pushLogEntry("Sensor 2 automatisch aktiviert: " + detectedDescription());
  }
}

String SensorDetector::detectedDescription() const {
  String primary;
  switch (_detectedType) {
    case ModuleType::DHT_SENSOR:
      primary = "DHT-Sensor";
      break;
    case ModuleType::I2C_SENSOR_KNOWN: {
      char buf[40];
      snprintf(buf, sizeof(buf), "I2C-Sensor (%s)", _detectedChipName.c_str());
      primary = String(buf);
      break;
    }
    case ModuleType::I2C_SENSOR_UNKNOWN: {
      char buf[40];
      snprintf(buf, sizeof(buf), "I2C-Sensor (unbekannt, 0x%02X)", _detectedI2cAddress);
      primary = String(buf);
      break;
    }
    case ModuleType::NONE:
    default:
      return "Kein Sensor / Relais";
  }

  // Bei einem I2C-Kombimodul (z.B. AHT20+BMP280) meldet der volle Sweep
  // mehr als ein Geraet - genutzt wird weiterhin nur das primaere (siehe
  // Klassenkommentar zu detectedChipName()), die weiteren werden hier nur
  // zur Info angehaengt, nicht automatisch mitgelesen.
  if (_i2cHitCount > 1) {
    char suffix[32];
    snprintf(suffix, sizeof(suffix), " (+%u weitere I2C-Geraet%s)", _i2cHitCount - 1,
              (_i2cHitCount - 1) == 1 ? "" : "e");
    primary += suffix;
  }
  return primary;
}

void SensorDetector::loop() {
  static const unsigned long RESCAN_INTERVAL_MS = 60UL * 1000UL;  // 1x je Minute
  // Erster Durchlauf (_lastRescanMillis == 0): sofort scannen statt 60s zu
  // warten - analog zum Muster in SensorManager::loop().
  if (_lastRescanMillis != 0 && millis() - _lastRescanMillis < RESCAN_INTERVAL_MS) return;
  _lastRescanMillis = millis();

  // Ergebnislosigkeit setzt den bisherigen Status NICHT zurueck (siehe Header-Kommentar).
  if (scanI2cBus()) {
    DeviceConfig cfg = _config.getConfig();
    if (!cfg.sensor2Enabled) {
      cfg.sensor2Enabled = true;
      _config.setConfig(cfg);
      _data.pushLogEntry("Sensor 2 automatisch aktiviert: " + detectedDescription());
    }
  }
}
