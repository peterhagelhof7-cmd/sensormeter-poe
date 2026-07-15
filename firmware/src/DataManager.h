#pragma once

#include <Arduino.h>
#include <freertos/semphr.h>
#include "SystemState.h"

// Zentrale Datenhaltung (Pflichtenheft 3.1): Sensorwerte, Systemstatus,
// Zeitstempel, 7-Tage-Ringpuffer. Thread-safe fuer den Zugriff aus mehreren
// Tasks (Sensor/Network/Web/Display/Syslog/Mqtt), die aus unterschiedlichen
// Kontexten (Hauptloop, async Webserver) zugreifen.

// Stundenwert fuer den 7-Tage-Ringpuffer/history.csv - Sensor 1 (intern,
// immer DHT11) hat immer nur Temperatur/Feuchte; Sensor 2 (extern, RJ45-
// Modul) kann je nach gestecktem Modultyp Temperatur/Feuchte (DHT11/DHT21/
// AHT20/AHT21), Luftdruck (BMP280), Helligkeit (BH1750) oder Luftguete
// (ENS160) liefern - nicht angewendete Felder bleiben NAN. Siehe
// docs/entscheidungen.md "Sensor-2-Datenmodell erweitert" fuer die
// Groessenrechnung (Ringpuffer/LittleFS-Partition).
struct HourValue {
  time_t timestamp = 0;
  float sensor1Temperature = NAN;
  float sensor1Humidity = NAN;
  float sensor2Temperature = NAN;
  float sensor2Humidity = NAN;
  float sensor2PressureHpa = NAN;
  float sensor2Lux = NAN;
  float sensor2Eco2Ppm = NAN;
};

// Live-Messwert fuer Sensor 1 oder Sensor 2 (siehe HourValue-Kommentar zur
// Feldbelegung je Modultyp - bei Sensor 1 bleiben pressureHpa/lux/eco2Ppm
// immer NAN).
struct SensorReading {
  float temperature = NAN;
  float humidity = NAN;
  float pressureHpa = NAN;
  float lux = NAN;
  float eco2Ppm = NAN;
  bool valid = false;
  unsigned long lastReadMillis = 0;
};

struct LogEntry {
  time_t timestamp = 0;
  String message;
  int severity = 6;      // Syslog-Konvention: 3 = Error, 6 = Informational
  unsigned long sequence = 0;  // fortlaufend, damit SyslogManager neue
                                // Eintraege erkennen kann, ohne zu pollen
                                // "welcher war der letzte Text"
};

class DataManager {
 public:
  // Benannte Severity-Stufen (RFC5424-artige Skala, wie bereits in
  // SyslogManager verwendet) statt roher Zahlen an den Aufrufstellen.
  // WARNING (4) ist neu: fuer transiente, selbstheilende Ereignisse (z.B.
  // ein kurzer LAN- oder WLAN-Aussetzer, der sich von selbst erholt) -
  // getrennt von ERROR (3), das weiterhin echten, admin-relevanten
  // Fehlern vorbehalten bleibt (z.B. beide Interfaces gleichzeitig down).
  static const int SEVERITY_ERROR = 3;
  static const int SEVERITY_WARNING = 4;
  static const int SEVERITY_INFO = 6;

  static const size_t RINGBUFFER_SIZE = 168;  // 7 Tage * 24 Stunden
  static const size_t LOG_CAPACITY = 5;       // Lastenheft 5.3: "letzte 5 Meldungen"
  // Persistenter Log-Puffer auf LittleFS (/log.txt, bei Ueberschreiten
  // umbenannt nach /log.old.txt - siehe appendLogFile()): 32 KB je Datei,
  // max. 64 KB gesamt (siehe Sensormeter-WLAN-Projekt, docs/entscheidungen.md,
  // fuer die Groessenrechnung).
  static const size_t LOG_FILE_MAX_BYTES = 32UL * 1024UL;

  void begin();

  SystemState getSystemState();
  void setSystemState(SystemState state);

  SensorReading getSensor1();
  void setSensor1(const SensorReading& reading);

  SensorReading getSensor2();
  void setSensor2(const SensorReading& reading);

  // Ringpuffer: pushHourValue() persistiert bei jedem Aufruf (1x/Stunde)
  // nach /history.csv auf LittleFS, damit der 7-Tage-Verlauf einen Neustart
  // uebersteht - vernachlaessigbarer Flash-Verschleiss bei stuendlicher
  // Schreibfrequenz (siehe docs/entscheidungen.md im Sensormeter-Projekt).
  void pushHourValue(const HourValue& value);
  size_t getRingbuffer(HourValue* out, size_t maxCount);

  // Muss erst NACH StorageManager::begin() (LittleFS-Mount) aufgerufen
  // werden - main.cpp ruft dataManager.begin() bewusst frueher auf (Mutex
  // fuer alle anderen Datenfelder muss vor jedem anderen Modul stehen),
  // daher kein impliziter load() in begin().
  void loadRingbuffer();

  // Lokales Ereignisprotokoll (Lastenheft 5.3 "Syslog-Ansicht": letzte 5
  // Meldungen). Wird von SyslogManager zusaetzlich per UDP versendet -
  // dieselbe Quelle fuer beides. severity folgt der Syslog-Konvention
  // (3 = Error, 6 = Informational).
  void pushLogEntry(const String& message, int severity = SEVERITY_INFO);
  size_t getLogEntries(LogEntry* out, size_t maxCount);  // neueste zuerst

  // Fuer SyslogManager: liefert nur Eintraege mit sequence > afterSequence
  // (chronologisch), damit Fehler-Events nicht doppelt oder verpasst werden.
  size_t getLogEntriesAfter(unsigned long afterSequence, LogEntry* out, size_t maxCount);

 private:
  void saveRingbuffer();
  // Haengt einen formatierten Eintrag an /log.txt an (Rotation nach
  // /log.old.txt bei Ueberschreiten von LOG_FILE_MAX_BYTES) - siehe
  // DataManager.cpp fuer das Zeilenformat. Unabhaengig vom RAM-Ringpuffer
  // oben (_log/LOG_CAPACITY), der weiterhin nur fuer die "letzte 5
  // Meldungen"-Webseite und den SyslogManager-Versand dient.
  void appendLogFile(time_t timestamp, int severity, const String& message);

  SemaphoreHandle_t _mutex = nullptr;

  SystemState _systemState = SystemState::BOOT;
  SensorReading _sensor1;
  SensorReading _sensor2;

  HourValue _ringbuffer[RINGBUFFER_SIZE];
  size_t _ringbufferCount = 0;
  size_t _ringbufferNextIndex = 0;

  LogEntry _log[LOG_CAPACITY];
  size_t _logCount = 0;
  size_t _logNextIndex = 0;
  unsigned long _logSequenceCounter = 0;
};
