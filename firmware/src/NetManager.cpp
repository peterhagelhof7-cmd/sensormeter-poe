#include "NetManager.h"

#include "pins.h"
#include <ETH.h>   // muss nach pins.h stehen: ETH_PHY_TYPE/_ADDR/... werden
                   // von pins.h vorgegeben, ETH.h uebernimmt sie als Default
#include <SPI.h>

NetManager* NetManager::_instance = nullptr;

// "Netzwerk-Check": solange kein Interface eine IP hat, warten wir laut
// Lastenheft 5 Minuten, bevor der eigene Fallback-Access-Point "installer"
// gestartet wird.
static const unsigned long NETWORK_CHECK_TIMEOUT_MS = 5UL * 60UL * 1000UL;
// Kurzer Timeout, wenn ueber die Einstellungsseite im Fallback-AP gerade
// neue WLAN-Zugangsdaten eingegeben wurden (DeviceConfig::wlanPendingTest) -
// schnelles Feedback statt 5 Minuten Wartezeit, siehe begin().
static const unsigned long WLAN_TEST_TIMEOUT_MS = 30UL * 1000UL;
static const unsigned long FALLBACK_RETRY_INTERVAL_MS = 30UL * 1000UL;
// Aktiver WLAN-Reconnect-Versuch alle 20s, solange im NETWORK_CHECK noch kein
// Interface eine IP hat - statt passiv auf den ESP32-Core-Auto-Reconnect zu
// warten (siehe loop()).
static const unsigned long RECONNECT_RETRY_INTERVAL_MS = 20UL * 1000UL;
static const char* FALLBACK_WLAN_SSID = "installer";
static const char* FALLBACK_WLAN_PSK = "installer";

// Eigener Access Point statt Beitritt zu einem bestehenden Netz - nur IP +
// Subnetzmaske konfiguriert, kein Gateway/DNS, da der AP nicht ins Internet
// weiterleitet. DHCP-Server laeuft automatisch (ESP32-Arduino-Core startet
// ihn implizit mit WiFi.softAP()).
static const IPAddress FALLBACK_AP_IP(192, 168, 4, 1);
static const IPAddress FALLBACK_AP_SUBNET(255, 255, 255, 0);

NetManager::NetManager(DataManager& dataManager, ConfigManager& configManager)
    : _data(dataManager), _config(configManager) {
  _instance = this;
}

String NetManager::sanitizeHostname(const String& name) {
  String out;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
      out += c;
    } else if ((c == ' ' || c == '_') && out.length() > 0 && out[out.length() - 1] != '-') {
      out += '-';
    }
  }
  while (out.length() > 0 && out[out.length() - 1] == '-') out.remove(out.length() - 1);
  while (out.length() > 0 && out[0] == '-') out.remove(0, 1);
  if (out.isEmpty()) out = "sensormeter-poe";
  return out;
}

void NetManager::onNetworkEvent(WiFiEvent_t event) {
  if (!_instance) return;

  switch (event) {
    case ARDUINO_EVENT_ETH_START: {
      Serial.println("[NET] Ethernet (W5500) gestartet");
      String hostname = sanitizeHostname(_instance->_config.getConfig().systemName);
      ETH.setHostname(hostname.c_str());
      break;
    }
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("[NET] Ethernet-Kabel verbunden");
      _instance->_ethConnected = true;
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("[NET] LAN-IP erhalten: ");
      Serial.println(ETH.localIP());
      _instance->_ethGotIp = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("[NET] Ethernet-Kabel getrennt");
      _instance->_ethConnected = false;
      _instance->_ethGotIp = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("[NET] Ethernet gestoppt");
      _instance->_ethConnected = false;
      _instance->_ethGotIp = false;
      break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[NET] WLAN verbunden");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("[NET] WLAN-IP erhalten: ");
      Serial.println(WiFi.localIP());
      _instance->_wlanGotIp = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[NET] WLAN-Verbindung verloren");
      _instance->_wlanGotIp = false;
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      Serial.println("[NET] Client mit Fallback-Access-Point verbunden");
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Serial.println("[NET] Client vom Fallback-Access-Point getrennt");
      break;

    default:
      break;
  }
}

void NetManager::startFallbackAp() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAPConfig(FALLBACK_AP_IP, FALLBACK_AP_IP, FALLBACK_AP_SUBNET);
  _apActive = WiFi.softAP(FALLBACK_WLAN_SSID, FALLBACK_WLAN_PSK);

  if (_apActive) {
    Serial.print("[NET] Fallback-Access-Point \"");
    Serial.print(FALLBACK_WLAN_SSID);
    Serial.print("\" gestartet, IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("[NET] Fallback-Access-Point konnte nicht gestartet werden");
  }
}

// ETH.config()/WiFi.config() setzen DNS NUR, wenn dns1/dns2 != 0.0.0.0 sind.
// Ohne explizite Angabe bliebe bei statischer IP sonst gar kein DNS-Server
// gesetzt und z.B. eine spaetere Hostname-Aufloesung wuerde fehlschlagen.
// Leeres/ungueltiges DNS-Feld -> Gateway als DNS verwenden (funktioniert bei
// den meisten Routern).
void NetManager::applyLanConfig() {
  const DeviceConfig& cfg = _config.getConfig();
  if (cfg.lanDhcp) return;  // Default nach ETH.begin() ist bereits DHCP

  IPAddress ip, mask, gateway;
  if (ip.fromString(cfg.lanIp) && mask.fromString(cfg.lanMask) && gateway.fromString(cfg.lanGateway)) {
    IPAddress dns;
    if (cfg.lanDns.length() == 0 || !dns.fromString(cfg.lanDns)) {
      dns = gateway;
    }
    ETH.config(ip, gateway, mask, dns);
    Serial.println("[NET] LAN: statische IP angewendet (DNS: " + dns.toString() + ")");
  } else {
    Serial.println("[NET] LAN: statische IP konfiguriert, aber ungueltig -> bleibe bei DHCP");
  }
}

void NetManager::applyWlanConfig() {
  const DeviceConfig& cfg = _config.getConfig();
  if (cfg.wlanDhcp) return;

  IPAddress ip, mask, gateway;
  if (ip.fromString(cfg.wlanIp) && mask.fromString(cfg.wlanMask) && gateway.fromString(cfg.wlanGateway)) {
    IPAddress dns;
    if (cfg.wlanDns.length() == 0 || !dns.fromString(cfg.wlanDns)) {
      dns = gateway;
    }
    WiFi.config(ip, gateway, mask, dns);
    Serial.println("[NET] WLAN: statische IP angewendet (DNS: " + dns.toString() + ")");
  } else {
    Serial.println("[NET] WLAN: statische IP konfiguriert, aber ungueltig -> bleibe bei DHCP");
  }
}

bool NetManager::hasStaticConfig() const {
  const DeviceConfig& cfg = _config.getConfig();
  return !cfg.lanDhcp || !cfg.wlanDhcp;
}

void NetManager::beginDhcpFallbackTest() {
  Serial.println("[NET] NTP nicht erreichbar -> DHCP-Test auf statisch konfigurierten Interfaces");
  const DeviceConfig& cfg = _config.getConfig();
  if (!cfg.lanDhcp) ETH.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  if (!cfg.wlanDhcp) WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
}

void NetManager::restoreConfiguredAddresses() {
  Serial.println("[NET] DHCP-Test erfolglos -> gesetzte IP-Konfiguration wiederherstellen");
  applyLanConfig();
  applyWlanConfig();
}

void NetManager::begin() {
  _data.setSystemState(SystemState::INIT);
  Serial.println("[NET] Init: Ethernet (W5500) + ggf. WLAN");

  WiFi.onEvent(onNetworkEvent);

  // W5500 haengt komplett an SPI (kein RMII-Takt wie beim WT32-ETH01) -
  // eigener SPI-Bus fuer den W5500, Pins/Adresse aus pins.h.
  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI);
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, SPI);
  applyLanConfig();

  DeviceConfig cfg = _config.getConfig();

  // Einmal-Flag konsumieren: wurde ueber die Einstellungsseite im
  // Fallback-AP gerade ein neues WLAN eingetragen, diesen einen Boot mit
  // kurzem Timeout pruefen, dann das Flag sofort wieder loeschen -
  // unabhaengig vom Ergebnis nur ein Versuch mit dem kurzen Timeout.
  if (cfg.wlanPendingTest) {
    _networkCheckTimeoutMs = WLAN_TEST_TIMEOUT_MS;
    cfg.wlanPendingTest = false;
    _config.setConfig(cfg);
    Serial.println("[NET] Neu eingetragenes WLAN wird getestet (kurzer Timeout)");
  } else {
    _networkCheckTimeoutMs = NETWORK_CHECK_TIMEOUT_MS;
  }

  _wlanConfigured = cfg.wlanSsid.length() > 0;
  if (_wlanConfigured) {
    WiFi.mode(WIFI_MODE_STA);
    // Core-seitigen Auto-Reconnect aktivieren (zusaetzlich zum aktiven
    // Reconnect in loop()), damit WLAN nach kurzem Aussetzer selbst zurueckkommt.
    WiFi.setAutoReconnect(true);
    // WLAN-Powersave abschalten - stabilisiert die Verbindung deutlich (der
    // ESP32-Default-Modem-Sleep verursacht mit manchen APs haeufige kurze
    // Abbrueche). Betrifft nur WiFi, ETH ist davon unberuehrt.
    WiFi.setSleep(false);
    WiFi.begin(cfg.wlanSsid.c_str(), cfg.wlanPsk.c_str());
    applyWlanConfig();
    Serial.println("[NET] WLAN-Verbindungsversuch gestartet (konfiguriertes Netz)");
  }

  _data.setSystemState(SystemState::NETWORK_CHECK);
  _networkCheckStartedMillis = millis();
  _lastReconnectAttemptMillis = millis();
}

void NetManager::loop() {
  SystemState state = _data.getSystemState();

  if (state == SystemState::NETWORK_CHECK) {
    if (networkOk()) {
      _data.setSystemState(SystemState::RUN_NORMAL);
    } else if (millis() - _networkCheckStartedMillis > _networkCheckTimeoutMs) {
      _data.pushLogEntry("Netzwerk: kein Link, starte eigenen Access-Point \"installer\"", 3);
      startFallbackAp();
      _lastFallbackJoinAttemptMillis = millis();
      _data.setSystemState(SystemState::FALLBACK_MODE);
    } else if (_wlanConfigured &&
               millis() - _lastReconnectAttemptMillis > RECONNECT_RETRY_INTERVAL_MS) {
      // WLAN aktiv erneut verbinden, statt nur auf den Core-Auto-Reconnect zu
      // warten - sonst haengt ein WLAN-only-Betrieb nach einem Aussetzer bis
      // zum 5-min-Timeout und kippt unnoetig in den Fallback-AP. ETH reagiert
      // unabhaengig ueber seine eigenen Link-Events.
      Serial.println("[NET] WLAN weg - aktiver Reconnect-Versuch");
      WiFi.reconnect();
      _lastReconnectAttemptMillis = millis();
    }
  } else if (state == SystemState::FALLBACK_MODE) {
    if (networkOk()) {
      _data.setSystemState(SystemState::RUN_NORMAL);
    } else if (millis() - _lastFallbackJoinAttemptMillis > FALLBACK_RETRY_INTERVAL_MS) {
      // Seltener Fall: WiFi.softAP() ist beim ersten Versuch fehlgeschlagen -
      // erneut versuchen.
      startFallbackAp();
      _lastFallbackJoinAttemptMillis = millis();
    }
  } else if (state == SystemState::RUN_NORMAL && !networkOk()) {
    _data.pushLogEntry("Netzwerk: Verbindung verloren (LAN+WLAN down)", 3);
    _apActive = false;
    _networkCheckTimeoutMs = NETWORK_CHECK_TIMEOUT_MS;
    _data.setSystemState(SystemState::NETWORK_CHECK);
    _networkCheckStartedMillis = millis();
    _lastReconnectAttemptMillis = millis();
  }
}

IPAddress NetManager::getLanIp() const {
  return _ethGotIp ? ETH.localIP() : IPAddress(0, 0, 0, 0);
}

IPAddress NetManager::getWlanIp() const {
  if (_apActive) return WiFi.softAPIP();
  return _wlanGotIp ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
}

IPAddress NetManager::getLanGateway() const {
  return _ethGotIp ? ETH.gatewayIP() : IPAddress(0, 0, 0, 0);
}

IPAddress NetManager::getWlanGateway() const {
  // Eigener Fallback-AP hat keinen Gateway/kein Routing ins Internet -
  // eigene IP zurueckgeben, konsistent mit der softAPConfig()-Einstellung.
  if (_apActive) return WiFi.softAPIP();
  return _wlanGotIp ? WiFi.gatewayIP() : IPAddress(0, 0, 0, 0);
}

IPAddress NetManager::getLanDns() const {
  return _ethGotIp ? ETH.dnsIP() : IPAddress(0, 0, 0, 0);
}

IPAddress NetManager::getWlanDns() const {
  if (_apActive) return IPAddress(0, 0, 0, 0);
  return _wlanGotIp ? WiFi.dnsIP() : IPAddress(0, 0, 0, 0);
}

String NetManager::getLanMac() const {
  return ETH.macAddress();
}

String NetManager::getWlanMac() const {
  return WiFi.macAddress();
}

String NetManager::getWlanSsid() const {
  if (_apActive) return String(FALLBACK_WLAN_SSID);
  return _wlanGotIp ? WiFi.SSID() : String("");
}

int NetManager::getWlanRssi() const {
  return _wlanGotIp ? WiFi.RSSI() : 0;
}
