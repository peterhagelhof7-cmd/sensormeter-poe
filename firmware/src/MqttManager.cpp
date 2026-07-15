#include "MqttManager.h"

#include <ArduinoJson.h>
#include <esp_netif.h>
#include "lwip/netif.h"

namespace {
const unsigned long RECONNECT_INTERVAL_MS = 5000;

// Setzt den lwIP-Default-Netif fest auf das per ConfigManager::mqttInterface
// gewaehlte Interface, bevor PubSubClient/WiFiClient eine neue TCP-
// Verbindung aufbaut - lwIP entscheidet das ausgehende Interface anhand des
// Default-Netif, sobald die Ziel-IP (Broker) nicht im direkt angeschlossenen
// Subnetz eines Interfaces liegt (siehe MqttManager.h-Klassenkommentar).
//
// Nutzt bewusst die lwIP-Funktion netif_set_default() statt
// esp_netif_set_default_netif(): letztere gibt es zwar auf diesem Core (3.x),
// nicht aber auf dem aelteren Arduino-ESP32-2.0.17-Core von Sensormeter - mit
// der lwIP-Funktion darunter ist der Code auf beiden Projekten identisch.
// esp_netif_get_netif_impl_index() liefert den lwIP-"Netif-Index"
// (netif->num + 1, siehe esp_netif_lwip.c), passend zu lwIPs
// netif_get_by_index() - damit laesst sich vom esp_netif-Handle
// (ifkey "ETH_DEF"/"WIFI_STA_DEF", esp_netif_defaults.h) auf das darunter
// liegende struct netif* schliessen, ohne dass esp_netif dessen Pointer
// direkt exponieren muss.
//
// Liefert den vorherigen Default-Netif zurueck, damit ensureConnected() ihn
// nach dem Verbindungsversuch wiederherstellen kann - andere Subsysteme
// (NTP, mDNS, ...) sollen von dieser MQTT-spezifischen Festlegung nicht
// dauerhaft betroffen sein. Liefert nullptr, wenn das gewaehlte Interface
// (noch) keinen Netif-Handle/-Index hat (z.B. WLAN nicht konfiguriert) - der
// Aufrufer laesst den Default dann unveraendert, und der nachfolgende
// connect()-Versuch scheitert regulaer wie bei fehlendem Netz, statt auf ein
// falsches Interface auszuweichen.
struct netif* pinMqttInterface(const String& choice) {
  const char* ifkey = (choice == "wlan") ? "WIFI_STA_DEF" : "ETH_DEF";
  esp_netif_t* target = esp_netif_get_handle_from_ifkey(ifkey);
  if (!target) return nullptr;
  int index = esp_netif_get_netif_impl_index(target);
  if (index <= 0) return nullptr;
  struct netif* targetLwipNetif = netif_get_by_index((u8_t)index);
  if (!targetLwipNetif) return nullptr;
  struct netif* previous = netif_default;
  netif_set_default(targetLwipNetif);
  return previous;
}
}  // namespace

MqttManager* MqttManager::_instance = nullptr;

MqttManager::MqttManager(DataManager& dataManager, ConfigManager& configManager, NetManager& networkManager,
                          RelayManager& relayManager)
    : _data(dataManager), _config(configManager), _network(networkManager), _relay(relayManager),
      _client(_transport) {
  _instance = this;
}

void MqttManager::begin() {
  _client.setCallback(onMqttMessage);
  Serial.println("[MQTT] Grundgeruest bereit");
}

bool MqttManager::mqttEnabled() const {
  const DeviceConfig& cfg = _config.getConfig();
  return cfg.mqttEnabled && cfg.mqttServer.length() > 0;
}

String MqttManager::topicPrefix() const {
  const DeviceConfig& cfg = _config.getConfig();
  if (cfg.mqttTopicPrefix.length() > 0) return cfg.mqttTopicPrefix;
  return NetManager::sanitizeHostname(cfg.systemName);
}

void MqttManager::onMqttMessage(char* topic, uint8_t* payload, unsigned int length) {
  if (!_instance) return;
  String payloadStr;
  payloadStr.reserve(length);
  for (unsigned int i = 0; i < length; i++) payloadStr += (char)payload[i];
  _instance->handleMessage(String(topic), payloadStr);
}

void MqttManager::handleMessage(const String& topic, const String& payload) {
  String relayCommandTopic = topicPrefix() + "/relay/set";
  if (topic != relayCommandTopic) return;

  // Gleicher Schreibpfad wie Web/REST (Lastenheft Abschnitt 16.2) - HA
  // schickt standardmaessig die Payloads "ON"/"OFF" fuer eine Switch-Entity.
  _relay.setOn(payload == "ON");
}

void MqttManager::ensureConnected() {
  const DeviceConfig& cfg = _config.getConfig();

  // Server/Port koennten sich seit dem letzten setServer()-Aufruf geaendert
  // haben (Einstellungsseite) - PubSubClient uebernimmt neue Werte erst nach
  // dem naechsten setServer()-Aufruf, daher hier bei jedem
  // Verbindungsversuch neu setzen (billig, kein Netzwerkzugriff).
  _client.setServer(cfg.mqttServer.c_str(), cfg.mqttPort);

  unsigned long now = millis();
  if (now - _lastReconnectAttemptMillis < RECONNECT_INTERVAL_MS) return;
  _lastReconnectAttemptMillis = now;

  String clientId = "sensormeter-poe-" + topicPrefix();
  struct netif* previousDefaultNetif = pinMqttInterface(cfg.mqttInterface);
  bool ok;
  if (cfg.mqttUser.length() > 0) {
    ok = _client.connect(clientId.c_str(), cfg.mqttUser.c_str(), cfg.mqttPassword.c_str());
  } else {
    ok = _client.connect(clientId.c_str());
  }
  if (previousDefaultNetif) netif_set_default(previousDefaultNetif);

  if (ok) {
    Serial.println("[MQTT] Verbunden mit Broker " + cfg.mqttServer);
    _discoverySent = false;  // nach jedem (Re-)Connect neu ankuendigen
    _relayStateKnown = false;  // Relais-Zustand nach Reconnect erneut publizieren
    subscribeCommandTopics();
  }
}

void MqttManager::subscribeCommandTopics() {
  if (!_config.getConfig().relayEnabled) return;
  String relayCommandTopic = topicPrefix() + "/relay/set";
  _client.subscribe(relayCommandTopic.c_str());
}

void MqttManager::publishDiscovery() {
  String prefix = topicPrefix();
  const DeviceConfig& cfg = _config.getConfig();
  String stateTopic = prefix + "/state";

  // Gemeinsamer "device"-Block, damit Home Assistant alle Entities einem
  // Geraet zuordnet statt loser Sensoren/Schalter.
  auto deviceBlock = [&](JsonDocument& doc) {
    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = prefix;
    device["name"] = cfg.systemName;
    device["manufacturer"] = "Sensormeter-Familie";
    device["model"] = cfg.systemType;
  };

  auto publishSensorDiscovery = [&](const char* key, const char* name, const char* deviceClass,
                                     const char* unit, const char* valueTemplate) {
    JsonDocument doc;
    doc["name"] = name;
    doc["device_class"] = deviceClass;
    doc["unit_of_measurement"] = unit;
    doc["state_topic"] = stateTopic;
    doc["value_template"] = valueTemplate;
    doc["unique_id"] = prefix + "_" + key;
    deviceBlock(doc);

    String payload;
    serializeJson(doc, payload);
    String topic = "homeassistant/sensor/" + prefix + "/" + key + "/config";
    _client.publish(topic.c_str(), payload.c_str(), true);  // retain=true
  };

  publishSensorDiscovery("temperature1", "Temperatur (intern)", "temperature", "°C",
                          "{{ value_json.temperature1 }}");
  publishSensorDiscovery("humidity1", "Luftfeuchte (intern)", "humidity", "%", "{{ value_json.humidity1 }}");

  if (cfg.sensor2Enabled) {
    String label2 = cfg.sensor2Name.length() > 0 ? cfg.sensor2Name : String("extern");
    publishSensorDiscovery("temperature2", ("Temperatur (" + label2 + ")").c_str(), "temperature", "°C",
                            "{{ value_json.temperature2 }}");
    publishSensorDiscovery("humidity2", ("Luftfeuchte (" + label2 + ")").c_str(), "humidity", "%",
                            "{{ value_json.humidity2 }}");
  }

  if (cfg.relayEnabled) {
    JsonDocument doc;
    doc["name"] = "Relais";
    doc["state_topic"] = prefix + "/relay/state";
    doc["command_topic"] = prefix + "/relay/set";
    doc["unique_id"] = prefix + "_relay";
    deviceBlock(doc);

    String payload;
    serializeJson(doc, payload);
    String topic = "homeassistant/switch/" + prefix + "/relay/config";
    _client.publish(topic.c_str(), payload.c_str(), true);
  }

  _discoverySent = true;
  Serial.println("[MQTT] Discovery-Payloads gesendet");
}

void MqttManager::publishSensorState() {
  SensorReading s1 = _data.getSensor1();
  if (!s1.valid) return;

  const DeviceConfig& cfg = _config.getConfig();

  JsonDocument doc;
  doc["temperature1"] = serialized(String(s1.temperature, 1));
  doc["humidity1"] = serialized(String(s1.humidity, 1));

  if (cfg.sensor2Enabled) {
    SensorReading s2 = _data.getSensor2();
    // s2.valid heisst nur "Lesung erfolgreich", NICHT mehr zwingend
    // "liefert Temperatur/Feuchte" - siehe Kommentar in SNMPManager.cpp.
    // Druck/Lux/Luftguete sind noch nicht per MQTT exportiert (siehe
    // docs/entscheidungen.md).
    if (s2.valid && !isnan(s2.temperature) && !isnan(s2.humidity)) {
      doc["temperature2"] = serialized(String(s2.temperature, 1));
      doc["humidity2"] = serialized(String(s2.humidity, 1));
    }
  }

  String payload;
  serializeJson(doc, payload);
  String topic = topicPrefix() + "/state";
  _client.publish(topic.c_str(), payload.c_str());
}

void MqttManager::publishRelayState() {
  String topic = topicPrefix() + "/relay/state";
  _client.publish(topic.c_str(), _relay.isOn() ? "ON" : "OFF", true);  // retain, damit HA den
                                                                        // Zustand nach einem
                                                                        // Neustart sofort kennt
}

void MqttManager::loop() {
  if (!mqttEnabled() || !(_network.isLanUp() || _network.isWlanUp())) return;

  if (!_client.connected()) {
    ensureConnected();
    return;  // im selben Tick noch nicht weitermachen, erst naechster loop()
  }
  _client.loop();

  if (!_discoverySent) {
    publishDiscovery();
  }

  // State-Update bei jedem Sensorzyklus (Aenderung von lastReadMillis) -
  // gleiches Erkennungsmuster wie SyslogManager::loop().
  unsigned long currentReadMillis = _data.getSensor1().lastReadMillis;
  if (currentReadMillis != 0 && currentReadMillis != _lastSensorReadMillisSeen) {
    _lastSensorReadMillisSeen = currentReadMillis;
    publishSensorState();
  }

  // Relais-Zustand sofort bei Aenderung publizieren (Lastenheft 16.2),
  // unabhaengig vom Sensorzyklus.
  if (_config.getConfig().relayEnabled) {
    bool on = _relay.isOn();
    if (!_relayStateKnown || on != _lastRelayOnSeen) {
      _relayStateKnown = true;
      _lastRelayOnSeen = on;
      publishRelayState();
    }
  }
}
