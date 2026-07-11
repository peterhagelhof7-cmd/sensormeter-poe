#pragma once

#include <Arduino.h>
#include "ConfigManager.h"
#include "DataManager.h"

// BOOT-Taster-Bedienung (Lastenheft Abschnitt 11.1, Pflichtenheft "Task:
// ButtonTask") - eigenstaendiges Modul (anders als bei Sensormeter WLAN, wo
// dieselbe Logik in DisplayManager::handleButton() eingebettet ist), damit
// DisplayManager nur noch fuers Zeichnen zustaendig ist. Funktioniert in
// jedem Systemzustand (auch waehrend Boot/Fallback), da als Recovery-Weg
// ganz ohne Netzwerkzugriff gedacht:
//
// - Kurzer Tipp (>=50ms, <3s): naechste Seite - siehe consumePageAdvance()
// - >=3s gehalten: Reset-Bestaetigung mit 20s-Countdown (siehe
//   isHeldForReset()/isAwaitingRelease()/resetCountdownSecondsLeft())
// - Fail-Safe: der Werksreset (nur Einstellungen) wird erst BEIM
//   tatsaechlichen Loslassen nach voller Haltezeit ausgeloest, nicht schon
//   waehrend des Haltens - ein verklemmter/defekter Taster kann so nie von
//   selbst einen Reset ausloesen.

class ButtonManager {
 public:
  ButtonManager(DataManager& dataManager, ConfigManager& configManager);

  void begin();
  void loop();

  // Liefert true GENAU EINMAL pro kurzem Tipp (danach automatisch wieder
  // false, bis zum naechsten Tipp) - DisplayManager ruft dies einmal pro
  // loop()-Durchlauf ab, um zur naechsten Seite zu schalten.
  bool consumePageAdvance();

  // true, solange der Taster >=3s gehalten wird (Reset-Bestaetigung bzw.
  // "Loslassen zum Bestaetigen" soll statt der normalen Seite angezeigt
  // werden).
  bool isHeldForReset() const;
  // true, wenn der 20s-Countdown abgelaufen ist, der Taster aber weiter
  // gehalten wird (wartet auf das Loslassen, siehe Fail-Safe oben).
  bool isAwaitingRelease() const;
  // Verbleibende Sekunden bis zum Ende des Countdowns (nur waehrend
  // isHeldForReset() && !isAwaitingRelease() sinnvoll).
  int resetCountdownSecondsLeft() const;

 private:
  DataManager& _data;
  ConfigManager& _config;

  // 0 = Taster nicht gedrueckt, sonst Zeitpunkt (millis()), seit dem er
  // durchgehend gedrueckt gehalten wird.
  unsigned long _pressStartMillis = 0;
  bool _pageAdvancePending = false;
};
