#pragma once

#include <Arduino.h>
#include <SNMP_Agent.h>
#include <WiFiUdp.h>
#include "ConfigManager.h"
#include "DataManager.h"
#include "NetManager.h"

// SNMP v1 read-only Agent (Lastenheft Abschnitt 7, feste OID-Struktur unter
// .1.3.6.1.4.1.99999.x - bewusst dieselbe Basis-OID wie Sensormeter und
// Sensormeter WLAN, siehe docs/lastenheft.txt). "read-only" ist hier durch
// Konstruktion erzwungen - es wird nirgends isSettable=true gesetzt. Werte
// werden periodisch aktualisiert statt bei jedem GET neu berechnet.

class SNMPManager {
 public:
  SNMPManager(DataManager& dataManager, ConfigManager& configManager, NetManager& networkManager);

  void begin();
  void loop();

 private:
  DataManager& _data;
  ConfigManager& _config;
  NetManager& _network;

  WiFiUDP _udp;
  SNMPAgent _agent;

  unsigned long _lastRefreshMillis = 0;

  // Werden periodisch befuellt (refreshValues()) und der Bibliothek als
  // Zeiger uebergeben - sie liest bei jedem GET live von dieser Adresse.
  char _systemName[33] = {0};
  char _systemType[24] = {0};
  char _lanIp[16] = {0};
  char _wlanIp[16] = {0};
  char _sensor2Name[25] = {0};
  char* _systemNamePtr = _systemName;
  char* _systemTypePtr = _systemType;
  char* _lanIpPtr = _lanIp;
  char* _wlanIpPtr = _wlanIp;
  char* _sensor2NamePtr = _sensor2Name;

  int _wlanRssi = 0;
  int _temperature1X10 = 0;
  int _humidity1X10 = 0;
  int _temperature2X10 = 0;
  int _humidity2X10 = 0;
  uint32_t _uptimeTicks = 0;
  uint32_t _freeHeap = 0;

  void refreshValues();
};
