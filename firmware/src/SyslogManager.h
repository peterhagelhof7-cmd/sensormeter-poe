#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include "ConfigManager.h"
#include "DataManager.h"
#include "NetManager.h"

// Syslog-Versand (Pflichtenheft "SyslogTask", Lastenheft Abschnitt 9):
// periodischer Statusreport bei jedem Sensorzyklus + sofortiger Versand von
// Fehler-Events. Fehler-Events kommen aus DataManager's Ereignisprotokoll -
// derselben Quelle, die auch die Webseite fuer "letzte 5 Meldungen" nutzt.
// SyslogManager erkennt neue Eintraege anhand einer fortlaufenden
// Sequenznummer, kein Observer-Pattern noetig. UDP Port 514, deaktiviert
// wenn syslogServer "0.0.0.0" ist (Default).

class SyslogManager {
 public:
  SyslogManager(DataManager& dataManager, ConfigManager& configManager, NetManager& networkManager);

  void begin();
  void loop();

 private:
  DataManager& _data;
  ConfigManager& _config;
  NetManager& _network;

  WiFiUDP _udp;
  unsigned long _lastSeenLogSequence = 0;
  unsigned long _lastSensorReadMillisSeen = 0;

  bool syslogEnabled() const;
  void sendSyslog(int severity, const String& message);
  void sendStatusReport();
  void checkForNewLogEntries();
};
