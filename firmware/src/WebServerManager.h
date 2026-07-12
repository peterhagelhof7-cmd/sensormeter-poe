#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "BrandingManager.h"
#include "ConfigManager.h"
#include "ContactManager.h"
#include "DataManager.h"
#include "NetManager.h"
#include "OtaManager.h"
#include "RelayManager.h"
#include "SensorDetector.h"

// Webserver (Pflichtenheft "WebServerTask"): Hauptseite (Status, Graph,
// Syslog-Tabelle, CSV-Download), passwortgeschuetzte Einstellungsseite,
// REST-API (/api/status, /api/sensors, /api/network, /api/logs, /api/config,
// /api/relay), OTA-Update per lokalem .bin-Upload. Async (non-blocking) per
// AsyncWebServer - Ausnahme: OTA-Flash ist eine admin-ausgeloeste Einzel-
// aktion und blockiert kurzzeitig. Der WLAN-Scan (/api/wifi/scan) laeuft
// dagegen bewusst NICHT blockierend (WiFi.scanNetworks(true) + Polling
// durch die Seite).
//
// Gegenueber Sensormeter (WT32-ETH01) neu: RelayManager (Aktor-Steuerung,
// /api/relay - gleicher Schreibpfad wie MqttManager) und SensorDetector
// (Anzeige des erkannten Modultyps + "Erkennung neu starten"-Button).

class WebServerManager {
 public:
  WebServerManager(DataManager& dataManager, ConfigManager& configManager, NetManager& networkManager,
                    OtaManager& otaManager, RelayManager& relayManager, SensorDetector& sensorDetector,
                    ContactManager& contactManager, BrandingManager& brandingManager);

  void begin();

 private:
  DataManager& _data;
  ConfigManager& _config;
  NetManager& _network;
  OtaManager& _ota;
  RelayManager& _relay;
  SensorDetector& _detector;
  ContactManager& _contact;
  BrandingManager& _branding;

  AsyncWebServer _server;

  // Streaming-Zustand fuer den lokalen .bin-Upload (siehe /api/ota/upload).
  bool _otaInProgress = false;
  bool _otaSuccess = false;

  // Streaming-Puffer fuer den XML-Import (config.xml ist klein genug, um
  // komplett im RAM zwischengehalten zu werden).
  String _importBuffer;

  bool checkAuth(AsyncWebServerRequest* request);

  void handleRoot(AsyncWebServerRequest* request);
  void handleSettingsPage(AsyncWebServerRequest* request);
  void handleValuesCsv(AsyncWebServerRequest* request);

  void handleApiStatus(AsyncWebServerRequest* request);
  void handleApiSensors(AsyncWebServerRequest* request);
  void handleApiNetwork(AsyncWebServerRequest* request);
  void handleApiLogs(AsyncWebServerRequest* request);
  void handleApiGraph(AsyncWebServerRequest* request);

  void handleApiConfigGet(AsyncWebServerRequest* request);
  void handleApiConfigPost(AsyncWebServerRequest* request);
  void handleApiConfigExport(AsyncWebServerRequest* request);
  void handleApiConfigImportUpload(AsyncWebServerRequest* request, const String& filename, size_t index,
                                    uint8_t* data, size_t len, bool final);

  void handleApiReboot(AsyncWebServerRequest* request);
  void handleApiWifiScan(AsyncWebServerRequest* request);
  void handleApiWifiConnect(AsyncWebServerRequest* request);
  void handleApiFactoryReset(AsyncWebServerRequest* request);
  void handleApiNetworkApply(AsyncWebServerRequest* request);

  // Aktor (Lastenheft Abschnitt 16.2): GET liefert den aktuellen Zustand,
  // POST setzt ihn - derselbe Codepfad wie die Weboberflaeche (Formular auf
  // der Einstellungsseite) und MqttManager (command_topic).
  void handleApiRelayGet(AsyncWebServerRequest* request);
  void handleApiRelayPost(AsyncWebServerRequest* request);

  // Modul-Erkennung (Lastenheft Abschnitt 15): loest SensorDetector erneut
  // aus, z.B. nach einem Modulwechsel im laufenden Betrieb.
  void handleApiDetectRerun(AsyncWebServerRequest* request);

  // Kontakt (Tuerkontakt/Reed, RJ45 Pin 5 im Modus "contact") - reiner
  // Lesepfad fuer die Einstellungsseite, kein POST noetig (Zustand kommt
  // vom Modul, nicht von einer Nutzeraktion wie beim Relais).
  void handleApiContactGet(AsyncWebServerRequest* request);

  // Anbieter-Branding: Logo-Upload (Streaming, analog handleApiConfigImportUpload/
  // OTA-Upload), Logo-Auslieferung als on-the-fly synthetisiertes 1-Bit-BMP
  // (kein PNG/JPEG-Decoder noetig, siehe BrandingManager.h) und Loeschen.
  void handleApiBrandingLogoUpload(AsyncWebServerRequest* request, const String& filename, size_t index,
                                    uint8_t* data, size_t len, bool final);
  void handleBrandingLogoBmp(AsyncWebServerRequest* request);
  void handleApiBrandingLogoDelete(AsyncWebServerRequest* request);

  bool _brandingUploadOk = false;

  String buildPageShell(const String& title, const String& bodyContent) const;
  String buildMainPageBody() const;
  String buildSettingsPageBody() const;
};
