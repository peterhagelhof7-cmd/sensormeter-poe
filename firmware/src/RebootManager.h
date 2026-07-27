#pragma once

#include <Arduino.h>
#include "ConfigManager.h"
#include "DataManager.h"

// Taeglicher automatischer Neustart zu einer ueber die Einstellungsseite
// festgelegten Uhrzeit (optional, Default aus) - z.B. um sich langfristig
// ansammelnden Speicherfragmentierungs-/Verbindungsproblemen vorzubeugen.
// Braucht eine per NTP synchronisierte Uhr (siehe TimeUtils.h::isTimeSynced()),
// sonst koennte die ESP32-RTC kurz nach dem Boot (nahe Unix-Epoche 0) einen
// falschen Treffer liefern. Kein zusaetzliches "heute schon ausgeloest"-Flag
// noetig: ESP.restart() beendet loop() sofort beim ersten Treffer, und nach
// dem Neustart vergehen bis zum naechsten Treffer derselben Uhrzeit rund 24h -
// der Neustart selbst verhindert also ein Mehrfachausloesen.
class RebootManager {
 public:
  RebootManager(DataManager& dataManager, ConfigManager& configManager);

  void loop();

 private:
  DataManager& _data;
  ConfigManager& _config;
};
