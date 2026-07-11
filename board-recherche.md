# Board-Entscheidung: Waveshare ESP32-S3-ETH + PoE-Modul

Für das zukünftige `sensormeter-poe`-Projekt. Referenzpunkt ist das
aktuell in `sensormeter` verwendete Board **WT32-ETH01**: ESP32-WROOM-32E,
4 MB Flash, **kein PSRAM**, LAN8720-PHY (nativ), **kein PoE** (externer
Splitter nötig).

## Entscheidung (2026-07-11)

Gewählt: **Waveshare ESP32-S3-ETH** mit dem optionalen, aufsteckbaren
("Huckepack") PoE-Modul — nicht die Industrie-Variante mit 8 eingebauten
Relais (ESP32-S3-ETH-8DI-8RO), die zuvor als Alternative verglichen wurde.

| Merkmal | Wert |
|---|---|
| Chip | ESP32-S3R8: Xtensa LX7 Dual-Core, 240 MHz |
| Speicher | 16 MB Flash + **8 MB PSRAM** |
| Ethernet | W5500 über SPI (kein natives EMAC, dafür nur ~5 statt ~9 GPIO belegt) |
| PoE | **Optionales, aufsteckbares Modul** (nicht auf der Hauptplatine integriert) |
| GPIO | ~27 freie Pins auf 2×20-Pin-Headern (GPIO33-37 intern durch PSRAM belegt) |
| Sonstiges | USB-C, Micro-SD-Slot, Kamera-Anschluss (OV2640/OV5640, für dieses Projekt nicht benötigt) |
| Hersteller | [Waveshare](https://www.waveshare.com/esp32-s3-eth.htm) |

**Warum dieses Board:** Das erweiterte Lastenheft (Touch-Display 2,4"+,
mehrere I2C-Sensoren, PIR, Gassensor, Relais, RJ45-Modularanschluss)
braucht vor allem GPIO-Budget. Natives RMII-Ethernet (wie beim
ursprünglich erwogenen Olimex ESP32-POE2 oder dem aktuellen WT32-ETH01)
blockiert dafür zu viele Pins (~9 von wenigen verfügbaren). SPI-Ethernet
(W5500) kostet nur ~5 GPIO und ist bei den hier anfallenden kleinen
SNMP/JSON-Payloads durchsatzseitig kein Nachteil.

## Preis

| Quelle | Preis | Hinweis |
|---|---|---|
| AliExpress (Shop1105281022, „ESP32-S3 ETH Kamera-Entwicklungsboard") | 31,19€ | inkl. MwSt., verifiziert per Headless-Chrome-PDF-Rendering (AliExpress ist wie Amazon.de per direktem Abruf nicht lesbar) |
| Amazon | 36,00€ | von Peter direkt bestätigt |
| **Ø Preis** | **~33,60€** | Basisboard ohne PoE-Modul; Preis für das PoE-Zusatzmodul separat, noch nicht verifiziert |

## Datenblatt

Offizielles Schaltplan-PDF vom Hersteller, lokal abgelegt:
[`docs/ESP32-S3-ETH-Schematic.pdf`](docs/ESP32-S3-ETH-Schematic.pdf)
(Quelle: `files.waveshare.com`, 862 KB, Stand 2026-07-11).

## Display (2026-07-11)

**Entscheidung: 1,5" OLED, 128×128, SH1107-Controller, I2C.** Ersetzt die
zuvor angedachte Touch-Display-Anforderung (2,4"+) — kein Touch mehr,
stattdessen eine reine Statusanzeige. Das vereinfacht den Scope wieder
näher an die bestehende Sensormeter/Sensormeter-WLAN-Konvention (dort:
0,96" SSD1306, ebenfalls I2C, ebenfalls reine Anzeige ohne Touch), nur mit
mehr Fläche (128×128 statt 128×64) für zusätzliche Statuszeilen oder einen
kleinen Graphen.

| Merkmal | Wert |
|---|---|
| Diagonale / Auflösung | 1,5" / 128×128 Pixel |
| Controller | SH1107 |
| Bus | I2C (nur SDA+SCL — 2 Pins, teilt sich den Bus mit den übrigen Sensoren) |

**Wichtig für die Firmware:** SH1107 ist **nicht** pin- oder API-kompatibel
mit dem bisher in Sensormeter/Sensormeter WLAN verwendeten SSD1306 (dort
`Adafruit_SSD1306`-Bibliothek). Für dieses Display wird stattdessen
**`Adafruit_SH110X`** benötigt (deckt SH1106 und SH1107 ab) — beim aus
`DisplayManager.cpp` der Geschwisterprojekte abgeleiteten Code entsprechend
den Bibliotheks-Include und die Initialisierung anpassen, nicht einfach
kopieren.

**Auswirkung auf GPIO-Budget:** Durch den Wegfall des SPI-Touch-Displays
(vorher realistisch 5-7 Pins für Display+Touch) sinkt der Pin-Bedarf für
die Anzeige auf nur noch 2 (I2C, geteilt mit Temp-/Luftgüte-Sensor und
ggf. dem RJ45-Modularanschluss) — beim ohnehin schon großzügigen
GPIO-Budget des ESP32-S3-ETH (Abschnitt oben) damit erst recht
unkritisch.

## Quellen

- [Waveshare ESP32-S3-ETH (Produktseite)](https://www.waveshare.com/esp32-s3-eth.htm)
- [Waveshare ESP32-S3-ETH Schaltplan (PDF, im Repo gespeichert)](https://files.waveshare.com/wiki/ESP32-S3-ETH/ESP32-S3-ETH-Schematic.pdf)
- [CNX Software: Waveshare ESP32-S3-ETH Review](https://www.cnx-software.com/2024/10/28/waveshare-esp32-s3-eth-board-provides-ethernet-and-camera-connectors-supports-raspberry-pi-pico-hats/)
- [WT32-ETH01 Referenz-Board (GitHub)](https://github.com/egnor/wt32-eth01)
