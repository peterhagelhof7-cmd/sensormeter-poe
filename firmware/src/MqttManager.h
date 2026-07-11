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
