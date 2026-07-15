#include "RelayManager.h"

#include "pins.h"

RelayManager::RelayManager(DataManager& dataManager, ConfigManager& configManager, ContactManager& contactManager)
    : _data(dataManager), _config(configManager), _contact(contactManager) {}

void RelayManager::begin() {
  pinMode(PIN_RJ45_PIN6_RELAY_OUT, OUTPUT);
  digitalWrite(PIN_RJ45_PIN6_RELAY_OUT, HIGH);  // active LOW -> HIGH = aus (sicherer Boot-Default)
  pinMode(PIN_RJ45_PIN7_RELAY_FB, INPUT_PULLUP);
  Serial.println("[RELAY] Grundgeruest bereit (Default: aus)");
}

void RelayManager::loop() {
  const DeviceConfig& cfg = _config.getConfig();
  if (cfg.relayAutoMode != "sensor") return;

  if (cfg.relayAutoSource == "contact") {
    if (cfg.pin5Mode != "contact") return;  // Quelle aktuell nicht verfuegbar
    String current = _contact.isClosed() ? "closed" : "open";
    setOn(current == cfg.relayAutoContactState);
    return;
  }

  if (cfg.relayAutoSource == "sensor2" && (cfg.pin5Mode != "sensor" || !cfg.sensor2Enabled)) return;
  SensorReading reading = (cfg.relayAutoSource == "sensor1") ? _data.getSensor1() : _data.getSensor2();
  if (!reading.valid) return;  // kein gueltiger Messwert -> Zustand unveraendert lassen

  float value = (cfg.relayAutoValue == "temp") ? reading.temperature : reading.humidity;
  // Sensor 2 kann seit der Druck/Lux/Luftguete-Erweiterung "valid" sein,
  // OHNE Temperatur/Feuchte zu liefern (z.B. BMP280/BH1750/ENS160-Modul,
  // siehe DataManager.h) - value waere dann NAN, und ein Vergleich mit NAN
  // ist in IEEE 754 immer false, wuerde das Relais also unbemerkt staendig
  // ausschalten statt den Zustand unveraendert zu lassen.
  if (isnan(value)) return;
  bool desired = (cfg.relayAutoCompare == "above") ? (value > cfg.relayAutoThreshold)
                                                    : (value < cfg.relayAutoThreshold);
  setOn(desired);
}

void RelayManager::setOn(bool on) {
  if (!_config.getConfig().relayEnabled) {
    Serial.println("[RELAY] Schaltbefehl ignoriert - Relais (Aktor) ist nicht aktiviert");
    return;
  }
  if (on == _relayOn) return;

  _relayOn = on;
  digitalWrite(PIN_RJ45_PIN6_RELAY_OUT, on ? LOW : HIGH);  // active LOW
  _data.pushLogEntry(String("Relais ") + (on ? "eingeschaltet" : "ausgeschaltet"));
}

bool RelayManager::feedbackOn() const {
  // active LOW, wie das Steuersignal (siehe Lastenheft Abschnitt 14) -
  // LOW = Feedback-Leitung aktiv gezogen.
  return digitalRead(PIN_RJ45_PIN7_RELAY_FB) == LOW;
}
