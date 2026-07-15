#include "ConfigManager.h"

#include <LittleFS.h>
#include <tinyxml2.h>

using namespace tinyxml2;

static const char* CONFIG_PATH = "/config.xml";
static const char* CONFIG_TMP_PATH = "/config.xml.tmp";

namespace {

bool parseBool(const char* value, bool fallback) {
  if (!value) return fallback;
  return strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
}

String attrOrEmpty(const XMLElement* el, const char* name) {
  if (!el) return "";
  const char* v = el->Attribute(name);
  return v ? String(v) : String("");
}

String textOr(const XMLElement* parent, const char* childName, const String& fallback) {
  if (!parent) return fallback;
  const XMLElement* child = parent->FirstChildElement(childName);
  if (!child || !child->GetText()) return fallback;
  return String(child->GetText());
}

// Lastenheft Abschnitt 4: "Systemtyp ist KEIN eigenstaendiges
// Einstellungsfeld, sondern wird immer aus 'Sensor 2 aktiv' abgeleitet" -
// identisches Muster wie im Sensormeter-Projekt (WT32-ETH01).
String deriveSystemType(bool sensor2Enabled) {
  return sensor2Enabled ? "Sensormeter PoE PRO" : "Sensormeter PoE";
}

}  // namespace

void ConfigManager::begin() {
  if (!load()) {
    Serial.println("[CONFIG] Keine gueltige config.xml gefunden -> Defaults werden angelegt");
    _config = DeviceConfig();
    _config.systemType = deriveSystemType(_config.sensor2Enabled);
    save();
  }
}

bool ConfigManager::load() {
  if (!LittleFS.exists(CONFIG_PATH)) return false;

  File file = LittleFS.open(CONFIG_PATH, "r");
  if (!file) return false;
  String xml = file.readString();
  file.close();

  return importXml(xml);
}

bool ConfigManager::importXml(const String& xml) {
  XMLDocument doc;
  if (doc.Parse(xml.c_str(), xml.length()) != XML_SUCCESS) {
    Serial.println("[CONFIG] XML-Parse-Fehler");
    return false;
  }

  const XMLElement* root = doc.FirstChildElement("config");
  if (!root) {
    Serial.println("[CONFIG] XML ohne <config>-Wurzelelement");
    return false;
  }

  // Beginnt mit Defaults - Felder, die im XML fehlen, bleiben auf Default
  // (Pflichtenheft: "Default-Werte fallback").
  DeviceConfig cfg;

  const XMLElement* network = root->FirstChildElement("network");
  if (network) {
    const XMLElement* lan = network->FirstChildElement("lan");
    if (lan) {
      cfg.lanDhcp = parseBool(lan->Attribute("dhcp"), cfg.lanDhcp);
      cfg.lanIp = attrOrEmpty(lan, "ip");
      cfg.lanMask = attrOrEmpty(lan, "mask");
      cfg.lanGateway = attrOrEmpty(lan, "gateway");
      cfg.lanDns = attrOrEmpty(lan, "dns");
    }
    const XMLElement* wlan = network->FirstChildElement("wlan");
    if (wlan) {
      cfg.wlanDhcp = parseBool(wlan->Attribute("dhcp"), cfg.wlanDhcp);
      cfg.wlanSsid = attrOrEmpty(wlan, "ssid");
      cfg.wlanPsk = attrOrEmpty(wlan, "psk");
      cfg.wlanIp = attrOrEmpty(wlan, "ip");
      cfg.wlanMask = attrOrEmpty(wlan, "mask");
      cfg.wlanGateway = attrOrEmpty(wlan, "gateway");
      cfg.wlanDns = attrOrEmpty(wlan, "dns");
      cfg.wlanPendingTest = parseBool(wlan->Attribute("pendingTest"), cfg.wlanPendingTest);
    }
  }

  const XMLElement* system = root->FirstChildElement("system");
  cfg.systemName = textOr(system, "name", cfg.systemName);
  cfg.settingsPassword = textOr(system, "password", cfg.settingsPassword);

  const XMLElement* syslog = root->FirstChildElement("syslog");
  cfg.syslogServer = textOr(syslog, "server", cfg.syslogServer);

  const XMLElement* sensors = root->FirstChildElement("sensors");
  if (sensors) {
    const XMLElement* sensor1 = sensors->FirstChildElement("sensor1");
    if (sensor1) {
      cfg.sensor1TempOffset = sensor1->FloatAttribute("tempOffset", cfg.sensor1TempOffset);
      cfg.sensor1HumOffset = sensor1->FloatAttribute("humOffset", cfg.sensor1HumOffset);
      cfg.sensor1CalibratedTs = sensor1->UnsignedAttribute("calibratedTs", cfg.sensor1CalibratedTs);
    }
    const XMLElement* sensor2 = sensors->FirstChildElement("sensor2");
    if (sensor2) {
      cfg.sensor2Enabled = parseBool(sensor2->Attribute("enabled"), cfg.sensor2Enabled);
      String name = attrOrEmpty(sensor2, "name");
      if (name.length() > 0) cfg.sensor2Name = name;
      cfg.sensor2TempOffset = sensor2->FloatAttribute("tempOffset", cfg.sensor2TempOffset);
      cfg.sensor2HumOffset = sensor2->FloatAttribute("humOffset", cfg.sensor2HumOffset);
      cfg.sensor2CalibratedTs = sensor2->UnsignedAttribute("calibratedTs", cfg.sensor2CalibratedTs);
    }
  }

  const XMLElement* kontakt = root->FirstChildElement("kontakt");
  if (kontakt) {
    String mode = attrOrEmpty(kontakt, "pin5Mode");
    if (mode == "sensor" || mode == "contact") cfg.pin5Mode = mode;
    String dhtType = attrOrEmpty(kontakt, "pin5DhtType");
    if (dhtType == "DHT11" || dhtType == "DHT21") cfg.pin5DhtType = dhtType;
    String contactName = attrOrEmpty(kontakt, "name");
    if (contactName.length() > 0) cfg.contactName = contactName.substring(0, 20);
    String alarmAt = attrOrEmpty(kontakt, "alarmAt");
    if (alarmAt == "open" || alarmAt == "closed" || alarmAt == "change") cfg.contactAlarmAt = alarmAt;
  }

  const XMLElement* snmp = root->FirstChildElement("snmp");
  if (snmp) {
    String community = attrOrEmpty(snmp, "community");
    if (community.length() > 0) cfg.snmpCommunity = community;
  }

  const XMLElement* aktor = root->FirstChildElement("aktor");
  if (aktor) {
    cfg.relayEnabled = parseBool(aktor->Attribute("relayEnabled"), cfg.relayEnabled);
    String autoMode = attrOrEmpty(aktor, "autoMode");
    if (autoMode == "off" || autoMode == "sensor") cfg.relayAutoMode = autoMode;
    String autoSource = attrOrEmpty(aktor, "autoSource");
    if (autoSource == "sensor1" || autoSource == "sensor2" || autoSource == "contact") {
      cfg.relayAutoSource = autoSource;
    }
    String autoValue = attrOrEmpty(aktor, "autoValue");
    if (autoValue == "temp" || autoValue == "humidity") cfg.relayAutoValue = autoValue;
    String autoCompare = attrOrEmpty(aktor, "autoCompare");
    if (autoCompare == "above" || autoCompare == "below") cfg.relayAutoCompare = autoCompare;
    cfg.relayAutoThreshold = aktor->FloatAttribute("autoThreshold", cfg.relayAutoThreshold);
    String autoContactState = attrOrEmpty(aktor, "autoContactState");
    if (autoContactState == "open" || autoContactState == "closed") {
      cfg.relayAutoContactState = autoContactState;
    }
  }

  const XMLElement* mqtt = root->FirstChildElement("mqtt");
  if (mqtt) {
    cfg.mqttEnabled = parseBool(mqtt->Attribute("enabled"), cfg.mqttEnabled);
    cfg.mqttServer = attrOrEmpty(mqtt, "server");
    cfg.mqttPort = static_cast<uint16_t>(mqtt->UnsignedAttribute("port", cfg.mqttPort));
    cfg.mqttUser = attrOrEmpty(mqtt, "user");
    cfg.mqttPassword = attrOrEmpty(mqtt, "password");
    cfg.mqttTopicPrefix = attrOrEmpty(mqtt, "topicPrefix");
    String interfaceChoice = attrOrEmpty(mqtt, "interface");
    if (interfaceChoice == "lan" || interfaceChoice == "wlan") cfg.mqttInterface = interfaceChoice;
  }

  const XMLElement* branding = root->FirstChildElement("branding");
  if (branding) {
    cfg.brandingVendorName = attrOrEmpty(branding, "vendorName");
  }

  const XMLElement* display = root->FirstChildElement("display");
  if (display) {
    cfg.extDisplayPages = static_cast<uint8_t>(display->UnsignedAttribute("pages", cfg.extDisplayPages));
    cfg.extDisplaySlideSec = static_cast<uint16_t>(display->UnsignedAttribute("slideSec", cfg.extDisplaySlideSec));
    if (cfg.extDisplaySlideSec < 2) cfg.extDisplaySlideSec = 2;
    if (cfg.extDisplaySlideSec > 60) cfg.extDisplaySlideSec = 60;
  }

  cfg.systemType = deriveSystemType(cfg.sensor2Enabled);
  _config = cfg;
  return true;
}

String ConfigManager::exportXml() const {
  XMLDocument doc;
  XMLElement* root = doc.NewElement("config");
  doc.InsertFirstChild(root);

  XMLElement* network = doc.NewElement("network");
  root->InsertEndChild(network);

  XMLElement* lan = doc.NewElement("lan");
  lan->SetAttribute("dhcp", _config.lanDhcp ? "true" : "false");
  lan->SetAttribute("ip", _config.lanIp.c_str());
  lan->SetAttribute("mask", _config.lanMask.c_str());
  lan->SetAttribute("gateway", _config.lanGateway.c_str());
  lan->SetAttribute("dns", _config.lanDns.c_str());
  network->InsertEndChild(lan);

  XMLElement* wlan = doc.NewElement("wlan");
  wlan->SetAttribute("dhcp", _config.wlanDhcp ? "true" : "false");
  wlan->SetAttribute("ssid", _config.wlanSsid.c_str());
  wlan->SetAttribute("psk", _config.wlanPsk.c_str());
  wlan->SetAttribute("ip", _config.wlanIp.c_str());
  wlan->SetAttribute("mask", _config.wlanMask.c_str());
  wlan->SetAttribute("gateway", _config.wlanGateway.c_str());
  wlan->SetAttribute("dns", _config.wlanDns.c_str());
  wlan->SetAttribute("pendingTest", _config.wlanPendingTest ? "true" : "false");
  network->InsertEndChild(wlan);

  XMLElement* system = doc.NewElement("system");
  root->InsertEndChild(system);
  XMLElement* name = doc.NewElement("name");
  name->SetText(_config.systemName.c_str());
  system->InsertEndChild(name);
  XMLElement* type = doc.NewElement("type");
  type->SetText(_config.systemType.c_str());
  system->InsertEndChild(type);
  XMLElement* password = doc.NewElement("password");
  password->SetText(_config.settingsPassword.c_str());
  system->InsertEndChild(password);

  XMLElement* syslog = doc.NewElement("syslog");
  root->InsertEndChild(syslog);
  XMLElement* server = doc.NewElement("server");
  server->SetText(_config.syslogServer.c_str());
  syslog->InsertEndChild(server);

  XMLElement* sensors = doc.NewElement("sensors");
  root->InsertEndChild(sensors);
  XMLElement* sensor1 = doc.NewElement("sensor1");
  sensor1->SetAttribute("tempOffset", _config.sensor1TempOffset);
  sensor1->SetAttribute("humOffset", _config.sensor1HumOffset);
  sensor1->SetAttribute("calibratedTs", static_cast<unsigned int>(_config.sensor1CalibratedTs));
  sensors->InsertEndChild(sensor1);
  XMLElement* sensor2 = doc.NewElement("sensor2");
  sensor2->SetAttribute("enabled", _config.sensor2Enabled ? "true" : "false");
  sensor2->SetAttribute("name", _config.sensor2Name.c_str());
  sensor2->SetAttribute("tempOffset", _config.sensor2TempOffset);
  sensor2->SetAttribute("humOffset", _config.sensor2HumOffset);
  sensor2->SetAttribute("calibratedTs", static_cast<unsigned int>(_config.sensor2CalibratedTs));
  sensors->InsertEndChild(sensor2);

  XMLElement* kontakt = doc.NewElement("kontakt");
  kontakt->SetAttribute("pin5Mode", _config.pin5Mode.c_str());
  kontakt->SetAttribute("pin5DhtType", _config.pin5DhtType.c_str());
  kontakt->SetAttribute("name", _config.contactName.c_str());
  kontakt->SetAttribute("alarmAt", _config.contactAlarmAt.c_str());
  root->InsertEndChild(kontakt);

  XMLElement* snmp = doc.NewElement("snmp");
  snmp->SetAttribute("community", _config.snmpCommunity.c_str());
  root->InsertEndChild(snmp);

  XMLElement* aktor = doc.NewElement("aktor");
  aktor->SetAttribute("relayEnabled", _config.relayEnabled ? "true" : "false");
  aktor->SetAttribute("autoMode", _config.relayAutoMode.c_str());
  aktor->SetAttribute("autoSource", _config.relayAutoSource.c_str());
  aktor->SetAttribute("autoValue", _config.relayAutoValue.c_str());
  aktor->SetAttribute("autoCompare", _config.relayAutoCompare.c_str());
  aktor->SetAttribute("autoThreshold", _config.relayAutoThreshold);
  aktor->SetAttribute("autoContactState", _config.relayAutoContactState.c_str());
  root->InsertEndChild(aktor);

  XMLElement* mqtt = doc.NewElement("mqtt");
  mqtt->SetAttribute("enabled", _config.mqttEnabled ? "true" : "false");
  mqtt->SetAttribute("server", _config.mqttServer.c_str());
  mqtt->SetAttribute("port", _config.mqttPort);
  mqtt->SetAttribute("user", _config.mqttUser.c_str());
  mqtt->SetAttribute("password", _config.mqttPassword.c_str());
  mqtt->SetAttribute("topicPrefix", _config.mqttTopicPrefix.c_str());
  mqtt->SetAttribute("interface", _config.mqttInterface.c_str());
  root->InsertEndChild(mqtt);

  XMLElement* branding = doc.NewElement("branding");
  branding->SetAttribute("vendorName", _config.brandingVendorName.c_str());
  root->InsertEndChild(branding);

  XMLElement* display = doc.NewElement("display");
  display->SetAttribute("pages", _config.extDisplayPages);
  display->SetAttribute("slideSec", _config.extDisplaySlideSec);
  root->InsertEndChild(display);

  XMLPrinter printer;
  doc.Print(&printer);
  return String(printer.CStr());
}

bool ConfigManager::save() {
  String xml = exportXml();

  File file = LittleFS.open(CONFIG_TMP_PATH, "w");
  if (!file) {
    Serial.println("[CONFIG] Konnte config.xml.tmp nicht oeffnen");
    return false;
  }
  size_t written = file.print(xml);
  file.close();

  if (written != xml.length()) {
    Serial.println("[CONFIG] Schreibfehler beim Speichern (unvollstaendig) - config.xml bleibt unveraendert");
    LittleFS.remove(CONFIG_TMP_PATH);
    return false;
  }

  // Erst die vollstaendig geschriebene Tmp-Datei an die Stelle der alten
  // config.xml verschieben, damit ein Stromausfall waehrend des Schreibens
  // nicht die bisherige, funktionierende Konfiguration zerstoert.
  LittleFS.remove(CONFIG_PATH);
  if (!LittleFS.rename(CONFIG_TMP_PATH, CONFIG_PATH)) {
    Serial.println("[CONFIG] Konnte config.xml.tmp nicht in config.xml umbenennen");
    return false;
  }

  Serial.println("[CONFIG] config.xml gespeichert");
  return true;
}

void ConfigManager::setConfig(const DeviceConfig& config) {
  _config = config;
  _config.systemType = deriveSystemType(_config.sensor2Enabled);
  save();
}
