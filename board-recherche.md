# Board-Recherche: ESP32 mit PoE und mehr Speicher

Recherche vom 2026-07-10 für das zukünftige `sensormeter-poe`-Projekt.
Referenzpunkt ist das aktuell in `sensormeter` verwendete Board
**WT32-ETH01**: ESP32-WROOM-32E, 4 MB Flash, **kein PSRAM**, LAN8720-PHY
(nativ), **kein PoE** (externer Splitter nötig).

## Preishinweis

Amazon.de blockiert automatisierte Preisabfragen (Preis wird per
JavaScript nachgeladen, in keinem der Abrufversuche zugänglich — auch
nicht über Google-Snippets). Alle Preise unten stammen stattdessen von
**mindestens zwei verifizierten, aktuell abrufbaren Quellen**: DigiKey.de
(Live-Lagerbestand einsehbar) und/oder deutschen Fachhändlern
(roboter-bausatz.de), ergänzt um den Hersteller-Direktpreis. Alle Preise
inkl. deutscher MwSt., sofern nicht anders vermerkt. Stand: 2026-07-10,
Olimex/LILYGO-Preise ändern sich gelegentlich — vor Kauf erneut prüfen.

## Vergleichstabelle

| Board | Hersteller | Chip / Speicher | Ethernet | PoE | Preis Quelle 1 | Preis Quelle 2 | Ø Preis |
|---|---|---|---|---|---|---|---|
| **ESP32-POE2** ⭐ empfohlen | [Olimex](https://www.olimex.com/Products/IoT/ESP32/ESP32-POE2/open-source-hardware) | ESP32-WROVER-E-N4R8: 4 MB Flash + **8 MB PSRAM** | LAN8720, nativ (RMII) | integriert, TPS2378PW, 802.3af Class 4 (12,95–25,5 W) | DigiKey.de: 29,39€ (inkl. MwSt., auf Bestellung, 5 Wo. Lieferzeit) | Olimex direkt: 20,95€ (Herstellerpreis, exkl. Versand) | **~25,20€** |
| ESP32-POE-ISO-WROVER-EA | [Olimex](https://www.olimex.com/Products/IoT/ESP32/ESP32-POE-ISO/open-source-hardware) | ESP32-WROVER-E: 4 MB Flash + **8 MB PSRAM**, externe Antenne (U.FL) | LAN8720, nativ | integriert, TPS2375PW, 802.3af, **+3000 VDC galvanische Trennung** | DigiKey.de: 33,95€ (inkl. MwSt., 7 auf Lager) | Olimex direkt: 24,95€ | **~29,45€** |
| ESP32-POE-ISO (Basis) | [Olimex](https://www.olimex.com/Products/IoT/ESP32/ESP32-POE-ISO/open-source-hardware) | ESP32-WROOM-32UE: 4 MB Flash, **kein PSRAM** | LAN8720, nativ | integriert, isoliert | roboter-bausatz.de: 41,01€ (sofort verfügbar, 1-3 Tage) | Olimex direkt: 24,95€ | **~33,00€** |
| ESP32-POE-ISO-16MB | [Olimex](https://www.olimex.com/Products/IoT/ESP32/ESP32-POE-ISO/open-source-hardware) | ESP32-WROOM: **16 MB Flash**, kein PSRAM | LAN8720, nativ | integriert, isoliert | Olimex direkt: ~25€ (Herstellerpreis) | *kein zweiter Preis verifiziert* | — |
| LILYGO T-POE-Pro | [LILYGO](https://lilygo.cc/products/t-poe-pro) | ESP32-WROVER-E: **16 MB Flash + 8 MB PSRAM** (meiste Rohkapazität) | LAN8720, nativ | integriert/mitgeliefert, 802.3af **+ at** | LILYGO offiziell: 33,05 USD (≈ 30,70€, Umrechnungskurs, **kein EUR-Direktpreis**) | *kein zweiter Preis verifiziert (auf Amazon.de gelistet, Preis nicht abrufbar)* | — |
| Waveshare ESP32-S3-ETH + PoE-Modul (nicht empfohlen) | [Waveshare](https://www.waveshare.com/esp32-s3-eth.htm) | ESP32-S3R8: 16 MB Flash + 8 MB PSRAM (modernster Chip) | **W5500 über SPI** (kein natives EMAC, mehr CPU-Overhead) | **separates Zusatzmodul**, nicht auf der Hauptplatine | Amazon.com (Referenz, nicht .de): 25,99–34,99 USD | AliExpress: 11,09–22,35 USD | grobe Spanne 12–35 USD |

## Einordnung

- **PSRAM statt reinem Flash** ist der wertvollere "mehr Speicher"-Zugewinn
  für ein Projekt mit Webserver/SNMP/JSON (mehr Heap für TLS/Puffer) — die
  WROVER-Varianten (POE2, POE-ISO-WROVER, T-POE-Pro) sind deshalb den
  reinen 16-MB-Flash-Varianten vorzuziehen, außer es wird wirklich viel
  Dateisystem-/OTA-Platz gebraucht.
- **Natives Ethernet (LAN8720, RMII)** statt SPI-Ethernet (W5500) ist für
  einen Sensor mit kleinen Payloads zwar nicht durchsatzkritisch, aber die
  Waveshare-S3-Boards bringen ohne Not mehr Komplexität (separates
  PoE-Modul, zum Testzeitpunkt spärliche Dokumentation) — deshalb keine
  Empfehlung trotz modernerem Chip.
- **Praxis-Validierung:** In der Home-Assistant-Community berichtete ein
  Nutzer von 20 im Feld eingesetzten Olimex-ESP32-POE-Boards ("They are
  awesome!") — reales Langzeit-Feedback, kein reiner Datenblatt-Vergleich.
- **Empfehlung: Olimex ESP32-POE2** — neuestes Olimex-Board, USB-C, 8 MB
  PSRAM, günstigste integrierte PoE-Option (~25€ im Schnitt), etablierter
  Open-Source-Hardware-Hersteller. Nur falls galvanische Trennung wichtig
  ist (Störumgebung, Sicherheit), stattdessen zur ISO-WROVER-Variante
  greifen (~29€ im Schnitt, +3000 VDC Isolation).

## Quellen

- [Olimex ESP32-POE2](https://www.olimex.com/Products/IoT/ESP32/ESP32-POE2/open-source-hardware)
- [Olimex ESP32-POE-ISO](https://www.olimex.com/Products/IoT/ESP32/ESP32-POE-ISO/open-source-hardware)
- [DigiKey.de: ESP32-POE2](https://www.digikey.de/de/products/detail/olimex-ltd/ESP32-POE2/23330971)
- [DigiKey.de: ESP32-POE-ISO-WROVER-EA](https://www.digikey.de/de/products/detail/olimex-ltd/ESP32-POE-ISO-WROVER-EA/21662210)
- [roboter-bausatz.de: ESP32-POE-ISO](https://www.roboter-bausatz.de/p/olimex-esp32-poe-iso-iot-entwicklungsboard)
- [LILYGO T-POE-Pro](https://lilygo.cc/products/t-poe-pro)
- [Waveshare ESP32-S3-ETH (CNX Software Review)](https://www.cnx-software.com/2024/10/28/waveshare-esp32-s3-eth-board-provides-ethernet-and-camera-connectors-supports-raspberry-pi-pico-hats/)
- [Home Assistant Community: LILYGO vs. Olimex PoE-ESP32](https://community.home-assistant.io/t/lilygo-poe-esp32-or-olimex-poe-esp32/340986)
- [WT32-ETH01 Referenz-Board (GitHub)](https://github.com/egnor/wt32-eth01)
