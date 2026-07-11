# scripts/

## flash.ps1 / flash.cmd

Richtet auf einem beliebigen Windows-PC alles ein, was für einen
Flash-Vorgang nötig ist, und flasht anschließend eines von vier
Sensormeter-Schwesterprojekten:

1. **Projekt wählen** – interaktiv (1-4-Menü) oder per `-Project
   sensormeter|wlan|display|poe`:
   - **Sensormeter** (WT32-ETH01, Ethernet + bis zu 2 Sensoren)
   - **Sensormeter WLAN** (generisches ESP32-WROOM-32-DevKit, WLAN-only)
   - **Sensormeter Display** (ESP32-Touchdisplay, SNMP-Client)
   - **Sensormeter PoE** (Waveshare ESP32-S3-ETH, PoE optional, Relais)
2. Python installieren (falls nicht vorhanden, über winget)
3. Git installieren (falls nicht vorhanden, über winget)
4. PlatformIO installieren (falls nicht vorhanden, über pip)
5. Das gewählte Repo klonen (falls noch nicht vorhanden) bzw. aktualisieren
   (`git pull`, nur wenn keine lokalen Änderungen vorliegen)
6. `firmware/include/config.h` aus der Vorlage anlegen, falls das gewählte
   Projekt eine braucht und sie noch fehlt (eine vorhandene wird nie
   überschrieben; **Sensormeter Display** braucht keine – alle
   Einstellungen wie WLAN, Betriebsmodus, Sensormeter-Ziel, Ping-Ziele
   werden am Gerät selbst per Touch eingerichtet und in NVS gespeichert)
7. Firmware bauen (`pio run`)
8. Firmware flashen (`pio run --target upload`)

Dieses Skript liegt identisch in allen vier Repos (`scripts/flash.ps1`) –
unabhängig davon, welches Projekt gerade lokal ausgecheckt ist, lässt sich
darüber jedes der vier flashen (die jeweils anderen werden bei Bedarf
automatisch in einen Unterordner neben dem Skript geklont).

**Anschluss-Voraussetzungen:**
- **Sensormeter** (WT32-ETH01): Board am USB-Seriell-Adapter
  (Debug-Burning-Schnittstelle, nicht am 20-Pin-Hauptheader!)
  angeschlossen, siehe
  [`../docs/flash-vorbereitung.pdf`](../docs/flash-vorbereitung.pdf)
  (im Sensormeter-Repo).
- **Sensormeter WLAN** / **Sensormeter PoE**: Board per USB-C angeschlossen,
  keine bekannten Besonderheiten.
- **Sensormeter Display** (HW-458B): Board per USB-C oder USB-Micro
  angeschlossen, ggf. CH340-Treiber installieren.

### Nutzung

Nur diese eine Datei `flash.ps1` (oder zusätzlich die `.cmd` zum
Doppelklicken) auf den Ziel-PC kopieren und ausführen – das jeweilige Repo
muss dafür noch **nicht** vorher geklont sein, das Skript erledigt das
selbst.

**Per Doppelklick:** `flash.cmd` – öffnet ein Konsolenfenster, fragt nach
dem gewünschten Projekt und bleibt nach Abschluss offen (zum Lesen von
Meldungen/Fehlern).

**Per PowerShell:**

```powershell
.\flash.ps1                                  # fragt interaktiv nach dem Projekt
.\flash.ps1 -Project sensormeter             # Projekt direkt angeben
.\flash.ps1 -Project wlan -Port COM5         # fester COM-Port
.\flash.ps1 -Project display -SkipUpload     # nur bauen, nicht flashen
.\flash.ps1 -Project poe -RepoPath C:\Projekte\sensormeter-poe
```

Falls PowerShell die Ausführung wegen der Execution Policy verweigert:

```powershell
powershell -ExecutionPolicy Bypass -File .\flash.ps1
```

**Geplant, noch nicht umgesetzt:** Mac-Unterstützung, ausdrücklich nur für
Apple-Silicon-Macs (ARM, kein Intel-Mac) - siehe
[`../docs/entscheidungen.md`](../docs/entscheidungen.md) für offene Fragen
zur Umsetzung.

## convert-logo.ps1

Konvertiert ein Anbieter-Logo (beliebiges Bildformat) in das fürs
Anbieter-Branding-Feature kompatible Rohformat – liegt identisch in allen
vier Repos, analog zu `flash.ps1`. Fragt zuerst (interaktiv oder per
`-Display sensormeter|wlan|poe|display|custom`), für welches Display
konvertiert werden soll, und reduziert Auflösung **und Farbtiefe**
konsequent auf das, was das jeweilige Display tatsächlich darstellen kann
– ein 24-Bit-Foto wird also nicht einfach verkleinert, sondern für die
monochromen OLEDs auf echte 1-Bit-Schwarzweiß-Werte reduziert (kein
Graustufen-„so tun als ob").

**Alle vier Projekte haben ein implementiertes Branding-Feature**
(`BrandingManager`, siehe jeweiliges `docs/entscheidungen.md`):

| Preset | Projekt(e) | Display | Zielgröße | Format |
|---|---|---|---|---|
| `sensormeter` / `wlan` | Sensormeter, Sensormeter WLAN | OLED SSD1306 | 128×64 | 1-Bit monochrom |
| `poe` | Sensormeter PoE | OLED SH1107 | 128×128 | 1-Bit monochrom |
| `display` | Sensormeter Display | TFT ST7789P3 (Farbe) | 128×64 | RGB565, 2 Byte/Pixel |

Die Farbziel-Größe (128×64) ist bewusst **dieselbe** wie bei den
monochromen Projekten (nicht die native Panel-Auflösung 240×320) – nur die
Farbtiefe unterscheidet sich, damit dieses eine Skript für alle vier
Projekte eine einheitliche Logo-Größe nutzt.

Monochromes Ausgabeformat: exakt Breite/8 × Höhe Byte, MSB-zuerst je
Zeile, kein Padding – identisch zu dem, was `Adafruit_GFX::drawBitmap()`
erwartet und was `BrandingManager.h` als `LOGO_WIDTH`/`LOGO_HEIGHT`/
`LOGO_BYTES` vorschreibt.

Farb-Ausgabeformat (Sensormeter Display): RGB565, Little-Endian,
zeilenweise ohne Padding. **Achtung – nicht auf echter Hardware
verifiziert:** dieses Panel läuft mit `TFT_RGB_ORDER=0` (BGR) und zeigte
selbst dabei einzelne falsch interpretierte Farben (siehe
`sensormeter-display/docs/entscheidungen.md`) – färbt ein hochgeladenes
Logo auf echter Hardware sichtbar falsch (Rot/Blau vertauscht), hilft der
Schalter `-SwapRedBlue`.

Das Quellbild wird seitenverhältnistreu eingepasst (nicht verzerrt) und
zentriert mit der Padding-Farbe (Default Schwarz) aufgefüllt. Transparente
Bereiche einer PNG-Quelle werden dabei automatisch mit der Padding-Farbe
unterlegt.

```powershell
.\convert-logo.ps1 -InputPath .\firmenlogo.png                # fragt interaktiv nach dem Display
.\convert-logo.ps1 -InputPath .\firmenlogo.png -Display wlan   # 128x64, 1bpp, direkt hochladbar
.\convert-logo.ps1 -InputPath .\firmenlogo.png -Display poe    # 128x128, 1bpp
.\convert-logo.ps1 -InputPath .\firmenlogo.png -Display display -SwapRedBlue
.\convert-logo.ps1 -InputPath .\firmenlogo.png -Display custom -Width 96 -Height 48 -ColorMode mono
```

Das Ergebnis (`<Name>-<Display>-<Breite>x<Höhe>.bin`) lässt sich direkt
über die Einstellungsseite des jeweiligen Geräts hochladen – bei
Sensormeter/Sensormeter WLAN/Sensormeter PoE unter „Anbieter-Branding →
Logo hochladen", bei Sensormeter Display unter „Branding-Logo → Logo
hochladen".

## ../firmware/tools/simulate_json_load.cpp

**Nur im Sensormeter-Repo vorhanden** (nicht Teil der drei
Geschwisterprojekte, dieser Abschnitt betrifft also nur diese eine Kopie
der Datei). Siehe
[`../firmware/tools/README.md`](../firmware/tools/README.md) – natives
Testprogramm zur Heap-Last-Simulation der REST-API, kein
Setup-/Flash-Werkzeug.
