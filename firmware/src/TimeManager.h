#pragma once

#include <Arduino.h>
#include "DataManager.h"
#include "NetManager.h"

// NTP-Sync gemaess docs/lastenheft.txt: de.pool.ntp.org, 60s nach Boot,
// danach alle 5h, zusaetzlich sofort nach jedem Link-Up-Event. Sommerzeit
// (CET/CEST) per POSIX-TZ-String.
//
// Interface-Reihenfolge (2026-07-22, auf Nutzerwunsch): ein NTP-Versuch wird
// zuerst explizit ueber LAN unternommen (NetManager::pinDefaultInterface,
// derselbe Mechanismus wie MqttManager's Interface-Pinning). Schlaegt das
// 5 Minuten lang fehl UND ist WLAN verfuegbar, wird als naechstes 5 Minuten
// lang explizit ueber WLAN versucht. Ohne dieses Pinning wuerde configTzTime()
// einfach das lwIP-Default-Netif benutzen, das das Arduino-Core bei jedem
// GOT_IP-Event automatisch auf das zuletzt verbundene Interface umstellt -
// bei einem instabilen WLAN (Reconnects) kann das NTP so unbemerkt dauerhaft
// auf ein schlechteres Interface ziehen, obwohl LAN die ganze Zeit stabil
// laeuft (siehe sensormeter-Log-Analyse 2026-07-22, identischer Mechanismus
// auf diesem Projekt uebertragen).
//
// Fehlerkette bei anhaltendem NTP-Ausfall (nach beiden Interface-Versuchen,
// bzw. sofort nach dem einzigen verfuegbaren): nur falls LAN oder WLAN
// statisch konfiguriert ist -> DHCP-Test, nach weiteren 3 Minuten ohne
// Erfolg -> Konfiguration wiederherstellen (ERROR_MODE).

class TimeManager {
 public:
  TimeManager(DataManager& dataManager, NetManager& networkManager);

  void begin();
  void loop();

  bool isSynced() const { return _synced; }

 private:
  enum class SyncPhase { Lan, Wlan };

  DataManager& _data;
  NetManager& _network;

  bool _synced = false;
  bool _wasNetworkUp = false;

  bool _attemptActive = false;
  unsigned long _attemptStartedMillis = 0;
  unsigned long _nextAttemptDueMillis = 0;
  SyncPhase _currentPhase = SyncPhase::Lan;
  // Waehrend eines Sync-Versuchs per pinDefaultInterface() gemerkter
  // vorheriger Default-Netif, damit unpinInterface() ihn zuverlaessig
  // wiederherstellen kann - siehe NetManager::pinDefaultInterface().
  struct netif* _pinnedPreviousNetif = nullptr;

  bool _dhcpTestActive = false;
  unsigned long _dhcpTestStartedMillis = 0;

  void startSyncAttempt();
  void onSyncSuccess();
  void unpinInterface();
};
