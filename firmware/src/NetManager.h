#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "ConfigManager.h"
#include "DataManager.h"

// Treibt den Boot-Zustandsautomaten an (docs/lastenheft.txt Abschnitt 8+12):
//   BOOT -> INIT -> NETWORK_CHECK -> RUN_NORMAL bzw. FALLBACK_MODE
//
// Ethernet laeuft hier ueber den W5500 (SPI), nicht ueber einen nativen
// RMII-PHY wie beim Sensormeter-Projekt (WT32-ETH01, LAN8720) - die
// oeffentliche Schnittstelle dieser Klasse (isLanUp()/getLanIp()/...) ist
// bewusst identisch gehalten, nur die .cpp-Implementierung von begin()
// unterscheidet sich (ETH.begin() mit SPI-Parametern statt MDC/MDIO/Power,
// siehe pins.h und docs/entscheidungen.md).
//
// Fallback: haben weder LAN noch WLAN nach 5 Minuten eine IP, spannt das
// Geraet einen eigenen Access Point auf (SSID/PSK "installer", DHCP aktiv,
// nur eigene IP + Subnetzmaske konfiguriert, kein Gateway/DNS). Betrifft nur
// den WLAN-Zweig, LAN bleibt davon unberuehrt und hat weiterhin Vorrang -
// kommt es waehrend des Fallback-APs zustande, uebernimmt networkOk() das
// automatisch.

class NetManager {
 public:
  NetManager(DataManager& dataManager, ConfigManager& configManager);

  void begin();
  void loop();

  bool isLanUp() const { return _ethGotIp; }
  bool isLanLinkUp() const { return _ethConnected; }
  bool isWlanUp() const { return _wlanGotIp || _apActive; }
  bool isUsingFallbackWlan() const { return _apActive; }

  // Fuer Display/Webserver. Implementiert in .cpp, damit dieser Header nicht
  // <ETH.h> einbinden muss (Makro-Reihenfolge, siehe .cpp).
  IPAddress getLanIp() const;
  IPAddress getWlanIp() const;
  IPAddress getLanGateway() const;
  IPAddress getWlanGateway() const;
  IPAddress getLanDns() const;
  IPAddress getWlanDns() const;
  String getLanMac() const;
  String getWlanMac() const;
  String getWlanSsid() const;
  int getWlanRssi() const;

  // Fuer TimeManager (Lastenheft: "nach 5 min ohne NTP -> DHCP aktivieren,
  // nach weiteren 3 min -> gesetzte IP-Einstellungen wiederherstellen").
  bool hasStaticConfig() const;
  void beginDhcpFallbackTest();
  void restoreConfiguredAddresses();

  // Leitet aus dem frei eingebbaren Systemnamen (ConfigManager) einen
  // DNS-/mDNS-tauglichen Hostnamen ab (nur a-z/0-9/-, keine Leerzeichen/
  // Umlaute/Grossbuchstaben) - wird sowohl fuer ETH.setHostname() als auch
  // fuer den mDNS-Namen (main.cpp) und den MQTT-Topic-Praefix-Default
  // (MqttManager) verwendet, damit alle drei konsistent sind.
  static String sanitizeHostname(const String& name);

 private:
  DataManager& _data;
  ConfigManager& _config;

  unsigned long _networkCheckStartedMillis = 0;
  unsigned long _lastFallbackJoinAttemptMillis = 0;
  // Zeitpunkt des letzten aktiven WLAN-Reconnect-Versuchs im NETWORK_CHECK
  // (siehe RECONNECT_RETRY_INTERVAL_MS in der .cpp).
  unsigned long _lastReconnectAttemptMillis = 0;
  bool _wlanConfigured = false;
  // Timeout fuer den aktuellen NETWORK_CHECK-Durchlauf - normal 5 Minuten,
  // kurz (30s) direkt nach Eingabe neuer WLAN-Zugangsdaten im Fallback-AP
  // (siehe begin() / DeviceConfig::wlanPendingTest).
  unsigned long _networkCheckTimeoutMs = 0;

  void applyLanConfig();
  void applyWlanConfig();
  // Startet den eigenen Fallback-Access-Point (SSID/PSK "installer").
  // Betrifft nur WiFi - ETH laeuft als separates Interface unabhaengig
  // weiter.
  void startFallbackAp();
  bool networkOk() const { return _ethGotIp || _wlanGotIp || _apActive; }

  static NetManager* _instance;
  static void onNetworkEvent(WiFiEvent_t event);

  volatile bool _ethConnected = false;
  volatile bool _ethGotIp = false;
  volatile bool _wlanGotIp = false;
  volatile bool _apActive = false;
};
