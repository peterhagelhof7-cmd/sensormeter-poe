# Stückliste (BOM) – Sensormeter PoE

## Pro Gerät

| Bauteil | Menge | Hinweis |
|---|---|---|
| Waveshare ESP32-S3-ETH (ESP32-S3R8, W5500-Ethernet per SPI) | 1 | Hauptmodul, siehe `docs/ESP32-S3-ETH-Datenblatt.pdf` |
| Waveshare PoE-Modul, 802.3af, aufsteckbar | 0–1 | Optional — Gerät läuft auch rein per USB-C/5V ohne PoE, siehe `stromversorgung.md` |
| DHT-22, 3-Draht-Modul | 1 | intern, Sensor 1, Data → GPIO15, Pull-up 10 kΩ → 3,3V |
| DHT-22, 3-Draht-Modul | 0–1 | extern, Sensor 2, über RJ45 Pin 5 → GPIO16 |
| OLED SSD1306, 0,96", 128×64, I2C | 1 | intern, SDA → GPIO1, SCL → GPIO2, Adresse 0x3C |
| OLED SH1107, 1,5", 128×128, I2C | 0–1 | optionales externes RJ45-Steckmodul, Adresse 0x3D, gleicher I2C-Bus wie das interne Display (RJ45 Pin 3/4) |
| RJ45-Buchse, 8P8C, geschirmt | 1 | Modularanschluss für Sensor 2 / Relais / externes Display |
| Relais-Modul, 5V, 1 Kanal | 0–1 | Steuerung über RJ45 Pin 6 (GPIO17, active LOW), Feedback über Pin 7 (GPIO18) |
| Pull-up-Widerstand 10 kΩ | 1 | intern DHT-22-Data (GPIO15) → 3,3V |
| Pull-up-Widerstand 4,7 kΩ | 1–4 | extern DHT-22-Data (RJ45-Modulseite, Pin 5) → 3,3V; bei I2C-Modul (SH1107 o. Ä.) zusätzlich SDA + SCL |
| Gehäuse / Grundplatte | 1 | noch nicht final entworfen, siehe `entscheidungen.md` |
| Netzteil 5V, ≥ 1 A (USB-C) **oder** PoE-Injektor/Switch 802.3af | 1 | Herleitung siehe `stromversorgung.md` |

## Werkzeug (einmalig, nicht pro Gerät)

| Werkzeug | Hinweis |
|---|---|
| USB-C-Kabel | Zum Flashen und Betreiben — anders als `sensormeter` (WT32-ETH01, externer Debug-Burning-Adapter nötig) hat die Waveshare ESP32-S3-ETH natives USB, automatischer Upload ohne manuellen BOOT/EN-Handgriff (bestätigt beim Board-Bringup 2026-07-18, siehe `entscheidungen.md`) |

## RJ45-Pinbelegung (Referenz)

Siehe `docs/verdrahtungsplan.html` für das vollständige Schema. Pin-Rollen
identisch zu `sensormeter` (WT32-ETH01) und `lastenheft.txt` Abschnitt 14 –
die konkreten GPIO-Nummern unterscheiden sich zwangsläufig (anderer Chip,
siehe `firmware/include/pins.h`):

| RJ45-Pin | Funktion | GPIO |
|---|---|---|
| 3/4 | I2C-Bus (externes Display, künftige I2C-Sensormodule) | GPIO1 (SDA) / GPIO2 (SCL), geteilt mit dem internen Display |
| 5 | Sensor 2 (DHT-22) **oder** Kontakt (Modultyp manuell gewählt) | GPIO16 |
| 6 | Relais-Steuerung, active LOW | GPIO17 |
| 7 | Relais-Feedback / Interrupt | GPIO18 |
| 8 | 5V-Versorgungsschiene (kein GPIO mehr, siehe „RJ45 Pin 8: 5V statt Reserve" in `entscheidungen.md`) | – |

## Nicht Teil dieses Projekts (bewusst)

- Fest verbautes PoE (das aufsteckbare 802.3af-Modul ist optional, nicht
  im Standard-Lieferumfang)
- Kontaktmodul und Relais gleichzeitig über RJ45 Pin 5/6/7 sind möglich
  (unabhängige Pins), aber bisher nicht gemeinsam auf echter Hardware
  getestet (siehe `systemlast.md`, `entscheidungen.md`)
