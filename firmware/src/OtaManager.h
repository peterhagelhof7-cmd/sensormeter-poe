#pragma once

#include <Arduino.h>

// Lokales OTA-Update per .bin-Upload (kein HTTPS-Client/GitHub-Versionscheck
// - identisch zur Begruendung in den beiden Schwesterprojekten).

class OtaManager {
 public:
  bool beginLocalUpdate(size_t contentLength);
  bool writeLocalUpdateChunk(uint8_t* data, size_t len);
  bool endLocalUpdate();
};
