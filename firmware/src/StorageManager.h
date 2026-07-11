#pragma once

// LittleFS-Zugriff (config.xml, values.csv).

class StorageManager {
 public:
  bool begin();  // true = LittleFS erfolgreich gemountet
};
