#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include "ConfigManager.h"
#include "DataManager.h"
#include "NetManager.h"
#include "RelayManager.h"

// Home-Assistant-Anbindung ueber MQTT-Discovery (Lastenheft Abschnitt 16).
// Architekturvorbild: MqttManager aus Sensormeter WLAN (dort nur Sensor-
// Rolle) - hier zusaetzlich die Aktor-Rolle (Relais, Abschnitt 16.2), da
// dieses Board einen RJ45-Modularanschluss besitzt. Deaktiviert, solange
// kein Broker konfiguriert ist (mqttEnabled=false, Default). Publiziert
// Sensorwerte bei jedem Sensorzyklus (erkannt wie bei SyslogManager an einer
// Aenderung von lastReadMillis), Relais-Zustand sofort bei Aenderung -
// Discovery-Payload nur einmal je (Re-)Connect.
//
// Zwei-Interface-Besonderheit (LAN + WLAN gleichzeitig moeglich): seit
// 2026-07-16 (ConfigManager::mqttInterface) wird das nicht dem lwIP-
// Standardverhalten ueberlassen - ensureConnected() setzt vor jedem
// connect()-Versuch per lwIP netif_set_default() (lwip/netif.h) explizit das
// gewaehlte Interface als Default und stellt danach den vorherigen Zustand
// wieder her (siehe MqttManager.cpp) - damit ist deterministisch festgelegt,
// ueber welches Interface der Broker erreicht wird, auch wenn beide
// gleichzeitig eine IP haben. Bewusst die lwIP-Funktion statt
// esp_netif_set_default_netif(): identischer Code wie bei Sensormeter
// (WT32-ETH01, Arduino-ESP32 2.0.17), wo esp_netif_set_default_netif() noch
// nicht existiert - die lwIP-Funktion darunter gibt es auf beiden Core-
// Versionen. Siehe docs/entscheidungen.md.

class MqttManager {
 public:
  MqttManager(DataManager& dataManager, ConfigManager& configManager, NetManager& networkManager,
              RelayManager& relayManager);

  void begin();
  void loop();

  // Leitet aus dem Systemnamen wie beim mDNS-Hostnamen einen Default ab,
  // sofern cfg.mqttTopicPrefix leer ist (siehe ConfigManager.h) - oeffentlich,
  // damit die Einstellungsseite den tatsaechlich verwendeten Praefix als
  // Platzhalter anzeigen kann.
  String topicPrefix() const;

 private:
  DataManager& _data;
  ConfigManager& _config;
  NetManager& _network;
  RelayManager& _relay;

  WiFiClient _transport;
  PubSubClient _client;

  bool _discoverySent = false;
  unsigned long _lastSensorReadMillisSeen = 0;
  unsigned long _lastReconnectAttemptMillis = 0;
  bool _lastRelayOnSeen = false;
  bool _relayStateKnown = false;

  bool mqttEnabled() const;
  void ensureConnected();
  void publishDiscovery();
  void publishSensorState();
  void publishRelayState();
  void subscribeCommandTopics();

  static MqttManager* _instance;
  static void onMqttMessage(char* topic, uint8_t* payload, unsigned int length);
  void handleMessage(const String& topic, const String& payload);
};
