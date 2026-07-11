#include "TimeManager.h"

#include <time.h>
#include "TimeUtils.h"

static const unsigned long FIRST_SYNC_DELAY_MS = 60UL * 1000UL;               // 60s nach Boot
static const unsigned long RESYNC_INTERVAL_MS = 5UL * 60UL * 60UL * 1000UL;   // alle 5h
static const unsigned long SYNC_FAIL_TIMEOUT_MS = 5UL * 60UL * 1000UL;        // 5 Minuten
static const unsigned long DHCP_TEST_DURATION_MS = 3UL * 60UL * 1000UL;       // 3 Minuten

static const char* NTP_SERVER = "de.pool.ntp.org";
// Europe/Berlin inkl. automatischer Sommerzeitumschaltung (POSIX-TZ-String)
static const char* TZ_GERMANY = "CET-1CEST,M3.5.0,M10.5.0/3";

TimeManager::TimeManager(DataManager& dataManager, NetManager& networkManager)
    : _data(dataManager), _network(networkManager) {}

void TimeManager::begin() {
  Serial.println("[TIME] Grundgeruest bereit, erster NTP-Versuch 60s nach Boot");
  _nextAttemptDueMillis = millis() + FIRST_SYNC_DELAY_MS;
}

void TimeManager::startSyncAttempt() {
  Serial.printf("[TIME] NTP-Sync-Versuch (%s)\n", NTP_SERVER);
  configTzTime(TZ_GERMANY, NTP_SERVER);
  _attemptActive = true;
  _attemptStartedMillis = millis();
}

void TimeManager::onSyncSuccess() {
  _synced = true;
  _attemptActive = false;
  _dhcpTestActive = false;
  _nextAttemptDueMillis = millis() + RESYNC_INTERVAL_MS;

  time_t now = time(nullptr);
  Serial.print("[TIME] Synchronisiert: ");
  Serial.print(ctime(&now));

  SystemState state = _data.getSystemState();
  if (state == SystemState::DHCP_TEST || state == SystemState::ERROR_MODE) {
    _data.setSystemState(SystemState::RUN_NORMAL);
  }
}

void TimeManager::loop() {
  bool netUp = _network.isLanUp() || _network.isWlanUp();

  // Link-Up-Event: sofort resynchronisieren, unabhaengig vom 5h-Rhythmus
  if (netUp && !_wasNetworkUp) {
    Serial.println("[TIME] Link-Up erkannt -> NTP-Resync vorgezogen");
    _nextAttemptDueMillis = millis();
  }
  _wasNetworkUp = netUp;

  if (!netUp) return;  // "nur wenn eine Netzwerkverbindung aktiv ist" (Lastenheft)

  unsigned long now = millis();

  if (!_synced && isTimeSynced()) {
    onSyncSuccess();
    return;
  }

  if (_dhcpTestActive) {
    if (isTimeSynced()) {
      onSyncSuccess();
      return;
    }
    if (now - _dhcpTestStartedMillis > DHCP_TEST_DURATION_MS) {
      _network.restoreConfiguredAddresses();
      _dhcpTestActive = false;
      _attemptActive = false;
      _data.pushLogEntry("NTP: weiterhin nicht erreichbar, Konfiguration wiederhergestellt", 3);
      _data.setSystemState(SystemState::ERROR_MODE);
      _nextAttemptDueMillis = now + RESYNC_INTERVAL_MS;
    }
    return;
  }

  if (_attemptActive) {
    if (now - _attemptStartedMillis > SYNC_FAIL_TIMEOUT_MS) {
      if (_network.hasStaticConfig()) {
        _data.pushLogEntry("NTP: 5 Minuten nicht erreichbar, DHCP-Test gestartet", 3);
        _network.beginDhcpFallbackTest();
        _dhcpTestActive = true;
        _dhcpTestStartedMillis = now;
        _data.setSystemState(SystemState::DHCP_TEST);
      } else {
        _data.pushLogEntry("NTP: nicht erreichbar (DHCP aktiv), spaeter erneut versuchen", 3);
        _attemptActive = false;
        _nextAttemptDueMillis = now + SYNC_FAIL_TIMEOUT_MS;
      }
    }
    return;
  }

  if ((long)(now - _nextAttemptDueMillis) >= 0) {
    startSyncAttempt();
  }
}
