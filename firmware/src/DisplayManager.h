#pragma once

#include <Arduino.h>
#include "ButtonManager.h"
#include "ConfigManager.h"
#include "DataManager.h"
#include "NetManager.h"
#include "TimeManager.h"

// OLED-Anzeige (Pflichtenheft-Task "DisplayTask"): SH1107 1,5" 128x128 I2C
// auf PIN_I2C_SDA/PIN_I2C_SCL (siehe pins.h), Adresse 0x3C. Anders als bei
// den beiden Schwesterprojekten (SSD1306 128x64) API-inkompatibel -
// Adafruit_SH110X statt Adafruit_SSD1306 (siehe platformio.ini). Rotierende
// Infoseiten alle 10s (Lastenheft Abschnitt 11: Systemname+Systemtyp / IPs
// (Ethernet+WLAN) / Uhrzeit / Sensorwerte / Status / WLAN-Signal). Waehrend
// des Bootens (BOOT/INIT/NETWORK_CHECK) Systemname + Systemtyp + Countdown
// 100->0 bis das Netzwerk bereit ist. Im Fallback-Access-Point ("installer")
// stattdessen ausschliesslich die eigene IP.
//
// Die eigentliche BOOT-Taster-Zustandsmaschine lebt in ButtonManager (eigenes
// Modul, siehe dortigen Klassenkommentar) - DisplayManager fragt sie nur ab,
// um die Reset-Bestaetigung/den Countdown zu zeichnen bzw. die Seite manuell
// weiterzuschalten.

class DisplayManager {
 public:
  DisplayManager(DataManager& dataManager, ConfigManager& configManager, NetManager& networkManager,
                 TimeManager& timeManager, ButtonManager& buttonManager);

  void begin();
  void loop();

 private:
  DataManager& _data;
  ConfigManager& _config;
  NetManager& _network;
  TimeManager& _time;
  ButtonManager& _button;

  bool _initialized = false;

  unsigned long _lastPageSwitchMillis = 0;
  int _currentPage = 0;
  static const int PAGE_COUNT = 6;

  unsigned long _lastCountdownTickMillis = 0;
  int _countdownValue = 100;

  // Horizontal+vertikal zentriert, feste groessere Schrift - einheitlich auf
  // allen Screens. Zeilen, die dabei nicht auf einmal passen (z.B. eine
  // lange WLAN-SSID), laufen waagerecht durch statt geschrumpft zu werden -
  // siehe drawScrollingLine().
  void drawLines(const String lines[], int count);
  // progress: 0.0 (Start) bis 1.0 (Ende) - vom Aufrufer berechnet, damit
  // sowohl "einmal durchlaufen und am Ende halten" (rotierende Seiten,
  // synchron zum Seitenwechsel-Timer) als auch "dauerhaft wiederholen"
  // (Fallback-Seite, keine Wechsel-Deadline) denselben Zeichencode nutzen
  // koennen.
  void drawScrollingLine(const String& text, int y, int size, float progress);
  void drawBootScreen();
  void drawPage(int page);
  void drawSystemNamePage();
  void drawIpsPage();
  void drawTimePage();
  void drawSensorsPage();
  void drawStatusPage();
  void drawSignalPage();
  void drawFallbackIpPage();
  // Zeigt die Reset-Bestaetigung/den Countdown, solange der BOOT-Taster
  // entsprechend gehalten wird (siehe ButtonManager) - true, wenn diese
  // Anzeige gerade aktiv ist (loop() zeigt dann keine normale Seite an).
  bool drawButtonOverlayIfActive();
};
