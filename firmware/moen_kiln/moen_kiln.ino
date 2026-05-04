// Moen Kiln – Arduino Uno R4 WiFi
// April 2026

#include <SPI.h>
#include <Adafruit_MAX31855.h>
#include <WiFiS3.h>
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"
#include "config.h"
#include "profiles.h"
#include "web_ui.h"
#include "led_display.h"
#include "email.h"

void triggerAlarm(const __FlashStringHelper* alarmMsg, uint8_t evType = EV_ERR_TERMO);

// ── Hardware ──────────────────────────────────────────────────────────────────
Adafruit_MAX31855 tc(PIN_CS_THERMO);
ArduinoLEDMatrix  ledMatrix;

// ── State machine ─────────────────────────────────────────────────────────────
enum State { IDLE, RAMPING, HOLDING, FREE_COOL, COMPLETE, ESTOPPED, ERR };
State kilnState = IDLE;

// ── Sensor ────────────────────────────────────────────────────────────────────
float currentTemp = 0.0f;
float prevTemp    = 0.0f;   // forrige avlesning for delta-beregning
float peakTemp    = 0.0f;   // høyeste temperatur i denne brenningen

// ── Aktiv profil ──────────────────────────────────────────────────────────────
const Profile*  profile      = nullptr;
uint8_t         segIdx       = 0;
float           segStartTemp = 0.0f;
unsigned long   segStartMs   = 0;
unsigned long   holdStartMs  = 0;

// ── PID ───────────────────────────────────────────────────────────────────────
float setpoint   = 0.0f;
float pidOutput  = 0.0f;
float pidI       = 0.0f;
float pidLastErr = 0.0f;
unsigned long pidLastMs = 0;

// ── Relay-vindu ───────────────────────────────────────────────────────────────
unsigned long relayWinMs = 0;

// ── Timing ────────────────────────────────────────────────────────────────────
unsigned long lastSampleMs = 0;

// ── Nødstopp-interrupt ────────────────────────────────────────────────────────
volatile bool          estopFlag      = false;
volatile unsigned long estopLastMs    = 0;
bool                   estopAnnounced = false;

void onEstop() {
  unsigned long now = millis();
  if (now - estopLastMs < 300) return;
  estopLastMs = now;
  digitalWrite(PIN_RELAY, LOW);
  estopFlag = true;
}

// Startknapp-interrupt – setter flagg umiddelbart uansett hva loop gjør
volatile bool btnStartFlag = false;
void onBtnStart() { btnStartFlag = true; }

// ── WiFi og webserver ─────────────────────────────────────────────────────────
WiFiServer httpServer(WEB_PORT);

// ── Data-logg ─────────────────────────────────────────────────────────────────
DataPoint     logBuf[LOG_SIZE];          // detaljlogg: 15 sek, siste 2 timer (RAM)
uint16_t      logHead  = 0;
uint16_t      logCount = 0;
unsigned long firingStartMs  = 0;
unsigned long lastLogMs      = 0;

DataPoint     fullLogBuf[FULL_LOG_SIZE]; // fulllogg: 5 min, hele brenningen (RAM + EEPROM)
uint16_t      fullLogHead  = 0;
uint16_t      fullLogCount = 0;
unsigned long lastFullLogMs = 0;

Event         eventLog[EVENT_LOG_SIZE];
uint8_t       eventCount = 0;

// ── LED-display ───────────────────────────────────────────────────────────────
uint8_t       displayScreen   = 0;    // 0=navn 1=temp 2=P:hh:mm 3=T:hh:mm
unsigned long displayScreenMs = 0;
bool          matrixDone      = false;
bool    showingIP        = true;  // vis IP-adresse til første knapp trykkes
bool    serverReady      = false; // satt etter at HTTP-server er oppe
bool    reportSent       = false; // hindrer dobbel utsending per brenning
bool    manualRelay      = false; // manuell reléstyring i IDLE
uint8_t firingId         = 0;    // økes ved ny brenning – brukes av GUI til å tømme graf
bool    cancelHeld       = false; // startknapp holdes inne under brenning
unsigned long stoppedMs  = 0;    // tidsstempel for "stanset"-bekreftelse på display
bool    sensorMissing    = false; // MAX31855 ikke tilkoblet eller feiler
unsigned long testTimeoutMs  = 0; // non-zero under Config Test: fires at firingStartMs + 4 h
unsigned long lastWifiRetryMs = 0; // last time we attempted a reconnect


// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  // Relay LOW first — pin floats high-Z before pinMode, SSR must not fire
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  // matrixBegin() wakes the ESP32-S3; call it before Serial.begin() so
  // boot bytes are discarded rather than forwarded to the Serial Monitor
  matrixBegin();
  delay(1500);

  Serial.begin(115200);
  Serial.println(F("DBG1"));

  pinMode(LEDR, OUTPUT); digitalWrite(LEDR, HIGH);  // aktiv lav – start av
  pinMode(LEDG, OUTPUT); digitalWrite(LEDG, HIGH);
  pinMode(LEDB, OUTPUT); digitalWrite(LEDB, HIGH);
  Serial.println(F("DBG2"));

  pinMode(PIN_ESTOP, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ESTOP), onEstop, FALLING);
  Serial.println(F("DBG3"));

  pinMode(PIN_BTN_START, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_START), onBtnStart, FALLING);
  Serial.println(F("DBG4"));

  Serial.println(F("=== Moen Kiln ==="));
  Serial.print(F("Pins: relay=")); Serial.print(PIN_RELAY);
  Serial.print(F(" estop=")); Serial.print(PIN_ESTOP);
  Serial.print(F(" start=")); Serial.println(PIN_BTN_START);

  if (!tc.begin()) {
    Serial.println(F("WARNING: No MAX31855 connected"));
    sensorMissing = true;
  } else {
    Serial.println(F("MAX31855 OK"));
    delay(200);
    readTemp();
    if (sensorMissing) {
      Serial.println(F("WARNING: No MAX31855 connected"));
    } else {
      Serial.print(F("Temp: ")); Serial.print(currentTemp, 1); Serial.println(F("C"));
    }
  }

  loadPersistentLog();
  setupWiFi();

  relayWinMs = millis();
  pidLastMs  = millis();
  printHelp();

  if (WiFi.status() == WL_CONNECTED) loadAndSendPendingReport();
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  if (btnStartFlag) {   // satt av interrupt – umiddelbar respons uavhengig av HTTP-blokkering
    btnStartFlag = false;
    matrixClear();
    showingIP = false;
  }
  handleSerial();
  maybeReconnectWiFi();
  handleHTTP();
  checkStartButton();

  if (estopFlag) {
    handleEstop();
    updateLED();
    return;
  }

  unsigned long now = millis();
  if (now - lastSampleMs >= SAMPLE_MS) {
    prevTemp     = currentTemp;
    lastSampleMs = now;
    readTemp();
    tick();
    printStatus();
    logData();
    updateLED();

  }

  updateRelay();
  updateStatusLED();
}

// ── Temperaturlesing ──────────────────────────────────────────────────────────
void readTemp() {
  double t = tc.readCelsius();
  sensorMissing = isnan(t);
  if (sensorMissing) {
    if (kilnState != IDLE) triggerAlarm(F("Thermocouple error"));
    return;
  }
  currentTemp = (float)t;
  if (currentTemp > peakTemp) peakTemp = currentTemp;
}

// ── Hoved-tick ────────────────────────────────────────────────────────────────
void tick() {
  if (testTimeoutMs > 0 && millis() >= testTimeoutMs &&
      (kilnState == RAMPING || kilnState == HOLDING || kilnState == FREE_COOL)) {
    testTimeoutMs = 0;
    Serial.println(F("=== CONFIG TEST: 4h timeout – sending report ==="));
    logEvent(EV_FERDIG);
    kilnState = COMPLETE; pidOutput = 0;
    maybeSendReport();
    return;
  }
  switch (kilnState) {
    case RAMPING:   tickRamping();  break;
    case HOLDING:   tickHolding();  break;
    case FREE_COOL: tickFreeCool(); break;
    default:        pidOutput = 0;  break;
  }
}

void tickRamping() {
  const Segment& seg = profile->segments[segIdx];
  float elapsed = (millis() - segStartMs) / 1000.0f;
  bool  rampUp  = (seg.targetTemp >= segStartTemp);
  float rate    = seg.ratePerHour;

  if (rampUp) {
    setpoint = segStartTemp + (rate / 3600.0f) * elapsed;
    setpoint = min(setpoint, seg.targetTemp);
  } else {
    setpoint = segStartTemp - (rate / 3600.0f) * elapsed;
    setpoint = max(setpoint, seg.targetTemp);
  }

  if (setpoint == seg.targetTemp) {
    bool atTarget = rampUp ? (currentTemp >= seg.targetTemp - 2.0f)
                           : (currentTemp <= seg.targetTemp + 2.0f);
    if (atTarget) {
      if (seg.holdMin > 0) {
        kilnState = HOLDING; holdStartMs = millis();
        logEvent(EV_HOLD);
        Serial.print(F("Hold: ")); Serial.println(seg.name);
      } else { nextSegment(); }
      return;
    }
  }

  pidOutput = (!rampUp && currentTemp <= setpoint) ? 0 : computePID(setpoint, currentTemp);
  checkAlarms();
}

void tickHolding() {
  const Segment& seg = profile->segments[segIdx];
  setpoint  = seg.targetTemp;
  pidOutput = computePID(setpoint, currentTemp);

  unsigned long holdMs = (unsigned long)seg.holdMin * 60000UL;
  if (millis() - holdStartMs >= holdMs) nextSegment();
  checkAlarms();
}

void tickFreeCool() {
  pidOutput = 0;
  if (currentTemp <= profile->segments[segIdx].targetTemp + 5.0f) nextSegment();
}

void savePendingReport() {
  unsigned long totalSec = (firingStartMs > 0) ? (millis()-firingStartMs)/1000UL : 0;
  uint16_t maxT = 0;
  for (uint16_t i = 0; i < logCount; i++) maxT = max(maxT, logBuf[i].temp);
  uint8_t profIdx = 0;
  for (uint8_t i = 0; i < PROFILE_COUNT; i++) if (profile == &PROFILES[i]) profIdx = i;

  // Skriv metadata
  EEPROM.update(EEPROM_PENDING_PROF, profIdx);
  EEPROM.update(EEPROM_PENDING_SEC+0, (totalSec >> 24) & 0xFF);
  EEPROM.update(EEPROM_PENDING_SEC+1, (totalSec >> 16) & 0xFF);
  EEPROM.update(EEPROM_PENDING_SEC+2, (totalSec >>  8) & 0xFF);
  EEPROM.update(EEPROM_PENDING_SEC+3, (totalSec      ) & 0xFF);
  EEPROM.update(EEPROM_PENDING_MAXT+0, (maxT >> 8) & 0xFF);
  EEPROM.update(EEPROM_PENDING_MAXT+1, (maxT     ) & 0xFF);
  EEPROM.update(EEPROM_PENDING_LCNT+0, (logCount >> 8) & 0xFF);
  EEPROM.update(EEPROM_PENDING_LCNT+1, (logCount     ) & 0xFF);

  // Skriv logg kronologisk
  uint16_t start = (logCount == LOG_SIZE) ? logHead % LOG_SIZE : 0;
  for (uint16_t i = 0; i < logCount; i++) {
    DataPoint dp = logBuf[(start + i) % LOG_SIZE];
    EEPROM.put(EEPROM_PENDING_LOG + (int)i * (int)sizeof(DataPoint), dp);
  }

  EEPROM.update(EEPROM_PENDING_FLAG, 0x42);  // sett flagg til slutt
  Serial.println(F("Email: report saved to EEPROM"));
}

void loadAndSendPendingReport() {
  if (EEPROM.read(EEPROM_PENDING_FLAG) != 0x42) { Serial.println(F("Email: no pending report")); return; }

  uint8_t profIdx = EEPROM.read(EEPROM_PENDING_PROF);
  uint16_t lcnt = ((uint16_t)EEPROM.read(EEPROM_PENDING_LCNT+0) << 8)
                | (uint16_t)EEPROM.read(EEPROM_PENDING_LCNT+1);
  unsigned long tsec = (unsigned long)EEPROM.read(EEPROM_PENDING_SEC+0) << 24
                     | (unsigned long)EEPROM.read(EEPROM_PENDING_SEC+1) << 16
                     | (unsigned long)EEPROM.read(EEPROM_PENDING_SEC+2) <<  8
                     | (unsigned long)EEPROM.read(EEPROM_PENDING_SEC+3);

  // Sanity: ugyldig data fra tilfeldig EEPROM-innhold → slett og avbryt
  if (profIdx >= PROFILE_COUNT || lcnt == 0 || lcnt > LOG_SIZE || tsec == 0 || tsec > 172800UL) {
    Serial.println(F("Email: invalid EEPROM data, clearing flag"));
    EEPROM.update(EEPROM_PENDING_FLAG, 0);
    return;
  }

  Serial.println(F("Email: sending saved report from EEPROM..."));
  EEPROM.update(EEPROM_PENDING_FLAG, 0);  // slett flagg FØR sending

  profile   = &PROFILES[profIdx];
  logCount  = lcnt;
  logHead   = lcnt;
  for (uint16_t i = 0; i < lcnt; i++)
    EEPROM.get(EEPROM_PENDING_LOG + (int)i * (int)sizeof(DataPoint), logBuf[i]);

  firingStartMs = 0;
  sendFiringReport(tsec);
  profile = nullptr; logHead = 0; logCount = 0;
}

void maybeSendReport() {
  if (reportSent || firingStartMs == 0) return;
  reportSent = true;
  if (WiFi.status() != WL_CONNECTED) {
    savePendingReport();
    return;
  }
  sendFiringReport();
}

void nextSegment() {
  segIdx++;
  if (segIdx >= profile->segCount) {
    kilnState = COMPLETE; pidOutput = 0;
    Serial.println(F("=== FIRING COMPLETE ==="));
    logEvent(EV_FERDIG);
    maybeSendReport();
    return;
  }
  logEvent(EV_SEGMENT);
  startSegment();
}

void startSegment() {
  const Segment& seg = profile->segments[segIdx];
  segStartTemp = currentTemp;
  segStartMs   = millis();
  setpoint = segStartTemp;
  Serial.print(F("Segment: ")); Serial.print(seg.name);
  Serial.print(F("  Target: ")); Serial.print(seg.targetTemp, 0); Serial.println(F("C"));
  kilnState = (seg.ratePerHour == 0) ? FREE_COOL : RAMPING;
  pidOutput = 0;
}

// ── PID ───────────────────────────────────────────────────────────────────────
float computePID(float sp, float pv) {
  unsigned long now = millis();
  float dt = (now - pidLastMs) / 1000.0f;
  if (dt <= 0.0f || dt > 5.0f) { pidLastMs = now; return pidOutput; }
  float err = sp - pv;
  float p   = PID_KP * err;
  pidI += err * dt;
  pidI = constrain(pidI, -100.0f / (PID_KI + 0.0001f), 100.0f / (PID_KI + 0.0001f));
  float i = PID_KI * pidI;
  float d = PID_KD * (err - pidLastErr) / dt;
  pidLastErr = err; pidLastMs = now;
  return constrain(p + i + d, 0.0f, 100.0f);
}

// ── Relay: tidsproporsjonell ───────────────────────────────────────────────────
void updateRelay() {
  static bool lastRelay = false;
  bool next;
  if (estopFlag || kilnState == ESTOPPED || kilnState == ERR
               || kilnState == COMPLETE || kilnState == FREE_COOL) {
    next = false;
  } else if (kilnState == IDLE) {
    next = manualRelay;
  } else {
    unsigned long now = millis();
    if (now - relayWinMs >= RELAY_WINDOW_MS) relayWinMs = now;
    unsigned long onTime = (unsigned long)((pidOutput / 100.0f) * RELAY_WINDOW_MS);
    if (onTime < RELAY_MIN_ON_MS) onTime = 0;
    next = (now - relayWinMs < onTime);
  }
  if (next != lastRelay) {
    Serial.print(F("SSR: ")); Serial.println(next ? F("ON") : F("OFF"));
    lastRelay = next;
  }
  digitalWrite(PIN_RELAY, next ? HIGH : LOW);
}

// ── Status-LED: rød puls under brenning ───────────────────────────────────────
void updateStatusLED() {
  bool active = (kilnState == RAMPING || kilnState == HOLDING || kilnState == FREE_COOL);
  if (!active) { digitalWrite(LEDR, HIGH); return; }   // av
  digitalWrite(LEDR, (millis() / 500) % 2 == 0 ? LOW : HIGH);  // 1 Hz puls
}

// ── Alarmer ───────────────────────────────────────────────────────────────────
void checkAlarms() {
  if (currentTemp > MAX_TEMP_C) triggerAlarm(F("Max temperature exceeded"), EV_ERR_MAXTEMP);
}

void triggerAlarm(const __FlashStringHelper* alarmMsg, uint8_t evType) {
  digitalWrite(PIN_RELAY, LOW); estopFlag = true; kilnState = ERR;
  logEvent(evType);
  Serial.print(F("ALARM: ")); Serial.println(alarmMsg);
  maybeSendReport();
}

// ── Nødstopp ──────────────────────────────────────────────────────────────────
void handleEstop() {
  if (!estopAnnounced) {
    estopAnnounced = true; if (kilnState != ERR) kilnState = ESTOPPED; pidOutput = 0;
    logEvent(EV_NODSTOPP);
    Serial.println(F("ESTOP – type 'reset'"));
  }
}


// ── Data-logg ─────────────────────────────────────────────────────────────────
void logEvent(uint8_t type) {
  if (eventCount >= EVENT_LOG_SIZE) return;
  Event& ev = eventLog[eventCount];
  ev.sec  = (firingStartMs > 0) ? (uint16_t)min((millis()-firingStartMs)/1000UL, 65535UL) : 0;
  ev.type = type;
  ev.temp = (uint16_t)(currentTemp + 0.5f);
  EEPROM.put(EEPROM_ELOG_LOG + eventCount * (int)sizeof(Event), ev);
  eventCount++;
  EEPROM.update(EEPROM_ELOG_CNT, eventCount);
  EEPROM.update(EEPROM_ELOG_FLAG, 0x45);
}

const char* eventStr(uint8_t t) {
  switch(t) {
    case EV_START:      return "Firing started";
    case EV_SEGMENT:    return "New segment";
    case EV_HOLD:       return "Hold started";
    case EV_ERR_TERMO:  return "ERR: Thermocouple error";
    case EV_ERR_MAXTEMP:return "ERR: Max temperature exceeded";
    case EV_NODSTOPP:   return "Emergency stop";
    case EV_FERDIG:     return "Firing complete";
    case EV_AVBRYT:     return "Cancelled by user";
    default:            return "Unknown";
  }
}

void writeFullLogPoint(unsigned long now) {
  uint16_t idx = fullLogHead % FULL_LOG_SIZE;
  DataPoint& dp = fullLogBuf[idx];
  dp.sec    = (uint16_t)min((now - firingStartMs) / 1000UL, 65535UL);
  dp.temp   = (uint16_t)(currentTemp + 0.5f);
  dp.sp     = (uint16_t)(setpoint + 0.5f);
  dp.pid    = (uint8_t)constrain((int)(pidOutput + 0.5f), 0, 100);
  dp.segIdx = (uint8_t)((digitalRead(PIN_RELAY) ? 0x80 : 0) | (segIdx & 0x7F));
  fullLogHead++;
  if (fullLogCount < FULL_LOG_SIZE) fullLogCount++;
  EEPROM.put(EEPROM_PLOG_LOG + (int)idx * (int)sizeof(DataPoint), dp);
  EEPROM.update(EEPROM_PLOG_HEAD,   (fullLogHead >> 8) & 0xFF);
  EEPROM.update(EEPROM_PLOG_HEAD+1, fullLogHead & 0xFF);
  EEPROM.update(EEPROM_PLOG_LCNT,   (fullLogCount >> 8) & 0xFF);
  EEPROM.update(EEPROM_PLOG_LCNT+1, fullLogCount & 0xFF);
  EEPROM.update(EEPROM_PLOG_FLAG, 0x44);
}

void logData() {
  if (kilnState != RAMPING && kilnState != HOLDING && kilnState != FREE_COOL) return;
  unsigned long now = millis();

  // Detaljlogg: 15 sek, RAM-buffer (siste 2 timer)
  if (now - lastLogMs >= LOG_INTERVAL_MS) {
    lastLogMs = now;
    uint16_t idx = logHead % LOG_SIZE;
    DataPoint& dp = logBuf[idx];
    dp.sec    = (uint16_t)min((now - firingStartMs) / 1000UL, 65535UL);
    dp.temp   = (uint16_t)(currentTemp + 0.5f);
    dp.sp     = (uint16_t)(setpoint + 0.5f);
    dp.pid    = (uint8_t)constrain((int)(pidOutput + 0.5f), 0, 100);
    dp.segIdx = (uint8_t)((digitalRead(PIN_RELAY) ? 0x80 : 0) | (segIdx & 0x7F));
    logHead++;
    if (logCount < LOG_SIZE) logCount++;
  }

  // Fulllogg: 5 min, RAM + EEPROM (hele brenningen)
  if (now - lastFullLogMs >= FULL_LOG_INTERVAL_MS) {
    lastFullLogMs = now;
    writeFullLogPoint(now);
  }
}

void loadPersistentLog() {
  if (EEPROM.read(EEPROM_PLOG_FLAG) != 0x44) { Serial.println(F("EEPROM: no log")); return; }
  uint16_t cnt = ((uint16_t)EEPROM.read(EEPROM_PLOG_LCNT)   << 8) | EEPROM.read(EEPROM_PLOG_LCNT+1);
  uint16_t hd  = ((uint16_t)EEPROM.read(EEPROM_PLOG_HEAD)   << 8) | EEPROM.read(EEPROM_PLOG_HEAD+1);
  if (cnt == 0 || cnt > FULL_LOG_SIZE) return;
  for (uint16_t i = 0; i < cnt; i++)
    EEPROM.get(EEPROM_PLOG_LOG + (int)(i % FULL_LOG_SIZE) * (int)sizeof(DataPoint), fullLogBuf[i]);
  fullLogCount = cnt; fullLogHead = hd;
  Serial.print(F("EEPROM: loaded ")); Serial.print(cnt); Serial.println(F(" full-log entries"));

  if (EEPROM.read(EEPROM_ELOG_FLAG) == 0x45) {
    uint8_t ecnt = EEPROM.read(EEPROM_ELOG_CNT);
    if (ecnt <= EVENT_LOG_SIZE) {
      for (uint8_t i = 0; i < ecnt; i++)
        EEPROM.get(EEPROM_ELOG_LOG + i * (int)sizeof(Event), eventLog[i]);
      eventCount = ecnt;
      Serial.print(F("EEPROM: loaded ")); Serial.print(ecnt); Serial.println(F(" events"));
    }
  }
}

// ── Segment-fremdrift ─────────────────────────────────────────────────────────
float getSegmentProgress() {
  if (!profile) return 0.0f;
  const Segment& seg = profile->segments[segIdx];
  if (kilnState == HOLDING) {
    unsigned long holdMs = (unsigned long)seg.holdMin * 60000UL;
    return constrain((float)(millis() - holdStartMs) / (float)holdMs, 0.0f, 1.0f);
  }
  if (kilnState == RAMPING) {
    float range = abs(seg.targetTemp - segStartTemp);
    if (range < 1.0f) return 1.0f;
    return constrain(abs(currentTemp - segStartTemp) / range, 0.0f, 1.0f);
  }
  return 1.0f;
}

long getTotalRemaining() {
  if (!profile || kilnState == IDLE || kilnState == COMPLETE) return 0;
  long total = getSegmentRemaining();
  float prev = profile->segments[segIdx].targetTemp;
  for (uint8_t i = segIdx + 1; i < profile->segCount; i++) {
    const Segment& seg = profile->segments[i];
    if (seg.ratePerHour > 0)
      total += (long)(fabs(seg.targetTemp - prev) / seg.ratePerHour * 3600.0f);
    total += (long)seg.holdMin * 60L;
    prev = seg.targetTemp;
  }
  return total;
}

long getSegmentRemaining() {
  if (!profile) return 0;
  const Segment& seg = profile->segments[segIdx];
  if (kilnState == HOLDING) {
    unsigned long holdMs = (unsigned long)seg.holdMin * 60000UL;
    long elapsed = (long)(millis() - holdStartMs);
    return max(0L, (long)(holdMs - elapsed) / 1000L);
  }
  if (kilnState == RAMPING) {
    float rate = seg.ratePerHour;
    if (rate < 1.0f) return 0;
    float remaining = abs(seg.targetTemp - currentTemp);
    return (long)(remaining / rate * 3600.0f);
  }
  return 0;
}

// ── LED-hjelper: tid → 4 tegn (f.eks. "1H45", "45M", "12H3") ─────────────────
void formatTime4(char* out, long sec) {
  int h = (int)(sec / 3600L);
  int m = (int)((sec % 3600L) / 60L);
  if (h >= 10)
    snprintf(out, 5, "%d:%d", h, m / 10);   // "12:3" = 12 t, 3x min
  else
    snprintf(out, 5, "%d:%02d", h, m);       // "1:45" eller "0:45"
}

// ── LED-hjelper: segmentnavn → 4 tegn (f.eks. "GLA1", "BIS2") ────────────────
static char _up(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

void formatSegName4(char* out, const char* name) {
  char cmp[16] = {};
  int j = 0;
  for (int i = 0; name[i] && j < 15; i++)
    if (name[i] != ' ') cmp[j++] = name[i];
  int n = min(j, 4);
  for (int i = 0; i < n; i++) out[i] = _up(cmp[i]);
  if (j > 4) out[3] = _up(cmp[j - 1]);
  out[n] = 0;
}

// ── IP-display ved oppstart ───────────────────────────────────────────────────
void updateIPDisplay() {
  if (!serverReady) { matrixClear(); return; }
  IPAddress ip = WiFi.localIP();
  char buf[16];
  snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  matrixText(buf);
}

// ── LED-display ───────────────────────────────────────────────────────────────

void checkStartButton() {
  static bool          last       = HIGH;
  static unsigned long holdStart  = 0;
  static unsigned long lastPress  = 0;
  static bool          glazeArmed = false;

  bool btn = digitalRead(PIN_BTN_START);
  unsigned long now = millis();

  if (btn == HIGH) {
    // Knapp sluppet
    bool wasCancelHeld = cancelHeld;
    if (cancelHeld) cancelHeld = false;
    if (glazeArmed && kilnState == IDLE) {
      // Sluppet mellom 1 s og 5 s → start Glaze
      glazeArmed = false; holdStart = 0;
      showingIP = false;
      if (!sensorMissing) startProfile(&PROFILES[0]);
      last = HIGH;
      return;
    }
    glazeArmed = false;
    holdStart = 0;
    last = HIGH;
    // Oppdater display umiddelbart etter slipp – ikke vent på neste sekund-tick
    if (wasCancelHeld) updateLED();
    return;
  }

  if (last == HIGH) {
    if (now - lastPress < 50) { last = LOW; return; }
    lastPress = now;
    holdStart = now;
    matrixClear();  // tøm skjermen umiddelbart ved ethvert trykk

    if (kilnState == IDLE) {
      Serial.println(F("Button: pressed (hold 2s=Glaze, 5s=ConfigTest)"));
      showingIP = false;
    } else if (kilnState == RAMPING || kilnState == HOLDING || kilnState == FREE_COOL) {
      displayScreen = (displayScreen + 1) % 4;
      displayScreenMs = now;
      updateLED();
    }
  }

  // Hold i IDLE: 2 s = Glaze klar, 5 s = Config Test
  if (holdStart > 0 && kilnState == IDLE) {
    unsigned long held = now - holdStart;
    cancelHeld = true;
    drawCancelCountdown(min(1.0f, (float)held / 5000.0f));
    if (!glazeArmed && held >= 2000) {
      glazeArmed = true;
    }
    if (held >= 5000) {
      cancelHeld = false; glazeArmed = false; holdStart = 0;
      showingIP = false;
      if (!sensorMissing) startProfile(&PROFILES[2]);  // Config Test
    }
  }

  // Hold under aktiv brenning: vis nedtelling etter 500 ms, avbryt ved 5 s
  if (holdStart > 0 && (kilnState == RAMPING || kilnState == HOLDING || kilnState == FREE_COOL)) {
    unsigned long held = now - holdStart;
    if (held >= 500) {
      cancelHeld = true;
      drawCancelCountdown(min(1.0f, (float)(held - 500) / 4500.0f));
      if (held >= 5000) {
        cancelHeld = false; holdStart = 0;
        logEvent(EV_AVBRYT);
        maybeSendReport();
        estopFlag = false; estopAnnounced = false;
        kilnState = IDLE; pidOutput = 0; pidI = 0; setpoint = 0;
        showingIP = false;
        stoppedMs = millis();
        matrixWord4("STOP");
        Serial.println(F("Cancelled via button"));
      }
    }
  }

  last = LOW;
}

void updateLED() {
  if (cancelHeld) return;

  static bool blinkOn = false;
  blinkOn = !blinkOn;

  if (kilnState == ESTOPPED || kilnState == ERR) {
    if (blinkOn) matrixWord4("ERR!"); else matrixClear();
    return;
  }

  if (kilnState == COMPLETE) {
    if (!matrixDone) { matrixDone = true; matrixWord4("DONE"); }
    return;
  }

  unsigned long now = millis();

  // "STOP"-bekreftelse: hold i 5 sekunder, så vis grader
  if (stoppedMs > 0) {
    if (now - stoppedMs < 5000) return;
    stoppedMs = 0;
    displayScreen = 1;
    displayScreenMs = now;
  }

  // IP-adresse ved oppstart: vis frem til dismissed
  if (showingIP && kilnState == IDLE) {
    updateIPDisplay();
    return;
  }

  // Alltid fall tilbake til grader etter 15 sekunder
  if (displayScreen != 1 && now - displayScreenMs >= 15000UL) {
    displayScreen = 1;
    displayScreenMs = now;
  }

  bool firingDot = blinkOn && (kilnState == RAMPING || kilnState == HOLDING || kilnState == FREE_COOL);

  char buf[5];
  switch (displayScreen) {
    case 0:
      if (profile && kilnState != IDLE) {
        formatSegName4(buf, profile->segments[segIdx].name);
        matrixWord4(buf, firingDot);
        break;
      }
      // fall through til grader om IDLE eller ingen profil
    case 1:
      matrixTemp((int)(currentTemp + 0.5f), firingDot);
      break;
    case 2: {
      formatTime4(buf, getSegmentRemaining());
      matrixWord4(buf, firingDot);
      break;
    }
    case 3: {
      formatTime4(buf, getTotalRemaining());
      matrixWord4(buf, firingDot);
      break;
    }
  }
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
void setupWiFi() {
  if (String(WIFI_SSID) == "ditt_nett") {
    Serial.println(F("WiFi: not configured"));
    return;
  }
  Serial.print(F("WiFi: ")); Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print(F("WiFi: trying ")); Serial.println(WIFI_SSID2);
    WiFi.begin(WIFI_SSID2, WIFI_PASSWORD2);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500); Serial.print('.');
    }
    Serial.println();
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print(F("WiFi: trying ")); Serial.println(WIFI_SSID3);
    WiFi.begin(WIFI_SSID3, WIFI_PASSWORD3);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500); Serial.print('.');
    }
    Serial.println();
  }
  if (WiFi.status() == WL_CONNECTED) {
    httpServer.begin();
    serverReady = true;
    Serial.print(F("IP: ")); Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("WiFi failed"));
  }
}

// ── WiFi reconnect (prøver hvert 60 sek hvis tilkoblingen er borte) ───────────
void maybeReconnectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastWifiRetryMs < 60000UL) return;
  lastWifiRetryMs = now;
  Serial.println(F("WiFi: lost – reconnecting..."));
  const char* ssids[]  = { WIFI_SSID,  WIFI_SSID2,  WIFI_SSID3  };
  const char* passes[] = { WIFI_PASSWORD, WIFI_PASSWORD2, WIFI_PASSWORD3 };
  for (uint8_t i = 0; i < 3; i++) {
    WiFi.begin(ssids[i], passes[i]);
    for (int j = 0; j < 10 && WiFi.status() != WL_CONNECTED; j++) delay(500);
    if (WiFi.status() == WL_CONNECTED) {
      if (!serverReady) { httpServer.begin(); serverReady = true; }
      Serial.print(F("WiFi: reconnected – IP ")); Serial.println(WiFi.localIP());
      loadAndSendPendingReport();
      return;
    }
  }
  Serial.println(F("WiFi: reconnect failed"));
}

// ── HTTP server ───────────────────────────────────────────────────────────────
void handleHTTP() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClient client = httpServer.available();
  if (!client) return;

  String req = "";
  int contentLen = 0;
  unsigned long t0 = millis();

  while (client.connected() && millis() - t0 < 300) {
    if (!client.available()) continue;
    String line = client.readStringUntil('\n'); line.trim();
    if (req.isEmpty()) req = line;
    else if (line.startsWith("Content-Length:")) contentLen = line.substring(16).toInt();
    else if (line.isEmpty()) break;
  }

  String body = "";
  for (int i = 0; i < contentLen && client.connected() && millis() - t0 < 500; i++) {
    while (!client.available() && millis() - t0 < 500);
    if (client.available()) body += (char)client.read();
  }

  if (req.startsWith("POST ")) showingIP = false;

  if (req.startsWith("GET / ")) {
    httpOK(client, "text/html");
    client.print(WEB_UI);

  } else if (req.startsWith("GET /api/status")) {
    float progress = getSegmentProgress();
    float delta    = currentTemp - prevTemp;
    long  rem      = getSegmentRemaining();

    httpOK(client, "application/json");
    client.print(F("{\"state\":\"")); client.print(stateStr()); client.print('"');
    client.print(F(",\"temp\":")); client.print(currentTemp, 1);
    client.print(F(",\"setpoint\":")); client.print(setpoint, 1);
    client.print(F(",\"pid\":")); client.print(pidOutput, 1);
    client.print(F(",\"delta\":")); client.print(delta, 3);
    client.print(F(",\"progress\":")); client.print(progress, 3);
    client.print(F(",\"remaining\":")); client.print(rem);
    client.print(F(",\"totalRemaining\":")); client.print(getTotalRemaining());
    client.print(F(",\"showingIP\":")); client.print(showingIP ? "true" : "false");
    client.print(F(",\"manualRelay\":")); client.print(manualRelay ? "true" : "false");
    if (firingStartMs > 0) { client.print(F(",\"elapsed\":")); client.print((millis() - firingStartMs) / 1000UL); }
    client.print(F(",\"firingId\":")); client.print(firingId);
    client.print(F(",\"sensorMissing\":")); client.print(sensorMissing ? "true" : "false");
    if (profile) {
      client.print(F(",\"profile\":\"")); client.print(profile->name); client.print('"');
      client.print(F(",\"segIdx\":")); client.print(segIdx);
      client.print(F(",\"segCount\":")); client.print(profile->segCount);
      client.print(F(",\"segNames\":["));
      for (uint8_t i = 0; i < profile->segCount; i++) {
        if (i) client.print(',');
        client.print('"'); client.print(profile->segments[i].name); client.print('"');
      }
      client.print(F("],\"segs\":["));
      for (uint8_t i = 0; i < profile->segCount; i++) {
        const Segment& s = profile->segments[i];
        if (i) client.print(',');
        client.print(F("{\"t\":")); client.print((int)s.targetTemp);
        client.print(F(",\"r\":")); client.print((int)s.ratePerHour);
        client.print(F(",\"h\":")); client.print(s.holdMin);
        client.print('}');
      }
      client.print(']');
      if (kilnState != IDLE && kilnState != COMPLETE) {
        client.print(F(",\"segment\":\"")); client.print(profile->segments[segIdx].name); client.print('"');
        client.print(F(",\"segTarget\":")); client.print((int)profile->segments[segIdx].targetTemp);
      }
    }
    client.print('}');

  } else if (req.startsWith("GET /api/detail.csv")) {
    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: text/csv"));
    client.println(F("Content-Disposition: attachment; filename=\"detail-log.csv\""));
    client.println(F("Connection: close"));
    client.println();
    client.println(F("sec,temp,setpoint,relay,duty_pct,seg_idx"));
    uint16_t dstart = (logCount == LOG_SIZE) ? logHead % LOG_SIZE : 0;
    for (uint16_t i = 0; i < logCount; i++) {
      const DataPoint& dp = logBuf[(dstart + i) % LOG_SIZE];
      client.print(dp.sec); client.print(',');
      client.print(dp.temp); client.print(',');
      client.print(dp.sp); client.print(',');
      client.print((dp.segIdx & 0x80) ? 1 : 0); client.print(',');
      client.print(dp.pid); client.print(',');
      client.println(dp.segIdx & 0x7F);
    }

  } else if (req.startsWith("GET /api/log.csv")) {
    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: text/csv"));
    client.println(F("Content-Disposition: attachment; filename=\"firing.csv\""));
    client.println(F("Connection: close"));
    client.println();
    client.println(F("sec,temp,setpoint,relay,duty_pct,seg_idx,event"));
    uint16_t fstart = (fullLogCount == FULL_LOG_SIZE) ? fullLogHead % FULL_LOG_SIZE : 0;
    uint8_t  ei = 0;
    for (uint16_t i = 0; i < fullLogCount; i++) {
      const DataPoint& dp = fullLogBuf[(fstart + i) % FULL_LOG_SIZE];
      // Legg inn hendelser som skjedde før dette tidspunktet
      while (ei < eventCount && eventLog[ei].sec <= dp.sec) {
        client.print(eventLog[ei].sec); client.print(F(","));
        client.print(eventLog[ei].temp); client.print(F(",,,,,"));
        client.println(eventStr(eventLog[ei].type));
        ei++;
      }
      client.print(dp.sec); client.print(',');
      client.print(dp.temp); client.print(',');
      client.print(dp.sp); client.print(',');
      client.print((dp.segIdx & 0x80) ? 1 : 0); client.print(',');
      client.print(dp.pid); client.print(',');
      client.print(dp.segIdx & 0x7F); client.println(',');
    }
    // Resterende hendelser etter siste datapunkt
    while (ei < eventCount) {
      client.print(eventLog[ei].sec); client.print(F(","));
      client.print(eventLog[ei].temp); client.print(F(",,,,,"));
      client.println(eventStr(eventLog[ei].type));
      ei++;
    }

  } else if (req.startsWith("GET /api/events")) {
    httpOK(client, "application/json");
    client.print('[');
    for (uint8_t i = 0; i < eventCount; i++) {
      if (i) client.print(',');
      client.print(F("{\"sec\":")); client.print(eventLog[i].sec);
      client.print(F(",\"temp\":")); client.print(eventLog[i].temp);
      client.print(F(",\"type\":")); client.print(eventLog[i].type);
      client.print(F(",\"txt\":\"")); client.print(eventStr(eventLog[i].type)); client.print(F("\"}"));
    }
    client.print(']');

  } else if (req.startsWith("GET /api/fulllog")) {
    httpOK(client, "application/json");
    client.print('[');
    uint16_t fstart = (fullLogCount == FULL_LOG_SIZE) ? fullLogHead % FULL_LOG_SIZE : 0;
    for (uint16_t i = 0; i < fullLogCount; i++) {
      const DataPoint& dp = fullLogBuf[(fstart + i) % FULL_LOG_SIZE];
      if (i) client.print(',');
      client.print('['); client.print(dp.sec); client.print(',');
      client.print(dp.temp); client.print(',');
      client.print(dp.sp); client.print(',');
      client.print(dp.segIdx); client.print(']');
    }
    client.print(']');

  } else if (req.startsWith("GET /api/history")) {
    httpOK(client, "application/json");
    client.print('[');
    uint8_t start = (logCount == LOG_SIZE) ? logHead % LOG_SIZE : 0;
    for (uint8_t i = 0; i < logCount; i++) {
      const DataPoint& dp = logBuf[(start + i) % LOG_SIZE];
      if (i) client.print(',');
      client.print('['); client.print(dp.sec); client.print(',');
      client.print(dp.temp, 1); client.print(','); client.print(dp.sp, 1);
      client.print(','); client.print(dp.segIdx); client.print(']');
    }
    client.print(']');

  } else if (req.startsWith("POST /api/start")) {
    if (sensorMissing) {
      httpOK(client, "application/json"); client.print(F("{\"ok\":false,\"error\":\"no sensor\"}"));
    } else {
      int idx = 0;
      int eq = body.indexOf('=');
      if (eq >= 0) idx = constrain(body.substring(eq + 1).toInt(), 0, PROFILE_COUNT - 1);
      startProfile(&PROFILES[idx]);
      httpOK(client, "application/json"); client.print(F("{\"ok\":true}"));
    }

  } else if (req.startsWith("POST /api/relay")) {
    if (kilnState == IDLE) manualRelay = (formParam(body, "state") == "1");
    httpOK(client, "application/json"); client.print(F("{\"ok\":true}"));

  } else if (req.startsWith("POST /api/dismiss")) {
    showingIP = false;
    httpOK(client, "application/json"); client.print(F("{\"ok\":true}"));

  } else if (req.startsWith("POST /api/stop")) {
    if (kilnState != IDLE) {
      maybeSendReport();
      logEvent(EV_AVBRYT);
      digitalWrite(PIN_RELAY, LOW);
      kilnState = IDLE; pidOutput = 0; pidI = 0; setpoint = 0;
      stoppedMs = millis();
    }
    httpOK(client, "application/json"); client.print(F("{\"ok\":true}"));

  } else if (req.startsWith("POST /api/reset")) {
    maybeSendReport();
    estopFlag = false; estopAnnounced = false;
    kilnState = IDLE; pidOutput = 0; pidI = 0; setpoint = 0;
    matrixClear();
    httpOK(client, "application/json"); client.print(F("{\"ok\":true}"));

  } else if (req.startsWith("GET /api/settings")) {
    char key[50]={0}, to[50]={0}, cc[50]={0}, frm[50]={0};
    loadEmailConfig(key, to, cc, frm);
    httpOK(client, "application/json");
    client.print(F("{\"to\":\""));   client.print(to);
    client.print(F("\",\"cc\":\""));  client.print(cc);
    client.print(F("\",\"from\":\"")); client.print(frm);
    client.print(F("\",\"hasKey\":")); client.print(key[0] ? "true" : "false");
    client.print('}');

  } else if (req.startsWith("POST /api/settings")) {
    String newKey  = formParam(body, "key");
    String newTo   = formParam(body, "to");
    String newCc   = formParam(body, "cc");
    String newFrom = formParam(body, "from");
    char key[50]={0}, to[50]={0}, cc[50]={0}, frm[50]={0};
    loadEmailConfig(key, to, cc, frm);
    if (newTo.length())   strncpy(to,  newTo.c_str(),   49);
    if (newCc.length())   strncpy(cc,  newCc.c_str(),   49);
    if (newFrom.length()) strncpy(frm, newFrom.c_str(), 49);
    if (newKey.length())  strncpy(key, newKey.c_str(),  49);
    saveEmailConfig(key, to, cc, frm);
    httpOK(client, "application/json"); client.print(F("{\"ok\":true}"));

  } else {
    client.println(F("HTTP/1.1 404 Not Found\r\nConnection: close\r\n"));
  }

  delay(2);
  client.stop();
}

void httpOK(WiFiClient& c, const char* ct) {
  c.println(F("HTTP/1.1 200 OK"));
  c.print(F("Content-Type: ")); c.println(ct);
  c.println(F("Access-Control-Allow-Origin: *"));
  c.println(F("Connection: close"));
  c.println();
}

// ── Serielle kommandoer ───────────────────────────────────────────────────────
void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim(); cmd.toLowerCase();

  if (cmd.startsWith("start ")) {
    if (sensorMissing) { Serial.println(F("ERROR: no sensor")); }
    else {
      int idx = cmd.substring(6).toInt();
      if (idx >= 0 && (uint8_t)idx < PROFILE_COUNT) startProfile(&PROFILES[idx]);
      else Serial.println(F("Unknown profile"));
    }

  } else if (cmd == "relay on")  {
    if (kilnState == IDLE) { manualRelay = true;  Serial.println(F("Relay: ON")); }
    else Serial.println(F("Only available in IDLE"));
  } else if (cmd == "relay off") {
    manualRelay = false; Serial.println(F("Relay: OFF"));
  } else if (cmd == "stop")    {
    if (kilnState != IDLE) { maybeSendReport(); logEvent(EV_AVBRYT); digitalWrite(PIN_RELAY, LOW); kilnState = IDLE; pidOutput = 0; pidI = 0; setpoint = 0; stoppedMs = millis(); }
    Serial.println(F("Stopped"));
  } else if (cmd == "reset")   {
    maybeSendReport();
    estopFlag = false; estopAnnounced = false;
    kilnState = IDLE; pidOutput = 0; pidI = 0; setpoint = 0;
    matrixClear();
    Serial.println(F("Reset"));
  } else if (cmd == "status")  { printStatus();
  } else if (cmd == "ip")      {
    if (WiFi.status() == WL_CONNECTED) Serial.println(WiFi.localIP());
    else Serial.println(F("Not connected"));
  } else if (cmd == "profiles") {
    for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
      Serial.print(i); Serial.print(F(": ")); Serial.println(PROFILES[i].name);
    }
  } else if (cmd == "display") {
    displayScreen = (displayScreen + 1) % 4;
    displayScreenMs = millis();
    Serial.print(F("Display: ")); Serial.println(displayScreen);
  } else if (cmd == "help")    { printHelp(); }
}

void startProfile(const Profile* p) {
  logHead = 0; logCount = 0; lastLogMs = 0;
  fullLogHead = 0; fullLogCount = 0; lastFullLogMs = 0;
  eventCount = 0;
  firingStartMs = millis();
  peakTemp = 0.0f; reportSent = false; manualRelay = false; matrixDone = false;
  testTimeoutMs = (strcmp(p->id, "configtest") == 0) ? firingStartMs + 4UL * 3600UL * 1000UL : 0;
  profile = p; segIdx = 0; estopFlag = false; kilnState = RAMPING;
  pidI = 0; pidLastMs = millis();
  // Vis første 4 tegn av profilnavn (f.eks. "GLAZ" / "BISQ")
  char word[5] = {0};
  for (int i = 0; i < 4 && p->name[i]; i++)
    word[i] = (p->name[i] >= 'a' && p->name[i] <= 'z') ? p->name[i] - 32 : p->name[i];
  matrixWord4(word);
  Serial.print(F("Starting: ")); Serial.println(p->name);
  readTemp();
  if (sensorMissing) { kilnState = IDLE; return; }
  firingId++;
  EEPROM.update(EEPROM_PLOG_FLAG, 0);
  EEPROM.update(EEPROM_ELOG_FLAG, 0);
  digitalWrite(PIN_RELAY, HIGH); delay(500); digitalWrite(PIN_RELAY, LOW);
  startSegment();
  lastFullLogMs = millis();
  writeFullLogPoint(millis());
  logEvent(EV_START);
}

// ── Utskrift ──────────────────────────────────────────────────────────────────
const char* stateStr() {
  switch (kilnState) {
    case IDLE:      return "IDLE";
    case RAMPING:   return "RAMP";
    case HOLDING:   return "HOLD";
    case FREE_COOL: return "COOL";
    case COMPLETE:  return "DONE";
    case ESTOPPED:  return "STOP";
    case ERR:       return "ERR";
    default:        return "?";
  }
}

void printStatus() {
  Serial.print(stateStr());
  if (profile && kilnState != IDLE && kilnState != COMPLETE) {
    Serial.print(F(" [")); Serial.print(profile->segments[segIdx].name); Serial.print(F("]"));
  }
  Serial.print(F("  T:")); Serial.print(currentTemp, 1);
  Serial.print(F("C  SP:")); Serial.print(setpoint, 1);
  Serial.print(F("C  PID:")); Serial.print(pidOutput, 1); Serial.println(F("%"));
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  start 0/1/2  – start profile (0=Glaze 1=Bisque 2=ConfigTest)"));
  Serial.println(F("  stop / reset"));
  Serial.println(F("  relay on/off – manual relay (IDLE only)"));
  Serial.println(F("  display      – toggle LED mode"));
  Serial.println(F("  profiles / status / ip / help"));
}

