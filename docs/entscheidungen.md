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

## Verdrahtungsplan interaktiv: Klick auf Draht hebt hervor + zeigt Von/Nach

Auf Anfrage, familienweit für alle vier Projekte. `docs/verdrahtungsplan.html`
war hier bereits als HTML mit Inline-SVG vorhanden (einziges Projekt der
Familie ohne reines PDF) - daher als Erstes umgesetzt und als Vorlage für
die übrigen drei Projekte verwendet.

Jeder der 15 `<path>`-Drähte im SVG hat jetzt `data-wire`/`data-from`/
`data-to`-Attribute sowie einen unsichtbaren, breiteren "Hit"-Pfad
dahinter (11px statt der sichtbaren 1,6px Strichbreite - bei der dünnen
Originallinie kaum treffbar gewesen). Klick auf einen Draht (oder seinen
Hit-Bereich): der Draht wird hervorgehoben (dickerer Strich, Schlagschatten),
alle anderen gedimmt, und eine Info-Zeile unter dem Schema zeigt
"`<Von>` → `<Nach>`" (z.B. "GPIO15 → DHT-22 DATA"). Erneuter Klick auf
denselben Draht oder Klick auf die freie Fläche hebt die Auswahl wieder auf.
Reines Vanilla-JS (keine Bibliothek), ca. 25 Zeilen IIFE am Ende des
`<svg>`-Blocks - passend zum Rest der Doku-Seiten dieser Familie, die
ebenfalls ohne Build-Tooling auskommen.

Für den Druckfall (`@media print`) sind Cursor-Hinweise und die Info-Zeile
ausgeblendet, da eine PDF-Ausgabe ohnehin keine Klicks kennt.

Getestet mit Headless Chrome (`--headless=new --dump-dom`) und einer
temporären Testkopie, die per `MouseEvent`/`dispatchEvent` einen
synthetischen Klick auf Draht `w7` (GPIO15 → DHT-22 DATA) auslöst: bestätigt
korrektes Hervorheben (1 aktiv, 14 gedimmt), korrekten Info-Text, korrektes
Zurücksetzen bei erneutem Klick auf denselben Draht sowie bei Klick auf die
freie Fläche. Kein echtes Board nötig, da rein clientseitiges HTML/JS ohne
Firmware-Bezug.

## docs/projektfamilie.html neu ergänzt (fehlte bisher komplett)

Anders als die drei Geschwisterprojekte hatte dieses Repo noch nie eine
Architekturübersicht - `docs/projektfamilie.html` neu aus dem inzwischen
auf vier Karten erweiterten Familien-Diagramm übernommen (identische Kopie
wie in den anderen vier Repos, siehe dortige `entscheidungen.md`: Karte
"Sensormeter PoE" mit neuer Akzentfarbe `--rust` und Blitz-Icon ergänzt,
Layout neu vermessen wegen der längeren PoE-Kartentexte).

README ergänzt: `<picture>`-Vorschaubild oben (wie bei den Geschwistern),
Dokutabellen-Eintrag, "Schwesterprojekte"-Zeile um Sensormeter Display
erweitert (fehlte dort bisher, obwohl es das dritte Geschwisterprojekt
ist - vermutlich vor dessen Fertigstellung geschrieben und nie
nachgezogen).

Rein statisches HTML/CSS/SVG ohne Firmware-Bezug, kein Board nötig.

## Versionierung

Bisher war `DEVICE_FIRMWARE_VERSION` mit `0.1.0-p0` an ein phasenbasiertes
Schema angelehnt (wie ursprünglich bei Sensormeter), ohne dass hier je
weitere Phasenstände (`-p1`, `-p2`, ...) gepflegt wurden - die Firmware war
von Anfang an in einem Rutsch code-vollständig gemäß Lastenheft/
Pflichtenheft, nur nie auf echter Hardware verifiziert.

**Umstellung auf Semantic Versioning**, analog zu Sensormeter WLAN,
Sensormeter Display und Sensormeter (alle drei zuvor bereits umgestellt,
siehe deren jeweilige `entscheidungen.md`): aktueller Stand auf
**`0.9.0-rc4`** (Beta) gesetzt - gleiches Kriterium wie bei den drei
Geschwisterprojekten: alle Kernfunktionen aus dem Lastenheft sind
umgesetzt, aber nicht auf echter Hardware verifiziert, daher
Release-Candidate-Status statt `1.0.0`.

Die Versionsnummer lebt weiterhin als einzige Quelle der Wahrheit in
`firmware/include/config.h(.example)`, zusätzlich in README und
One-Pager (Badge + Kennzahlen-Tabelle) vermerkt.

## Serial-Kommandozeile + Werksreset-Umfangsauswahl (Port aus Sensormeter WLAN)

Beide Features wurden zuerst in Sensormeter WLAN gebaut und auf echter
Hardware verifiziert, dann hierher portiert (identisches Muster wie beim
Port nach Sensormeter, siehe dortige `entscheidungen.md`) - im selben
Arbeitsgang wie die Versionierungs-Umstellung oben.

**Serial-Kommandozeile** (`handleSerialCommands()` in `main.cpp`):
`status`, `dhcp <lan|wlan>`, `ip <lan|wlan> <ip> <maske> <gateway> [dns]`,
`wifi <ssid> <passwort>`, `dump`/`upload`, `reset`/`reset all`. Zwei
Interfaces (LAN + optionales WLAN) wie bei Sensormeter, daher brauchen
`dhcp`/`ip` ein Interface-Argument; `wifi` bleibt WLAN-only. `status` gibt
zusätzlich LAN-Status/-IP, Sensor 2 (falls `sensor2Enabled`) sowie den
Relais-Zustand aus - Letzteres gibt es bei keinem der Geschwisterprojekte,
da nur Sensormeter PoE einen Aktor hat. Anders als bei Sensormeter (WT32-
ETH01, kein Taster möglich) ist dies hier NICHT der einzige
Netzwerk-unabhängige Reset-Weg - der bestehende BOOT-Taster
(`ButtonManager`, 3s+20s halten) bleibt als zusätzlicher, unveränderter
Codepfad bestehen.

**Werksreset-Umfangsauswahl** (`handleApiFactoryReset()` in
`WebServerManager.cpp`): identische Umstellung wie bei Sensormeter WLAN und
Sensormeter - vier Umfänge (Alles / Nur Konfiguration / Nur Messwerte / Nur
Anbieter-Branding) statt der bisherigen zwei Buttons, inkl.
JS-Bestätigungsdialog. Behebt denselben vorbestehenden Bug wie bei den
Geschwisterprojekten: der alte "Einstellungen + Daten"-Reset löschte nie
die Logo-Datei - jetzt rufen "Alles" und "Nur Anbieter-Branding"
`_branding.deleteLogo()` auf. "Nur Konfiguration" bewahrt
`brandingVendorName` gezielt (inkl. `relayEnabled` und aller MQTT-Felder,
die es nur hier gibt).

Die BOOT-Taster-Logik (`ButtonManager`) bleibt bewusst unverändert - sie
ist eigenständig, nicht Teil dieser Werksreset-Umfangsauswahl, die sich
explizit nur auf den Werksreset-Button im Webserver bezieht.

Nur per `pio run` gebaut (kein Board für Sensormeter PoE vorhanden), Build
erfolgreich (Flash 21,6 %, RAM 19,3 %, minimal gestiegen gegenüber der
vorherigen Fassung). Noch nicht auf echter Hardware verifiziert - bereits
bekannter offener Punkt (s.o.).

## Familien-Standardlogo automatisch provisioniert, außer eigenes Logo bereits vorhanden

Analog zu Sensormeter WLAN (dort zuerst umgesetzt und auf echter Hardware
verifiziert, siehe dessen `entscheidungen.md`): das Standard-Branding-Logo
(Tri-Orbit + Dial Mark, siehe sensormeter-family) musste bisher nach jedem
Flash manuell über die Einstellungsseite hochgeladen werden. Jetzt
automatisch: `BrandingManager::begin()` prüft zuerst `checkLogoOnDisk()`
wie bisher, ruft bei fehlendem Logo aber zusätzlich neu
`provisionDefaultLogo()` auf, das das in `DefaultLogo.h` eingebettete
Rohbild einmalig auf LittleFS schreibt - Tmp-Datei-plus-Umbenennen-Muster
wie beim regulären Web-Upload. Anders als bei den beiden 128×64-OLED-
Geschwisterprojekten ist das Logo hier 128×128 (SH1107, siehe
BrandingManager.h), exakt 2048 Byte - `DefaultLogo.h` wurde entsprechend
aus der `-oled-128x128`-Variante des Familienlogos generiert, nicht aus
der 128×64-Variante der anderen drei Projekte.

**Genau das erwartete Verhalten, kein Entweder-Oder:** Ist bereits ein
Logo vorhanden (eigenes hochgeladenes ODER schon einmal automatisch
provisioniertes), bleibt es unangetastet - `provisionDefaultLogo()` wird
dann gar nicht erst aufgerufen. Ein Kunden-eigenes Logo wird also nie
überschrieben, auch nicht bei einem erneuten Firmware-Flash (ein normaler
`pio run --target upload` rührt die LittleFS-Datenpartition ohnehin nicht
an).

`DefaultLogo.h` liegt im Repo (kein Klartext-Geheimnis, nur Bilddaten) und
wird aus der vorkonvertierten Datei in `sensormeter-family/logo/` generiert
- bei einem neuen Default-Logo einfach neu konvertieren und den
Array-Inhalt ersetzen, nicht von Hand editieren.

Nur per `pio run` gebaut (kein Board für Sensormeter PoE vorhanden) - beide
Zweige (Logo fehlt → automatisch schreiben; Logo vorhanden → nichts tun)
nur per Code-Review verifiziert, nicht auf echter Hardware getestet.

## `scripts/flash.sh`: Mac-/Linux-Unterstützung umgesetzt

Neues `scripts/flash.sh` (Bash-Pendant zu `flash.ps1` für macOS - nur
Apple Silicon/arm64 - und Linux, nur Flashen, kein `convert-logo`/
`snmp-load`-Äquivalent) identisch aus dem Sensormeter-Repo übernommen -
volle Begründung und Verifizierungsstand dort in `docs/entscheidungen.md`
("`scripts/flash.sh`: Mac-/Linux-Unterstützung umgesetzt"). Betrifft auch
hier die bereits bekannte PlatformIO-Paket-Pool-Isolation ("pioarduino"
vs. "espressif32") - `flash.sh` muss dafür nichts Zusätzliches tun, ebenso
wie `flash.ps1` schon nicht.

## Tuerkontakt auf RJ45 Pin 5 aus `sensormeter` nachgerüstet ("Modultyp" Sensor/Kontakt, Kategorie-1/2-Gliederung der Einstellungsseite)

Portiert das im Sensormeter-Projekt (WT32-ETH01) neu eingeführte
Kontakt-Feature 1:1 nach - siehe dortige `docs/entscheidungen.md`
("Türkontakt auf RJ45 Pin 5 (Modultyp-Auswahl Sensor/Kontakt)" und
"Einstellungsseite neu gegliedert...") für die volle Design-Begründung.
Hier nur die projektspezifischen Abweichungen:

- Gleiche Pin-Rolle (`PIN_RJ45_PIN5_RESERVE`), andere GPIO-Nummer (16 statt
  15 beim WT32-ETH01, siehe `pins.h`) - `ContactManager` selbst ist
  unverändert übernommen (verwendet nur das Makro, keine Literalzahl).
- `ConfigManager`: `<kontakt pin5Mode="sensor" name="Kontakt"
  alarmAt="open"/>` identisch zu Sensormeter, eingefügt zwischen
  `<sensors>` und `<snmp>` im Schema.
- `WebServerManager`: identische Gliederung ("Externe Schnittstelle" →
  Kategorie 1 (I2C, immer sichtbare Erkennungsanzeige) / Kategorie 2
  (Relais + Pin-5-Modultyp-Pulldown mit Sensor-/Kontakt-Feldern) inkl.
  `.subsection`-CSS. Anders als Sensormeter hat dieses Projekt zusätzlich
  einen BOOT-Taster (`ButtonManager`) - davon unberührt, da Pin 5 damit
  nichts zu tun hat.
- `SensorManager::readExternalSensorIfEnabled()` und
  `SensorDetector::runDetection()` erhalten dieselbe `pin5Mode`-Absicherung
  wie bei Sensormeter (DHT-Lesepfad/-Sondierung nur im Modus "sensor").
- Serial-CLI `status`-Kommando um Kontakt-Zeile ergänzt, analog Relais.

Flash-Kosten: 21,7 % (1.425.315 von 6.553.600 Byte, ESP32-S3 mit 8 MB statt
4 MB Flash - deutlich mehr Reserve als beim WT32-ETH01, daher hier keine
Notwendigkeit, den Flash-Zuwachs separat zu beziffern). RAM 19,3 %.

Nur per `pio run` gebaut (kein Board für Sensormeter PoE vorhanden) - wie
bei allen bisherigen Portierungen in dieses Projekt nur per Code-Review
verifiziert, nicht auf echter Hardware getestet.

## Relais: optionales automatisches Schalten nach Sensor-/Kontakt-Bedingung, aus `sensormeter` nachgerüstet

Portiert 1:1 das neue automatische Relais-Schalten - siehe dortige
`docs/entscheidungen.md` ("Relais: optionales automatisches Schalten nach
Sensor-/Kontakt-Bedingung") für die volle Design-Begründung. Hier nur die
projektspezifischen Punkte:

- `RelayManager` bekommt denselben neuen `ContactManager&`-Parameter,
  `main.cpp` entsprechend umgestellt (`contactManager` vor `relayManager`
  deklariert - hier zusätzlich zwischen `buttonManager` und
  `relayManager`, da dieses Projekt den BOOT-Taster hat).
- Neue `DeviceConfig`-Felder identisch (`relayAutoMode` Default `"off"`),
  gebündelt im bestehenden `<aktor>`-Element.
- `WebServerManager`: identisches "Automatisch schalten"-Auswahlfeld samt
  Folgefeldern in Kategorie 2, `/api/relay` liefert zusätzlich `auto`.

Flash-Kosten: 21,8 % (1.431.563 von 6.553.600 Byte, +6.248 Byte gegenüber
dem vorherigen Kontakt-Stand). RAM 19,3 %.

Nur per `pio run` gebaut (kein Board für Sensormeter PoE vorhanden) - nur
per Code-Review verifiziert, nicht auf echter Hardware getestet.

## Internes Display: SH1107 → SSD1306 (familienweite Standardisierung)

Kehrtwende der oben (Abschnitt „Erste Firmware-Fassung") als „einzige
echte Abweichung gegenüber den Geschwisterprojekten" dokumentierten
Entscheidung: auf ausdrücklichen Beschluss nutzen künftig **alle**
Sensormeter-Geräte außer Sensormeter Display intern dasselbe kleine
SSD1306 (0,96″, 128×64, I2C 0x3C) — genau wie Sensormeter (WT32-ETH01)
und Sensormeter WLAN. Das bisher hier verbaute größere SH1107 (1,5″,
128×128) gibt es seither nur noch als optionales **externes**
RJ45-Steckmodul (I2C 0x3D, siehe `sensormeter-family/repo/module-design/
sh1107-display-modul.md`) — für Sensormeter und Sensormeter PoE
gleichermaßen, da beide dieselbe RJ45-Modularbuchse haben.

**Warum die Kehrtwende statt einer Ausnahme beizubehalten**: ein
größeres/kleineres Display ist kein funktionaler Unterschied, der eine
eigene Geräteklasse rechtfertigt — es war lediglich die zuerst verbaute
Variante. Die Aufteilung „klein und einheitlich intern, groß optional
extern" deckt beide Anwendungsfälle (kompaktes Gerät vs. aus der Distanz
lesbare Anzeige) ab, ohne dass drei von vier Projekten mit
API-inkompatiblem Displaycode (`Adafruit_SH110X` statt `Adafruit_SSD1306`)
auseinanderlaufen.

**Umgesetzte Änderungen** (`DisplayManager.h`/`.cpp`, 1:1 von
`sensormeter/repo` übernommen bis auf die dort nicht vorhandenen
`TimeManager&`/`ButtonManager&`-Abhängigkeiten, die dieses Projekt wegen
des BOOT-Tasters weiterhin braucht):

- `SCREEN_HEIGHT` 128 → 64, `Adafruit_SH110X`/`Adafruit_SH1107` →
  `Adafruit_SSD1306`, `SH110X_WHITE` → `SSD1306_WHITE`,
  `display.begin(addr, true)` → `display.begin(SSD1306_SWITCHCAPVCC, addr)`
  (unterschiedliche Init-Signaturen der beiden Adafruit-Bibliotheken).
- `platformio.ini`: `Adafruit SSD1306` neu in `lib_deps` aufgenommen.
  `Adafruit SH110X` bewusst **nicht** entfernt — treibt jetzt das
  optionale externe Display-Steckmodul (siehe `SensorDetector`/
  `DisplayManager`-Erweiterung für Adresse 0x3D).
- `BrandingManager.h`: `LOGO_WIDTH/HEIGHT/BYTES` 128×128/2048 →
  128×64/1024 Byte, `DefaultLogo.h` aus der `-oled-128x64`-Variante des
  Familienlogos neu eingebettet (identisch zu `sensormeter/repo` seither,
  vorher aus der `-oled-128x128`-Variante).
- `WebServerManager.cpp`/`BrandingManager.cpp`: Logo-Upload-Hinweistexte
  und Fehlermeldungen von 128×128/2048 Byte auf 128×64/1024 Byte
  korrigiert.

**Kein Umbau der bestehenden I2C-Verdrahtung nötig**: `PIN_I2C_SDA`/
`PIN_I2C_SCL` und die Geräteadresse `0x3C` bleiben unverändert — nur der
Chip/die Auflösung auf der Platine ändert sich für künftige Bestückungen.

Flash-Kosten: 21,8 % (1.431.403 von 6.553.600 Byte). RAM 18,4 % (60.180
von 327.680 Byte) — beide praktisch unverändert gegenüber dem vorherigen
Stand (SH1107- gegen SSD1306-Bibliothek getauscht, `Adafruit SH110X`
bleibt zusätzlich im Baum). Nur per `pio run` gebaut (weiterhin kein
Board für Sensormeter PoE vorhanden) — nicht auf echter Hardware
getestet.

## Optionales externes Display-Steckmodul (SH1107, I2C 0x3D)

Direkte Folge des Display-Umbaus oben: das intern entfernte SH1107
128x128 gibt es jetzt als optionales externes RJ45-Steckmodul (siehe
sensormeter-family/repo/module-design/sh1107-display-modul.md),
identisch für Sensormeter und Sensormeter PoE umgesetzt (siehe dortiges
`docs/entscheidungen.md`).

**`SensorDetector.cpp`**: `EXTERNAL_DISPLAY_I2C_ADDRESS = 0x3D` zusätzlich
zu `DISPLAY_I2C_ADDRESS = 0x3C` vom I2C-Scan ausgenommen. Ohne diese
Ausnahme hätte ein gestecktes externes Display zwei Probleme verursacht:
als „unbekannter I2C-Sensor" fälschlich `sensor2Enabled` gesetzt, UND
(da `0x3D` vor den meisten bekannten Sensor-Adressen im Scan-Bereich
0x08-0x77 liegt) den Scan abgebrochen, bevor ein tatsächlich dahinter
gestecktes Sensor-Modul mit höherer Adresse gefunden würde (betrifft
insbesondere CCS811 auf 0x5A/0x5B).

**Neue Klasse `ExternalDisplayManager`**: eigenständig neben
`DisplayManager`, spricht `Adafruit_SH110X` auf `0x3D` an (dieselbe
Bibliothek, die bisher das jetzt entfernte interne SH1107 trieb - siehe
oben, deshalb bewusst nicht aus `lib_deps` entfernt). Zeigt dieselben
Infoseiten wie das interne Display mit eigener 10s-Rotation, bewusst OHNE
Boot-Countdown-Seite, Fallback-AP-Sonderseite und BOOT-Taster-Overlay -
diese bleiben Aufgabe des internen Displays, das externe Modul ist reine
Zusatzanzeige für den Normalbetrieb. Branding-Seite zeigt nur den
Vendor-Namen als Text, kein Logo-Bitmap (das gespeicherte Logo ist jetzt
128x64-formatiert, würde auf dem 128x128 großen externen Display verzerrt
dargestellt - eigenes Logoformat dafür noch nicht umgesetzt). Kein
gestecktes Modul -> `begin()` schlägt fehl, `loop()` ist ein No-op.

Flash-Kosten: 22,0 % (1.439.571 von 6.553.600 Byte, gegenüber 21,8 % nach
dem Display-Umbau oben). RAM 18,4 % (60.300 von 327.680 Byte). Nur per
`pio run` gebaut (weiterhin kein Board vorhanden) — nicht auf echter
Hardware getestet.

## I2C-Lesepfad für Sensor 2 (BME280, AHT20/AHT21)

Identisch zu sensormeter/repo umgesetzt (siehe dessen
`docs/entscheidungen.md` „I2C-Lesepfad für Sensor 2" für die volle
Begründung) - schließt einen Teil der in `sensormeter-family/repo/
module-design/README.md` als „Firmware-Lücke" dokumentierten Lücke.
`SensorManager::readExternalSensorIfEnabled()` liest ein erkanntes BME280
oder AHT20/AHT21 jetzt tatsächlich per I2C aus (`Adafruit_BME280`/
`Adafruit_AHTX0`), statt wie bisher immer einen DHT-22-Leseversuch auf
Pin 5 zu unternehmen. BH1750 (Lux) und CCS811 (eCO₂/TVOC) bleiben bewusst
ohne I2C-Lesepfad - beide Messgrößen passen nicht ins bestehende
Temperatur/Feuchte-Datenmodell von „Sensor 2".

**Nebenbei behobener Bug**: der bisherige `cfg.pin5Mode != "sensor"`-Gate
blockierte fälschlich auch I2C-Lesepfade, obwohl I2C (SCL/SDA) und Pin 5
unabhängige Pins sind - ein Kontakt-Modul auf Pin 5 UND ein I2C-Sensor auf
dem Bus können gleichzeitig gesteckt sein. Die I2C-Zweige prüfen
`pin5Mode` jetzt nicht mehr, nur der DHT-Fallback-Zweig weiterhin.

**Wichtiger Sonderfall beim BME280+CCS811-Kombimodul** (siehe
sensormeter-family/repo/module-design/bme280-ccs811-modul.md): der
I2C-Scan bricht beim ERSTEN Treffer ab, und CCS811 (`0x5A`/`0x5B`) liegt
niedriger als BME280 (`0x76`/`0x77`) - auf diesem Kombimodul wird
deshalb praktisch immer CCS811 zuerst erkannt, BME280 nie erreicht.
„Sensor 2" bleibt bei diesem Modul trotz des neuen BME280-Lesepfads
weiterhin ungültig. Betrifft NICHT das reine BME280-Einzelmodul.

`SensorDetector` bekommt zwei neue öffentliche Getter (`detectedChipName()`,
`detectedI2cAddress()`, vorher privat) - `SensorManager` haelt dafür jetzt
eine `SensorDetector&`-Referenz (`main.cpp`: `sensorDetector` vor
`sensorManager` deklariert).

`platformio.ini`: `Adafruit BME280 Library` und `Adafruit AHTX0` neu in
`lib_deps`.

Flash-Kosten: 22,0 % (1.444.843 von 6.553.600 Byte, gegenüber 22,0 % vor
dieser Änderung, +5.272 Byte). RAM 18,4 % (60.412 von 327.680 Byte). Nur
per `pio run` gebaut (weiterhin kein Board vorhanden) - nicht auf echter
Hardware getestet.

## RJ45 Pin 8: 5V statt Reserve

Identisch zu sensormeter/repo umgesetzt (siehe dessen `docs/
entscheidungen.md` „RJ45 Pin 8: 5V statt Reserve" für die volle
Begründung) - auf ausdrücklichen Beschluss trägt RJ45 Pin 8 künftig fest
die 5V-Versorgungsschiene des Geräts statt wie bisher als „Reserve" auf
einen GPIO herausgeführt zu sein. Kein einziges entworfenes Modul nutzt
Pin 8 aktuell - er wurde bislang nur 1:1 durchgeschleift.

**Hier unkritischer als bei Sensormeter (WT32-ETH01)**: Pin 8 war bei
diesem Projekt mit `GPIO19` verdrahtet - kein Boot-Strapping-Pin (siehe
`pins.h`-Kommentar: "frei, kein Boot-Strapping-Pin, anders als beim
WT32-ETH01"), keine Pull-down-Anforderung. Die Umstellung ist deshalb
nur eine Leiterbahn von `GPIO19` weg, hin zur 5V-Schiene, ohne
Sicherheits-Implikation für den Bootvorgang.

**Umgesetzte Änderungen**:
- `firmware/include/pins.h`: `PIN_RJ45_PIN8_RESERVE` (bisher `19`)
  entfernt - Pin 8 hat keinen GPIO mehr. Per Grep bestätigt, dass dieses
  Define nirgendwo im Code referenziert wurde.
- `docs/verdrahtungsplan.html`: Pin-8-Zeile/-Draht von `GPIO19`/„Reserve,
  unbenutzt" auf `5V`/„5V-Versorgung" umgestellt, neuer Warn-Hinweis zur
  3,3V-Verwechslungsgefahr.
- `docs/lastenheft.txt` Abschnitt 14 (RJ45-Pinbelegung): Pin-8-Zeile
  entsprechend angepasst.
- **Offen, nicht verifizierbar ohne echte Platine**: wie viel Strom die
  5V-Schiene an Pin 8 tatsächlich liefern kann, ist mangels vorhandenem
  Board nicht nachgemessen - vor dem Bestücken eines ersten 5V-Moduls
  prüfen.

Betrifft nur Dokumentation/`pins.h` - kein Modul nutzt Pin 8 aktuell,
daher keine funktionale Änderung am Verhalten bestehender Module. Siehe
`sensormeter-family/repo/module-design/README.md` für die familienweite
Pinbelegungstabelle.

## 2026-07-15 — Periodischer I2C-Rescan (1x/Minute), ohne Taktänderung

Analoge Änderung zu `sensormeter/repo/docs/entscheidungen.md` vom
gleichen Datum - dort steht die vollständige Begründung (50 kHz
zurückgestellt, "vollständiger Scan beim Start" existierte bereits,
"asynchron" als eigener `millis()`-Timer statt echter Parallelität, da
kein RTOS-Task-Scheduler vorhanden ist).

**Umgesetzte Änderungen** (`SensorDetector.h`/`.cpp`, `main.cpp`,
identisch zu sensormeter):
- I2C-Sweep aus `runDetection()` in `scanI2cBus()` extrahiert, von
  `runDetection()` und der neuen `SensorDetector::loop()` gemeinsam
  genutzt.
- `SensorDetector::loop()` neu: alle 60s ein reiner I2C-Rescan, bewusst
  ohne erneuten DHT-Leseversuch (teilt sich den GPIO mit
  `SensorManager`s `dhtExternal`).
- Ergebnisloser periodischer Scan setzt den zuletzt bekannten
  Erkennungsstatus nicht zurück.
- `main.cpp`: `sensorDetector.loop();` im Hauptloop ergänzt.

Getestet: `pio run` - baut sauber (ESP32-S3-ETH, Espressif32-Framework),
keine neuen Warnungen. Nicht getestet: echte Hardware.

### Nachtrag (gleicher Tag) — "vollständig" hieß auch: alle Treffer auswerten, nicht nur den ersten

Analoge Korrektur zu `sensormeter/repo/docs/entscheidungen.md` vom
gleichen Datum - dort steht die vollständige Begründung. Kurzfassung:
"vollständiger Scan" bezog sich nicht nur auf den Adressbereich (der war
schon vorher komplett), sondern sollte auch bedeuten, dass jede gefundene
Adresse ausgewertet wird, nicht nur die erste. `scanI2cBus()` bricht jetzt
nicht mehr beim ersten Treffer ab, sammelt alle Treffer in einem neuen
`I2cHit[MAX_LOGGED_I2C_HITS=8]`-Array (`detectedI2cDeviceCount()`/
`detectedI2cDeviceAt()`), spiegelt aber weiterhin nur das primäre Gerät
(niedrigste Adresse) in die bestehenden Felder, die `SensorManager`
liest - Mehrfach-Nutzung bleibt bewusst offen. `detectedDescription()`
hängt bei mehr als einem Treffer einen Hinweis an.

Getestet: `pio run` - baut sauber. Nicht getestet: echte Hardware.

## 2026-07-15 — Sensor-2-Datenmodell erweitert: Druck/Lux/Luftgüte + values.csv-Größe

Analoge Änderung zu `sensormeter/repo/docs/entscheidungen.md` vom gleichen
Datum - dort steht die vollständige Begründung inkl. Größenrechnung. Start
der Firmware-Einpflege für die in `module-design/README.md` "Firmware-
Lücke" gelisteten Punkte (BMP280-Chip-ID-Check, BH1750-Lux, ENS160-
Luftgüte, DHT11/DHT21-Typauswahl) - ausdrücklich NICHT die spätere "Modul-
Integration" (mehrere gleichzeitig gesteckte Module gemeinsam lesen).

**Sensormeter-PoE-Besonderheit**: anders als bei Sensormeter (dort intern
DHT11) ist hier sowohl der interne als auch der externe Sensor werkseitig
ein DHT-22-Pfad (siehe Klassenkommentar `SensorManager.h`) - die neue
`pin5DhtType`-Typauswahl (DHT11/DHT21) betrifft ausschließlich den
**externen** RJ45-Anschluss (Sensor 2), der interne, fest verbaute DHT-22
(Sensor 1) bleibt unverändert `plausibleDht22()`.

**Größenrechnung**: `RINGBUFFER_SIZE` (168) bleibt unverändert. Bei der
16-MB-Flash-Partition (`default_16MB.csv`) ist die values.csv-Größe (auch
mit den neuen 8 statt 3 Spalten, ~10-12 KB worst case) ohnehin nie
relevant eng gewesen - keine weitere Rechnung nötig.

### Umgesetzte Änderungen (identisch zu sensormeter, siehe dortiger Eintrag)

- `ConfigManager`: neues Feld `pin5DhtType` ("DHT11"|"DHT21", Default
  "DHT21"), persistiert im `<kontakt>`-Element.
- `SensorManager`: zwei DHT-Objekte (`dhtExternalDht11`/`dhtExternalDht21`)
  für den externen Pin statt einem fest auf DHT22 codierten; drei neue
  Lesepfade `readExternalBmp280()`/`readExternalBh1750()`/
  `readExternalEns160()`. `loop()` umgebaut: `maybeRecordHourValue()`
  läuft jetzt NACH beiden Sensor-Lesungen (vorher hing die stündliche
  Ringpuffer-Speicherung an `readInternalSensor()` und lief vor Sensor 2).
- `SensorDetector`: 0x76/0x77 unterscheidet jetzt per Chip-ID-Register
  (0xD0) zwischen BME280 (0x60) und BMP280 (0x58) statt pauschal "BME280"
  zu meldern. `KNOWN_CHIPS` um ENS160 (0x52/0x53) ergänzt.
- `DataManager`: `HourValue`/`SensorReading` auf 8 Felder erweitert,
  `saveRingbuffer()`/`loadRingbuffer()` auf 8-Spalten-CSV umgestellt. Alte
  3-Spalten-Zeilen werden beim Laden übersprungen (einmaliger
  Historie-Verlust beim ersten Boot nach dem Update, kein Korruptions-
  Risiko).
- `WebServerManager`: `values.csv`, `/api/sensors`, `/api/graph`,
  Dashboard-Zeile auf die neuen Felder erweitert (Chart.js-Darstellung der
  neuen Sensor-2-Werte bewusst nicht Teil dieser Änderung, nur die Daten
  sind bereits abrufbar).
- **Bugfix**: `SensorReading.valid` bedeutet seit den drei neuen
  Modultypen nicht mehr zwingend "liefert Temperatur/Feuchte" - `SNMPManager`,
  `MqttManager` und `RelayManager` (Auto-Schalten mit Quelle "sensor2")
  prüften nur `valid`, jetzt zusätzlich `!isnan(temperature)` abgesichert
  (sonst NAN-Export per SNMP/MQTT bzw. `RelayManager` hätte das Relais
  unbemerkt bei jedem Zyklus ausgeschaltet).
- Neue Bibliotheksabhängigkeiten: `adafruit/Adafruit BMP280
  Library@^3.0.0`, `claws/BH1750@^1.3.0`, `adafruit/ENS160 - Adafruit
  Fork@^3.0.1`.

### Bewusst nicht Teil dieser Änderung

SNMP/MQTT-Export der drei neuen Messgrößen, Kalibrier-Offset für Druck,
Dashboard-Chart-Darstellung der neuen Werte, echtes gleichzeitiges Lesen
mehrerer gesteckter Module, Verifikation der ENS160-Warmlaufzeit bei
wiederholtem `begin()` - siehe sensormeter-Eintrag für die Begründung
je Punkt.

Getestet: `pio run` (PowerShell, siehe Hinweis oben zu MSYS) - baut sauber
(Flash 22,3%/RAM 19,8%, vorher 22,1%/18,5%), drei neue Bibliotheken
erfolgreich aufgelöst. Nicht getestet: echte Hardware.

## 2026-07-16 — MQTT fest an ein Interface binden

Wie bei Sensormeter WLAN hat auch dieses Board zwei mögliche aktive
Interfaces (LAN + WLAN). Bisher war offen, welches Interface lwIP für die
MQTT-Verbindung wählt, wenn beide gleichzeitig eine IP haben - jetzt lässt
sich das über die Einstellungsseite fest vorgeben.

- Neues Feld `ConfigManager::mqttInterface` (`"lan"` | `"wlan"`, Default
  `"lan"`), per `interface`-Attribut im `<mqtt .../>`-Element persistiert.
  Bewusst **kein** `"auto"`/dritte Option - ist das gewählte Interface
  gerade nicht verbunden, schlägt der MQTT-Connect regulär fehl, auch wenn
  das jeweils andere Interface erreichbar wäre (bewusst kein stilles
  Failover).
- Einstellungsseite: neues Pulldown "Interface" (LAN/WLAN) im MQTT-Block,
  REST-API (`/api/config` GET/POST) um `mqttInterface` erweitert.
- `MqttManager::ensureConnected()` setzt vor jedem `connect()`-Versuch per
  lwIP `netif_set_default()` (`lwip/netif.h`) explizit das gewählte
  Interface als Default-Netif und stellt danach den vorherigen Zustand
  wieder her.
- **Bewusst `netif_set_default()` (lwIP) statt `esp_netif_set_default_netif()`
  (esp_netif)**, obwohl letztere auf diesem Core (3.x) verfügbar wäre: bei
  Sensormeter (Arduino-ESP32 2.0.17) existiert diese Funktion nicht (siehe
  dortiger Eintrag vom selben Tag), die lwIP-Funktion darunter dagegen auf
  beiden Core-Versionen - damit ist der Code hier identisch zu Sensormeter,
  statt zweier divergierender Implementierungen für dasselbe Problem.
  Umsetzung: `esp_netif_get_netif_impl_index()` liefert den lwIP-
  "Netif-Index" (`netif->num + 1`), passend zu lwIPs `netif_get_by_index()`
  - so lässt sich vom esp_netif-Handle (ifkey `"ETH_DEF"`/`"WIFI_STA_DEF"`)
  auf das darunterliegende `struct netif*` schließen.

Getestet: `pio run` (PowerShell) - baut sauber (Flash 22,3%/RAM 19,8%,
unverändert gegenüber vorherigem Stand). Nicht getestet: echte Hardware mit
gleichzeitig aktivem LAN und WLAN.

## 2026-07-16 — OTA-Upload: Projekt-/Versionspruefung gegen Verwechslungen

Direkter Anlass: die Frage, wie sichergestellt wird, dass niemand
versehentlich die Sensormeter-Display-Firmware auf ein Sensormeter-PoE-
Geraet hochlaedt (oder umgekehrt) - `/api/ota/upload` pruefte bisher nur
Basic-Auth, nicht den Inhalt der `.bin`. Identischer Mechanismus in allen
vier Projekten der Familie umgesetzt, siehe Sensormeter-Eintrag vom
selben Tag fuer die vollstaendige Begruendung:

- Neues Feld `FIRMWARE_PROJECT_ID` (`"SENSORMETER-POE"`) neben
  `DEVICE_FIRMWARE_VERSION` in `include/config.h(.example)`. Beide
  zusammen ergeben den in `main.cpp` einkompilierten Marker
  `"SM-FW-ID:SENSORMETER-POE:0.9.0-rc4:SM-FW-END"`.
- `OtaManager` sucht diesen Marker chunk-uebergreifend im Byte-Stream
  eines Uploads, vergleicht Projekt-ID (exakt) und Version (Semver,
  `a.b.c[-rcN]`-Schema) gegen die eigenen Werte - `endLocalUpdate()`
  committet nur bei Uebereinstimmung, sonst `Update.abort()`.
- Neue Checkbox "Downgrade erzwingen" im Firmware-Formular (bewusst VOR
  dem Datei-Feld wegen ESPAsyncWebServers Multipart-Parse-Reihenfolge),
  erlaubt einen bewussten Ruecksprung auf eine aeltere Version.
- Vier unterscheidbare Fehlermeldungen statt einem generischen "Update
  fehlgeschlagen" (Schreibfehler / kein Marker / falsches Projekt / zu
  alte Version).
- Kein kryptografischer Schutz, nur Verwechslungs-Pruefung - siehe
  Sensormeter-Eintrag.

Getestet: `pio run` (PowerShell) - baut sauber (Flash 22,4%/RAM 19,8%,
kaum veraendert gegenueber vorherigem Stand). Marker per Byte-Suche in
`firmware.bin` verifiziert (`SM-FW-ID:SENSORMETER-POE:0.9.0-rc4:
SM-FW-END`). Nicht getestet: echter OTA-Upload auf echter Hardware.

**Standing-Vorgabe**: dieser Mechanismus ist ab jetzt fester Bestandteil
dieses Projekts und laeuft bei kuenftigen Firmware-Versionen automatisch
mit (siehe Sensormeter-Eintrag).

## 2026-07-16 — Persistenter Log-Puffer auf LittleFS (/log.txt) + WARNING-Stufe

Identischer Mechanismus wie im Sensormeter-Projekt (siehe dortiger
Eintrag vom selben Tag fuer die vollstaendige Begruendung inkl.
Groessen-/Rotationsrechnung und der Pro-Interface-WARNING-Logik fuer
LAN/WLAN, urspruenglich aus Sensormeter WLAN portiert):

- Neue benannte Severity-Stufen `DataManager::SEVERITY_ERROR/_WARNING/_INFO`.
- `pushLogEntry()` haengt jeden Eintrag zusaetzlich an `/log.txt` an
  (Rotation nach `/log.old.txt` bei 32 KB).
- `NetManager::logInterfaceTransitions()` trackt LAN und WLAN einzeln
  (nicht nur den kombinierten `networkOk()`-Zustand) und loggt Verlust als
  `WARNING`, Wiederverbindung als `INFO` mit Ausfalldauer.
- Web-UI: `/log.txt`/`/log.old.txt` aus LittleFS gestreamt, neue Buttons
  "Log"/"Log (alt)" auf der Hauptseite.

Getestet: `pio run` (PowerShell) - baut sauber (Flash 22,5%/RAM 19,8%,
kaum veraendert). Nicht getestet: echte Hardware.

**Standing-Vorgabe**: analog zur OTA-Pruefung oben ist dieser Mechanismus
ab jetzt fester Bestandteil dieses Projekts.

## 2026-07-16 — OTA-Marker-Scan byte-sicher gemacht (Bug uebernommen aus Sensormeter)

Beim Debuggen eines Zeitzonen-Fixes im Schwesterprojekt `sensormeter`
wurde entdeckt, dass der dortige OTA-Marker-Scan (`OtaManager::
scanChunkForMarker()`) eine echte, gueltige `.bin`-Datei nie akzeptieren
konnte: die Suche nutzte Arduino `String`/`indexOf()` (intern
`strstr()`-basiert) auf den rohen Binaerdaten des Uploads - `strstr()`
bricht am ersten eingebetteten Null-Byte ab, ein kompiliertes
ESP32-Image hat sein erstes Null-Byte aber schon bei Byte 9
(Image-Header-Padding), lange vor dem Marker selbst. Dieses Projekt hat
denselben OTA-Marker-Mechanismus mit identischem Code-Muster (siehe
`sensormeter`-Eintrag "OTA-Marker-Scan fand echte .bin nie" fuer die
volle Herleitung) - Bug bestaetigt, gleiche Ursache.

Fix uebernommen: `OtaManager::scanChunkForMarker()` auf rohe
`uint8_t`-Puffer und eine Null-Byte-sichere `findBytes()`-Suche
(memcmp-basiert statt strstr-basiert) umgestellt, `String _tail`/
`_capture` durch feste Byte-Puffer ersetzt. `pio run` erfolgreich (Flash
22,5%/RAM 19,8%, praktisch unveraendert). Nicht getestet: echter
OTA-Upload auf echter Hardware - kein Board in dieser Sitzung
angeschlossen.

## 2026-07-18 — Task-Watchdog (TWDT): Panic-on-Hang aktiviert, portiert von ESP-BMC

Bislang lief der ESP-IDF-eigene Task-Watchdog-Timer (TWDT) nur mit
Default-Konfiguration mit: Timeout ab Werk, beide Idle-Tasks angemeldet,
aber `panic=false` - ein haengender Task erzeugt damit nur eine Logzeile,
kein Reboot. Nach dem Vorbild von ESP-BMCs `watchdog_manager` jetzt auch
hier scharf geschaltet, aber bewusst NUR der reine
Watchdog-Mechanismus - keine RGB-LED (kein bestaetigtes adressierbares
LED auf diesem Board, und ohnehin nicht Teil dieses Backlog-Punkts).

**Framework-Unterschied zu sm/sm-wlan beachtet**: dieses Projekt laeuft
auf Arduino-ESP32 3.x (pioarduino-Fork, siehe Kommentar oben in
`platformio.ini`, IDF5.x-Basis) - dort ist `esp_task_wdt_init()` bereits
auf die neue struct-basierte Signatur (`esp_task_wdt_config_t` mit
`timeout_ms`/`idle_core_mask`/`trigger_panic`) umgestellt, anders als die
alte zweiargumentige API bei sm/sm-wlan (Arduino-ESP32 2.0.17). Da der
TWDT je nach Core-Version bereits implizit initialisiert sein kann, prueft
der Code auf `ESP_ERR_INVALID_STATE` und ruft in dem Fall stattdessen
`esp_task_wdt_reconfigure()` mit derselben Konfiguration auf.
`idle_core_mask = 0x3` deckt weiterhin beide Idle-Tasks ab (Verhalten wie
bei impliziter Default-Initialisierung), zusaetzlich zum Haupt-Loop.

**Timeout bewusst auf 10s statt ESP-BMCs 5s gesetzt**: `loop()` durchlaeuft
hier synchron sehr viele Manager (u.a. `MqttManager::loop()` mit einem
alle 5s versuchten `_client.connect()`, dessen TCP-Verbindungsaufbau bei
einem nicht erreichbaren Broker mehrere Sekunden blockieren kann) - 5s
waere zu knapp gewesen und haette ohne echten Hang faelschlich einen
Panic-Reboot ausgeloest.

**Anmeldezeitpunkt**: erst am Ende von `setup()`, nicht ganz am Anfang -
die vorangehenden `*.begin()`-Aufrufe (v.a. ein etwaiger
LittleFS-Erststart-Format) sind einmalig und duerfen laenger dauern, ohne
als Hang gewertet zu werden. Ab `loop()` sind alle Zyklen kurz und
beschraenkt (bestehende `delay(50)`-Taktung), `esp_task_wdt_reset()` wird
dort einmal pro Durchlauf aufgerufen.

Verifiziert per `pio run` unter PowerShell (siehe Hinweis oben zu
MSYS/Git-Bash), sauberer Build, RAM/Flash-Nutzung unveraendert gegenueber
vorher, noch nicht auf echter Hardware geflasht/getestet.

## 2026-07-18 — OTA-Marker-Scan: identischer Chunkgroessen-Bug wie sensormeter, vorsorglich gefixt

Bei sensormeter deckte der erste echte End-to-End-OTA-Test (HTTP-Upload
ueber LAN, nicht nur `pio run`) auf, dass der urspruengliche NUL-Byte-Fix
zwar das Kernproblem loeste, dabei aber einen neuen, subtileren Bug
einbaute: `scanChunkForMarker()` kopierte jeden Chunk in einen auf
`kTailCap+512` = 528 Byte GEDECKELTEN Zwischenpuffer und durchsuchte nur
diesen - ein echter HTTP-Upload liefert aber regelmaessig groessere
Chunks, wodurch alles jenseits der Deckelung STILLSCHWEIGEND
uebersprungen wurde. Vollstaendige Herleitung samt Live-Nachweis
(Geraetelog zeigt Ablehnung vor dem Fix, Erfolg danach) im
`sensormeter`-Repo.

Dieses Projekt hat exakt dieselbe `kJoinCap = kTailCap + 512`-Struktur
(identischer Code, wie schon beim urspruenglichen NUL-Byte-Bug portiert)
- Bug per Quellcode-Vergleich bestaetigt, nicht nur vermutet. Fix
identisch uebernommen: kein kopierter Zwischenpuffer mehr fuer den Chunk
selbst - `findBytes()` durchsucht `data`/`len` jetzt direkt, ein kleiner
Join-Puffer (`kTailCap+kMarkerPrefixLen` = 25 Byte) wird nur noch fuer
den echten Tail-Grenzfall gebraucht. `pio run` (PowerShell) erfolgreich
(Flash 22,5%/RAM 19,8%, praktisch unveraendert). Noch nicht auf echter
Hardware geflasht/per echtem OTA-Upload getestet - kein Board in dieser
Sitzung angeschlossen.

## 2026-07-18 — Erster echter Board-Bringup: vier reale Bugs gefunden und behoben

Erstes physisches sensormeter-poe-Board (Waveshare ESP32-S3-ETH+PoE,
nur interner DHT bestueckt, noch kein internes/externes OLED gesteckt)
diese Sitzung erstmals angeschlossen und geflasht (COM8, natives USB,
automatischer Upload funktioniert ohne manuellen BOOT/EN-Handgriff -
anders als bei sensormeter/WT32-ETH01). Vier voneinander unabhaengige,
echte Bugs aufgedeckt, jeder erst durch den vorherigen Fix sichtbar
geworden:

**1. PSRAM-Init schlaegt fehl** (`quad_psram: PSRAM chip is not
connected, or wrong PSRAM line mode`, Guru-Meditation-Crash noch vor
jedem eigenen Code): weder `opi`- noch `qio`-Modus (`board_build.
psram_type`) funktionierten auf diesem konkreten Board. `esptool`s
Chip-Erkennung meldet zwar "Embedded PSRAM 8MB" (Efuse-Feature-Flag),
die tatsaechliche Initialisierung schlaegt aber in beiden Bus-Modi fehl.
PSRAM komplett deaktiviert (`board_build.psram_type` entfernt,
`-D BOARD_HAS_PSRAM` raus) - Anwendungscode nutzt PSRAM ohnehin nirgends
direkt (`ps_malloc`/`MALLOC_CAP_SPIRAM` kommt im gesamten `src/` nicht
vor). Die seit laengerem offene GPIO33/34-PSRAM-Frage bleibt trotzdem
offen (echte Ursache - defekte Bestueckung, falsche Verdrahtung oder
grundsaetzlich kein PSRAM auf dieser Boardvariante - nicht ermittelt,
nur der Software-Workaround).

**2. SNMPManager-Konstruktor crasht** (`Guru Meditation Error:
StoreProhibited`, `std::list::_M_hook`, noch vor `app_main()`/`setup()`,
per `addr2line` dekodiert): `SNMPAgent` (SNMP_Agent-Bibliothek,
`SNMP_Agent.h`) registriert sich im eigenen Konstruktor in einer
STATISCHEN Klassenmitglied-Liste `SNMPAgent::agents`
(`SNMPAgent::agents.push_back(this)`), die in einer anderen
Uebersetzungseinheit (`SNMP_Agent.cpp`) definiert ist. Die Reihenfolge
globaler C++-Konstruktoren ueber Dateigrenzen hinweg ist im Standard
NICHT garantiert ("static initialization order fiasco") - auf diesem
Board (Arduino-ESP32 3.x/pioarduino, anderer Compiler/Linker als
sensormeter/sensormeter-wlan) lief `SNMPManager`s globaler Konstruktor
in `main.cpp` vor dem der Bibliotheks-Liste, Schreibzugriff auf einen
noch nicht konstruierten `std::list` fuehrte zum Absturz. Bei sm/sm-wlan
lief es bisher nur zufaellig in der richtigen Reihenfolge (aelterer
Toolchain, andere Konstruktor-Reihenfolge). Fix: `snmpManager` von einem
globalen Objekt auf einen Pointer umgestellt, der erst am Ende von
`setup()` per `new` angelegt wird - zu diesem Zeitpunkt sind garantiert
alle globalen Konstruktoren (auch die der Bibliothek) bereits
durchgelaufen, unabhaengig von der Link-Reihenfolge. Nur `main.cpp`
betroffen (`snmpManager.begin()`/`.loop()` -> `snmpManager->begin()`/
`->loop()`), keine andere Datei referenziert das Objekt.

**3. loopTask-Stack-Overflow** (`Stack canary watchpoint triggered
(loopTask)`, Core 1, erst nach Fix 2 sichtbar geworden - vorher crashte
das Geraet schon vorher): identischer Befund und Fix wie bei sensormeter
(WT32-ETH01) - dort zuerst gefunden, hier beim ersten echten Boot dieses
Projekts unabhaengig reproduziert. `SET_LOOP_TASK_STACK_SIZE(16384)`
uebernommen (Standard-Arduino-ESP32-Stack von 8192 Byte reicht bei der
Zahl gleichzeitig in `loop()` laufender Manager nicht mehr).

**4. SSD1306-Anzeige "initialisiert" ohne Geraet** (kein Crash, aber
endlose `i2c_master_transmit failed`-Spam-Kaskade jede `loop()`-Runde,
erst nach Fix 3 als eigenstaendiges Problem sichtbar geworden):
`Adafruit_SSD1306::begin()` prueft nach Bibliotheks-Quellcode NICHT, ob
am I2C-Bus ueberhaupt ein Geraet antwortet - es gibt nur bei einem
`malloc()`-Fehler `false` zurueck, sonst immer `true` ("Success"),
unabhaengig vom tatsaechlichen I2C-Erfolg (bekannte Einschraenkung
dieser Bibliothek). Ohne gestecktes internes Display wurde `_initialized`
faelschlich `true`, `DisplayManager::loop()` versuchte danach jede
50ms-Runde erneut, gegen ein nicht vorhandenes Display zu zeichnen -
alle Schreibversuche schlugen fehl, aber ohne Ende. `ExternalDisplayManager`
(SH1107) ist NICHT betroffen - `Adafruit_SH1107::begin()` prueft den
Rueckgabewert von `oled_commandList()` korrekt und gibt bei Fehler
`false` zurueck. Fix: expliziter `Wire.beginTransmission(addr);
Wire.endTransmission() == 0`-Probe vor `display.begin()` in
`DisplayManager::begin()` - nur bei echtem ACK wird die Bibliothek
ueberhaupt aufgerufen.

**Ergebnis:** alle vier Fixes zusammen geflasht, per `curl` gegen
`/api/status` verifiziert (nicht per seriellem Log - natives USB-CDC
verliert offenbar Log-Zeilen kurz nach einem RTS-ausgeloesten Reset,
bevor der Host die Neuanmeldung des USB-Geraets abgeschlossen hat,
eigene Tooling-Einschraenkung, kein Firmware-Problem):
`{"systemName":"Sensormeter PoE","firmwareVersion":"0.9.0-rc4",
"uptimeSeconds":24,"freeHeap":218808,"timeSynced":true}` - Geraet laeuft
sauber, ueber Ethernet erreichbar (`192.168.178.108`, per PoE-Modul).

## 2026-07-18, spaeter am selben Tag — Korrektur: PSRAM-Fehler war ein veralteter Bootloader-Cache, keine Hardware-/Konfigurationsursache

Punkt 1 oben ("PSRAM-Init schlaegt fehl") war voreilig als Hardware-
Problem eingestuft und PSRAM komplett deaktiviert worden. Nutzer wies
auf die Waveshare-Produktseite hin: der verbaute Chip ist ein echtes
ESP32-S3R8 mit 8MB Octal-PSRAM im Chip-Package (vom Hersteller
bestaetigt), nicht ein PSRAM-loses N8. Recherche in mehreren
Community-Faellen mit identischem Fehlerbild
(`forum.arduino.cc/t/psram-not-detected-on-esp32-s3-n16r8...`) bestaetigt:
fuer echte R8-Boards ist Octal (`opi`) der korrekte Modus, nicht Quad -
`opi` war auch bereits unsere urspruengliche, korrekte erste Wahl.
Pruefung der pioarduino-Build-Skripte (`builder/frameworks/espidf.py`)
zeigt, dass `board_build.psram_type = opi` bereits korrekt zu
`CONFIG_SPIRAM_MODE_OCT=y` uebersetzt wird - die Konfiguration war also
nie falsch.

**Tatsaechliche Ursache:** ein veralteter `bootloader.bin` aus einem
fruehen Build-Versuch, der mitten im Flash-Vorgang durch einen
PowerShell/Python-Encoding-Absturz (Unicode-Fortschrittsbalken-Zeichen
von `esptool` gegen die `cp1252`-Konsolenkodierung, unabhaengig von
PSRAM) abgebrochen wurde - PlatformIOs inkrementeller Build hat das
danach nicht sauber invalidiert. Nach einem kompletten Leeren von
`.pio/build/esp32-s3-eth` und erneutem Bauen mit identischer
`psram_type = opi`-Einstellung lief die PSRAM-Initialisierung sauber
durch - kein Crash-Loop mehr, per `/api/status` bestaetigt (`uptimeSeconds`
steigt ueber mehrere Abfragen sauber, kein Reset).

`board_build.psram_type = opi` wieder aktiviert, `-D BOARD_HAS_PSRAM`
wieder ergaenzt. **Lehre:** nach einem durch einen Absturz/Interrupt
unterbrochenen `pio run --target upload`-Versuch immer erst
`.pio/build/<env>` loeschen, bevor aus einem fehlgeschlagenen Ergebnis
auf eine Hardware-/Konfigurationsursache geschlossen wird - passt zum
bereits bekannten Component-Manager-Cache-Muster bei ESP-BMC (dort:
`REQUIRES`-Aenderungen brauchen ebenfalls einen manuellen Cache-Wipe).

## 2026-07-18, spaeter am selben Tag — Zeitzonen-Bug identisch zu sensormeter, aber nie hierher portiert

Nutzer bemerkte (remote, ohne physischen Zugriff auf das Geraet):
`values.csv`-Zeitstempel wirkten falsch bzw. es schien kein aktueller
Wert geloggt zu werden. Diagnose komplett per HTTP-API (kein serieller
Zugriff noetig, Geraet war nicht vor Ort erreichbar):

- `/api/status`s `time`-Feld (roher Unix-Epoch) war durchgehend korrekt.
- `values.csv` zeigte nach einem per `/api/reboot` ausgeloesten
  Software-Reset einen Zeitstempel ca. 2 Stunden VOR der echten Zeit
  (z.B. `15:19:17` statt real `17:19`) - klassisches UTC/CEST-
  Differenzmuster.

Root Cause identisch zum bereits bekannten sensormeter-Bug (siehe
dortiges `docs/entscheidungen.md`), aber nie nach sensormeter-poe
uebertragen: `TimeManager::loop()` prueft `if (!_synced &&
isTimeSynced())` - die ESP32-Hardware-RTC ueberlebt einen Software-Reset
(`ESP.restart()`), daher liefert `isTimeSynced()` (reine Epoch-
Plausibilitaetspruefung) nach jedem Neustart ausser dem allerersten
sofort `true`. Der "bereits synchronisiert"-Schnellpfad ruft
`startSyncAttempt()`/`configTzTime()` in diesem Bootzyklus dann gar
nicht mehr auf - die POSIX-TZ-Umgebungsvariable (reiner Prozessspeicher,
anders als die Hardware-RTC bei jedem Neustart geloescht) faellt auf UTC
zurueck, obwohl der Epoch-Wert selbst korrekt bleibt.

Fix identisch zu sensormeter uebernommen: `TimeManager::begin()` ruft
jetzt unbedingt `setenv("TZ", TZ_GERMANY, 1); tzset();` auf, unabhaengig
vom Sync-Status - TZ wird ab sofort bei jedem Boot gesetzt, bevor
ueberhaupt geprueft wird, ob schon eine gueltige Zeit vorliegt.

**Live verifiziert, komplett remote per HTTP** (kein physischer Zugriff
verfuegbar): Fix gebaut und ueber die bereits bestehende
COM8-USB-Verbindung geflasht, danach per wiederholten `/api/status`-
Abfragen bestaetigt, dass das Geraet wieder online kam. `values.csv`
zeigt danach sowohl neue als auch (rueckwirkend neu formatierte) alte
Zeilen korrekt: `17:24:31` bei tatsaechlicher Zeit ~17:25 - kein
UTC-Versatz mehr.

**Separat beobachtet, bewusst NICHT gefixt**: die erste
`maybeRecordHourValue()`-Zeile nach einem Neustart kann leere
Sensorwerte haben, falls der allererste DHT-Leseversuch (der im selben
`loop()`-Aufruf laeuft) noch fehlschlaegt - der Stundenslot ist dann bis
zur naechsten Stunde verbraucht. Identischer Code/identisches Verhalten
in sensormeter, dort nie als Bug behandelt - familienweite Eigenschaft,
nicht Ursache der urspruenglich gemeldeten Beobachtung.

## 2026-07-18, spaeter am selben Tag — CSS-Layoutfehler: Werksreset-Dropdown bricht aus der Karte aus

Identischer Fund/Fix wie bei sensormeter (siehe dortiges
`docs/entscheidungen.md`): `<select id="resetScope">` hatte keine
CSS-Breitenbegrenzung, rendert deshalb so breit wie seine laengste
`<option>` und lief ueber den Rand der "KONFIGURATION"-Karte hinaus.
Neue Regel `select{...max-width:100%;...}` ergaenzt - kurze Selects
(z.B. "Automatisch schalten") bleiben kompakt, nur lange Options-Texte
werden gedeckelt. Live per OTA verifiziert (Vorher/Nachher-Screenshot,
headless Chrome).

## 2026-07-18, spaeter am selben Tag — PSRAM-Korrektur: intermittierend, nicht build-cache-bedingt

Beim CSS-Fix-Deploy (per OTA, inkrementeller Build - nur
`WebServerManager.cpp` geaendert) trat der PSRAM-Fehler ("PSRAM chip is
not connected, or wrong PSRAM line mode") ERNEUT auf, obwohl der vorige
Eintrag ("Korrektur: PSRAM-Fehler war veralteter Bootloader-Cache") ihn
bereits als geloest eingestuft hatte. Das widerlegt die
"veralteter-Bootloader"-Erklaerung - ein inkrementeller Build aendert
`bootloader.bin` gar nicht erst. Diesmal aber **kein Absturz**: das
Geraet bootete trotz der PSRAM-Warnung normal durch (`uptimeSeconds`
stieg sauber, per HTTP erreichbar, Boot-Log per USB mitgeschnitten).

**Praezisierte Einordnung**: PSRAM-Init ist auf diesem Testgeraet
**intermittierend unzuverlaessig** (mal erfolgreich, mal nicht, exakt
gleiche `opi`-Konfiguration) - aber folgenlos, weil kein Anwendungscode
PSRAM tatsaechlich nutzt (`ps_malloc`/`MALLOC_CAP_SPIRAM` kommt in
`src/` nicht vor). Der urspruengliche Absturz bei PSRAM-Fehler (siehe
Bringup-Eintrag oben) lag nicht an PSRAM selbst, sondern am damals noch
ungefixten SNMP-Konstruktor-Absturz, der zufaellig zur gleichen Zeit
auftrat - seit dessen Fix ist ein PSRAM-Fehlschlag nur noch eine
Logzeile, kein Blocker mehr. `board_build.psram_type = opi` bleibt
gesetzt (schadet nicht, hilft wenn PSRAM diesmal doch initialisiert) -
kein weiterer Handlungsbedarf, ausser ein zukuenftiges Feature braucht
PSRAM wirklich zuverlaessig.

## 2026-07-21 — Bekanntes Problem (noch nicht gefixt): ExternalDisplayManager ohne I2C-Vorab-Probe

Beim Live-Check am angeschlossenen Geraet ("ERSTER-PoE") im seriellen
Boot-Log zwei `i2c_master_transmit failed: ESP_ERR_INVALID_STATE`-Zeilen
auf HAL-Ebene gesehen, bevor die App selbst "[EXT-DISPLAY] Kein externes
SH1107 gefunden" meldet. Root Cause identifiziert: anders als
`DisplayManager::begin()` (internes SSD1306, 0x3C), das seit dem
frueheren Fix (siehe Eintrag zum "endlosen i2c_master_transmit
failed-Spam") erst per `Wire.beginTransmission()`/`endTransmission()`
prueft, ob am Bus ueberhaupt ein Geraet antwortet, ruft
`ExternalDisplayManager::begin()` (externes SH1107, 0x3D) die
Adafruit-Bibliothek direkt auf (`display.begin(EXTERNAL_DISPLAY_I2C_ADDRESS,
true)`, `ExternalDisplayManager.cpp` Zeile 28) - ohne den gleichen
Vorab-Probe.

Konkreter Anlass des Live-Fundes war ein Wackelkontakt am Nutzer-seitigen
Board, kein Firmware-Bug - das eigentliche Symptom bleibt aber unabhaengig
davon bestehen: bei JEDEM Boot ohne gestecktes externes SH1107-Modul (dem
Normalfall fuer die meisten Geraete, da optional) erzeugen diese zwei
HAL-Zeilen unnoetigen, alarmierend wirkenden Log-Lärm. **Kein
Funktionsbug** (kein Endlos-Spam in `loop()` wie beim urspruenglichen,
bereits gefixten Bug - `begin()` laeuft nur einmalig in `setup()`), nur
Inkonsistenz zum bereits saubereren internen Display.

**Auf Nutzerwunsch zurueckgestellt, noch nicht behoben.** Fix waere
analog zum internen Display: `Wire.beginTransmission(EXTERNAL_DISPLAY_I2C_ADDRESS)`
+ `endTransmission()` vor `display.begin()` in `ExternalDisplayManager::begin()`.

Beim Gegenpruefen festgestellt: **sensormeter (sm) hat das gleiche Problem
zusaetzlich auch beim INTERNEN Display** - dort wurde der Vorab-Probe-Fix
nie zurueckportiert, siehe eigener Eintrag in
`sensormeter/repo/docs/entscheidungen.md`.

## 2026-07-21 — Lastenheft SNMP-Vorgabe v1 -> v2c (kein Code-Aenderungsbedarf hier)

Nutzerentscheidung (Details/Begruendung siehe
`sensormeter-display/repo/docs/entscheidungen.md`, selber Tag): Client
(sensormeter-display) stellt seine SNMP-Anfragen von v1 auf v2c um.
Auf dieser Agenten-Seite ist **keine Code-Aenderung** noetig - die
eingesetzte Bibliothek `SNMP_Agent@2.1.0` verhandelt die Version bereits
pro eingehender Anfrage automatisch (`SNMPPacket.cpp` liest sie direkt
aus dem Paket), `SNMPManager.cpp` referenziert `SNMP_VERSION` an keiner
Stelle. `docs/lastenheft.txt` Abschnitt 7 von "SNMP (v1 READ ONLY)" auf
"SNMP (v2c READ ONLY)" aktualisiert, damit die Spec den tatsaechlichen
Betriebszustand widerspiegelt. Kein Sicherheitsgewinn (weiterhin
Community-String im Klartext) - rein pragmatische Umstellung, siehe
sensormeter-display fuer die vorher geprueften und verworfenen
SNMPv3-Alternativen.

## 2026-07-21 — I2C-Vorab-Probe fuer ExternalDisplayManager nachgezogen (Fix umgesetzt)

Der oben als "zurueckgestellt" dokumentierte Fix ist jetzt eingebaut:
`ExternalDisplayManager::begin()` prueft jetzt wie `DisplayManager::begin()`
per `Wire.beginTransmission(EXTERNAL_DISPLAY_I2C_ADDRESS)`/
`endTransmission()` vor, ob am Bus ueberhaupt ein Geraet antwortet, bevor
`display.begin()` aufgerufen wird - vermeidet die rohen
"i2c_master_transmit failed"-HAL-Zeilen bei fehlendem externem SH1107.

`pio run -e esp32-s3-eth` erfolgreich (Flash 22,5%, RAM 19,3%,
Speicherbedarf praktisch unveraendert). OTA-Bin unter
`firmware/.pio/build/esp32-s3-eth/firmware.bin` bereitgestellt - Nutzer
laedt selbst per OTA hoch, nicht ueber diese Sitzung geflasht/verifiziert.
