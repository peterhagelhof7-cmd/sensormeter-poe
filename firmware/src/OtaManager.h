#pragma once

#include <Arduino.h>

// Lokales OTA-Update per .bin-Upload (kein HTTPS-Client/GitHub-Versionscheck
// - identisch zur Begruendung in den beiden Schwesterprojekten).
//
// Seit 2026-07-16: waehrend des Uploads wird zusaetzlich im Byte-Stream nach
// einem in jedes Sensormeter-Projekt einkompilierten Marker
// "SM-FW-ID:<FIRMWARE_PROJEKT_ID>:<DEVICE_FIRMWARE_VERSION>:SM-FW-END"
// gesucht (siehe main.cpp und OtaManager.cpp) - damit laesst sich
// verhindern, dass versehentlich die .bin eines Schwesterprojekts (z.B.
// Sensormeter Display) oder eine aeltere Version der eigenen Firmware
// geflasht wird. Kein kryptografischer Schutz, nur eine Plausibilitaets-
// pruefung gegen Verwechslungen - siehe docs/entscheidungen.md.

class OtaManager {
 public:
  bool beginLocalUpdate(size_t contentLength);
  bool writeLocalUpdateChunk(uint8_t* data, size_t len);
  bool endLocalUpdate();

  // Erlaubt einen bewussten Ruecksprung auf eine aeltere Version (z.B. nach
  // einer fehlerhaften Vorabversion) - muss vor beginLocalUpdate() gesetzt
  // werden, sonst wird eine Version mit niedrigerer Semver-Praezedenz als
  // DEVICE_FIRMWARE_VERSION abgelehnt.
  void setAllowDowngrade(bool allow) { _allowDowngrade = allow; }

  // Erst nach dem letzten writeLocalUpdateChunk()-Aufruf (bzw. nach
  // endLocalUpdate()) aussagekraeftig - fuer die Fehlermeldung im
  // WebServerManager.
  bool markerFound() const { return _markerFound; }
  bool identityMatches() const { return _identityMatches; }
  bool versionAllowed() const { return _versionAllowed; }

 private:
  bool _allowDowngrade = false;
  bool _markerFound = false;
  bool _identityMatches = false;
  bool _versionAllowed = false;

  // Bewusst rohe Byte-Puffer statt Arduino String: die .bin ist Binaerdaten
  // und enthaelt reichlich eingebettete Null-Bytes (schon ab Byte 9 im
  // ESP32-Image-Header) - String::indexOf() ist intern strstr()-basiert und
  // bricht am ersten Null-Byte ab, wuerde den Marker in einer echten
  // Firmware-Datei also praktisch nie finden. Siehe docs/entscheidungen.md
  // "OTA-Marker-Scan fand echte .bin nie - String::indexOf() bricht bei
  // eingebetteten Null-Bytes ab" (uebernommen aus sensormeter).
  bool _capturing = false;
  static const size_t kTailCap = 16;  // > kMarkerPrefixLen - 1
  uint8_t _tailBuf[kTailCap];
  size_t _tailLen = 0;
  static const size_t kCaptureCap = 128;  // > kMaxCaptureLen
  uint8_t _captureBuf[kCaptureCap];
  size_t _captureLen = 0;

  void scanChunkForMarker(uint8_t* data, size_t len);
  void handleMarkerPayload(const String& payload);
};
