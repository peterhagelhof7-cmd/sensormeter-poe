# Systemlast (CPU, RAM, Flash)

Einordnung gegenüber den Performance-Zielwerten aus
[`docs/pflichtenheft.txt`](pflichtenheft.txt) Abschnitt 8:

> CPU load < 40% · RAM usage < 60% · Sensor polling minimal (60s) ·
> Webserver non-blocking (Async)

Anders als bei `sensormeter` existiert für dieses Projekt **kein**
natives Host-Testprogramm für simulierte Heap-Lasten
(`firmware/tools/simulate_json_load.cpp` wurde nicht portiert — offene
Lücke, siehe `entscheidungen.md`). Zwei Datenquellen:

1. **Gemessen** – reale `pio run`-Build-Ausgabe (Flash/RAM), Stand nach
   dem ersten Hardware-Bringup (2026-07-18).
2. **Abgeschätzt** – CPU-Zeitbudget pro Loop-Tick, hergeleitet aus dem
   tatsächlichen `loop()`-Code (`firmware/src/main.cpp`) und bekannten
   Bibliotheks-Timings (DHT-Protokoll, I2C-Taktrate).

## 1. Flash/RAM (gemessen)

```
RAM:   19,3 % (63.140 von 327.680 Byte)
Flash: 22,5 % (1.473.871 von 6.553.600 Byte App-Partition)
```

**Reserve:** 6.553.600 − 1.473.871 = **5.079.729 Byte Flash frei
(77,5 %)**, 264.540 von 327.680 Byte statisches RAM frei (80,7 %) für
Heap + Stack zur Laufzeit. Deutlich mehr Reserve als bei `sensormeter`
(WT32-ETH01, 4 MB Flash) — die Waveshare ESP32-S3-ETH läuft mit
16 MB-Flash-Partitionslayout (`default_16MB.csv`), entsprechend fällt
derselbe Firmware-Umfang prozentual kleiner aus, obwohl absolut mehr
Bibliotheken eingebunden sind (u. a. `Ethernet`/W5500, `SNMP_Agent`,
`PubSubClient` für MQTT, `Adafruit_SH110X` für das optionale externe
SH1107-Modul — Funktionen, die `sensormeter` in dieser Kombination nicht
alle gleichzeitig hat).

`SET_LOOP_TASK_STACK_SIZE(16384)` (siehe `main.cpp`, seit dem
Board-Bringup fest gesetzt wegen eines real aufgetretenen
`loopTask`-Stack-Overflows) erhöht den `loopTask`-Stack von den
Arduino-ESP32-Standard-8192 Byte auf 16.384 Byte — dieser zusätzliche
Stack liegt ebenfalls im oben ausgewiesenen RAM-Wert.

## 2. CPU-Zeitbudget pro Loop-Tick (abgeschätzt)

Die Hauptschleife läuft alle 50 ms (`delay(50)` am Ende von `loop()` in
`main.cpp`, danach `esp_task_wdt_reset()`), also ~20 Takte/Sekunde.
`loop()` ruft nacheinander die `loop()`-Methoden von 13 Managern auf
(`networkManager`, `timeManager`, `sensorDetector`, `sensorManager`,
`contactManager`, `relayManager`, `buttonManager`, `displayManager`,
`externalDisplayManager`, `snmpManager`, `syslogManager`, `mqttManager`,
plus serielle Kommandoverarbeitung) — mehr Manager als bei `sensormeter`
(kein Relais/Kontakt/MQTT/externes Display dort), war der direkte Auslöser
für den oben genannten Stack-Overflow-Fix.

| Vorgang | Kosten | Takt | Ø-Last |
|---|---|---|---|
| Basis-Checks aller 13 Manager (Zustands-/Zeitvergleiche, kein I/O) | < 2 ms | jeder Tick (50 ms) | < 4 % |
| DHT-22 intern (blockierend, Protokoll-Timing) | ~20–25 ms | alle 60 s | ~0,04 % |
| DHT-22 extern über RJ45 (nur bei aktiviertem Sensor 2) | ~20–25 ms | alle 60 s | ~0,04 % |
| I2C-Scan (`SensorDetector`, Modul-Erkennung) | ~10–15 ms | alle 60 s (nach Boot) | ~0,02 % |
| OLED-Seitenwechsel (SSD1306, internes Display) | ~20–25 ms | alle 10 s (Normalbetrieb) | ~0,22 % |
| OLED-Seitenwechsel (SH1107, externes Modul, falls gesteckt) | ~20–25 ms | alle 10 s (Normalbetrieb) | ~0,22 % |
| MQTT-Publish (Sensor- + Relaiswerte) | ~5–10 ms | alle 60 s (nur wenn Broker verbunden) | ~0,01 % |

**Ø-CPU-Last im Normalbetrieb (beide Displays gesteckt, Sensor 2 +
MQTT aktiv, ungünstigster Fall): ~4–5 %** — weiterhin weit unter dem
40-%-Zielwert, auch mit deutlich mehr aktiven Managern als bei
`sensormeter`.

## Fazit

| Zielwert (Pflichtenheft 8) | Rechnerischer/gemessener Stand |
|---|---|
| CPU load < 40 % | ~4–5 % im ungünstigsten Normalbetrieb (Rechnung) |
| RAM usage < 60 % | 19,3 % statisch, `pio run` gemessen → weit darunter |
| Sensor polling minimal (60s) | Umgesetzt (`SensorManager`, `SensorDetector`, `MqttManager`-Publish folgen demselben Takt) |
| Webserver non-blocking (Async) | Umgesetzt (`ESPAsyncWebServer`, wie bei allen Geschwisterprojekten) |

Der erste echte Hardware-Bringup (2026-07-18) bestätigt einen stabilen
Dauerbetrieb ohne Absturz/Watchdog-Reset (nach Behebung der drei beim
Bringup gefundenen Bugs, siehe `entscheidungen.md`) — allerdings bisher
nur mit internem Sensor, ohne beide Displays gleichzeitig, ohne aktiven
Relais-Zyklus und ohne echten MQTT-Broker gegengetestet. Der obige
Ø-Last-Wert von 4–5 % ist daher der **rechnerische** Worst Case bei
voller Modulbestückung, keine gemessene Dauerlast unter dieser Konfiguration.
