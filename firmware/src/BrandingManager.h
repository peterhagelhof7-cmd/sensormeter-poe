#pragma once

#include <Arduino.h>
#include <FS.h>
#include "ConfigManager.h"

// Anbieter-Branding (Weisslabel): Vendor-Name (in ConfigManager/config.xml,
// siehe dort) + optionales Logo-Bild, das separat als Rohdatei auf LittleFS
// liegt statt in config.xml (Binaerdaten gehoeren nicht in ein XML-Dokument,
// das komplett im RAM ge-/entladen wird). Format bewusst simpel gehalten,
// um keine PNG/JPEG-Decoder-Bibliothek einzubinden (haette 30-80 KB Flash
// gekostet, siehe Machbarkeitseinschaetzung/entscheidungen.md im
// Sensormeter-Repo): exakt 128x128 Pixel (SH1107, siehe DisplayManager -
// ANDERS als die 128x64-Geschwisterprojekte), 1 Bit pro Pixel, MSB-zuerst
// je Zeile, kein Padding - selbes Format wie Adafruit_GFX::drawBitmap()
// erwartet (identisch zu den rotierenden OLED-Seiten), 2048 Byte fest.
// Eine extern (z.B. per scripts/convert-logo.ps1 -Display poe)
// vorkonvertierte Datei muss exakt diese Groesse haben - jede Abweichung
// wird abgelehnt statt ein verzerrtes Bild stillschweigend anzuzeigen.
//
// Upload-Streaming (beginLogoUpload/writeLogoUploadChunk/endLogoUpload)
// folgt demselben Muster wie OtaManager fuer den lokalen .bin-Upload:
// WebServerManager reicht die Multipart-Chunks direkt durch, diese Klasse
// haelt den Streaming-Zustand. Geschrieben wird zunaechst in eine Tmp-Datei,
// die erst bei exakt passender Endgroesse an die Zielposition verschoben
// wird (identisches Muster zu ConfigManager::save() - ein Stromausfall oder
// eine falsch grosse Datei mitten im Upload darf ein zuvor funktionierendes
// Logo nicht zerstoeren).
//
// hasLogo() liest bewusst NICHT bei jedem Aufruf LittleFS.exists() - die
// Arduino-ESP32-LittleFS-Bibliothek implementiert exists() intern als
// open(path,"r") (siehe LittleFS.cpp), was die ESP-IDF-VFS-Schicht bei
// jedem Fehlschlag mit einer "[E]"-Logzeile quittiert. Bei einer alle 10s
// (Seitenwechsel-Takt) wiederholten Pruefung waere das eine dauerhaft
// wiederkehrende, aber harmlose Fehlerzeile im Boot-Log gewesen (auf
// echter Hardware beobachtet) - stattdessen wird der Zustand einmalig in
// begin() geprueft und bei jedem Upload/Loeschen aktualisiert, danach nur
// noch aus dem RAM-Cache gelesen (spart ausserdem wiederholte Flash-Zugriffe).

class BrandingManager {
 public:
  explicit BrandingManager(ConfigManager& configManager);

  void begin();

  bool hasVendorName() const;
  String vendorName() const;
  bool hasLogo() const { return _logoPresent; }
  bool isActive() const { return hasVendorName() || hasLogo(); }

  static const int LOGO_WIDTH = 128;
  static const int LOGO_HEIGHT = 128;
  static const size_t LOGO_BYTES = (LOGO_WIDTH / 8) * LOGO_HEIGHT;  // 2048

  // Liest das gespeicherte Logo vollstaendig in buffer (muss mindestens
  // LOGO_BYTES gross sein). false falls keine Logo-Datei vorhanden, die
  // Datei nicht exakt LOGO_BYTES gross ist, oder ein Lesefehler auftritt.
  bool loadLogo(uint8_t* buffer, size_t bufferSize) const;

  // Streaming-Callbacks aus WebServerManager fuer den Logo-Upload (siehe
  // Klassenkommentar).
  bool beginLogoUpload();
  bool writeLogoUploadChunk(const uint8_t* data, size_t len);
  bool endLogoUpload();

  bool deleteLogo();

 private:
  ConfigManager& _config;

  bool _logoPresent = false;
  // Prueft tatsaechlich per LittleFS (exists()+Groessencheck) - nur von
  // begin()/endLogoUpload()/deleteLogo() aufgerufen, nie aus dem heissen
  // Pfad (siehe Klassenkommentar).
  bool checkLogoOnDisk() const;

  File _uploadFile;
  size_t _uploadBytesWritten = 0;
  bool _uploadOpen = false;
};
