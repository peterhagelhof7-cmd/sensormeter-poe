#include "ExternalDisplayManager.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Wire.h>
#include <time.h>
#include "pins.h"

static const int SCREEN_WIDTH = 128;
static const int SCREEN_HEIGHT = 128;
static const uint8_t EXTERNAL_DISPLAY_I2C_ADDRESS = 0x3D;

// Slide-Intervall kommt jetzt aus der Konfiguration (extDisplaySlideSec),
// siehe slideIntervalMs() - Default 10s entspricht dem bisherigen Verhalten.

// Kein dediziertes Reset-Pin (typisches 4-Pin-I2C-Modul), analog zum
// bisherigen internen SH1107 vor dem Umbau auf SSD1306.
static Adafruit_SH1107 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1, 1000000, 100000);

ExternalDisplayManager::ExternalDisplayManager(DataManager& dataManager, ConfigManager& configManager,
                                               NetManager& networkManager, TimeManager& timeManager,
                                               BrandingManager& brandingManager)
    : _data(dataManager), _config(configManager), _network(networkManager), _time(timeManager),
      _branding(brandingManager) {}

void ExternalDisplayManager::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  _initialized = display.begin(EXTERNAL_DISPLAY_I2C_ADDRESS, true);
  if (!_initialized) {
    // Kein Fehler, sondern der Normalfall ohne gestecktes externes
    // Display-Modul - kein "[ERROR]"-Ton, siehe DisplayManager fuer das
    // analoge interne Verhalten.
    Serial.println("[EXT-DISPLAY] Kein externes SH1107 gefunden (I2C 0x3D) - optionales Modul, kein Fehler");
    return;
  }
  Serial.println("[EXT-DISPLAY] Externes SH1107-Display erkannt (I2C 0x3D)");
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
}

static const int LINE_TEXT_SIZE = 2;

void ExternalDisplayManager::drawScrollingLine(const String& text, int y, int size, float progress) {
  int textWidth = static_cast<int>(text.length()) * 6 * size;
  display.setTextSize(size);
  if (textWidth <= SCREEN_WIDTH) {
    int x = max(0, (SCREEN_WIDTH - textWidth) / 2);
    display.setCursor(x, y);
    display.println(text);
    return;
  }
  if (progress < 0.0f) progress = 0.0f;
  if (progress > 1.0f) progress = 1.0f;
  int scrollDistance = textWidth - SCREEN_WIDTH;
  int x = -static_cast<int>(progress * scrollDistance);
  display.setCursor(x, y);
  display.println(text);
}

void ExternalDisplayManager::drawLines(const String lines[], int count) {
  int lineHeight = LINE_TEXT_SIZE * 8;
  int y0 = max(0, (SCREEN_HEIGHT - count * lineHeight) / 2);

  static const unsigned long kScrollHoldMs = 1500UL;
  unsigned long interval = slideIntervalMs();
  unsigned long scrollWindowMs =
      interval > kScrollHoldMs ? interval - kScrollHoldMs : interval;
  unsigned long elapsed = millis() - _lastPageSwitchMillis;
  float progress = static_cast<float>(elapsed) / static_cast<float>(scrollWindowMs);

  display.clearDisplay();
  for (int i = 0; i < count; i++) {
    drawScrollingLine(lines[i], y0 + i * lineHeight, LINE_TEXT_SIZE, progress);
  }
  display.display();
}

void ExternalDisplayManager::drawSystemNamePage() {
  String lines[2];
  lines[0] = _config.getConfig().systemName;
  lines[1] = _config.getConfig().systemType;
  drawLines(lines, 2);
}

void ExternalDisplayManager::drawIpsPage() {
  String lines[2];
  lines[0] = "LAN " + (_network.isLanUp() ? _network.getLanIp().toString() : String("---"));
  lines[1] = "WLAN " + (_network.isWlanUp() ? _network.getWlanIp().toString() : String("---"));
  drawLines(lines, 2);
}

void ExternalDisplayManager::drawTimePage() {
  String lines[2];
  if (!_time.isSynced()) {
    lines[0] = "Zeit";
    lines[1] = "--:--:--";
  } else {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char dateBuf[16];
    char timeBuf[16];
    strftime(dateBuf, sizeof(dateBuf), "%d.%m.%Y", &timeinfo);
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);
    lines[0] = dateBuf;
    lines[1] = timeBuf;
  }
  drawLines(lines, 2);
}

void ExternalDisplayManager::drawSensorsPage() {
  const DeviceConfig& cfg = _config.getConfig();
  SensorReading s1 = _data.getSensor1();

  String lines[2];
  lines[0] = "Intern " + (s1.valid ? String(s1.temperature, 1) + "C " + String(s1.humidity, 0) + "%" : String("---"));

  if (cfg.sensor2Enabled) {
    SensorReading s2 = _data.getSensor2();
    // Je nach gestecktem Modultyp liefert Sensor 2 Temperatur/Feuchte,
    // nur Druck, nur Helligkeit oder nur Luftguete (siehe DataManager.h).
    String s2Value = "---";
    if (s2.valid) {
      if (!isnan(s2.temperature) && !isnan(s2.humidity)) {
        s2Value = String(s2.temperature, 1) + "C " + String(s2.humidity, 0) + "%";
      } else if (!isnan(s2.pressureHpa)) {
        s2Value = String(s2.pressureHpa, 0) + "hPa";
      } else if (!isnan(s2.lux)) {
        s2Value = String(s2.lux, 0) + "lx";
      } else if (!isnan(s2.eco2Ppm)) {
        s2Value = String(s2.eco2Ppm, 0) + "ppm";
      }
    }
    lines[1] = cfg.sensor2Name + " " + s2Value;
    drawLines(lines, 2);
  } else {
    drawLines(lines, 1);
  }
}

void ExternalDisplayManager::drawStatusPage() {
  String lines[2];
  lines[0] = String("LAN ") + (_network.isLanUp() ? "OK" : "--");
  if (_network.isUsingFallbackWlan()) {
    lines[1] = "WLAN Fallback";
  } else {
    lines[1] = String("WLAN ") + (_network.isWlanUp() ? "OK" : "--");
  }
  drawLines(lines, 2);
}

void ExternalDisplayManager::drawSignalPage() {
  String lines[2];
  lines[0] = "WLAN-Signal";
  lines[1] = _network.isWlanUp() ? String(_network.getWlanRssi()) + "dB" : String("---");
  drawLines(lines, 2);
}

void ExternalDisplayManager::drawBrandingPage() {
  // Nur der Vendor-Name als Textzeile, bewusst OHNE Logo-Bitmap: das
  // gespeicherte Logo (BrandingManager::LOGO_WIDTH/HEIGHT) ist seit dem
  // Umbau auf das interne SSD1306 128x64 formatiert - auf dem 128x128
  // grossen externen Display wuerde dasselbe Bitmap verzerrt/falsch
  // interpretiert dargestellt. Ein eigenes 128x128-Logoformat fuer dieses
  // Modul ist (noch) nicht umgesetzt, siehe sensormeter-family/repo/
  // module-design/sh1107-display-modul.md.
  String lines[1];
  lines[0] = _branding.vendorName();
  drawLines(lines, 1);
}

void ExternalDisplayManager::drawPage(int page) {
  switch (page) {
    case 0: drawSystemNamePage(); break;
    case 1: drawIpsPage(); break;
    case 2: drawTimePage(); break;
    case 3: drawSensorsPage(); break;
    case 4: drawStatusPage(); break;
    case 5: drawSignalPage(); break;
    case 6: drawBrandingPage(); break;
    default: break;
  }
}

unsigned long ExternalDisplayManager::slideIntervalMs() const {
  uint16_t sec = _config.getConfig().extDisplaySlideSec;
  if (sec < 2) sec = 2;  // Untergrenze gegen Flackern (Web-UI begrenzt ebenfalls)
  return static_cast<unsigned long>(sec) * 1000UL;
}

bool ExternalDisplayManager::pageEnabled(int page) const {
  if (page < 0 || page >= TOTAL_PAGES) return false;
  // Branding-Seite nur, wenn zusaetzlich ein Anbieter-Branding aktiv ist -
  // sonst waere die Seite leer (bisheriges Verhalten beibehalten).
  if (page == 6 && !_branding.isActive()) return false;
  return (_config.getConfig().extDisplayPages >> page) & 0x01;
}

int ExternalDisplayManager::nextEnabledPage(int from) const {
  for (int i = 1; i <= TOTAL_PAGES; i++) {
    int p = (from + i) % TOTAL_PAGES;
    if (pageEnabled(p)) return p;
  }
  return -1;  // keine Seite ausgewaehlt
}

void ExternalDisplayManager::drawNoPagesPage() {
  String lines[2];
  lines[0] = "Slide leer";
  lines[1] = "keine Seite";
  drawLines(lines, 2);
}

void ExternalDisplayManager::loop() {
  if (!_initialized) return;

  // Alle Seiten abgewaehlt -> Hinweis statt schwarzem Bild.
  if (nextEnabledPage(_currentPage) < 0) {
    drawNoPagesPage();
    return;
  }

  if (_lastPageSwitchMillis == 0) {
    _lastPageSwitchMillis = millis();
    if (!pageEnabled(_currentPage)) _currentPage = nextEnabledPage(_currentPage);
  } else if (millis() - _lastPageSwitchMillis >= slideIntervalMs()) {
    _lastPageSwitchMillis = millis();
    _currentPage = nextEnabledPage(_currentPage);
  }
  drawPage(_currentPage);
}
