#pragma once

// ── Pins ──────────────────────────────────────────────────────────────────────
#define PIN_CS_THERMO   8

// Innebygd RGB LED på Arduino Uno R4 WiFi (aktiv lav)
#ifndef LEDR
#define LEDR  22
#define LEDG  23
#define LEDB  24
#endif
#define PIN_RELAY       4
#define PIN_ESTOP       3   // Hardware interrupt

// ── Secrets (WiFi, API-nøkler – ikke committet til git) ──────────────────────
#include "config_secrets.h"

// ── Knapper ───────────────────────────────────────────────────────────────────
#define PIN_BTN_START   5   // Start Glaze (hold 1s) / Bisque (hold 5s)

// ── Data-logg ─────────────────────────────────────────────────────────────────
#define LOG_SIZE             480         // detaljlogg: RAM, siste 2 timer (15 sek)
#define LOG_INTERVAL_MS      15000UL
#define FULL_LOG_SIZE        144         // fulllogg: RAM + EEPROM, hele brenningen (5 min)
#define FULL_LOG_INTERVAL_MS 300000UL
// segIdx: bit 7 = relay-tilstand, bit 0-6 = segment-index
struct DataPoint { uint16_t sec; uint16_t temp; uint16_t sp; uint8_t segIdx; uint8_t pid; };

// ── Hendelseslogg ─────────────────────────────────────────────────────────────
#define EVENT_LOG_SIZE  32
#define EV_START        0   // Brenning startet
#define EV_SEGMENT      1   // Nytt segment
#define EV_HOLD         2   // Hold startet
#define EV_ERR_TERMO    3   // FEIL: Termoelement
#define EV_ERR_MAXTEMP  4   // FEIL: Maks temperatur
#define EV_NODSTOPP     5   // Nødstopp
#define EV_FERDIG       6   // Brenning ferdig
#define EV_AVBRYT       7   // Avbrutt av bruker
struct Event { uint16_t sec; uint8_t type; uint16_t temp; };

// ── Webserver ────────────────────────────────────────────────────────────────
#define WEB_PORT        80

// ── PID ──────────────────────────────────────────────────────────────────────
#define PID_KP   2.0f
#define PID_KI   0.005f
#define PID_KD   5.0f

// ── Timing ───────────────────────────────────────────────────────────────────
#define SAMPLE_MS        1000UL
#define RELAY_WINDOW_MS  10000UL
#define RELAY_MIN_ON_MS  1000UL   // kortere pulser droppes – unngår rask SSR-sykling

// ── Sikkerhet ────────────────────────────────────────────────────────────────
#define MAX_TEMP_C       1300.0f
#define TEMP_DEVIATION   10.0f

// ── EEPROM-layout ─────────────────────────────────────────────────────────────
// E-post konfigurasjon
#define EEPROM_API_KEY    32       // 50 bytes – Resend API-nøkkel
#define EEPROM_EMAIL_TO   82       // 50 bytes – mottaker
#define EEPROM_EMAIL_CC   132      // 50 bytes – CC (valgfritt)
#define EEPROM_EMAIL_FROM 182      // 50 bytes – avsender

// 232–291: 60 bytes free (previously Pushover token/user)

// Lagret brenningsrapport (sendes ved neste oppstart om WiFi manglet)
#define EEPROM_PENDING_FLAG   292  // 1 byte : 0x42 = rapport venter
#define EEPROM_PENDING_PROF   293  // 1 byte : profilindeks
#define EEPROM_PENDING_SEC    294  // 4 bytes: total brennetid i sekunder
#define EEPROM_PENDING_MAXT   298  // 2 bytes: maks temperatur
#define EEPROM_PENDING_LCNT   300  // 2 bytes: logCount
#define EEPROM_PENDING_LOG    304  // 480 * 8 bytes = 3840 → slutter på 4144

// Persistent fulllogg (5-min intervall, overlever strømbrudd)  0x44 = gyldig
#define EEPROM_PLOG_FLAG  4144  // 1 byte
#define EEPROM_PLOG_HEAD  4145  // 2 bytes
#define EEPROM_PLOG_LCNT  4147  // 2 bytes
#define EEPROM_PLOG_LOG   4149  // 144 * 8 bytes = 1152 → slutter på 5301

// Hendelseslogg (overlever strømbrudd)  0x45 = gyldig
#define EEPROM_ELOG_FLAG  5301  // 1 byte
#define EEPROM_ELOG_CNT   5302  // 1 byte
#define EEPROM_ELOG_LOG   5303  // 32 * 5 bytes = 160 → slutter på 5463

