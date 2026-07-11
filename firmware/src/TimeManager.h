#pragma once

#include <Arduino.h>
#include "DataManager.h"
#include "NetManager.h"

// NTP-Sync gemaess docs/lastenheft.txt: de.pool.ntp.org, 60s nach Boot,
// danach alle 5h, zusaetzlich sofort nach jedem Link-Up-Event. Sommerzeit
// (CET/CEST) per POSIX-TZ-String. Fehlerkette bei anhaltendem NTP-Ausfall:
// nach 5 Minuten ohne Erfolg (nur falls LAN oder WLAN statisch konfiguriert)
// -> DHCP-Test, nach weiteren 3 Minuten ohne Erfolg -> Konfiguration
// wiederherstellen (ERROR_MODE).

class TimeManager {
 public:
  TimeManager(DataManager& dataManager, NetManager& networkManager);

  void begin();
  void loop();

  bool isSynced() const { return _synced; }

 private:
  DataManager& _data;
  NetManager& _network;

  bool _synced = false;
  bool _wasNetworkUp = false;

  bool _attemptActive = false;
  unsigned long _attemptStartedMillis = 0;
  unsigned long _nextAttemptDueMillis = 0;

  bool _dhcpTestActive = false;
  unsigned long _dhcpTestStartedMillis = 0;

  void startSyncAttempt();
  void onSyncSuccess();
};
