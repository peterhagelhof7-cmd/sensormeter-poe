#include "SNMPManager.h"

#include <esp_timer.h>
#include <math.h>

#if __has_include("config.h")
#include "config.h"
#endif
#ifndef DEVICE_FIRMWARE_VERSION
#define DEVICE_FIRMWARE_VERSION "0.0.0"
#endif

// Basis-OID + Struktur laut docs/lastenheft.txt Abschnitt 7
static const char* OID_SYSTEM_NAME = ".1.3.6.1.4.1.99999.1.1.0";
static const char* OID_FIRMWARE = ".1.3.6.1.4.1.99999.1.2.0";
static const char* OID_SYSTEM_TYPE = ".1.3.6.1.4.1.99999.1.3.0";

static const char* OID_LAN_IP = ".1.3.6.1.4.1.99999.2.1.0";
static const char* OID_WLAN_IP = ".1.3.6.1.4.1.99999.2.2.0";
static const char* OID_WLAN_RSSI = ".1.3.6.1.4.1.99999.2.3.0";

static const char* OID_SENSOR1_NAME = ".1.3.6.1.4.1.99999.3.1.0";
static const char* OID_SENSOR1_TEMP = ".1.3.6.1.4.1.99999.3.2.0";  // Grad C x10
static const char* OID_SENSOR1_HUM = ".1.3.6.1.4.1.99999.3.3.0";   // % x10

static const char* OID_SENSOR2_NAME = ".1.3.6.1.4.1.99999.4.1.0";
static const char* OID_SENSOR2_TEMP = ".1.3.6.1.4.1.99999.4.2.0";
static const char* OID_SENSOR2_HUM = ".1.3.6.1.4.1.99999.4.3.0";

static const char* OID_UPTIME = ".1.3.6.1.4.1.99999.5.1.0";  // TimeTicks (1/100s)
static const char* OID_HEAP = ".1.3.6.1.4.1.99999.5.2.0";    // Bytes

// "polling optimized (no continuous refresh)"
static const unsigned long REFRESH_INTERVAL_MS = 5UL * 1000UL;

SNMPManager::SNMPManager(DataManager& dataManager, ConfigManager& configManager, NetManager& networkManager)
    : _data(dataManager), _config(configManager), _network(networkManager) {}

void SNMPManager::refreshValues() {
  const DeviceConfig& cfg = _config.getConfig();

  strncpy(_systemName, cfg.systemName.c_str(), sizeof(_systemName) - 1);
  strncpy(_systemType, cfg.systemType.c_str(), sizeof(_systemType) - 1);
  strncpy(_lanIp, _network.isLanUp() ? _network.getLanIp().toString().c_str() : "0.0.0.0", sizeof(_lanIp) - 1);
  strncpy(_wlanIp, _network.isWlanUp() ? _network.getWlanIp().toString().c_str() : "0.0.0.0", sizeof(_wlanIp) - 1);
  strncpy(_sensor2Name, cfg.sensor2Name.c_str(), sizeof(_sensor2Name) - 1);

  _wlanRssi = _network.isWlanUp() ? _network.getWlanRssi() : 0;

  SensorReading s1 = _data.getSensor1();
  _temperature1X10 = s1.valid ? (int)round(s1.temperature * 10) : 0;
  _humidity1X10 = s1.valid ? (int)round(s1.humidity * 10) : 0;

  if (cfg.sensor2Enabled) {
    SensorReading s2 = _data.getSensor2();
    // s2.valid heisst nur "Lesung erfolgreich", NICHT mehr zwingend
    // "liefert Temperatur/Feuchte" - ein reines Druck-/Lux-/Luftguete-Modul
    // (BMP280/BH1750/ENS160) laesst temperature/humidity NAN, siehe
    // DataManager.h. round(NAN) waere hier Unsinn, daher zusaetzlich
    // pruefen. Diese drei neuen Messgroessen selbst sind noch nicht per
    // SNMP exportiert (siehe docs/entscheidungen.md).
    bool hasTempHum = s2.valid && !isnan(s2.temperature) && !isnan(s2.humidity);
    _temperature2X10 = hasTempHum ? (int)round(s2.temperature * 10) : 0;
    _humidity2X10 = hasTempHum ? (int)round(s2.humidity * 10) : 0;
  } else {
    _temperature2X10 = 0;
    _humidity2X10 = 0;
  }

  _uptimeTicks = (uint32_t)(esp_timer_get_time() / 10000ULL);  // Zentisekunden
  _freeHeap = ESP.getFreeHeap();
}

void SNMPManager::begin() {
  const DeviceConfig& cfg = _config.getConfig();
  _agent.setReadOnlyCommunity(cfg.snmpCommunity.c_str());
  _agent.setReadWriteCommunity(cfg.snmpCommunity.c_str());
  _agent.setUDP(&_udp);

  refreshValues();

  _agent.addReadWriteStringHandler(OID_SYSTEM_NAME, &_systemNamePtr, sizeof(_systemName), false);
  _agent.addReadOnlyStaticStringHandler(OID_FIRMWARE, std::string(DEVICE_FIRMWARE_VERSION));
  _agent.addReadWriteStringHandler(OID_SYSTEM_TYPE, &_systemTypePtr, sizeof(_systemType), false);

  _agent.addReadWriteStringHandler(OID_LAN_IP, &_lanIpPtr, sizeof(_lanIp), false);
  _agent.addReadWriteStringHandler(OID_WLAN_IP, &_wlanIpPtr, sizeof(_wlanIp), false);
  _agent.addIntegerHandler(OID_WLAN_RSSI, &_wlanRssi, false);

  _agent.addReadOnlyStaticStringHandler(OID_SENSOR1_NAME, std::string("Intern"));
  _agent.addIntegerHandler(OID_SENSOR1_TEMP, &_temperature1X10, false);
  _agent.addIntegerHandler(OID_SENSOR1_HUM, &_humidity1X10, false);

  _agent.addReadWriteStringHandler(OID_SENSOR2_NAME, &_sensor2NamePtr, sizeof(_sensor2Name), false);
  _agent.addIntegerHandler(OID_SENSOR2_TEMP, &_temperature2X10, false);
  _agent.addIntegerHandler(OID_SENSOR2_HUM, &_humidity2X10, false);

  _agent.addTimestampHandler(OID_UPTIME, &_uptimeTicks, false);
  _agent.addGaugeHandler(OID_HEAP, &_freeHeap);

  _agent.sortHandlers();
  _agent.begin();

  Serial.println("[SNMP] Agent gestartet (v1/v2c read-only, Port 161)");
}

void SNMPManager::loop() {
  _agent.loop();

  if (millis() - _lastRefreshMillis >= REFRESH_INTERVAL_MS) {
    _lastRefreshMillis = millis();
    refreshValues();
  }
}
