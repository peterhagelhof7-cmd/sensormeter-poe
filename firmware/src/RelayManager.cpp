#include "RelayManager.h"

#include "pins.h"

RelayManager::RelayManager(DataManager& dataManager, ConfigManager& configManager)
    : _data(dataManager), _config(configManager) {}

void RelayManager::begin() {
  pinMode(PIN_RJ45_PIN6_RELAY_OUT, OUTPUT);
  digitalWrite(PIN_RJ45_PIN6_RELAY_OUT, HIGH);  // active LOW -> HIGH = aus (sicherer Boot-Default)
  pinMode(PIN_RJ45_PIN7_RELAY_FB, INPUT_PULLUP);
  Serial.println("[RELAY] Grundgeruest bereit (Default: aus)");
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
