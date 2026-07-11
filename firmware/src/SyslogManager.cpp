#include "SyslogManager.h"

#include <esp_timer.h>
#include <time.h>
#include "TimeUtils.h"

static const int SYSLOG_PORT = 514;
static const int SYSLOG_FACILITY = 16;  // local0
static const int SEVERITY_INFO = 6;

SyslogManager::SyslogManager(DataManager& dataManager, ConfigManager& configManager, NetManager& networkManager)
    : _data(dataManager), _config(configManager), _network(networkManager) {}

void SyslogManager::begin() {
  Serial.println("[SYSLOG] Grundgeruest bereit (UDP Port 514)");
}

bool SyslogManager::syslogEnabled() const {
  const String& server = _config.getConfig().syslogServer;
  return server.length() > 0 && server != "0.0.0.0";
}

void SyslogManager::sendSyslog(int severity, const String& message) {
  if (!syslogEnabled()) return;
  if (!(_network.isLanUp() || _network.isWlanUp())) return;

  IPAddress serverIp;
  if (!serverIp.fromString(_config.getConfig().syslogServer)) return;

  int priority = SYSLOG_FACILITY * 8 + severity;
  // Minimal an RFC 5424 angelehnt (PRI + Version + "-" Platzhalter fuer
  // Timestamp/Procid/Msgid) - die eigentliche Nutzinformation steht im
  // MSG-Teil im von Lastenheft Abschnitt 9 geforderten Pipe-Format.
  String packet =
      "<" + String(priority) + ">1 - " + _config.getConfig().systemName + " sensormeter-poe - - " + message;

  _udp.beginPacket(serverIp, SYSLOG_PORT);
  _udp.print(packet);
  _udp.endPacket();
}

void SyslogManager::sendStatusReport() {
  const DeviceConfig& cfg = _config.getConfig();

  String lanIp = _network.isLanUp() ? _network.getLanIp().toString() : String("-");
  String wlanIp = _network.isWlanUp() ? _network.getWlanIp().toString() : String("-");
  String rssi = _network.isWlanUp() ? String(_network.getWlanRssi()) : String("n/a");

  SensorReading s1 = _data.getSensor1();
  String sensor1Value =
      s1.valid ? ("Intern: " + String(s1.temperature, 1) + "C/" + String(s1.humidity, 0) + "%") : String("Intern: --");

  String sensor2Value = "--";
  if (cfg.sensor2Enabled) {
    SensorReading s2 = _data.getSensor2();
    sensor2Value = s2.valid ? (cfg.sensor2Name + ": " + String(s2.temperature, 1) + "C/" + String(s2.humidity, 0) + "%")
                            : (cfg.sensor2Name + ": --");
  }

  String isoTime = "unsynced";
  if (isTimeSynced()) {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &ti);
    isoTime = buf;
  }

  unsigned long uptimeSec = (unsigned long)(esp_timer_get_time() / 1000000ULL);
  char uptimeBuf[16];
  snprintf(uptimeBuf, sizeof(uptimeBuf), "%02lu:%02lu:%02lu", uptimeSec / 3600, (uptimeSec / 60) % 60, uptimeSec % 60);

  // Format laut Lastenheft Abschnitt 9:
  // Systemname | Ethernet-IP | WLAN-IP | RSSI | Sensor1 | Sensor2 | ISO-Zeit | Uptime
  String message = cfg.systemName + " | " + lanIp + " | " + wlanIp + " | " + rssi + " | " + sensor1Value + " | " +
                    sensor2Value + " | " + isoTime + " | " + String(uptimeBuf);

  sendSyslog(SEVERITY_INFO, message);
}

void SyslogManager::checkForNewLogEntries() {
  LogEntry entries[DataManager::LOG_CAPACITY];
  size_t count = _data.getLogEntriesAfter(_lastSeenLogSequence, entries, DataManager::LOG_CAPACITY);
  for (size_t i = 0; i < count; i++) {
    sendSyslog(entries[i].severity, entries[i].message);
    _lastSeenLogSequence = entries[i].sequence;
  }
}

void SyslogManager::loop() {
  checkForNewLogEntries();  // Fehler-Events: sofort (naechster Loop-Tick)

  // Statusreport bei jedem Sensorzyklus - erkannt an einer Aenderung von
  // Sensor 1's lastReadMillis, statt eines eigenen, potenziell abdriftenden
  // Timers.
  unsigned long currentReadMillis = _data.getSensor1().lastReadMillis;
  if (currentReadMillis != 0 && currentReadMillis != _lastSensorReadMillisSeen) {
    _lastSensorReadMillisSeen = currentReadMillis;
    sendStatusReport();
  }
}
