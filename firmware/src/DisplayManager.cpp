#include "DisplayManager.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <time.h>
#include "SystemState.h"
#include "pins.h"

#if __has_include("config.h")
#include "config.h"
#endif
#ifndef DEVICE_FIRMWARE_VERSION
#define DEVICE_FIRMWARE_VERSION "0.0.0"
#endif

static const int SCREEN_WIDTH = 128;
static const int SCREEN_HEIGHT = 64;
static const uint8_t SSD1306_I2C_ADDRESS = 0x3C;

static const unsigned long PAGE_INTERVAL_MS = 10UL * 1000UL;  // 10s (Lastenheft)
static const unsigned long COUNTDOWN_TICK_MS = 1000UL;        // 1x/s

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

DisplayManager::DisplayManager(DataManager& dataManager, ConfigManager& configManager,
                               NetManager& networkManager, TimeManager& timeManager,
                               ButtonManager& buttonManager, BrandingManager& brandingManager)
    : _data(dataManager), _config(configManager), _network(networkManager), _time(timeManager),
      _button(buttonManager), _branding(brandingManager) {}

void DisplayManager::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  _initialized = display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS);
  if (!_initialized) {
    Serial.println("[DISPLAY] SSD1306 nicht gefunden (I2C 0x3C) - Anzeige deaktiviert");
    return;
  }
  display.cp437(true);
  display.setTextColor(SSD1306_WHITE);
  // Wir brechen Zeilen selbst um (mehrere setCursor()/println()-Aufrufe)
  // bzw. lassen zu lange Zeilen bewusst laufen (siehe drawScrollingLine())
  // - das automatische Adafruit-GFX-Wrapping wuerde beides durcheinanderbringen.
  display.setTextWrap(false);
  drawBootScreen();
}

// Feste Zielschriftgroesse fuer alle rotierenden Seiten - Zeilen, die dabei
// nicht auf einmal passen (z.B. lange SSIDs), laufen waagerecht durch statt
// die Schrift fuer alle zu schrumpfen, siehe drawScrollingLine().
static const int LINE_TEXT_SIZE = 2;

// Zeichnet eine einzelne Zeile bei fester Groesse, zentriert falls sie
// passt, sonst waagerecht durchlaufend gemaess progress (0.0 = Anfang,
// 1.0 = Ende - siehe Aufrufer fuer die Zeitbasis).
void DisplayManager::drawScrollingLine(const String& text, int y, int size, float progress) {
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

// Zeichnet 1-4 Textzeilen bei fester Groesse (LINE_TEXT_SIZE), horizontal
// UND vertikal zentriert - laeuft eine Zeile ueber, scrollt sie synchron
// zum Seitenwechsel-Timer: einmal komplett durch, haelt dann am Ende an,
// bis die naechste Seite (PAGE_INTERVAL_MS) dran ist.
void DisplayManager::drawLines(const String lines[], int count) {
  int lineHeight = LINE_TEXT_SIZE * 8;
  int y0 = max(0, (SCREEN_HEIGHT - count * lineHeight) / 2);

  static const unsigned long kScrollHoldMs = 1500UL;
  unsigned long scrollWindowMs =
      PAGE_INTERVAL_MS > kScrollHoldMs ? PAGE_INTERVAL_MS - kScrollHoldMs : PAGE_INTERVAL_MS;
  unsigned long elapsed = millis() - _lastPageSwitchMillis;
  float progress = static_cast<float>(elapsed) / static_cast<float>(scrollWindowMs);

  display.clearDisplay();
  for (int i = 0; i < count; i++) {
    drawScrollingLine(lines[i], y0 + i * lineHeight, LINE_TEXT_SIZE, progress);
  }
  display.display();
}

void DisplayManager::drawBootScreen() {
  // Jede Zeile bekommt ihre eigene groesstmoegliche Schriftgroesse (statt
  // einer gemeinsamen wie drawLines()): der Systemname kann lang sein und
  // damit auf Groesse 1 begrenzt bleiben, waehrend Systemtyp und Countdown
  // deutlich groesser dargestellt werden koennen.
  String line1 = _config.getConfig().systemName;
  String line2 = _config.getConfig().systemType;
  // Auf 3 Stellen leerzeichen-aufgefuellt, damit die Zeilenlaenge (und damit
  // die Schriftgroesse) waehrend des Runterzaehlens 100->0 konstant bleibt.
  char countdownBuf[16];
  snprintf(countdownBuf, sizeof(countdownBuf), "%3d warte", _countdownValue);
  String line3 = countdownBuf;

  int size1 = max(1, SCREEN_WIDTH / (static_cast<int>(line1.length()) * 6));
  int size2 = max(1, SCREEN_WIDTH / (static_cast<int>(line2.length()) * 6));
  int size3 = max(1, SCREEN_WIDTH / (static_cast<int>(line3.length()) * 6));

  int h1 = size1 * 8, h2 = size2 * 8, h3 = size3 * 8;
  while (h1 + h2 + h3 > SCREEN_HEIGHT && (size1 > 1 || size2 > 1 || size3 > 1)) {
    if (size1 > 1) size1--;
    if (size2 > 1) size2--;
    if (size3 > 1) size3--;
    h1 = size1 * 8; h2 = size2 * 8; h3 = size3 * 8;
  }

  int y0 = max(0, (SCREEN_HEIGHT - (h1 + h2 + h3)) / 2);

  display.clearDisplay();
  auto drawCentered = [&](const String& text, int size, int y) {
    display.setTextSize(size);
    int w = static_cast<int>(text.length()) * 6 * size;
    int x = max(0, (SCREEN_WIDTH - w) / 2);
    display.setCursor(x, y);
    display.println(text);
  };
  drawCentered(line1, size1, y0);
  drawCentered(line2, size2, y0 + h1);
  drawCentered(line3, size3, y0 + h1 + h2);
  display.display();
}

void DisplayManager::drawSystemNamePage() {
  String lines[2];
  lines[0] = _config.getConfig().systemName;
  lines[1] = _config.getConfig().systemType;
  drawLines(lines, 2);
}

void DisplayManager::drawIpsPage() {
  String lines[2];
  lines[0] = "LAN " + (_network.isLanUp() ? _network.getLanIp().toString() : String("---"));
  lines[1] = "WLAN " + (_network.isWlanUp() ? _network.getWlanIp().toString() : String("---"));
  drawLines(lines, 2);
}

void DisplayManager::drawTimePage() {
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

void DisplayManager::drawSensorsPage() {
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

void DisplayManager::drawStatusPage() {
  String lines[3];
  lines[0] = String("LAN ") + (_network.isLanUp() ? "OK" : "--");
  if (_network.isUsingFallbackWlan()) {
    lines[1] = "WLAN Fallback";
  } else {
    lines[1] = String("WLAN ") + (_network.isWlanUp() ? "OK" : "--");
  }
  lines[2] = "v" DEVICE_FIRMWARE_VERSION;
  drawLines(lines, 3);
}

void DisplayManager::drawSignalPage() {
  String lines[2];
  lines[0] = "WLAN-Signal";
  lines[1] = _network.isWlanUp() ? String(_network.getWlanRssi()) + "dB" : String("---");
  drawLines(lines, 2);
}

void DisplayManager::drawBrandingPage() {
  // Nur erreichbar, wenn isActive() true ist (siehe pageCount()) - trotzdem
  // defensiv beide Faelle behandeln, statt sich blind auf den Aufrufer zu
  // verlassen. Bevorzugt das Logo (fuellt den ganzen Screen, kein Platz mehr
  // fuer Text daneben); ohne Logo den Vendor-Namen als grosse Textzeile wie
  // die uebrigen Seiten (drawLines()).
  if (_branding.hasLogo()) {
    static uint8_t logoBuf[BrandingManager::LOGO_BYTES];
    if (_branding.loadLogo(logoBuf, sizeof(logoBuf))) {
      display.clearDisplay();
      display.drawBitmap(0, 0, logoBuf, BrandingManager::LOGO_WIDTH, BrandingManager::LOGO_HEIGHT, SSD1306_WHITE);
      display.display();
      return;
    }
  }
  String lines[1];
  lines[0] = _branding.vendorName();
  drawLines(lines, 1);
}

void DisplayManager::drawFallbackIpPage() {
  String ip = _network.isWlanUp() ? _network.getWlanIp().toString() : String("---");

  // Anders als drawLines(): hier gibt es keine Seitenwechsel-Deadline (die
  // Seite bleibt, solange der Fallback-AP aktiv ist) - "Fallback aktiv" ist
  // bei Groesse 2 zu lang fuer eine Zeile, laeuft also dauerhaft wieder-
  // holend durch statt einmal bis zu einem Wechsel.
  static const unsigned long kCycleMs = 4000UL;
  float progress = static_cast<float>(millis() % kCycleMs) / static_cast<float>(kCycleMs);

  int lineHeight = LINE_TEXT_SIZE * 8;
  int y0 = max(0, (SCREEN_HEIGHT - 2 * lineHeight) / 2);

  display.clearDisplay();
  drawScrollingLine("Fallback aktiv", y0, LINE_TEXT_SIZE, progress);
  drawScrollingLine(ip, y0 + lineHeight, LINE_TEXT_SIZE, progress);
  display.display();
}

bool DisplayManager::drawButtonOverlayIfActive() {
  if (!_button.isHeldForReset()) return false;

  String lines[2];
  if (_button.isAwaitingRelease()) {
    // Zeit ist um, aber noch gehalten - wartet auf das Loslassen (Fail-Safe,
    // siehe ButtonManager), loest hier bewusst noch nichts aus.
    lines[0] = "Loslassen zum";
    lines[1] = "Bestaetigen";
  } else {
    lines[0] = "Werksreset?";
    lines[1] = String(_button.resetCountdownSecondsLeft()) + "s halten";
  }
  drawLines(lines, 2);
  return true;
}

void DisplayManager::drawPage(int page) {
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

void DisplayManager::loop() {
  if (!_initialized) return;

  // Taster hat Vorrang vor allem anderen (Boot/Fallback/Rotation) - er ist
  // als Recovery-Weg gedacht, der in jedem Systemzustand funktionieren muss.
  if (drawButtonOverlayIfActive()) return;

  if (_button.consumePageAdvance()) {
    _currentPage = (_currentPage + 1) % pageCount();
    _lastPageSwitchMillis = millis();
  }

  SystemState state = _data.getSystemState();
  bool booting = (state == SystemState::BOOT || state == SystemState::INIT || state == SystemState::NETWORK_CHECK);

  if (booting) {
    // Nur bei Countdown-Aenderung neu zeichnen (1x/s), nicht bei jedem
    // 50ms-Tick - ein volles I2C-Frame kostet spuerbare Zeit; bei jedem
    // Tick waere das unnoetige CPU-/I2C-Last waehrend eines bis zu 5
    // Minuten langen NETWORK_CHECK.
    if (_lastCountdownTickMillis == 0) {
      _lastCountdownTickMillis = millis();
      drawBootScreen();
    } else if (millis() - _lastCountdownTickMillis >= COUNTDOWN_TICK_MS) {
      _lastCountdownTickMillis = millis();
      if (_countdownValue > 0) _countdownValue--;
      drawBootScreen();
    }
    return;
  }

  if (_network.isUsingFallbackWlan()) {
    // Keine Seitenrotation im Fallback-AP - siehe drawFallbackIpPage().
    drawFallbackIpPage();
    return;
  }

  if (_lastPageSwitchMillis == 0) {
    _lastPageSwitchMillis = millis();
  } else if (millis() - _lastPageSwitchMillis >= PAGE_INTERVAL_MS) {
    _lastPageSwitchMillis = millis();
    _currentPage = (_currentPage + 1) % pageCount();
  }
  drawPage(_currentPage);
}
