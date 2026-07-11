#include "ButtonManager.h"

#include "pins.h"

static const unsigned long BUTTON_TAP_MIN_MS = 50UL;             // Entprellung fuer kurze Tipps
static const unsigned long BUTTON_RESET_HOLD_MS = 3000UL;        // ab hier Reset-Bestaetigung
static const unsigned long BUTTON_RESET_COUNTDOWN_MS = 20000UL;  // danach: Loslassen zum Bestaetigen

ButtonManager::ButtonManager(DataManager& dataManager, ConfigManager& configManager)
    : _data(dataManager), _config(configManager) {}

void ButtonManager::begin() {
  pinMode(BUTTON_BOOT_PIN, INPUT_PULLUP);
  Serial.println("[BUTTON] Grundgeruest bereit (BOOT-Taster)");
}

bool ButtonManager::consumePageAdvance() {
  bool pending = _pageAdvancePending;
  _pageAdvancePending = false;
  return pending;
}

bool ButtonManager::isHeldForReset() const {
  if (_pressStartMillis == 0) return false;
  return millis() - _pressStartMillis >= BUTTON_RESET_HOLD_MS;
}

bool ButtonManager::isAwaitingRelease() const {
  if (!isHeldForReset()) return false;
  return millis() - _pressStartMillis >= BUTTON_RESET_HOLD_MS + BUTTON_RESET_COUNTDOWN_MS;
}

int ButtonManager::resetCountdownSecondsLeft() const {
  if (!isHeldForReset()) return 0;
  unsigned long countdownElapsed = millis() - _pressStartMillis - BUTTON_RESET_HOLD_MS;
  if (countdownElapsed >= BUTTON_RESET_COUNTDOWN_MS) return 0;
  return static_cast<int>((BUTTON_RESET_COUNTDOWN_MS - countdownElapsed) / 1000UL) + 1;
}

void ButtonManager::loop() {
  bool pressed = (digitalRead(BUTTON_BOOT_PIN) == LOW);
  unsigned long now = millis();

  if (pressed && _pressStartMillis == 0) {
    _pressStartMillis = now;
    return;
  }

  if (!pressed && _pressStartMillis != 0) {
    unsigned long heldMs = now - _pressStartMillis;
    _pressStartMillis = 0;

    if (heldMs >= BUTTON_TAP_MIN_MS && heldMs < BUTTON_RESET_HOLD_MS) {
      // Kurzer Tipp: naechste Seite - siehe consumePageAdvance().
      _pageAdvancePending = true;
    } else if (heldMs >= BUTTON_RESET_HOLD_MS + BUTTON_RESET_COUNTDOWN_MS) {
      // Fail-Safe gegen einen verklemmten Taster: der Reset wird erst BEIM
      // tatsaechlichen Loslassen ausgeloest, nicht schon waehrend des
      // Haltens - ein Taster, der (z.B. durch einen Defekt) dauerhaft
      // gedrueckt bleibt, kann so nie von selbst einen Reset ausloesen, da
      // dafuer ein echtes Loslassen-Ereignis noetig ist.
      _data.pushLogEntry("Werksreset ueber Taster ausgeloest (nur Einstellungen)", 3);
      DeviceConfig defaults;
      _config.setConfig(defaults);
      delay(300);
      ESP.restart();
    }
    // sonst (zwischen 3s und 3s+20s losgelassen): Reset-Bestaetigung ohne
    // Wirkung abgebrochen.
  }
}
