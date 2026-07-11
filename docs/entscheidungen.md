# Entscheidungsprotokoll — Sensormeter PoE

Dieses Dokument haelt die beim Bau der ersten Firmware getroffenen
Entscheidungen und deren Begruendung fest - analog zu den gleichnamigen
Dokumenten bei Sensormeter (WT32-ETH01) und Sensormeter WLAN.

## Erste Firmware-Fassung (0.1.0-p0) umgesetzt

Auf Anfrage die komplette, in `docs/lastenheft.txt`/`docs/pflichtenheft.txt`
bereits ausgearbeitete Spezifikation in echten Code umgesetzt - **kein
Board vorhanden, daher nicht geflasht/auf Hardware getestet**, nur per
`pio run` gebaut und verifiziert (siehe unten).

Architektur/Code so weit wie moeglich direkt von den beiden
Schwesterprojekten uebernommen statt neu erfunden:
- `DataManager`, `ConfigManager` (+ neue `<aktor>`/`<mqtt>`-Bloecke),
  `StorageManager`, `TimeManager`, `SensorManager` (beide Sensoren DHT-22,
  nicht DHT-11 wie bei Sensormeter intern), `SNMPManager`, `SyslogManager`,
  `OtaManager`, `WebServerManager` (+ Aktor/MQTT/Erkennungs-Erweiterungen):
  strukturell 1:1 vom Sensormeter-Projekt (WT32-ETH01) uebernommen, da
  dieses Projekt dessen vollen Funktionsumfang uebernehmen soll (Lastenheft
  Abschnitt 3).
- `DisplayManager`: Sensormeter-Vorbild, aber Adafruit_SH110X statt
  Adafruit_SSD1306 (SH1107 128x128 statt SSD1306 128x64, API-inkompatibel),
  Taster-Anzeige-Logik an ButtonManager ausgelagert.
- `ButtonManager`: aus Sensormeter WLAN's `DisplayManager::handleButton()`
  herausgeloest in ein eigenstaendiges Modul (Pflichtenheft nennt es
  explizit als Task/Modul), damit DisplayManager nur noch zeichnet.
- `MqttManager`: Sensormeter-WLAN-Vorbild, erweitert um die Aktor-Rolle
  (Relais-Switch-Entity, command_topic-Handling) - siehe
  `sensormeter-wlan/repo/docs/entscheidungen.md` fuer den dortigen
  Sensor-only-Vorlaeufer.
- `SensorDetector`, `RelayManager`: komplett neu (kein Vorbild in den
  Schwesterprojekten) - I2C-Scan+DHT-Probe bzw. RJ45-Pin6/7-Ansteuerung
  gemaess Lastenheft Abschnitt 15/16.

------------------------------------------------------------

## Ethernet: W5500 braucht Arduino-ESP32 3.x, nicht das offizielle PlatformIO-Release

Vor dem Schreiben von `NetManager.cpp` real recherchiert (nicht
angenommen): das cachierte `framework-arduinoespressif32` fuer das
offizielle PlatformIO-`espressif32`-Platform (aktuell Version 7.0.1, Stand
Mai 2026) buendelt weiterhin **Arduino-ESP32 2.0.17** - dessen `ETH.h`
unterstuetzt ausschliesslich native RMII-PHYs (LAN8720, TLK110, RTL8201,
DP83848, DM9051, KSZ8041/8081), **kein SPI-basiertes W5500** (verifiziert
per direktem Blick in den lokal gecachten Header, nicht nur per
Websuche).

W5500-Support kam erst mit Arduino-ESP32 3.x (ESP-IDF-5.x-basiert) - das
offizielle PlatformIO-`espressif32`-Release hat diesen Sprung zum
Erstellungszeitpunkt noch nicht nachvollzogen (dieselbe Situation wie bei
der ESP32-P4-Recherche fuer die Board-Wahl, siehe `board-recherche.md`).
Loesung: **pioarduino** (`github.com/pioarduino/platform-espressif32`),
ein Community-Fork, der genau dafuer existiert. `platform =
https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip`
in `platformio.ini`.

Die W5500-Initialisierungssequenz (`SPI.begin(sck,miso,mosi);
ETH.begin(ETH_PHY_W5500, addr, cs, irq, rst, SPI);`) wurde **vor** dem
Schreiben von `NetManager.cpp` in einem isolierten Testprojekt gegen den
tatsaechlichen pioarduino-Build kompiliert und verifiziert (Google-Suche +
offizielles `ETH_W5500_Arduino_SPI`-Beispiel aus dem
`espressif/arduino-esp32`-Repo als Quelle, dann real gebaut, nicht nur
angenommen).

------------------------------------------------------------

## `NetworkManager` umbenannt zu `NetManager` (Namenskollision mit Arduino-ESP32 3.x)

Der erste Build-Versuch schlug fehl: Arduino-ESP32 3.x bringt selbst eine
Klasse `class NetworkManager : public NetworkEvents, public Printable`
mit (`libraries/Network/src/NetworkManager.h`, Teil des neuen,
vereinheitlichten Netzwerk-Event-Systems) - kollidiert direkt mit der
projekteigenen `NetworkManager`-Klasse (per `#include <WiFi.h>`
transitiv immer mit eingebunden). Betrifft NUR dieses Projekt (Arduino-ESP32
2.0.17 bei den Schwesterprojekten kennt diese Klasse nicht) - dort bleibt
der Name unveraendert.

**Fix:** projektweite Umbenennung `NetworkManager` -> `NetManager`
(Dateien `NetManager.h`/`.cpp`, alle Referenzen in `TimeManager`,
`SensorManager`* nein - nur wo tatsaechlich referenziert:
`ConfigManager.h` (Kommentar), `DisplayManager`, `SNMPManager`,
`SyslogManager`, `TimeManager`, `WebServerManager`, `MqttManager`,
`main.cpp`). Instanzvariablen-/Parameternamen (`_network`,
`networkManager`) blieben unveraendert, da nur der TYP-Name kollidiert,
keine Bezeichner.

------------------------------------------------------------

## Kritischer Zwischenfall: pioarduino ueberschrieb den geteilten Paket-Pool beider Schwesterprojekte

Waehrend der W5500-API-Verifikation (siehe oben) wurde ein Testprojekt mit
`platform = <pioarduino-URL>` **ohne eigenen `core_dir`** gebaut. Ergebnis:
pioarduino registriert sowohl das Platform-Paket als auch
`framework-arduinoespressif32` unter **denselben Namen** wie das
offizielle PlatformIO-`espressif32` (das ist beabsichtigt - es soll ein
Drop-in-Ersatz sein) - ein `pio run` ohne Isolation ueberschreibt dadurch
den unter `~/.platformio` global geteilten Paket-Pool mit der
Arduino-ESP32-3.x-Variante.

**Konkret passiert:** Sensormeter (WT32-ETH01) und Sensormeter WLAN
liessen sich direkt danach nicht mehr bauen (u. a. derselbe
`NetworkManager`-Klassenkonflikt wie oben, zusaetzlich fehlende
`pioarduino-build.py`, da auch die Platform-Builder-Skripte
ueberschrieben wurden). **Repariert** durch vollstaendiges Loeschen der
betroffenen globalen Platform-/Paket-Ordner
(`~/.platformio/platforms/espressif32`,
`~/.platformio/packages/framework-arduinoespressif32{,-libs}`) und
gezielte Neuinstallation der exakt gepinnten offiziellen Version
(`pio pkg install -g -t "platformio/framework-arduinoespressif32@3.20017.241212"`
sowie `pio pkg install -g -p "platformio/espressif32@7.0.1"`). Beide
Schwesterprojekte danach mit `pio run` verifiziert - bauen wieder mit den
bekannten Flash-/RAM-Werten (Sensormeter 84,3 %/1.104.613 B Flash,
Sensormeter WLAN 82,1 %/1.075.673 B Flash - identisch zum Stand vor dem
Zwischenfall).

**Dauerhafte Praevention:** `sensormeter-poe/repo/firmware/platformio.ini`
setzt jetzt `[platformio] core_dir = .pio-core` - das isoliert saemtliche
von diesem Projekt heruntergeladenen Pakete (Platform, Framework,
Toolchains) vollstaendig vom globalen `~/.platformio`-Pool, den die
beiden Schwesterprojekte weiterhin nutzen. `firmware/.pio-core/` ist
entsprechend in `.gitignore` aufgenommen. **Diese Einstellung darf nicht
entfernt werden**, ohne eine gleichwertige Isolation zu haben - sonst
wiederholt sich der Zwischenfall bei jedem erneuten Bauen dieses Projekts.

------------------------------------------------------------

## Windows: pio-Aufrufe muessen ueber PowerShell laufen, nicht Git-Bash/MSYS

Waehrend der pioarduino-Toolchain-Installation (`idf_tools.py`, laedt
u. a. `toolchain-xtensa-esp-elf`) trat unter Git-Bash/MSYS der Fehler
`"ERROR: MSys/Mingw is not supported. Please follow the getting started
guide"` auf - der Compiler (`xtensa-esp32s3-elf-g++`) blieb dadurch
unauffindbar, der Build schlug fehl. Derselbe Befehl **ueber PowerShell**
gestartet installierte den Toolchain sauber und baute erfolgreich. Grund:
`idf_tools.py`s Shell-Erkennung (ueber `MSYSTEM`/`SHELL`-Umgebungsvariablen)
lehnt MSYS-Umgebungen explizit ab, PowerShell triggert diese Pruefung
nicht.

**Praktische Konsequenz:** alle `pio`-Befehle fuer `sensormeter-poe`
(build, upload, monitor) muessen ueber PowerShell laufen. Betrifft nur
dieses Projekt (die Schwesterprojekte nutzen die officielle,
bereits vollstaendig gecachte 2.0.17-Toolchain, dort trat das Problem nie
auf, da kein Toolchain-Download mehr noetig war).

------------------------------------------------------------

## GPIO35-37 vorsorglich nicht belegt, trotz "frei" im Hersteller-Pinout

Der verbaute ESP32-S3R8 nutzt bei **Octal-PSRAM** (das "R8" in der
Chipbezeichnung: 8 MB PSRAM ueber eine Octal-SPI-Schnittstelle) GPIO35-37
silizium-seitig fuer die PSRAM-Anbindung - ein bekannter,
dokumentierter ESP32-S3-Hardwarefakt, unabhaengig vom jeweiligen Board.
Das bereits im Projekt vorhandene, aus der Herstellerseite extrahierte
Pinout-Diagramm (`docs/ESP32-S3-ETH-Datenblatt.pdf`) markiert diese Pins
allerdings als "gruen" (frei nutzbares GPIO).

Das deckt sich mit `board-recherche.md` (dort bereits bei der
Board-Auswahl vermerkt: "GPIO33-37 intern durch PSRAM belegt") - ein
interner Widerspruch zwischen den beiden bereits im Projekt vorhandenen
Dokumenten, der sich ohne echte Hardware nicht abschliessend aufloesen
liess. Board-recherche.md ist die aeltere, spezifischere Quelle (gezielt
zur GPIO-Budget-Frage recherchiert) und wird hier als massgeblich
behandelt: GPIO35-37 wurden in `pins.h` **vorsorglich fuer keine neue
Peripherie verwendet** - stattdessen GPIO1/2 (I2C),
GPIO15 (DHT intern), GPIO16-19 (RJ45-Modul), alles nachweislich
unkritische, nicht strapping-relevante Pins. Ebenfalls vermieden:
GPIO3/45/46 (Boot-Strapping-Pins).

------------------------------------------------------------

## Partitionsschema: `default_16MB.csv`, Octal-PSRAM

`platformio.ini` setzt `board_upload.flash_size = 16MB`,
`board_build.partitions = default_16MB.csv`,
`board_build.psram_type = opi` auf der generischen
`esp32-s3-devkitc-1`-Board-Definition (kein eigenes Board-JSON noetig) -
das im Arduino-ESP32-Framework mitgelieferte `default_16MB.csv` bringt
bereits zwei OTA-faehige App-Partitionen (`app0`/`app1`, je 6,4 MB) mit,
identisch zum bei den Schwesterprojekten etablierten Muster (siehe deren
`entscheidungen.md`) - kein zusaetzlicher Partitionierungsaufwand fuer die
in Lastenheft/Pflichtenheft geforderte lokale OTA-Faehigkeit.

------------------------------------------------------------

## Relais-Schalter bewusst NUR auf der (passwortgeschuetzten) Einstellungsseite, nicht auf der Hauptseite

Lastenheft Abschnitt 16.2 laesst beides zu ("Schalter auf der Hauptseite
ODER Einstellungsseite"). Da die Hauptseite laut Abschnitt 5.1 bewusst
**oeffentlich, ohne Login** ist, waere ein Aktor-Schalter dort ohne
Authentifizierung bedienbar - das haette ein echtes
Sicherheits-/Sabotage-Risiko fuer einen mit 240V-Verbraucher geschalteten
Ausgang bedeutet. Der Schalter liegt daher ausschliesslich im
passwortgeschuetzten "Aktor"-Block der Einstellungsseite; REST (`/api/relay`)
und MQTT bleiben zusaetzlich moeglich (MQTT-Zugriff setzt ohnehin
Broker-Zugangsdaten voraus, REST verlangt HTTP-Basic-Auth wie der Rest der
Einstellungsseite).

------------------------------------------------------------

## MQTT-Topic-Praefix: echtes Konfigurationsfeld statt reiner Laufzeit-Ableitung

Anders als bei Sensormeter WLAN (dort wird der Praefix immer live aus dem
Systemnamen abgeleitet, kein eigenes Feld) sieht Lastenheft Abschnitt 16.3
fuer Sensormeter PoE ausdruecklich ein editierbares "Topic-Praefix"-Feld
vor (Default: wie der mDNS-Hostname aus dem Systemnamen abgeleitet, aber
ueberschreibbar). Umgesetzt als `DeviceConfig::mqttTopicPrefix` (leer =
Ableitung, sonst der eingetragene Wert) - `MqttManager::topicPrefix()`
kapselt diese Fallback-Logik.

------------------------------------------------------------

## Build-Verifikation

Mit `pio run` (ueber PowerShell, isolierter `core_dir`) erfolgreich
gebaut: **Flash 21,4 % (1.401.071 B von 6.553.600 B App-Partition), RAM
17,3 % (56.780 B von 327.680 B)** - deutlich mehr Flash-Headroom als bei
den beiden Schwesterprojekten (80-84 %), da die 16-MB-Flash-Partition
(6,4 MB je App-Slot) gegenueber deren 1,25-MB-Slots viel grosszuegiger
ist. **Nicht geflasht** - kein Waveshare-ESP32-S3-ETH-Board zum
Erstellungszeitpunkt vorhanden. Beide Schwesterprojekte im Anschluss
erneut gebaut und als unveraendert funktionsfaehig verifiziert (siehe
Zwischenfall-Abschnitt oben).

Dokumentation (Lastenheft/Pflichtenheft) bereits vollstaendig vor diesem
Firmware-Stand vorhanden; neu erstellt in dieser Runde:
`docs/verdrahtungsplan.html` und `docs/sensormeter-poe-onepager.html`
(auf Wunsch nur als HTML, keine PDF-Exporte in dieser Runde).

## Anbieter-Branding (Weisslabel) von Sensormeter WLAN portiert

Auf Anfrage von Sensormeter WLAN (dort erste Umsetzung, siehe dessen
`docs/entscheidungen.md`) unverändert im Konzept hierher portiert: neuer
`BrandingManager` (freier Anbietername + optionales Logo auf LittleFS,
kein PNG/JPEG-Decoder), eigene OLED-Rotationsseite (nur Teil der
Rotation, wenn tatsächlich konfiguriert), Web-Header-Banner, Logo-Upload
per Multipart (gleiches Tmp-Datei-Muster wie `ConfigManager::save()`),
Web-Auslieferung als on-the-fly synthetisierter 1-Bit-BMP unter
`/branding/logo.bmp`.

**Einzige echte Abweichung gegenüber den Geschwisterprojekten**: dieses
Board nutzt ein SH1107-OLED mit 128x128 statt 128x64 Pixeln (siehe
`DisplayManager`) - `LOGO_WIDTH`/`LOGO_HEIGHT`/`LOGO_BYTES` in
`BrandingManager.h` daher auf 128x128/2048 Byte angepasst (statt
1024 Byte bei Sensormeter/Sensormeter WLAN). `scripts/convert-logo.ps1
-Display poe` erzeugt bereits das passende Format. Der bei Sensormeter
WLAN gefundene LittleFS-`exists()`-Logquirk (RAM-Cache-Fix) wurde von
Anfang an übernommen, tritt hier also gar nicht erst auf.

**Nicht auf echter Hardware getestet**: kein Waveshare-ESP32-S3-ETH-Board
in dieser Session angeschlossen (wie bei allen bisherigen
PoE-Firmware-Runden). Verifiziert wurde ausschließlich per `pio run`
(erfolgreicher Build, ueber PowerShell wegen des isolierten
`core_dir`/pioarduino-Setups) - die BMP-Konstruktionslogik selbst wurde
bereits unabhängig bei Sensormeter WLAN verifiziert (Python+Pillow-
Nachbau, siehe dortiges Protokoll), identischer Code hier übernommen.

Mit `pio run` (über PowerShell, isolierter `core_dir`) erfolgreich
gebaut: Flash 21,5 % (1.407.439 B von 6.553.600 B App-Partition,
gegenüber 21,4 % / 1.401.071 B vor dieser Änderung, +6.368 B), RAM
19,2 % (63.052 B von 327.680 B, gegenüber 17,3 % / 56.780 B, +6.272 B) -
bei 16-MB-Flash weiterhin vollkommen unkritisch. Lastenheft (Abschnitt
17) und Pflichtenheft (3.11, 4.3/4.4) sowie der One-Pager entsprechend
ergänzt.
