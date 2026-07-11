#pragma once

// ============================================================================
// Waveshare ESP32-S3-ETH - Pinbelegung
// Einzige gueltige Quelle: docs/ESP32-S3-ETH-Datenblatt.pdf (Abschnitt 3,
// direkt vom Hersteller-Pinout extrahiert) und docs/verdrahtungsplan.html.
//
// Bewusst vermiedene Pins fuer JEDE neue Peripherie, obwohl im Hersteller-
// Diagramm gruen (frei) markiert: GPIO35/36/37 - der verbaute ESP32-S3R8
// nutzt bei Octal-PSRAM (das "R8" in der Chipbezeichnung) diese drei Pins
// silizium-seitig fuer die PSRAM-Anbindung; ob das Diagramm dies fuer dieses
// konkrete Board tatsaechlich schon beruecksichtigt, liess sich ohne echte
// Hardware nicht verifizieren - daher hier vorsorglich nicht belegt (siehe
// docs/entscheidungen.md). Ebenfalls vermieden: GPIO3/45/46 (Boot-Strapping-
// Pins, koennen den Bootmodus beeinflussen, siehe Espressif-Technical-
// Reference-Manual) - nur GPIO0 (siehe unten) ist als bereits vorhandener,
// bewusst genutzter Strapping-Pin (Onboard-BOOT-Taster) eine Ausnahme.

// --- Ethernet (W5500 ueber SPI) - fest verdrahtet, NICHT aendern -----------
#define ETH_PHY_TYPE ETH_PHY_W5500
#define ETH_PHY_ADDR 1
#define ETH_PHY_CS   14
#define ETH_PHY_IRQ  10
#define ETH_PHY_RST  9
#define ETH_SPI_SCK  13
#define ETH_SPI_MISO 12
#define ETH_SPI_MOSI 11

// --- I2C-Bus: Display (SH1107) + externer I2C-Sensor am RJ45 (Pin 3/4) ----
#define PIN_I2C_SDA 1
#define PIN_I2C_SCL 2

// --- Sensor 1 (intern, DHT-22) ----------------------------------------------
#define PIN_DHT_INTERNAL 15
// Pull-up 10k zwischen PIN_DHT_INTERNAL und 3.3V erforderlich (siehe Stueckliste)

// --- RJ45 Modularanschluss (Sensor 2 / Relais / Reserve) - Pin-ROLLEN
// identisch zu Sensormeter (WT32-ETH01), siehe lastenheft.txt Abschnitt 14;
// die konkreten GPIO-Nummern unterscheiden sich zwangslaeufig (anderer Chip).
#define PIN_RJ45_PIN5_RESERVE   16  // externer DHT-22, DATA-Leitung
#define PIN_RJ45_PIN6_RELAY_OUT 17  // Relais-Steuerung, active LOW
#define PIN_RJ45_PIN7_RELAY_FB  18  // Relais-Feedback / Interrupt
#define PIN_RJ45_PIN8_RESERVE   19  // frei, kein Boot-Strapping-Pin (anders
                                    // als beim WT32-ETH01, siehe dortiges pins.h)

// --- Sensor 2 (extern, DHT-22 ueber RJ45 Pin 5) -----------------------------
#define PIN_DHT_EXTERNAL PIN_RJ45_PIN5_RESERVE

// --- Taster: onboard BOOT-Taster (aktiv LOW, interner Pullup) - anders als
// beim WT32-ETH01 hier frei nutzbar, da GPIO0 nicht mit dem Ethernet-Takt
// verdrahtet ist (W5500 haengt komplett an SPI, kein RMII-Takt noetig). ---
#define BUTTON_BOOT_PIN 0
