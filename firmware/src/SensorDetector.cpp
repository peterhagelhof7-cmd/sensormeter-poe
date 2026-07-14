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

void SensorDetector::runDetection() {
  _detectedType = ModuleType::NONE;
  _detectedChipName = "";
  _detectedI2cAddress = 0;

  // Schritt 1: I2C-Scan (7-Bit-Adressraum 0x08-0x77, Display ausgenommen).
  for (uint8_t address = 0x08; address <= 0x77; address++) {
    if (address == DISPLAY_I2C_ADDRESS || address == EXTERNAL_DISPLAY_I2C_ADDRESS) continue;

    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error != 0) continue;

    _detectedI2cAddress = address;
    String chipName = knownChipName(address);
    if (chipName.length() > 0) {
      _detectedType = ModuleType::I2C_SENSOR_KNOWN;
      _detectedChipName = chipName;
      Serial.printf("[DETECT] I2C-Sensor erkannt: %s (0x%02X)\n", chipName.c_str(), address);
    } else {
      _detectedType = ModuleType::I2C_SENSOR_UNKNOWN;
      Serial.printf("[DETECT] I2C-Geraet mit unbekannter Adresse 0x%02X gefunden\n", address);
    }
    break;  // erster Treffer genuegt (Lastenheft beschreibt genau ein Modul)
  }

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
  switch (_detectedType) {
    case ModuleType::DHT_SENSOR:
      return "DHT-Sensor";
    case ModuleType::I2C_SENSOR_KNOWN: {
      char buf[40];
      snprintf(buf, sizeof(buf), "I2C-Sensor (%s)", _detectedChipName.c_str());
      return String(buf);
    }
    case ModuleType::I2C_SENSOR_UNKNOWN: {
      char buf[40];
      snprintf(buf, sizeof(buf), "I2C-Sensor (unbekannt, 0x%02X)", _detectedI2cAddress);
      return String(buf);
    }
    case ModuleType::NONE:
    default:
      return "Kein Sensor / Relais";
  }
}
