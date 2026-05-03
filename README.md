# Moen Kiln

Arduino-based kiln controller for the Evenheat PF3220 ceramic kiln. Controls firing profiles via PID, logs temperature data, and provides a mobile-friendly web interface over WiFi.

## Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | Arduino Uno R4 WiFi |
| Temperature sensor | MAX31855 breakout (K-type thermocouple) |
| SSR | Single-phase solid state relay (adjust to your kiln – tested with 440V AC / 40A) |
| Buttons | 2× momentary push button |

### Wiring

| MAX31855 | Arduino pin |
|----------|-------------|
| VIN | 3.3V |
| GND | GND |
| DO | 12 (MISO) |
| CS | 8 |
| CLK | 13 (SCK) |
| 3VO | — (do not connect) |

| SSR | Arduino pin |
|-----|-------------|
| DC+ | 4 (via 220–470Ω resistor – check SSR minimum trigger current) |
| DC− | GND |

| Button | Arduino pin | Function |
|--------|-------------|----------|
| Emergency stop | 3 | Emergency stop (hardware interrupt) |
| Start | 5 | Start / cancel (hold 1s = Glaze, hold 5s = Bisque) |

## Getting started

### 1. Create your secrets file

Copy `firmware/moen_kiln/config_secrets.h.example` to `firmware/moen_kiln/config_secrets.h` and fill in your values:

```cpp
// config_secrets.h

#define WIFI_SSID        "YourNetwork"
#define WIFI_PASSWORD    "yourpassword"
#define WIFI_SSID2       "BackupNetwork"    // optional fallback
#define WIFI_PASSWORD2   "backuppassword"
#define WIFI_SSID3       "ThirdNetwork"     // optional fallback
#define WIFI_PASSWORD3   "thirdpassword"

#Using Resend.com to send e-mail. Create an account and add your API key below.
#define DEFAULT_API_KEY    "re_your_resend_api_key"
#define DEFAULT_EMAIL_FROM "kiln@yourdomain.com"
#define DEFAULT_EMAIL_TO   "you@youremail.com"
```

> `config_secrets.h` is listed in `.gitignore` and will never be committed.

### 2. Install Arduino libraries

- `WiFiS3` (bundled with Uno R4 WiFi SDK)
- `Adafruit MAX31855`
- `ArduinoJson`
- `Arduino_LED_Matrix` (bundled with Uno R4 WiFi SDK)
- `NTPClient`

### 3. Upload firmware

Open `firmware/moen_kiln/moen_kiln.ino` in the Arduino IDE and upload to your Arduino Uno R4 WiFi.

### 4. Connect

The IP address scrolls across the LED matrix at startup. Open it in your browser — the web interface runs on port 80.

## Features

- **Firing profiles** — pre-defined Glaze and Bisque profiles, editable via web UI
- **PID control** — time-proportional relay control with 10-second window
- **Two-tier logging**
  - Detail log: every 15 seconds, last 2 hours (RAM)
  - Full log: every 5 minutes, entire firing up to 12 hours (RAM + EEPROM)
- **Event log** — timestamped events: segment changes, errors, emergency stops (EEPROM-persisted)
- **CSV export** — download full log or detail log from the web UI
- **Email report** — SVG chart + CSV attachment sent via [Resend](https://resend.com) when firing completes
- **Power-cut recovery** — EEPROM stores state; log survives power loss

## Project structure

```
firmware/
  moen_kiln/                  Main firmware
    moen_kiln.ino
    config.h                  Pin definitions, EEPROM layout, structs
    config_secrets.h          Your credentials (NOT committed – see .gitignore)
    config_secrets.h.example  Template for config_secrets.h
    web_ui.h                  HTML/CSS/JS stored in PROGMEM
    profiles.h                Firing profiles
    led_display.h             LED matrix helpers
    email.h                   Resend.com email integration
  moen_kiln_test/             Minimal test sketch (MAX31855 + relay only)
```

## EEPROM layout

| Address | Size | Content |
|---------|------|---------|
| 32–231 | 200 B | Email configuration |
| 292–303 | 12 B | Pending report metadata |
| 304–4143 | 3840 B | Pending report log (480 × 8 bytes) |
| 4144–5300 | 1157 B | Full firing log (144 × 8 bytes + metadata) |
| 5301–5462 | 162 B | Event log (32 × 5 bytes + metadata) |

Total: 5463 / 8192 bytes used.
