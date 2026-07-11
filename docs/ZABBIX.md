# Zabbix-Integration

Das Board beantwortet SNMP-v1/v2c-GET-Anfragen direkt (siehe
`docs/entscheidungen.md`) – Zabbix kann es also wie jedes andere
SNMP-Gerät abfragen, ganz ohne zusätzliche Software auf dem Board oder
einem Gateway. SNMP ist read-only: Zabbix kann nichts am Gerät verändern.

**Hinweis:** Sensormeter PoE wurde zum Zeitpunkt der Erstellung dieses
Templates noch nicht auf echter Hardware geflasht/getestet (siehe
`docs/entscheidungen.md`) – die Items sind nur gegen den Firmware-
Quellcode (`firmware/src/SNMPManager.cpp`) geprüft, nicht gegen ein
echtes Board.

## OIDs (Lastenheft Abschnitt 7, Basis `.1.3.6.1.4.1.99999`)

Identisches Schema wie beim Sensormeter-Projekt (WT32-ETH01) – bewusst so
gewählt, damit z. B. das Sensormeter-Display-Projekt beide Produktlinien
ohne Codeänderung abfragen kann. **Nicht identisch mit Sensormeter WLAN**
(dort ein engeres, verschobenes Schema – eigenes Template, siehe dessen
`ZABBIX.md`).

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
Einstellungsseite des Geräts konfigurierbar (Default `public`). Das
Relais/Aktor (Lastenheft Abschnitt 16.2) ist bewusst NICHT Teil dieses
Templates – SNMP bleibt read-only, Steuerung nur über Web/REST/MQTT.

## Template importieren

1. In Zabbix: **Data collection → Templates → Import**
2. Datei [`zabbix-template-sensormeter-poe.yaml`](zabbix-template-sensormeter-poe.yaml) auswählen
3. Import bestätigen

## Host anlegen

1. **Data collection → Hosts → Create host**
2. Name vergeben (z. B. "Sensor Wohnzimmer")
3. Template **"Sensormeter PoE"** zuweisen
4. Interface hinzufügen: Typ **SNMP**, IP-Adresse des Boards, Port `161`,
   SNMP-Version **SNMPv2** (das Gerät antwortet unabhängig davon auch
   v1-Clients korrekt), Community `public` (oder dein eigener Wert)
5. Falls du die Community auf der Einstellungsseite des Geräts geändert
   hast: Host-Makro `{$SNMP_COMMUNITY}` im Host auf denselben Wert setzen

## Mehrere Boards

Jedes Board bekommt in Zabbix einen eigenen Host mit dem gleichen
Template, nur mit unterschiedlicher IP-Adresse im SNMP-Interface. Der
Systemname (Einstellungsseite) hilft dir, die Boards in Zabbix
wiederzuerkennen (taucht auch als eigener Item-Wert "System: Name" auf).

## Mitgelieferte Trigger

Schwellwerte über Host-Makros anpassbar (`{$TEMP_MAX_C}`,
`{$HUMIDITY_MAX_PERCENT}`, `{$HEAP_MIN_BYTES}`):

- Temperatur (Sensor 1 / Sensor 2) zu hoch
- Luftfeuchtigkeit (Sensor 1 / Sensor 2) zu hoch
- Freier Heap niedrig
- Keine Daten seit 10 Minuten (Board offline oder Sensor defekt)

Die Sensor-2-Trigger sind nur relevant, wenn Sensor 2 (Sensormeter PoE
PRO) aktiv ist – sonst liefert das Gerät dafür durchgehend `0`, die
Trigger lösen dann nicht aus.

## Testen ohne Zabbix

Mit Net-SNMP-Tools (`apt install snmp` unter Linux):

```
snmpget -v1 -c public <board-ip> .1.3.6.1.4.1.99999.3.2.0
snmpget -v2c -c public <board-ip> .1.3.6.1.4.1.99999.3.3.0
```

## Siehe auch

[docs/PRTG.md](PRTG.md) – gleichwertiges Geräte-Template für PRTG
Network Monitor (gleiches OID-Schema, unabhängiges Werkzeug).
