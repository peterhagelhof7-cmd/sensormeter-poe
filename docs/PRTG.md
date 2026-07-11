# PRTG-Integration

Das Board beantwortet SNMP-v1/v2c-GET-Anfragen direkt (siehe
`docs/lastenheft.txt` Abschnitt 7) – PRTG kann es daher wie jedes andere
SNMP-Gerät abfragen, ganz ohne zusätzliche Software auf dem Board oder
einem Gateway. SNMP ist read-only: PRTG kann nichts am Gerät verändern.

**Hinweis:** Sensormeter PoE wurde zum Zeitpunkt der Erstellung dieses
Templates noch nicht auf echter Hardware geflasht/getestet (siehe
`docs/entscheidungen.md`) – die SNMP-Antworten sind daher nur gegen den
Firmware-Quellcode (`firmware/src/SNMPManager.cpp`) geprüft, nicht gegen
ein echtes Board.

## OIDs (Lastenheft Abschnitt 7, Basis `.1.3.6.1.4.1.99999`)

Identisches Schema wie beim Sensormeter-Projekt (WT32-ETH01) – bewusst so
gewählt, damit z. B. das Sensormeter-Display-Projekt beide Produktlinien
ohne Codeänderung abfragen kann.

| OID | Bedeutung | Typ |
|---|---|---|
| `.1.99999.1.1.0` | Systemname | String |
| `.1.99999.1.2.0` | Firmwareversion | String |
| `.1.99999.1.3.0` | Systemtyp ("Sensormeter PoE" / "Sensormeter PoE PRO") | String |
| `.1.99999.2.1.0` | LAN-IP (Ethernet, W5500) | String |
| `.1.99999.2.2.0` | WLAN-IP | String |
| `.1.99999.2.3.0` | WLAN-Signalstärke | Integer, dBm |
| `.1.99999.3.1.0` | Sensor 1 Name | String |
| `.1.99999.3.2.0` | Sensor 1 Temperatur | Integer, ×10 (235 = 23.5 °C) |
| `.1.99999.3.3.0` | Sensor 1 Luftfeuchte | Integer, ×10 |
| `.1.99999.4.1.0` | Sensor 2 Name (nur PRO) | String |
| `.1.99999.4.2.0` | Sensor 2 Temperatur (nur PRO) | Integer, ×10 |
| `.1.99999.4.3.0` | Sensor 2 Luftfeuchte (nur PRO) | Integer, ×10 |
| `.1.99999.5.1.0` | Uptime | TimeTicks (Zentisekunden) |
| `.1.99999.5.2.0` | Freier Heap | Gauge32, Bytes |

(OIDs oben mit `.1.3.6.1.4.1` abgekürzt.) Community-String ist auf der
Einstellungsseite des Geräts konfigurierbar (Default `public`).

## Template importieren

PRTG-Geräte-Templates liegen als `.odt`-Datei im Ordner
`devicetemplates` der PRTG-Installation (Standard:
`C:\Program Files (x86)\PRTG Network Monitor\devicetemplates\`).

1. Datei [`prtg-template-sensormeter-poe.odt`](prtg-template-sensormeter-poe.odt)
   in den `devicetemplates`-Ordner des PRTG-Servers kopieren
2. Neue Templates werden von PRTG automatisch beim nächsten
   Auto-Discovery-Lauf erkannt (kein Neustart des PRTG-Core-Dienstes
   nötig, kann aber ein bis zwei Minuten dauern)

## Gerät anlegen

1. Im gewünschten Probe/Ordner/Gruppe: **Add Device**
2. Name vergeben, IPv4/DNS-Name des Boards eintragen
3. Bei **Device Template** den Punkt **"Automatic Device Identification
   (recommended)"** abwählen und stattdessen manuell **"Sensormeter
   PoE"** auswählen
4. Unter **SNMP Credentials** die Community eintragen (Default `public`),
   SNMP-Version **v2c** wählen, Port `161`
5. **Continue** → PRTG legt alle 15 Sensoren aus dem Template an

## Nicht zutreffende Sensoren entfernen

Die Firmware liefert für nicht vorhandene Zweige einfach keine Antwort
statt eines falschen Werts. Nach dem Anlegen ggf. manuell löschen:

- **"Sensor 2: ..."** (3 Sensoren) – nur relevant, wenn Sensor 2 auf der
  Einstellungsseite aktiviert ist (manuell oder per Auto-Erkennung,
  siehe `docs/lastenheft.txt` Abschnitt 15)

## Mitgelieferte Sensoren

| Sensor | PRTG-Sensortyp | Skalierung |
|---|---|---|
| Ping | Ping | – |
| System: Name / Firmwareversion / Systemtyp | SNMP Custom String | – |
| Netzwerk: LAN-IP / WLAN-IP | SNMP Custom String | – |
| Netzwerk: WLAN-Signalstärke | SNMP Custom (dBm) | ÷1 |
| Sensor 1/2: Name | SNMP Custom String | – |
| Sensor 1/2: Temperatur | SNMP Custom (°C) | ÷10 |
| Sensor 1/2: Luftfeuchtigkeit | SNMP Custom (%) | ÷10 |
| Status: Uptime | SNMP Custom (Sekunden) | ÷100 (TimeTicks sind Zentisekunden) |
| Status: Freier Heap | SNMP Custom (Bytes) | ÷1 |

Der Relais/Aktor-Zustand (Lastenheft Abschnitt 16.2) ist bewusst NICHT
Teil dieses Templates – er wird nicht über SNMP gesteuert/gemeldet
(SNMP bleibt read-only), sondern über Web/REST/MQTT.

## Warnschwellwerte

Das Template legt bewusst keine Limits/Trigger vorkonfiguriert an –
nach dem Import direkt am jeweiligen Sensor unter **Channels → Limits**
setzen (empfohlen: Sensor-Temperatur Ober-/Untergrenze, Luftfeuchtigkeit-
Obergrenze, Freier Heap Untergrenze ~20000 Bytes).

## Mehrere Boards

Jedes Board bekommt in PRTG ein eigenes Gerät mit demselben Template,
nur mit unterschiedlicher IP-Adresse.

## Testen ohne PRTG

Mit Net-SNMP-Tools (`apt install snmp` unter Linux):

```
snmpget -v1 -c public <board-ip> .1.3.6.1.4.1.99999.3.2.0
snmpget -v2c -c public <board-ip> .1.3.6.1.4.1.99999.3.3.0
```

## Technischer Hintergrund zum Template

Das Geräte-Template-Dateiformat (`.odt`) ist von Paessler nicht offiziell
dokumentiert. Dieses Template wurde daher nicht aus der Dokumentation
abgeleitet, sondern anhand eines echten, veröffentlichten PRTG-Templates
für einen vergleichbaren ESP32-basierten SNMP-Umweltsensor nachgebaut und
verifiziert (wohlgeformtes XML, korrekte `kind`-Werte
`snmpcustom`/`snmpcustomstring`/`ping`, funktionierende Skalierung über
`factord`) – alle OIDs zusätzlich direkt gegen `SNMPManager.cpp`
gegengeprüft. Da dieses Board noch nicht real getestet wurde, unbedingt
vor dem produktiven Einsatz testweise importieren und die Sensorwerte
gegen `snmpget` (siehe oben) gegenprüfen.
