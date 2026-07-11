#include "WebServerManager.h"

#include <ArduinoJson.h>
#include <ESP32Ping.h>
#include <ETH.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_timer.h>
#include <time.h>
#include "TimeUtils.h"

#if __has_include("config.h")
#include "config.h"
#endif
#ifndef DEVICE_FIRMWARE_VERSION
#define DEVICE_FIRMWARE_VERSION "0.0.0"
#endif

// Nur ein statischer Link fuer den Admin-Browser, kein Geraet-seitiger
// Netzwerkzugriff - daher unproblematisch ohne HTTPS-Client.
#define GITHUB_REPO_SLUG "peterhagelhof7-cmd/sensormeter-poe"

namespace {
// Sicherheits-Feature: vor dem Uebernehmen einer neu gesetzten statischen IP
// (LAN oder WLAN) prueft dies per Ping, ob im Netz bereits ein Geraet unter
// dieser Adresse antwortet.
bool ipRespondsToPing(const IPAddress& ip) {
  if (ip == IPAddress(0, 0, 0, 0)) return false;
  return Ping.ping(ip, 1);
}

String formatCalibratedTs(uint32_t ts) {
  if (ts == 0) return "noch nie";
  time_t t = static_cast<time_t>(ts);
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[20];
  snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900,
           tmv.tm_hour, tmv.tm_min);
  return String(buf);
}

// ISO 8601 (YYYY-MM-DD HH:MM:SS) fuer den CSV-Export - Tabellenkalkulationen
// erkennen und sortieren ISO 8601 zuverlaessig als Datum.
String formatCsvTimestamp(uint32_t ts) {
  time_t t = static_cast<time_t>(ts);
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return String(buf);
}
}  // namespace

WebServerManager::WebServerManager(DataManager& dataManager, ConfigManager& configManager,
                                    NetManager& networkManager, OtaManager& otaManager,
                                    RelayManager& relayManager, SensorDetector& sensorDetector)
    : _data(dataManager), _config(configManager), _network(networkManager), _ota(otaManager),
      _relay(relayManager), _detector(sensorDetector), _server(80) {}

bool WebServerManager::checkAuth(AsyncWebServerRequest* request) {
  if (!request->authenticate("admin", _config.getConfig().settingsPassword.c_str())) {
    request->requestAuthentication("Sensormeter PoE (Benutzername: admin)");
    return false;
  }
  return true;
}

// ----------------------------------------------------------------------------
// Seiten-Grundgeruest - Design an das Sensormeter-Display-Projekt angepasst
// (identisch zu den beiden Schwesterprojekten): Navy-Banner #0f1f3d,
// Orange-Akzent #c8622a, warmes Creme #f2f0e9 fuer Tabellenkoepfe,
// Kartenrahmen #e4e1d8.
// ----------------------------------------------------------------------------
String WebServerManager::buildPageShell(const String& title, const String& bodyContent) const {
  String html;
  html.reserve(bodyContent.length() + 1400);
  html += "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>" + title + "</title><style>";
  html += "*{box-sizing:border-box}";
  html += "body{background:#f7f5f1;color:#1c2430;font-size:15px;text-align:center;"
          "font-family:-apple-system,'Segoe UI',Roboto,'Helvetica Neue',Arial,sans-serif;"
          "margin:0;padding:20px 14px 28px;line-height:1.5;}";
  html += "h1{font-size:22px;background:#0f1f3d;color:#fff;margin:0 auto 18px;padding:18px 20px;"
          "border-radius:6px;max-width:680px;}";
  html += ".block{background:#fff;border:1px solid #e4e1d8;border-radius:6px;padding:14px 20px;"
          "margin:16px auto;max-width:680px;}";
  html += ".block h2{font-size:14px;color:#8f4a1e;margin:0 0 10px;padding-bottom:6px;"
          "border-bottom:2px solid #c8622a;text-transform:uppercase;letter-spacing:.04em;}";
  html += ".row{display:flex;justify-content:space-between;gap:16px;margin:8px 0;text-align:left;font-size:15px;}";
  html += "p.hint{font-size:12.5px;color:#6b6559;text-align:left;margin:6px 0;}";
  html += "button,input[type=submit]{background:#c8622a;color:#fff;border:none;padding:9px 18px;"
          "font-size:14px;font-weight:600;border-radius:4px;cursor:pointer;margin:8px;}";
  html += "button:hover,input[type=submit]:hover{opacity:.9;}";
  html += "table{margin:12px auto;border-collapse:collapse;font-size:13px;}";
  html += "td,th{border:1px solid #e4e1d8;padding:6px 12px;}";
  html += "th{background:#f2f0e9;}";
  html += "input[type=text],input[type=password]{font-size:14px;padding:7px;width:80%;"
          "border:1px solid #d8d4c8;border-radius:4px;}";
  html += "label{display:block;margin-top:10px;text-align:left;max-width:420px;margin-left:auto;"
          "margin-right:auto;font-size:13px;}";
  html += "a{color:#8f4a1e;text-decoration:none;}";
  html += "canvas{max-width:100%;background:#fbfaf7;border:1px solid #e4e1d8;border-radius:6px;}";
  html += "#scanResult div{cursor:pointer;padding:5px;font-size:13px;border-radius:3px;}";
  html += "#scanResult div:hover{background:#f2f0e9;}";
  html += "</style></head><body>";
  html += bodyContent;
  html += "</body></html>";
  return html;
}

String WebServerManager::buildMainPageBody() const {
  const DeviceConfig& cfg = _config.getConfig();
  SensorReading s1 = _data.getSensor1();
  SensorReading s2 = _data.getSensor2();

  unsigned long uptimeSec = (unsigned long)(esp_timer_get_time() / 1000000ULL);
  char uptimeBuf[16];
  snprintf(uptimeBuf, sizeof(uptimeBuf), "%02lu:%02lu:%02lu", uptimeSec / 3600, (uptimeSec / 60) % 60, uptimeSec % 60);

  String timeStr = "--:--:--";
  if (isTimeSynced()) {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[32];
    strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &ti);
    timeStr = buf;
  }

  String html;
  html += "<h1>" + cfg.systemName + "</h1>";

  html += "<div class=\"block\"><h2>System</h2>";
  html += "<div class=\"row\"><span>Zeit</span><span>" + timeStr + "</span></div>";
  html += "<div class=\"row\"><span>Firmware</span><span>" DEVICE_FIRMWARE_VERSION "</span></div>";
  html += "<div class=\"row\"><span>Systemtyp</span><span>" + cfg.systemType + "</span></div>";
  html += "<div class=\"row\"><span>Uptime</span><span>" + String(uptimeBuf) + "</span></div>";
  html += "<div class=\"row\"><span>Freier Heap</span><span>" + String(ESP.getFreeHeap() / 1024) + " kB</span></div>";
  html += "<div class=\"row\"><span>Chip-Temperatur</span><span>" + String(temperatureRead(), 1) + " C</span></div>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Netzwerk</h2>";
  html += "<div class=\"row\"><span>LAN IP</span><span>" + (_network.isLanUp() ? _network.getLanIp().toString() : String("-")) + "</span></div>";
  html += "<div class=\"row\"><span>LAN Link</span><span>" + String(_network.isLanLinkUp() ? "verbunden" : "getrennt") + "</span></div>";
  html += "<div class=\"row\"><span>WLAN IP</span><span>" + (_network.isWlanUp() ? _network.getWlanIp().toString() : String("-")) + "</span></div>";
  html += "<div class=\"row\"><span>WLAN SSID</span><span>" + (_network.isWlanUp() ? _network.getWlanSsid() : String("-")) + "</span></div>";
  html += "<div class=\"row\"><span>WLAN RSSI</span><span>" + (_network.isWlanUp() ? String(_network.getWlanRssi()) + " dBm" : String("-")) + "</span></div>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Sensoren</h2>";
  html += "<div class=\"row\"><span>Intern</span><span>" +
          (s1.valid ? String(s1.temperature, 1) + " C / " + String(s1.humidity, 0) + " %" : String("-")) +
          "</span></div>";
  if (cfg.sensor2Enabled) {
    html += "<div class=\"row\"><span>" + cfg.sensor2Name + "</span><span>" +
            (s2.valid ? String(s2.temperature, 1) + " C / " + String(s2.humidity, 0) + " %" : String("-")) +
            "</span></div>";
  }
  html += "</div>";

  html += "<div class=\"block\"><h2>7-Tage-Verlauf</h2><canvas id=\"chart\" height=\"200\"></canvas></div>";

  html += "<div class=\"block\"><h2>Letzte Meldungen</h2><table id=\"logtable\"><tr><th>Zeit</th><th>Meldung</th></tr></table></div>";

  html += "<div class=\"block\"><a href=\"/values.csv\"><button>values.csv</button></a>";
  html += "<a href=\"/settings\"><button>Einstellungen</button></a></div>";

  html += "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script><script>";
  html += "fetch('/api/graph').then(r=>r.json()).then(d=>{";
  html += "new Chart(document.getElementById('chart'),{type:'line',data:{labels:d.labels,datasets:[";
  html += "{label:'Temperatur (C)',data:d.temperature,borderColor:'#a63d2e',yAxisID:'y'},";
  html += "{label:'Luftfeuchte (%)',data:d.humidity,borderColor:'#2a5ba0',yAxisID:'y1'}]},";
  html += "options:{scales:{y:{position:'left'},y1:{position:'right',grid:{drawOnChartArea:false}}}}});});";
  html += "fetch('/api/logs').then(r=>r.json()).then(d=>{let t=document.getElementById('logtable');";
  html += "d.entries.forEach(e=>{let r=t.insertRow();r.insertCell(0).innerText=e.time;r.insertCell(1).innerText=e.message;});});";
  html += "setInterval(()=>location.reload(),60000);";
  html += "</script>";

  return html;
}

String WebServerManager::buildSettingsPageBody() const {
  const DeviceConfig& cfg = _config.getConfig();

  String html;
  html += "<h1>Einstellungen</h1>";
  html += "<form method=\"POST\" action=\"/api/config\">";

  html += "<div class=\"block\"><h2>System</h2>";
  html += "<label>Systemname<input type=\"text\" name=\"systemName\" value=\"" + cfg.systemName + "\"></label>";
  html += "<label>Neues Passwort (leer = unveraendert)<input type=\"password\" name=\"newPassword\"></label>";
  html += "</div>";

  html += "<div class=\"block\"><h2>LAN</h2>";
  html += "<label><input type=\"checkbox\" name=\"lanDhcp\" id=\"lanDhcp\" " +
          String(cfg.lanDhcp ? "checked" : "") + "> DHCP</label>";
  html += "<label>IP<input type=\"text\" name=\"lanIp\" id=\"lanIp\" value=\"" + cfg.lanIp + "\"></label>";
  html += "<label>Netzmaske<input type=\"text\" name=\"lanMask\" id=\"lanMask\" value=\"" + cfg.lanMask + "\"></label>";
  html += "<label>Gateway<input type=\"text\" name=\"lanGateway\" id=\"lanGateway\" value=\"" + cfg.lanGateway + "\"></label>";
  html += "<label>DNS-Server (leer = Gateway verwenden)<input type=\"text\" name=\"lanDns\" id=\"lanDns\" value=\"" +
          cfg.lanDns + "\"></label>";
  html += "<button type=\"button\" onclick=\"applyNetwork('lan')\">IP-Einstellungen uebernehmen &amp; neu "
          "starten</button> <span id=\"lanApplyStatus\"></span>";
  html += "<p class=\"hint\">Prueft vor der Uebernahme, ob die Verbindung tatsaechlich moeglich ist - bei "
          "statischer IP per Ping, bei DHCP durch einen echten Verbindungsversuch. Erst bei Erfolg werden die "
          "Netzwerkfelder gespeichert und das Geraet neu gestartet.</p>";
  html += "</div>";

  html += "<div class=\"block\"><h2>WLAN</h2>";
  html += "<label><input type=\"checkbox\" name=\"wlanDhcp\" id=\"wlanDhcp\" " +
          String(cfg.wlanDhcp ? "checked" : "") + "> DHCP</label>";
  html += "<label>SSID<input type=\"text\" name=\"wlanSsid\" id=\"wlanSsid\" value=\"" + cfg.wlanSsid + "\"></label>";
  html += "<button type=\"button\" onclick=\"scanWifi()\">SSIDs suchen (bis 20s)</button><div id=\"scanResult\"></div>";
  html += "<label>PSK<input type=\"password\" name=\"wlanPsk\" id=\"wlanPsk\" value=\"" + cfg.wlanPsk + "\"></label>";
  html += "<button type=\"button\" onclick=\"connectWifi()\">Verbinden &amp; testen (Neustart)</button> "
          "<span id=\"connectStatus\"></span>";
  html += "<p class=\"hint\">Speichert nur SSID/PSK, startet sofort neu und probiert die Verbindung fuer 30s - "
          "gelingt es nicht (und ist auch kein LAN-Kabel gesteckt), faellt das Geraet automatisch zurueck auf den "
          "eigenen Access-Point \"installer\".</p>";
  html += "<label>IP<input type=\"text\" name=\"wlanIp\" id=\"wlanIp\" value=\"" + cfg.wlanIp + "\"></label>";
  html += "<label>Netzmaske<input type=\"text\" name=\"wlanMask\" id=\"wlanMask\" value=\"" + cfg.wlanMask + "\"></label>";
  html += "<label>Gateway<input type=\"text\" name=\"wlanGateway\" id=\"wlanGateway\" value=\"" + cfg.wlanGateway + "\"></label>";
  html += "<label>DNS-Server (leer = Gateway verwenden)<input type=\"text\" name=\"wlanDns\" id=\"wlanDns\" value=\"" +
          cfg.wlanDns + "\"></label>";
  html += "<button type=\"button\" onclick=\"applyNetwork('wlan')\">IP-Einstellungen uebernehmen &amp; neu "
          "starten</button> <span id=\"wlanApplyStatus\"></span>";
  html += "<p class=\"hint\">Prueft vor der Uebernahme, ob die Verbindung tatsaechlich moeglich ist. Erst bei "
          "Erfolg werden die Netzwerkfelder gespeichert und das Geraet neu gestartet.</p>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Sensoren</h2>";
  html += "<label>Sensor 1 (intern) Korrektur Temperatur (&deg;C)<input type=\"text\" name=\"sensor1TempOffset\" "
          "value=\"" + String(cfg.sensor1TempOffset, 1) + "\"></label>";
  html += "<label>Sensor 1 (intern) Korrektur Feuchte (%)<input type=\"text\" name=\"sensor1HumOffset\" "
          "value=\"" + String(cfg.sensor1HumOffset, 1) + "\"></label>";
  html += "<div class=\"row\"><span>Sensor 1 zuletzt kalibriert</span><span>" +
          formatCalibratedTs(cfg.sensor1CalibratedTs) + "</span></div>";
  html += "<label><input type=\"checkbox\" name=\"sensor2Enabled\" " + String(cfg.sensor2Enabled ? "checked" : "") +
          "> Sensor 2 (extern, RJ45) aktiv</label>";
  html += "<label>Sensor-2-Name<input type=\"text\" name=\"sensor2Name\" value=\"" + cfg.sensor2Name + "\"></label>";
  html += "<label>Sensor 2 (extern) Korrektur Temperatur (&deg;C)<input type=\"text\" name=\"sensor2TempOffset\" "
          "value=\"" + String(cfg.sensor2TempOffset, 1) + "\"></label>";
  html += "<label>Sensor 2 (extern) Korrektur Feuchte (%)<input type=\"text\" name=\"sensor2HumOffset\" "
          "value=\"" + String(cfg.sensor2HumOffset, 1) + "\"></label>";
  html += "<div class=\"row\"><span>Sensor 2 zuletzt kalibriert</span><span>" +
          formatCalibratedTs(cfg.sensor2CalibratedTs) + "</span></div>";
  html += "<div class=\"row\"><span>Erkannter Modultyp/Chip</span><span>" + _detector.detectedDescription() +
          "</span></div>";
  html += "<button type=\"button\" onclick=\"rerunDetection()\">Erkennung neu starten</button> "
          "<span id=\"detectStatus\"></span>";
  html += "<p class=\"hint\">Scannt den RJ45-I2C-Bus bzw. probiert einen DHT-Leseversuch - findet die Erkennung "
          "ein Modul, wird \"Sensor 2 aktiv\" automatisch gesetzt (bleibt manuell wieder abschaltbar). Ein "
          "Relais-Modul laesst sich damit NICHT erkennen (siehe Aktor-Abschnitt unten).</p>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Aktor</h2>";
  html += "<label><input type=\"checkbox\" name=\"relayEnabled\" " + String(cfg.relayEnabled ? "checked" : "") +
          "> Relais (Aktor) aktiv</label>";
  html += "<p class=\"hint\">Rein manuell - ein Relais-Modul kann nicht automatisch erkannt werden (siehe "
          "Sensoren-Abschnitt oben). Unabhaengig von \"Sensor 2 aktiv\", da sich die RJ45-Pins nicht "
          "ueberschneiden - ein Kombi-Modul mit Sensor UND Relais ist moeglich.</p>";
  html += "<div class=\"row\"><span>Aktueller Zustand</span><span id=\"relayState\">-</span></div>";
  html += "<button type=\"button\" onclick=\"toggleRelay()\">Relais schalten</button>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Syslog</h2>";
  html += "<label>Syslog-Server-IP<input type=\"text\" name=\"syslogServer\" value=\"" + cfg.syslogServer + "\"></label>";
  html += "</div>";

  html += "<div class=\"block\"><h2>SNMP</h2>";
  html += "<label>Community<input type=\"text\" name=\"snmpCommunity\" value=\"" + cfg.snmpCommunity + "\"></label>";
  html += "</div>";

  html += "<div class=\"block\"><h2>MQTT (Home Assistant)</h2>";
  html += "<label><input type=\"checkbox\" name=\"mqttEnabled\" " + String(cfg.mqttEnabled ? "checked" : "") +
          "> Aktiv</label>";
  html += "<label>Broker-Adresse<input type=\"text\" name=\"mqttServer\" value=\"" + cfg.mqttServer + "\"></label>";
  html += "<label>Port<input type=\"text\" name=\"mqttPort\" value=\"" + String(cfg.mqttPort) + "\"></label>";
  html += "<label>Benutzername<input type=\"text\" name=\"mqttUser\" value=\"" + cfg.mqttUser + "\"></label>";
  html += "<label>Passwort<input type=\"password\" name=\"mqttPassword\" value=\"" + cfg.mqttPassword + "\"></label>";
  html += "<label>Topic-Praefix (leer = aus Systemname abgeleitet)<input type=\"text\" name=\"mqttTopicPrefix\" "
          "value=\"" + cfg.mqttTopicPrefix + "\" placeholder=\"" + NetManager::sanitizeHostname(cfg.systemName) +
          "\"></label>";
  html += "<p class=\"hint\">Meldet Sensoren (und bei aktivem Relais den Aktor) per MQTT-Discovery bei Home "
          "Assistant an. Bleibt inaktiv, solange keine Broker-Adresse eingetragen ist.</p>";
  html += "</div>";

  html += "<div class=\"block\"><input type=\"submit\" value=\"Speichern (LittleFS)\"></div>";
  html += "</form>";

  html += "<div class=\"block\"><h2>Konfiguration</h2>";
  html += "<a href=\"/api/config/export\"><button type=\"button\">XML Export</button></a>";
  html += "<form method=\"POST\" action=\"/api/config/import\" enctype=\"multipart/form-data\">";
  html += "<input type=\"file\" name=\"file\" accept=\".xml\"><input type=\"submit\" value=\"XML Import\">";
  html += "</form>";
  html += "<form method=\"POST\" action=\"/api/factory-reset\" "
          "onsubmit=\"return confirm('Wirklich alle Einstellungen auf Werkszustand zuruecksetzen? "
          "LAN/WLAN-Zugangsdaten, Kalibrierung etc. gehen verloren.')\">";
  html += "<input type=\"hidden\" name=\"scope\" value=\"settings\">";
  html += "<input type=\"submit\" value=\"Werksreset (nur Einstellungen)\"></form>";
  html += "<form method=\"POST\" action=\"/api/factory-reset\" "
          "onsubmit=\"return confirm('Wirklich Einstellungen UND den gespeicherten Verlauf loeschen? "
          "Das laesst sich nicht rueckgaengig machen.')\">";
  html += "<input type=\"hidden\" name=\"scope\" value=\"all\">";
  html += "<input type=\"submit\" value=\"Werksreset (Einstellungen + Daten)\"></form>";
  html += "</div>";

  html += "<div class=\"block\"><h2>Firmware</h2>";
  html += "<form method=\"POST\" action=\"/api/ota/upload\" enctype=\"multipart/form-data\">";
  html += "<input type=\"file\" name=\"file\" accept=\".bin\"><input type=\"submit\" value=\".bin hochladen\">";
  html += "</form>";
  html += "<a href=\"https://github.com/" GITHUB_REPO_SLUG "/releases\" target=\"_blank\"><button type=\"button\">Releases auf GitHub</button></a>";
  html += "</div>";

  html += "<div class=\"block\"><form method=\"POST\" action=\"/api/reboot\" onsubmit=\"return confirm('Wirklich neu starten?')\">";
  html += "<input type=\"submit\" value=\"Reboot\"></form></div>";

  html += "<script>";
  html += "function scanWifi(){"
          "document.getElementById('scanResult').innerText='Suche laeuft...';"
          "let tries=0;"
          "const poll=()=>{fetch('/api/wifi/scan').then(r=>r.json()).then(d=>{"
          "if(d.status==='done'){"
          "document.getElementById('scanResult').innerHTML=d.networks.length?d.networks.map(n=>"
          "`<div onclick=\"document.getElementById('wlanSsid').value='${n.ssid}'\">${n.ssid} (${n.rssi} dBm)</div>`).join(''):"
          "'Keine Netzwerke gefunden.';"
          "}else if(tries++<13){setTimeout(poll,1500);}"
          "else{document.getElementById('scanResult').innerText='Timeout bei der Suche.';}"
          "});};"
          "poll();}";
  html += "function connectWifi(){"
          "const body=new URLSearchParams({wlanSsid:document.getElementById('wlanSsid').value,"
          "wlanPsk:document.getElementById('wlanPsk').value});"
          "document.getElementById('connectStatus').innerText='Verbinde, Geraet startet neu...';"
          "fetch('/api/wifi/connect',{method:'POST',body});}";
  html += "function applyNetwork(iface){"
          "const p=iface;"
          "const body=new URLSearchParams({iface:iface,"
          "dhcp:document.getElementById(p+'Dhcp').checked?'1':'0',"
          "ip:document.getElementById(p+'Ip').value,mask:document.getElementById(p+'Mask').value,"
          "gateway:document.getElementById(p+'Gateway').value,dns:document.getElementById(p+'Dns').value});"
          "const status=document.getElementById(p+'ApplyStatus');"
          "status.innerText='Pruefe Erreichbarkeit (bis zu 8s)...';"
          "fetch('/api/network/apply',{method:'POST',body}).then(r=>r.text()).then(t=>{status.innerText=t;})"
          ".catch(()=>{status.innerText='Fehler bei der Anfrage.';});}";
  html += "function rerunDetection(){"
          "document.getElementById('detectStatus').innerText='Erkennung laeuft...';"
          "fetch('/api/detect/rerun',{method:'POST'}).then(r=>r.text()).then(t=>{"
          "document.getElementById('detectStatus').innerText=t;location.reload();});}";
  html += "function refreshRelayState(){"
          "fetch('/api/relay').then(r=>r.json()).then(d=>{"
          "document.getElementById('relayState').innerText=d.enabled?(d.on?'EIN':'AUS'):'deaktiviert';});}";
  html += "function toggleRelay(){"
          "fetch('/api/relay').then(r=>r.json()).then(d=>{"
          "const body=new URLSearchParams({on:d.on?'0':'1'});"
          "fetch('/api/relay',{method:'POST',body}).then(refreshRelayState);});}";
  html += "refreshRelayState();";
  html += "</script>";

  return html;
}

// ----------------------------------------------------------------------------
// Seiten
// ----------------------------------------------------------------------------
void WebServerManager::handleRoot(AsyncWebServerRequest* request) {
  request->send(200, "text/html", buildPageShell(_config.getConfig().systemName, buildMainPageBody()));
}

void WebServerManager::handleSettingsPage(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  request->send(200, "text/html", buildPageShell("Einstellungen", buildSettingsPageBody()));
}

void WebServerManager::handleValuesCsv(AsyncWebServerRequest* request) {
  HourValue buffer[DataManager::RINGBUFFER_SIZE];
  size_t count = _data.getRingbuffer(buffer, DataManager::RINGBUFFER_SIZE);

  String csv = "timestamp,temperature,humidity\n";
  for (size_t i = 0; i < count; i++) {
    csv += formatCsvTimestamp(buffer[i].timestamp) + "," + String(buffer[i].temperature, 1) + "," +
           String(buffer[i].humidity, 1) + "\n";
  }

  AsyncWebServerResponse* response = request->beginResponse(200, "text/csv", csv);
  response->addHeader("Content-Disposition", "attachment; filename=values.csv");
  request->send(response);
}

// ----------------------------------------------------------------------------
// REST-API
// ----------------------------------------------------------------------------
void WebServerManager::handleApiStatus(AsyncWebServerRequest* request) {
  const DeviceConfig& cfg = _config.getConfig();

  JsonDocument doc;
  doc["systemName"] = cfg.systemName;
  doc["systemType"] = cfg.systemType;
  doc["firmwareVersion"] = DEVICE_FIRMWARE_VERSION;
  doc["uptimeSeconds"] = (unsigned long)(esp_timer_get_time() / 1000000ULL);
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["chipTemperatureC"] = temperatureRead();
  doc["timeSynced"] = isTimeSynced();
  if (isTimeSynced()) doc["time"] = (unsigned long)time(nullptr);

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiSensors(AsyncWebServerRequest* request) {
  const DeviceConfig& cfg = _config.getConfig();
  SensorReading s1 = _data.getSensor1();
  SensorReading s2 = _data.getSensor2();

  JsonDocument doc;
  JsonObject sensor1 = doc["sensor1"].to<JsonObject>();
  sensor1["name"] = "Intern";
  sensor1["valid"] = s1.valid;
  sensor1["temperature"] = s1.temperature;
  sensor1["humidity"] = s1.humidity;

  if (cfg.sensor2Enabled) {
    JsonObject sensor2 = doc["sensor2"].to<JsonObject>();
    sensor2["name"] = cfg.sensor2Name;
    sensor2["valid"] = s2.valid;
    sensor2["temperature"] = s2.temperature;
    sensor2["humidity"] = s2.humidity;
  }

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiNetwork(AsyncWebServerRequest* request) {
  JsonDocument doc;
  doc["lanUp"] = _network.isLanUp();
  doc["lanLinkUp"] = _network.isLanLinkUp();
  doc["lanIp"] = _network.getLanIp().toString();
  doc["lanGateway"] = _network.getLanGateway().toString();
  doc["lanDns"] = _network.getLanDns().toString();
  doc["lanMac"] = _network.getLanMac();

  doc["wlanUp"] = _network.isWlanUp();
  doc["wlanIp"] = _network.getWlanIp().toString();
  doc["wlanGateway"] = _network.getWlanGateway().toString();
  doc["wlanDns"] = _network.getWlanDns().toString();
  doc["wlanMac"] = _network.getWlanMac();
  doc["wlanSsid"] = _network.getWlanSsid();
  doc["wlanRssi"] = _network.getWlanRssi();
  doc["usingFallbackWlan"] = _network.isUsingFallbackWlan();

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiLogs(AsyncWebServerRequest* request) {
  LogEntry entries[DataManager::LOG_CAPACITY];
  size_t count = _data.getLogEntries(entries, DataManager::LOG_CAPACITY);

  JsonDocument doc;
  JsonArray arr = doc["entries"].to<JsonArray>();
  for (size_t i = 0; i < count; i++) {
    JsonObject o = arr.add<JsonObject>();
    char buf[24];
    struct tm ti;
    localtime_r(&entries[i].timestamp, &ti);
    strftime(buf, sizeof(buf), "%d.%m. %H:%M:%S", &ti);
    o["time"] = buf;
    o["message"] = entries[i].message;
  }

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiGraph(AsyncWebServerRequest* request) {
  HourValue buffer[DataManager::RINGBUFFER_SIZE];
  size_t count = _data.getRingbuffer(buffer, DataManager::RINGBUFFER_SIZE);

  JsonDocument doc;
  JsonArray labels = doc["labels"].to<JsonArray>();
  JsonArray temps = doc["temperature"].to<JsonArray>();
  JsonArray hums = doc["humidity"].to<JsonArray>();

  for (size_t i = 0; i < count; i++) {
    struct tm ti;
    localtime_r(&buffer[i].timestamp, &ti);
    char buf[6];
    strftime(buf, sizeof(buf), "%H:%M", &ti);
    labels.add(String(buf));
    temps.add(buffer[i].temperature);
    hums.add(buffer[i].humidity);
  }

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiConfigGet(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  const DeviceConfig& cfg = _config.getConfig();

  JsonDocument doc;
  doc["systemName"] = cfg.systemName;
  doc["systemType"] = cfg.systemType;
  doc["lanDhcp"] = cfg.lanDhcp;
  doc["lanIp"] = cfg.lanIp;
  doc["lanMask"] = cfg.lanMask;
  doc["lanGateway"] = cfg.lanGateway;
  doc["lanDns"] = cfg.lanDns;
  doc["wlanDhcp"] = cfg.wlanDhcp;
  doc["wlanSsid"] = cfg.wlanSsid;
  doc["wlanIp"] = cfg.wlanIp;
  doc["wlanMask"] = cfg.wlanMask;
  doc["wlanGateway"] = cfg.wlanGateway;
  doc["wlanDns"] = cfg.wlanDns;
  doc["sensor1TempOffset"] = cfg.sensor1TempOffset;
  doc["sensor1HumOffset"] = cfg.sensor1HumOffset;
  doc["sensor1CalibratedTs"] = cfg.sensor1CalibratedTs;
  doc["sensor2Enabled"] = cfg.sensor2Enabled;
  doc["sensor2Name"] = cfg.sensor2Name;
  doc["sensor2TempOffset"] = cfg.sensor2TempOffset;
  doc["sensor2HumOffset"] = cfg.sensor2HumOffset;
  doc["sensor2CalibratedTs"] = cfg.sensor2CalibratedTs;
  doc["syslogServer"] = cfg.syslogServer;
  doc["snmpCommunity"] = cfg.snmpCommunity;
  doc["relayEnabled"] = cfg.relayEnabled;
  doc["mqttEnabled"] = cfg.mqttEnabled;
  doc["mqttServer"] = cfg.mqttServer;
  doc["mqttPort"] = cfg.mqttPort;
  doc["mqttUser"] = cfg.mqttUser;
  doc["mqttPassword"] = cfg.mqttPassword;
  doc["mqttTopicPrefix"] = cfg.mqttTopicPrefix;

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiConfigPost(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;

  DeviceConfig cfg = _config.getConfig();

  if (request->hasParam("systemName", true)) cfg.systemName = request->getParam("systemName", true)->value();
  if (request->hasParam("newPassword", true)) {
    String pw = request->getParam("newPassword", true)->value();
    if (pw.length() > 0) cfg.settingsPassword = pw;
  }

  cfg.lanDhcp = request->hasParam("lanDhcp", true);
  if (request->hasParam("lanIp", true)) cfg.lanIp = request->getParam("lanIp", true)->value();
  if (request->hasParam("lanMask", true)) cfg.lanMask = request->getParam("lanMask", true)->value();
  if (request->hasParam("lanGateway", true)) cfg.lanGateway = request->getParam("lanGateway", true)->value();
  if (request->hasParam("lanDns", true)) cfg.lanDns = request->getParam("lanDns", true)->value();

  cfg.wlanDhcp = request->hasParam("wlanDhcp", true);
  if (request->hasParam("wlanSsid", true)) cfg.wlanSsid = request->getParam("wlanSsid", true)->value();
  if (request->hasParam("wlanPsk", true)) cfg.wlanPsk = request->getParam("wlanPsk", true)->value();
  if (request->hasParam("wlanIp", true)) cfg.wlanIp = request->getParam("wlanIp", true)->value();
  if (request->hasParam("wlanMask", true)) cfg.wlanMask = request->getParam("wlanMask", true)->value();
  if (request->hasParam("wlanGateway", true)) cfg.wlanGateway = request->getParam("wlanGateway", true)->value();
  if (request->hasParam("wlanDns", true)) cfg.wlanDns = request->getParam("wlanDns", true)->value();

  // Alte Offsets merken, um "zuletzt kalibriert" NUR bei einer tatsaechlichen
  // Aenderung zu aktualisieren.
  float oldSensor1TempOffset = cfg.sensor1TempOffset;
  float oldSensor1HumOffset = cfg.sensor1HumOffset;
  float oldSensor2TempOffset = cfg.sensor2TempOffset;
  float oldSensor2HumOffset = cfg.sensor2HumOffset;

  if (request->hasParam("sensor1TempOffset", true)) {
    cfg.sensor1TempOffset = request->getParam("sensor1TempOffset", true)->value().toFloat();
  }
  if (request->hasParam("sensor1HumOffset", true)) {
    cfg.sensor1HumOffset = request->getParam("sensor1HumOffset", true)->value().toFloat();
  }
  cfg.sensor2Enabled = request->hasParam("sensor2Enabled", true);
  if (request->hasParam("sensor2Name", true)) cfg.sensor2Name = request->getParam("sensor2Name", true)->value();
  if (request->hasParam("sensor2TempOffset", true)) {
    cfg.sensor2TempOffset = request->getParam("sensor2TempOffset", true)->value().toFloat();
  }
  if (request->hasParam("sensor2HumOffset", true)) {
    cfg.sensor2HumOffset = request->getParam("sensor2HumOffset", true)->value().toFloat();
  }

  if (cfg.sensor1TempOffset != oldSensor1TempOffset || cfg.sensor1HumOffset != oldSensor1HumOffset) {
    cfg.sensor1CalibratedTs = static_cast<uint32_t>(time(nullptr));
  }
  if (cfg.sensor2TempOffset != oldSensor2TempOffset || cfg.sensor2HumOffset != oldSensor2HumOffset) {
    cfg.sensor2CalibratedTs = static_cast<uint32_t>(time(nullptr));
  }

  if (request->hasParam("syslogServer", true)) cfg.syslogServer = request->getParam("syslogServer", true)->value();

  if (request->hasParam("snmpCommunity", true)) {
    String community = request->getParam("snmpCommunity", true)->value();
    if (community.length() > 0) cfg.snmpCommunity = community;
  }

  cfg.relayEnabled = request->hasParam("relayEnabled", true);

  cfg.mqttEnabled = request->hasParam("mqttEnabled", true);
  if (request->hasParam("mqttServer", true)) cfg.mqttServer = request->getParam("mqttServer", true)->value();
  if (request->hasParam("mqttPort", true)) {
    cfg.mqttPort = static_cast<uint16_t>(request->getParam("mqttPort", true)->value().toInt());
  }
  if (request->hasParam("mqttUser", true)) cfg.mqttUser = request->getParam("mqttUser", true)->value();
  if (request->hasParam("mqttPassword", true)) cfg.mqttPassword = request->getParam("mqttPassword", true)->value();
  if (request->hasParam("mqttTopicPrefix", true)) {
    cfg.mqttTopicPrefix = request->getParam("mqttTopicPrefix", true)->value();
  }

  // Kollisions-Check: nur wenn DHCP aus ist UND sich die statische IP
  // gegenueber der aktuell aktiven Adresse tatsaechlich aendert.
  String ipConflictError;
  IPAddress newLanIp;
  if (!cfg.lanDhcp && newLanIp.fromString(cfg.lanIp) && newLanIp != _network.getLanIp() &&
      ipRespondsToPing(newLanIp)) {
    ipConflictError = "LAN-IP " + cfg.lanIp + " ist bereits belegt (ein Geraet antwortet auf Ping).";
  }
  IPAddress newWlanIp;
  if (ipConflictError.isEmpty() && !cfg.wlanDhcp && newWlanIp.fromString(cfg.wlanIp) &&
      newWlanIp != _network.getWlanIp() && ipRespondsToPing(newWlanIp)) {
    ipConflictError = "WLAN-IP " + cfg.wlanIp + " ist bereits belegt (ein Geraet antwortet auf Ping).";
  }
  if (!ipConflictError.isEmpty()) {
    _data.pushLogEntry(ipConflictError + " Einstellungen NICHT uebernommen.", 3);
    String body = "<h1>IP-Adresse belegt</h1><p>" + ipConflictError +
                  "</p><p>Alle Einstellungen dieser Seite wurden <b>nicht</b> uebernommen - bitte eine andere "
                  "Adresse waehlen und erneut speichern.</p><p><a href=\"/settings\">Zurueck zu den "
                  "Einstellungen</a></p>";
    request->send(409, "text/html", buildPageShell("IP belegt", body));
    return;
  }

  _config.setConfig(cfg);
  _data.pushLogEntry("Einstellungen gespeichert (Reboot noetig fuer Netzwerk-/SNMP-Aenderungen)");

  request->redirect("/settings");
}

void WebServerManager::handleApiConfigExport(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  String xml = _config.exportXml();
  AsyncWebServerResponse* response = request->beginResponse(200, "application/xml", xml);
  response->addHeader("Content-Disposition", "attachment; filename=config.xml");
  request->send(response);
}

void WebServerManager::handleApiConfigImportUpload(AsyncWebServerRequest* request, const String& filename,
                                                    size_t index, uint8_t* data, size_t len, bool final) {
  if (!checkAuth(request)) return;

  if (index == 0) _importBuffer = "";
  for (size_t i = 0; i < len; i++) _importBuffer += (char)data[i];

  if (final) {
    if (_config.importXml(_importBuffer)) {
      _config.save();
      _data.pushLogEntry("Konfiguration importiert (Reboot empfohlen)");
    } else {
      _data.pushLogEntry("Konfigurationsimport fehlgeschlagen (ungueltiges XML)", 3);
    }
  }
}

void WebServerManager::handleApiReboot(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  request->send(200, "text/plain", "Geraet startet neu...");
  _data.pushLogEntry("Reboot ueber Einstellungsseite ausgeloest");
  delay(500);
  ESP.restart();
}

void WebServerManager::handleApiWifiScan(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;

  // Nicht-blockierend (siehe Klassenkommentar): WiFi.scanComplete() liefert
  // WIFI_SCAN_FAILED (-2), solange kein Scan laeuft oder noch keiner
  // gestartet wurde -> in diesem Fall einen neuen asynchronen Scan anstossen
  // und sofort mit "started" antworten. Die Seite pollt diesen Endpunkt
  // anschliessend alle ~1,5s (bis zu ~20s), bis "done" mit den Ergebnissen
  // zurueckkommt.
  int result = WiFi.scanComplete();

  if (result == WIFI_SCAN_RUNNING) {
    request->send(200, "application/json", "{\"status\":\"running\"}");
    return;
  }
  if (result == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    request->send(200, "application/json", "{\"status\":\"started\"}");
    return;
  }

  JsonDocument doc;
  doc["status"] = "done";
  JsonArray networks = doc["networks"].to<JsonArray>();
  for (int i = 0; i < result; i++) {
    JsonObject o = networks.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
  }
  WiFi.scanDelete();

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiWifiConnect(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;

  DeviceConfig cfg = _config.getConfig();
  if (request->hasParam("wlanSsid", true)) cfg.wlanSsid = request->getParam("wlanSsid", true)->value();
  if (request->hasParam("wlanPsk", true)) cfg.wlanPsk = request->getParam("wlanPsk", true)->value();
  cfg.wlanPendingTest = true;
  _config.setConfig(cfg);
  _data.pushLogEntry("Neues WLAN \"" + cfg.wlanSsid + "\" gespeichert, starte neu zum Verbindungstest");

  request->send(200, "text/plain", "Gespeichert, Geraet startet neu ...");
  delay(500);
  ESP.restart();
}

void WebServerManager::handleApiFactoryReset(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;

  String scope = request->hasParam("scope", true) ? request->getParam("scope", true)->value() : "settings";
  _config.setConfig(DeviceConfig());  // Defaults - setConfig() speichert sofort nach config.xml

  if (scope == "all") {
    LittleFS.remove("/history.csv");
    _data.pushLogEntry("Werksreset: Einstellungen und Verlaufsdaten geloescht", 3);
  } else {
    _data.pushLogEntry("Werksreset: Einstellungen auf Standardwerte zurueckgesetzt", 3);
  }

  request->send(200, "text/plain", "Zurueckgesetzt, Geraet startet neu ...");
  delay(500);
  ESP.restart();
}

void WebServerManager::handleApiNetworkApply(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;

  static const unsigned long DHCP_TEST_TIMEOUT_MS = 8000;

  String iface = request->hasParam("iface", true) ? request->getParam("iface", true)->value() : "";
  if (iface != "lan" && iface != "wlan") {
    request->send(400, "text/plain", "Unbekanntes Interface.");
    return;
  }
  bool isLan = (iface == "lan");

  bool dhcp = request->hasParam("dhcp", true) && request->getParam("dhcp", true)->value() == "1";
  DeviceConfig cfg = _config.getConfig();

  if (dhcp) {
    IPAddress zero(0, 0, 0, 0);
    IPAddress gotIp;
    if (isLan) {
      ETH.config(zero, zero, zero);
    } else {
      WiFi.config(zero, zero, zero);
    }
    unsigned long start = millis();
    bool gotLease = false;
    while (millis() - start < DHCP_TEST_TIMEOUT_MS) {
      gotIp = isLan ? ETH.localIP() : WiFi.localIP();
      if (gotIp != zero) {
        gotLease = true;
        break;
      }
      delay(100);
    }
    if (!gotLease) {
      _network.restoreConfiguredAddresses();
      String label = isLan ? "LAN" : "WLAN";
      _data.pushLogEntry(label + ": kein DHCP-Lease erhalten - Einstellungen NICHT uebernommen.", 3);
      request->send(409, "text/plain",
                     "Kein DHCP-Server im Netz gefunden (keine Lease erhalten) - Einstellungen NICHT "
                     "uebernommen.");
      return;
    }
    if (isLan) {
      cfg.lanDhcp = true;
    } else {
      cfg.wlanDhcp = true;
    }
  } else {
    IPAddress newIp;
    if (!request->hasParam("ip", true) || !newIp.fromString(request->getParam("ip", true)->value())) {
      request->send(400, "text/plain", "Ungueltige IP-Adresse.");
      return;
    }
    IPAddress activeIp = isLan ? _network.getLanIp() : _network.getWlanIp();
    if (newIp != activeIp && ipRespondsToPing(newIp)) {
      String label = isLan ? "LAN" : "WLAN";
      _data.pushLogEntry(label + "-IP " + newIp.toString() + " ist bereits belegt - Einstellungen NICHT "
                          "uebernommen.", 3);
      request->send(409, "text/plain",
                     "IP " + newIp.toString() +
                         " ist bereits belegt (ein Geraet antwortet auf Ping) - Einstellungen NICHT uebernommen.");
      return;
    }
    String mask = request->hasParam("mask", true) ? request->getParam("mask", true)->value() : "";
    String gateway = request->hasParam("gateway", true) ? request->getParam("gateway", true)->value() : "";
    String dns = request->hasParam("dns", true) ? request->getParam("dns", true)->value() : "";
    if (isLan) {
      cfg.lanDhcp = false;
      cfg.lanIp = newIp.toString();
      cfg.lanMask = mask;
      cfg.lanGateway = gateway;
      cfg.lanDns = dns;
    } else {
      cfg.wlanDhcp = false;
      cfg.wlanIp = newIp.toString();
      cfg.wlanMask = mask;
      cfg.wlanGateway = gateway;
      cfg.wlanDns = dns;
    }
  }

  _config.setConfig(cfg);
  _data.pushLogEntry((isLan ? String("LAN") : String("WLAN")) +
                      "-Netzwerkeinstellungen geprueft und uebernommen, starte neu");
  request->send(200, "text/plain", "Geprueft und uebernommen, Geraet startet neu ...");
  delay(500);
  ESP.restart();
}

void WebServerManager::handleApiRelayGet(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  JsonDocument doc;
  doc["enabled"] = _config.getConfig().relayEnabled;
  doc["on"] = _relay.isOn();
  doc["feedback"] = _relay.feedbackOn();
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void WebServerManager::handleApiRelayPost(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  bool on = request->hasParam("on", true) && request->getParam("on", true)->value() == "1";
  _relay.setOn(on);
  request->send(200, "application/json", String("{\"on\":") + (_relay.isOn() ? "true" : "false") + "}");
}

void WebServerManager::handleApiDetectRerun(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  _detector.runDetection();
  request->send(200, "text/plain", "Erkannt: " + _detector.detectedDescription());
}

// ----------------------------------------------------------------------------
void WebServerManager::begin() {
  _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* r) { handleRoot(r); });
  _server.on("/settings", HTTP_GET, [this](AsyncWebServerRequest* r) { handleSettingsPage(r); });
  _server.on("/values.csv", HTTP_GET, [this](AsyncWebServerRequest* r) { handleValuesCsv(r); });

  _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiStatus(r); });
  _server.on("/api/sensors", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiSensors(r); });
  _server.on("/api/network", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiNetwork(r); });
  _server.on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiLogs(r); });
  _server.on("/api/graph", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiGraph(r); });

  _server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiConfigGet(r); });
  _server.on("/api/config", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiConfigPost(r); });
  _server.on("/api/config/export", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiConfigExport(r); });

  _server.on(
      "/api/config/import", HTTP_POST,
      [this](AsyncWebServerRequest* r) {
        if (checkAuth(r)) r->redirect("/settings");
      },
      [this](AsyncWebServerRequest* r, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        handleApiConfigImportUpload(r, filename, index, data, len, final);
      });

  _server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiReboot(r); });
  _server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiWifiScan(r); });
  _server.on("/api/wifi/connect", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiWifiConnect(r); });
  _server.on("/api/factory-reset", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiFactoryReset(r); });
  _server.on("/api/network/apply", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiNetworkApply(r); });

  _server.on("/api/relay", HTTP_GET, [this](AsyncWebServerRequest* r) { handleApiRelayGet(r); });
  _server.on("/api/relay", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiRelayPost(r); });
  _server.on("/api/detect/rerun", HTTP_POST, [this](AsyncWebServerRequest* r) { handleApiDetectRerun(r); });

  _server.on(
      "/api/ota/upload", HTTP_POST,
      [this](AsyncWebServerRequest* r) {
        if (!checkAuth(r)) return;
        if (_otaSuccess) {
          r->send(200, "text/plain", "Update erfolgreich, Geraet startet neu...");
          _data.pushLogEntry("OTA (lokaler Upload) erfolgreich, Neustart");
          delay(500);
          ESP.restart();
        } else {
          _data.pushLogEntry("OTA (lokaler Upload) fehlgeschlagen", 3);
          r->send(500, "text/plain", "Update fehlgeschlagen");
        }
      },
      [this](AsyncWebServerRequest* r, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        if (!checkAuth(r)) return;
        if (index == 0) {
          _otaInProgress = _ota.beginLocalUpdate(UPDATE_SIZE_UNKNOWN);
          _otaSuccess = false;
        }
        if (_otaInProgress) {
          _otaInProgress = _ota.writeLocalUpdateChunk(data, len);
        }
        if (final && _otaInProgress) {
          _otaSuccess = _ota.endLocalUpdate();
        }
      });

  _server.begin();
  Serial.println("[WEB] Server gestartet auf Port 80");
}
