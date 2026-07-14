#pragma once

#include <Arduino.h>
#include "BrandingManager.h"
#include "ConfigManager.h"
#include "DataManager.h"
#include "NetManager.h"
#include "TimeManager.h"

// Optionales externes Anzeige-Modul (Kategorie 1, aber kein Sensor): SH1107
// 1,5" 128x128 I2C an Adresse 0x3D (0x3C ist vom internen SSD1306 belegt,
// siehe DisplayManager) - siehe sensormeter-family/repo/module-design/
// sh1107-display-modul.md fuer die Hardwareseite. Rein additiv: zeigt
// dieselben Infoseiten wie das interne Display (Systemname/-typ, IPs,
// Uhrzeit, Sensorwerte, Status, WLAN-Signal, optional Branding), unabhaengig
// mit derselben 10s-Rotation, nur auf groesserer Flaeche - eigene, von
// DisplayManager unabhaengige Zeitbasis statt eines synchronisierten
// Zustands, das ist fuer eine reine Zusatzanzeige einfacher und robust
// genug. Bewusst OHNE Boot-Countdown-Seite, Fallback-AP-Sonderseite und
// BOOT-Taster-Overlay - diese sind an den Boot-/Reset-Ablauf des Geraets
// gebunden und bleiben Aufgabe des internen Displays; das externe Modul
// ist reine Zusatzanzeige fuer den Normalbetrieb (siehe „Bekannte
// Einschraenkungen" im Modul-Dokument). Fehlt das Modul, bleibt begin()
// erfolglos und loop() ist ein no-op - identisches Verhalten zum internen
// Display bei fehlendem Chip.

class ExternalDisplayManager {
 public:
  ExternalDisplayManager(DataManager& dataManager, ConfigManager& configManager, NetManager& networkManager,
                         TimeManager& timeManager, BrandingManager& brandingManager);

  void begin();
  void loop();

 private:
  DataManager& _data;
  ConfigManager& _config;
  NetManager& _network;
  TimeManager& _time;
  BrandingManager& _branding;

  bool _initialized = false;

  unsigned long _lastPageSwitchMillis = 0;
  int _currentPage = 0;
  // Seiten 0..5 = Standardseiten, 6 = Branding. Welche davon in der Rotation
  // erscheinen und wie schnell sie wechseln, kommt aus der Konfiguration
  // (DeviceConfig::extDisplayPages / extDisplaySlideSec, per Weboberflaeche
  // einstellbar).
  static const int TOTAL_PAGES = 7;
  unsigned long slideIntervalMs() const;
  bool pageEnabled(int page) const;
  int nextEnabledPage(int from) const;
  void drawNoPagesPage();

  void drawLines(const String lines[], int count);
  void drawScrollingLine(const String& text, int y, int size, float progress);
  void drawPage(int page);
  void drawSystemNamePage();
  void drawIpsPage();
  void drawTimePage();
  void drawSensorsPage();
  void drawStatusPage();
  void drawSignalPage();
  void drawBrandingPage();
};
