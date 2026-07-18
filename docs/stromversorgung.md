# Strombedarf & Stromversorgung

Strombudget für ein Sensormeter-PoE-Gerät, damit ein passendes Netzteil
bzw. ein passender PoE-Injektor/Switch gewählt werden kann. Unterschieden
wird zwischen **Durchschnitt** (bestimmt die Wärmeentwicklung/Dauerlast)
und **Spitze** (bestimmt, wie kräftig die Quelle kurzzeitig sein muss,
damit die Spannung nicht einbricht). Anders als bei `sensormeter` und
`sensormeter-wlan` gibt es hier **zwei mögliche Versorgungswege**
(USB-C **oder** PoE), die getrennt betrachtet werden.

## Strombudget pro Komponente (bei 3,3–5V)

| Komponente | Ø-Strom | Spitzenstrom | Quelle |
|---|---|---|---|
| ESP32-S3R8 (WLAN aktiv) | ~80–120 mA | ≥ 500 mA (WLAN-TX-Burst) | Espressif ESP32-S3-Datenblatt: TX-Spitzen bis ~700 mA bei voller Sendeleistung |
| W5500-Ethernet-PHY (auf der Waveshare ESP32-S3-ETH verbaut) | ~50–70 mA | ~180 mA | WIZnet W5500-Datenblatt |
| DHT-22 (intern) | ~0,3–1 mA | ~2,5 mA | Standard-Datenblattwerte; durch 60s-Abfragetakt liegt der Sensor die meiste Zeit im Standby |
| DHT-22 (extern, Sensor 2, nur bei aktiviertem RJ45-Modul) | ~0,3–1 mA | ~2,5 mA | Standard-Datenblattwerte, gleiche Logik wie Sensor 1 |
| OLED SSD1306, 0,96", 128×64 (intern) | 12–24 mA | ~24 mA | Herstellerangabe: "0,04W normal / 0,08W Vollbild" bei 3,3V → 12–24 mA |
| OLED SH1107, 1,5", 128×128 (optionales externes RJ45-Modul) | 15–25 mA | ~25 mA | Herstellerangabe, größeres Panel als SSD1306, ähnliche Leistungsaufnahme pro Fläche |
| Relais-Modul (5V, 1 Kanal, optional) | ~0 mA (Ruhe) | ~70–80 mA (Spule beim Schalten) | Standard-5V-Relaismodul mit Optokoppler-Ansteuerung |
| Pull-up-Widerstände (10k/4,7k) | < 1 mA | < 1 mA | vernachlässigbar |

## Gesamtbedarf pro Gerät

| Konfiguration | Ø-Strom (Dauerlast, 5V) | Spitzenstrom |
|---|---|---|
| **Minimal** (nur intern: ESP32-S3-ETH + Sensor 1 + internes Display) | ~145–215 mA | ~730 mA |
| **Vollausbau** (+ Sensor 2, externes SH1107, Relais aktiv) | ~160–235 mA | ~810 mA |

Der Spitzenwert wird praktisch komplett vom ESP32-S3 selbst bestimmt
(WLAN-Sendebursts, Flash-Schreibzugriffe bei `config.xml`/OTA) – analog
zu `sensormeter`, dort aber ohne die zusätzliche W5500-Grundlast und ohne
Relais-Spitze. Beide zusätzlichen Komponenten hier begründen einen etwas
höheren Sockelbedarf gegenüber dem Sensormeter-Basisprojekt.

## Versorgungsweg 1: USB-C, 5V

**5V-USB-C-Netzteil, mindestens 1A (1000 mA).**

Begründung analog zu `sensormeter`: 1A statt der bloßen
~235-mA-Dauerlast-Schätzung gibt großzügig Reserve gegen Spannungsabfall
durch dünne/billige Kabel und deckt den Spitzenwert (~810 mA) auch bei
gleichzeitig aktivem Relais und WLAN-Burst sicher ab. Nicht verwenden:
der Ausgang eines USB-Seriell-Adapters oder ein reiner Daten-USB-Port
ohne Ladefunktion.

## Versorgungsweg 2: Power over Ethernet (802.3af, optional)

Das aufsteckbare Waveshare-PoE-Modul (siehe `stueckliste.md`) ist
**IEEE 802.3af** (Class 0–3, bis 12,95 W an der speisenden Seite,
typischerweise ~9–10 W nutzbar am Ausgang nach Kabel-/Wandlerverlust).
Das reicht komfortabel für das oben ermittelte Budget (~235 mA × 5V ≈
1,2 W Dauerlast, ~810 mA × 5V ≈ 4,1 W Spitze) — selbst mit deutlichem
Sicherheitsaufschlag für den DC/DC-Wandlerwirkungsgrad des PoE-Moduls
(üblicherweise 80–90 %) bleibt reichlich Reserve zum 802.3af-Limit.

**Voraussetzung:** ein 802.3af-fähiger PoE-Switch-Port oder ein separater
PoE-Injektor im selben LAN-Segment. **802.3at/802.3bt** (PoE+/PoE++)
funktioniert ebenfalls (abwärtskompatibel), ist für dieses Gerät aber
nicht erforderlich.

> **Nicht nachgemessen:** Ob die tatsächliche 5V-Versorgungsschiene des
> Waveshare-PoE-Moduls unter Volllast (Relais + beide Displays + WLAN-Burst
> gleichzeitig) stabil bleibt, wurde mangels entsprechend bestückter
> Hardware noch nicht real getestet — der erste Board-Bringup
> (2026-07-18) lief ausschließlich über USB-C, ohne PoE-Modul gesteckt
> (siehe `entscheidungen.md`). Vor dem produktiven PoE-Betrieb mit voller
> Modulbestückung empfiehlt sich eine kurze Kontrollmessung.

## USB-C und PoE gleichzeitig

Beide Versorgungswege sind auf dem Board unabhängig voneinander nutzbar,
aber **nicht** für den gleichzeitigen Betrieb an beiden Quellen
ausgelegt (kein dokumentierter Redundanz-/Failover-Mechanismus zwischen
USB-C und PoE auf diesem Board) — im Zweifel nur eine der beiden Quellen
gleichzeitig anschließen.
