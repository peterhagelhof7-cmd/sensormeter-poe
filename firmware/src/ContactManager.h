#pragma once

#include <Arduino.h>
#include "ConfigManager.h"
#include "DataManager.h"

// Tuerkontakt/Reed-Kontakt an RJ45 Pin 5 (Kategorie-2-Direktmodul, siehe
// sensormeter-family/repo/module-design/README.md) - teilt sich den Pin mit
// dem DHT22-Sensormodul (Sensor 2); cfg.pin5Mode legt fest, welche Belegung
// aktiv ist (Einstellungsseite, Abschnitt "Externe Schnittstelle", rein
// manuelle Wahl - keine Auto-Erkennung, siehe module-design/README.md
// "Bekannte Einschraenkung der Auto-Erkennung fuer einen kuenftigen
// Tuerkontakt"). Eigener, binaerer Datenpfad statt Wiederverwendung von
// Sensor 2 (Temperatur/Feuchte passt nicht). Pull-up sitzt auf dem Modul -
// das Geraet nutzt INPUT_PULLUP nur als Rueckfallebene, falls kein Modul
// gesteckt ist (liest dann dauerhaft "geschlossen", elektrisch nicht von
// einem offenen Kontakt unterscheidbar). Portiert aus sensormeter/repo
// (WT32-ETH01), siehe dortige docs/entscheidungen.md.

class ContactManager {
 public:
  ContactManager(DataManager& dataManager, ConfigManager& configManager);

  void begin();
  // Liest den Pin nur, wenn cfg.pin5Mode == "contact"; protokolliert
  // Zustandswechsel per pushLogEntry und merkt sich den Wechsel fuer
  // alarmActive() im Modus "change" (siehe dort).
  void loop();

  bool isClosed() const { return _closed; }
  // Reiner Zustandstext ("Offen"/"Geschlossen"), ohne Alarm-Kennzeichnung -
  // fuer Hauptseite/Serial-Status (nicht konsumierend, siehe alarmActive()).
  String stateLabel() const { return _closed ? "Geschlossen" : "Offen"; }

  // true, wenn der aktuelle Zustand gemaess cfg.contactAlarmAt ("open" |
  // "closed" | "change") einen Alarm darstellt. Bei "open"/"closed"
  // zustandsgetriggert (bleibt true, solange der Zustand anhaelt). Bei
  // "change" kantengetriggert: liefert nur bis zum naechsten
  // acknowledgeChange()-Aufruf true (danach false, bis zum naechsten
  // tatsaechlichen Wechsel) - rein lesend, konsumiert selbst nichts.
  bool alarmActive() const;

  // Loescht das kantengetriggerte "gerade gewechselt"-Flag fuer den Modus
  // "change" - von der Einstellungsseite/REST-API (/api/contact) aufgerufen,
  // NACHDEM der Alarm dem Nutzer einmalig angezeigt wurde, damit er nicht bei
  // jedem Seitenaufruf erneut erscheint. Hauptseite/Serial-Status rufen dies
  // bewusst NICHT auf (reine Anzeige, kein "Quittieren").
  void acknowledgeChange() { _justChanged = false; }

 private:
  DataManager& _data;
  ConfigManager& _config;
  bool _closed = false;
  bool _stateKnown = false;
  bool _justChanged = false;
};
