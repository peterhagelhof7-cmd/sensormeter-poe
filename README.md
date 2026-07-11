# Sensormeter PoE

ESP32-S3-basierter Umweltsensor (2× DHT-22, OLED SH1107) auf dem
**Waveshare ESP32-S3-ETH** (W5500-Ethernet + WLAN, optionales
PoE-Huckepack-Modul). Dritter Familienzweig neben
[Sensormeter](https://github.com/peterhagelhof7-cmd/sensormeter)
(WT32-ETH01) und
[Sensormeter WLAN](https://github.com/peterhagelhof7-cmd/sensormeter-wlan)
(ESP32-WROOM-32) - übernimmt deren vollen Funktionsumfang 1:1, plus vier
neue Features, die auf den beiden anderen Boards aus Hardware-Gründen
nicht möglich waren: BOOT-Taster-Bedienung, automatische
RJ45-Modul-Erkennung, Relais/Aktor-Steuerung und
MQTT/Home-Assistant-Anbindung.

**Status:** erste Firmware-Fassung (`0.1.0-p0`), code-vollständig gemäß
Lastenheft/Pflichtenheft, mit `pio run` gebaut und verifiziert - **noch
nicht auf echter Hardware getestet/geflasht** (kein Board zum
Erstellungszeitpunkt vorhanden). Siehe
[docs/entscheidungen.md](docs/entscheidungen.md).

[**One-Pager (HTML)**](docs/sensormeter-poe-onepager.html) — kompakte Projektübersicht auf einer Seite.

**Schwesterprojekte:**
[Sensormeter](https://github.com/peterhagelhof7-cmd/sensormeter) (WT32-ETH01, Ethernet + bis zu 2 Sensoren) ·
[Sensormeter WLAN](https://github.com/peterhagelhof7-cmd/sensormeter-wlan) (ESP32-WROOM-32, WLAN-only)

## Dokumentation

| Datei | Inhalt |
|---|---|
| [docs/sensormeter-poe-onepager.html](docs/sensormeter-poe-onepager.html) | One-Pager: Projektübersicht, Architektur, Kennzahlen auf einer Seite |
| [docs/lastenheft.txt](docs/lastenheft.txt) | Fachliche Anforderungen: Webseite, Einstellungen, SNMP-OIDs, RJ45-Modularanschluss, Aktor, MQTT |
| [docs/pflichtenheft.txt](docs/pflichtenheft.txt) | Technische Umsetzung: Tasks, Softwaremodule, Speicherlayout |
| [docs/verdrahtungsplan.html](docs/verdrahtungsplan.html) | Pinbelegung + Verdrahtungsschema (ungeprüft, siehe Hinweis dort) |
| [docs/entscheidungen.md](docs/entscheidungen.md) | Entscheidungsprotokoll: Toolchain (W5500/pioarduino), Namenskollision, Paket-Isolation, GPIO-Vorsicht |
| [docs/PRTG.md](docs/PRTG.md) | PRTG-Integration: OIDs, Geräte-Template-Import, Sensor-Übersicht |
| [docs/prtg-template-sensormeter-poe.odt](docs/prtg-template-sensormeter-poe.odt) | Fertiges PRTG-Geräte-Template für Auto-Discovery |
| [docs/ZABBIX.md](docs/ZABBIX.md) | Zabbix-Integration: OIDs, Template-Import, Host-Einrichtung, Trigger |
| [docs/zabbix-template-sensormeter-poe.yaml](docs/zabbix-template-sensormeter-poe.yaml) | Fertiges Zabbix-Template |
| [board-recherche.md](board-recherche.md) | Board-Auswahl, Preisvergleich, GPIO-Budget-Begründung |
| [docs/ESP32-S3-ETH-Datenblatt.pdf](docs/ESP32-S3-ETH-Datenblatt.pdf) | Zusammengestelltes Hersteller-Datenblatt (Pinout, Maße, Bestückung) |

## Hardware

- Waveshare ESP32-S3-ETH (ESP32-S3R8, 16 MB Flash, 8 MB PSRAM, W5500-Ethernet über SPI)
- 2× DHT-22 (intern fest verbaut + extern über RJ45-Modularanschluss)
- OLED SH1107, 1,5", 128×128, I2C
- RJ45-Modularanschluss (Sensor 2 / Relais, Pin-Rollen identisch zu Sensormeter)
- Onboard-BOOT-Taster (GPIO0) als Bedienelement, PoE optional (Huckepack-Modul)

## Firmware

`firmware/` ist ein PlatformIO-Projekt (Board `esp32-s3-devkitc-1`,
Framework Arduino).

**Wichtig (Toolchain):** braucht Arduino-ESP32 3.x für W5500-Support -
das offizielle PlatformIO-`espressif32`-Platform bündelt das noch nicht,
daher nutzt `platformio.ini` den Community-Fork
[pioarduino](https://github.com/pioarduino/platform-espressif32) mit
einem eigenen, isolierten `core_dir` (`firmware/.pio-core/`), damit die
Pakete nicht mit denen der beiden Schwesterprojekte kollidieren. **Alle
`pio`-Befehle müssen über PowerShell laufen, nicht Git-Bash/MSYS** (der
Toolchain-Installer bricht dort ab). Details siehe
[docs/entscheidungen.md](docs/entscheidungen.md).

```
cd firmware
cp include/config.h.example include/config.h
pio run              # bauen
pio run --target upload   # flashen
pio device monitor   # seriellen Log ansehen (115200 Baud)
```

## Über dieses Projekt

Repo-Struktur, Firmware und Dokumentation entstehen in Zusammenarbeit mit
[Claude](https://claude.com/claude-code) (Anthropic) als KI-Coding-Assistent.
