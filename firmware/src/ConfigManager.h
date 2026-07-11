#pragma once

#include <Arduino.h>

// Laufzeitkonfiguration gemaess docs/lastenheft.txt Abschnitt 10 (config.xml
// auf LittleFS), Persistenz per tinyxml2 (vendored, siehe lib/tinyxml2/).
// Gegenueber Sensormeter (WT32-ETH01) neu: <aktor>/<mqtt> - Erkannter
// Modultyp/Chip (Lastenheft Abschnitt 15) ist bewusst NICHT Teil dieser
// Struktur, da rein informativ zur Laufzeit (siehe SensorDetector), kein
// persistiertes Einstellungsfeld.
//
// Schema (config.xml):
//
// <config>
//   <network>
//     <lan dhcp="true" ip="" mask="" gateway="" dns=""/>
//     <wlan dhcp="true" ssid="" psk="" ip="" mask="" gateway="" dns="" pendingTest="false"/>
//   </network>
//   <system>
//     <name>Sensormeter PoE</name>
//     <type>Sensormeter PoE</type>
//     <password>installer</password>
//   </system>
//   <syslog>
//     <server>0.0.0.0</server>
//   </syslog>
//   <sensors>
//     <sensor1 tempOffset="0.0" humOffset="0.0" calibratedTs="0"/>
//     <sensor2 enabled="false" name="Extern" tempOffset="0.0" humOffset="0.0" calibratedTs="0"/>
//   </sensors>
//   <snmp community="public"/>
//   <aktor relayEnabled="false"/>
//   <mqtt enabled="false" server="" port="1883" user="" password="" topicPrefix=""/>
// </config>

struct DeviceConfig {
  String systemName = "Sensormeter PoE";
  String systemType = "Sensormeter PoE";  // "Sensormeter PoE" oder "Sensormeter PoE PRO"
  String settingsPassword = "installer";

  // Kalibrierkorrektur je Sensor (fester Grad-/Prozent-Versatz, positiv
  // oder negativ) - wird direkt in SensorManager auf den validierten
  // Rohmesswert angewendet, damit Anzeige, SNMP UND Stundenwerte/CSV immer
  // denselben, bereits korrigierten Wert sehen.
  float sensor1TempOffset = 0.0f;
  float sensor1HumOffset = 0.0f;
  float sensor2TempOffset = 0.0f;
  float sensor2HumOffset = 0.0f;

  // Wall-Clock-Zeitpunkt (time(nullptr)), zu dem die jeweiligen Offsets
  // zuletzt TATSAECHLICH geaendert wurden. 0 = noch nie kalibriert.
  uint32_t sensor1CalibratedTs = 0;
  uint32_t sensor2CalibratedTs = 0;

  bool lanDhcp = true;
  String lanIp;
  String lanMask;
  String lanGateway;
  String lanDns;  // leer = Gateway als DNS verwenden

  bool wlanDhcp = true;
  String wlanIp;
  String wlanMask;
  String wlanGateway;
  String wlanDns;  // leer = Gateway als DNS verwenden
  String wlanSsid;
  String wlanPsk;
  // Einmal-Flag: nach Eingabe neuer WLAN-Zugangsdaten ueber die
  // Einstellungsseite im Fallback-Access-Point gesetzt, damit
  // NetManager den anschliessenden Verbindungsversuch nur kurz statt
  // 5 Minuten abwartet. Wird beim naechsten Boot sofort gelesen und
  // geloescht - ueberlebt also nur genau einen Neustart.
  bool wlanPendingTest = false;

  String syslogServer = "0.0.0.0";

  // Sensor 2 aktiv: wird beim Boot automatisch vorbelegt, sobald die
  // Modul-Erkennung (Lastenheft Abschnitt 15) ein DHT- oder I2C-Sensor-Modul
  // findet - bleibt als manueller Override weiterhin aenderbar (siehe
  // SensorDetector).
  bool sensor2Enabled = false;
  String sensor2Name = "Extern";

  String snmpCommunity = "public";

  // Relais (Aktor) - rein manuell, keine Auto-Erkennung moeglich (Lastenheft
  // Abschnitt 15.3), unabhaengig von sensor2Enabled (RJ45-Pins ueberschneiden
  // sich nicht, siehe Abschnitt 14). Der aktuelle Schaltzustand selbst wird
  // NICHT persistiert - RelayManager startet nach jedem Boot sicherheitshalber
  // immer mit AUS, siehe dortige Begruendung.
  bool relayEnabled = false;

  // Home-Assistant-Anbindung ueber MQTT-Discovery (Lastenheft Abschnitt 16).
  // Anders als bei Sensormeter WLAN ist topicPrefix hier ein echtes,
  // persistiertes Feld (nicht nur zur Laufzeit abgeleitet) - Default bei
  // leerem Wert: wie der mDNS-Hostname aus systemName ableiten, siehe
  // MqttManager::topicPrefix().
  bool mqttEnabled = false;
  String mqttServer;
  uint16_t mqttPort = 1883;
  String mqttUser;
  String mqttPassword;
  String mqttTopicPrefix;
};

class ConfigManager {
 public:
  // Laedt config.xml von LittleFS. Fehlt die Datei oder ist sie ungueltig,
  // werden Defaults verwendet und sofort als neue config.xml gespeichert.
  void begin();

  const DeviceConfig& getConfig() const { return _config; }

  // Uebernimmt eine neue Konfiguration und speichert sie sofort.
  void setConfig(const DeviceConfig& config);

  // XML-Import/-Export. importXml uebernimmt nur bei erfolgreichem Parsen
  // und speichert dann.
  bool importXml(const String& xml);
  String exportXml() const;

  bool save();

 private:
  DeviceConfig _config;
  bool load();
};
