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

  bool _capturing = false;
  String _tail;
  String _capture;

  void scanChunkForMarker(uint8_t* data, size_t len);
  void handleMarkerPayload(const String& payload);
};
