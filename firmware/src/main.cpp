// ============================================================================
// Sensormeter PoE - Waveshare ESP32-S3-ETH - erste Firmware (P0, unflashed)
//
// Verdrahtet alle Module. ConfigManager laedt/speichert config.xml auf
// LittleFS; NetManager bringt Ethernet (W5500 ueber SPI, siehe pins.h)
// und optional WLAN hoch und treibt den Boot-Zustandsautomaten aus
// docs/lastenheft.txt Abschnitt 12 an; TimeManager haengt sich mit der
// NTP-Sync-Kette daran; SensorManager liest zwei DHT-22 im 60s-Takt;
// SensorDetector scannt beim Boot (parallel zum Netzwerk-Warten) das
// RJ45-Modul und setzt Sensor 2 automatisch; ButtonManager wertet den
// BOOT-Taster aus (Seitenwechsel/Werksreset); RelayManager treibt den
// Aktor (RJ45 Pin 6/7); DisplayManager zeigt Boot-Countdown und rotierende
// Infoseiten auf dem SH1107; WebServerManager stellt Hauptseite,
// Einstellungsseite, REST-API (inkl. /api/relay) und lokalen OTA-Upload
// bereit; SNMPManager beantwortet SNMP-v1/v2c-GET-Anfragen read-only;
// SyslogManager sendet Statusreports/Fehler-Events per UDP; MqttManager
// meldet Sensoren UND (falls aktiviert) den Aktor per Home-Assistant-
// MQTT-Discovery an; BrandingManager haelt den optionalen Anbieter-Namen/
// das Logo (Weisslabel), das DisplayManager als eigene OLED-Seite und
// WebServerManager im Seiten-Header zeigt, sobald konfiguriert.
//
// Erste, vollstaendige Umsetzung von docs/lastenheft.txt/pflichtenheft.txt -
// noch NICHT auf echter Hardware geflasht/getestet (kein Board zum
// Erstellungszeitpunkt verfuegbar), siehe docs/entscheidungen.md.
// ============================================================================

#include <Arduino.h>
#include <ESPmDNS.h>

#include "BrandingManager.h"
#include "ButtonManager.h"
#include "ConfigManager.h"
#include "DataManager.h"
#include "DisplayManager.h"
#include "MqttManager.h"
#include "NetManager.h"
#include "OtaManager.h"
#include "RelayManager.h"
#include "SNMPManager.h"
#include "SensorDetector.h"
#include "SensorManager.h"
#include "StorageManager.h"
#include "SyslogManager.h"
#include "SystemState.h"
#include "TimeManager.h"
#include "WebServerManager.h"

#if __has_include("config.h")
#include "config.h"
#else
#error "config.h fehlt! Kopiere include/config.h.example nach include/config.h."
#endif

DataManager dataManager;
ConfigManager configManager;
StorageManager storageManager;
NetManager networkManager(dataManager, configManager);
TimeManager timeManager(dataManager, networkManager);
SensorManager sensorManager(dataManager, configManager);
SensorDetector sensorDetector(dataManager, configManager);
ButtonManager buttonManager(dataManager, configManager);
RelayManager relayManager(dataManager, configManager);
BrandingManager brandingManager(configManager);
DisplayManager displayManager(dataManager, configManager, networkManager, timeManager, buttonManager,
                               brandingManager);
OtaManager otaManager;
WebServerManager webServerManager(dataManager, configManager, networkManager, otaManager, relayManager,
                                   sensorDetector, brandingManager);
SNMPManager snmpManager(dataManager, configManager, networkManager);
SyslogManager syslogManager(dataManager, configManager, networkManager);
MqttManager mqttManager(dataManager, configManager, networkManager, relayManager);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.print("=== Sensormeter PoE ");
  Serial.print(DEVICE_FIRMWARE_VERSION);
  Serial.println(" ===");

  dataManager.begin();
  dataManager.setSystemState(SystemState::BOOT);

  storageManager.begin();
  dataManager.loadRingbuffer();
  configManager.begin();
  timeManager.begin();
  sensorManager.begin();
  buttonManager.begin();
  relayManager.begin();
  brandingManager.begin();
  syslogManager.begin();
  mqttManager.begin();

  // Modul-Erkennung VOR dem Netzwerkaufbau, aber nach ConfigManager (braucht
  // ggf. bereits gespeicherte sensor2Enabled-Werte als Ausgangspunkt) -
  // laeuft synchron und dauert nur wenige hundert Millisekunden (I2C-Scan +
  // ggf. ein DHT-Leseversuch), verzoegert den danach beginnenden
  // Boot-Countdown (Netzwerk-Warten) dadurch nicht spuerbar - siehe
  // docs/lastenheft.txt Abschnitt 15.1.
  sensorDetector.begin();
  sensorDetector.runDetection();

  displayManager.begin();

  networkManager.begin();     // setzt Zustand auf INIT, dann NETWORK_CHECK
  webServerManager.begin();   // async - kein eigener loop()-Aufruf noetig
  snmpManager.begin();
}

void loop() {
  networkManager.loop();
  timeManager.loop();
  sensorManager.loop();
  buttonManager.loop();
  displayManager.loop();
  snmpManager.loop();
  syslogManager.loop();
  mqttManager.loop();

  // Einmaliger mDNS-Start, sobald ein Interface eine IP hat (LAN, WLAN oder
  // Fallback-AP) - vor RUN_NORMAL ist noch keine IP vergeben.
  static bool mdnsStarted = false;
  if (!mdnsStarted && (networkManager.isLanUp() || networkManager.isWlanUp())) {
    String hostname = NetManager::sanitizeHostname(configManager.getConfig().systemName);
    if (MDNS.begin(hostname.c_str())) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[NET] mDNS gestartet: http://%s.local/\n", hostname.c_str());
    } else {
      Serial.println("[NET] mDNS-Start fehlgeschlagen");
    }
    mdnsStarted = true;
  }

  delay(50);
}
