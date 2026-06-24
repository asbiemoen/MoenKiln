#pragma once
#include <EEPROM.h>
#include "config.h"

extern float          currentTemp;
extern float          setpoint;
extern float          pidOutput;
extern uint8_t        segIdx;
extern bool           sensorMissing;
extern unsigned long  firingStartMs;
extern const Profile* profile;

#define CLOUD_LOG_INTERVAL_MS   60000UL
#define EEPROM_CLOUD_ENABLED    6351    // 1 byte : 0x01 = enabled
#define EEPROM_CLOUD_FIRING_ID  6352    // 2 bytes: persistent uint16_t counter

// Endpoint that receives the one-shot "firing ended" signal. Override in
// config_secrets.h if your deployment differs.
#ifndef CLOUD_LOG_END_PATH
#define CLOUD_LOG_END_PATH      "/api/firing-end"
#endif

#define CLOUD_LOG_END_RETRY_MS  30000UL

static bool     _clEnabled  = false;
static uint16_t _clFiringId = 0;
static unsigned long _clLastMs = 0;
static bool     _clEnded     = false;        // firing over → stop per-point sends
static bool     _clEndAcked  = false;        // end signal delivered to the server
static const char* _clEndReason = "completed";
static unsigned long _clEndTryMs = 0;        // last end-signal send attempt

void cloudLogInit() {
  _clEnabled  = EEPROM.read(EEPROM_CLOUD_ENABLED) == 0x01;
  _clFiringId = (uint16_t)EEPROM.read(EEPROM_CLOUD_FIRING_ID)
              | ((uint16_t)EEPROM.read(EEPROM_CLOUD_FIRING_ID + 1) << 8);
  if (_clFiringId == 0xFFFF) _clFiringId = 0;
}

void cloudLogNewFiring() {
  _clFiringId++;
  EEPROM.update(EEPROM_CLOUD_FIRING_ID,     (uint8_t)(_clFiringId & 0xFF));
  EEPROM.update(EEPROM_CLOUD_FIRING_ID + 1, (uint8_t)(_clFiringId >> 8));
  _clLastMs    = 0;
  _clEnded     = false;
  _clEndAcked  = false;
  _clEndReason = "completed";
  _clEndTryMs  = 0;
}

bool     cloudLogEnabled() { return _clEnabled; }
uint16_t cloudFiringId()   { return _clFiringId; }

void setCloudLogEnabled(bool en) {
  _clEnabled = en;
  EEPROM.update(EEPROM_CLOUD_ENABLED, en ? 0x01 : 0x00);
}

static void _clPost(uint16_t firingId, uint32_t sec, float temp, float sp,
                    bool relay, uint8_t duty, const char* seg, bool isErr) {
#ifndef CLOUD_LOG_HOST
  return;
#else
  WiFiSSLClient cl;
  cl.setTimeout(3000);

  if (!cl.connect(CLOUD_LOG_HOST, 443)) {
    Serial.println(F("CL: connect failed"));
    return;
  }

  char body[220];
  int blen = snprintf(body, sizeof(body),
    "{\"firing_id\":%u,\"sec\":%lu,\"temp\":%.1f,\"sp\":%.1f,"
    "\"relay\":%s,\"duty\":%u,\"segment\":\"%s\",\"is_err\":%s}",
    (unsigned)firingId, (unsigned long)sec, temp, sp,
    relay ? "true" : "false",
    (unsigned)duty, seg ? seg : "", isErr ? "true" : "false");

  cl.print(F("POST "));
  cl.print(F(CLOUD_LOG_PATH));
  cl.print(F(" HTTP/1.1\r\nHost: "));
  cl.print(F(CLOUD_LOG_HOST));
  cl.print(F("\r\nX-Kiln-Key: "));
  cl.print(F(CLOUD_LOG_KEY));
  cl.print(F("\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "));
  cl.print(blen);
  cl.print(F("\r\n\r\n"));
  cl.print(body);

  unsigned long t0 = millis();
  while (!cl.available() && millis() - t0 < 2000 && cl.connected());
  cl.stop();

  Serial.print(F("CL: sent #")); Serial.print(firingId);
  Serial.print(F(" temp=")); Serial.println(temp, 1);
#endif
}

// Attempt to deliver the "firing ended" signal. Sets _clEndAcked on a successful
// send; leaves it false (for retry) if WiFi/connect fails. Idempotent on the
// server (UPSERT), so retries are harmless.
static void _clSendEnd() {
  if (_clEndAcked) return;
  if (WiFi.status() != WL_CONNECTED) return;

#ifndef CLOUD_LOG_HOST
  _clEndAcked = true;                 // no cloud configured → nothing to retry
  return;
#else
  WiFiSSLClient cl;
  cl.setTimeout(3000);
  if (!cl.connect(CLOUD_LOG_HOST, 443)) {
    Serial.println(F("CL: end connect failed (will retry)"));
    return;
  }

  char body[64];
  int blen = snprintf(body, sizeof(body),
    "{\"firing_id\":%u,\"reason\":\"%s\"}",
    (unsigned)_clFiringId, _clEndReason);

  cl.print(F("POST "));
  cl.print(F(CLOUD_LOG_END_PATH));
  cl.print(F(" HTTP/1.1\r\nHost: "));
  cl.print(F(CLOUD_LOG_HOST));
  cl.print(F("\r\nX-Kiln-Key: "));
  cl.print(F(CLOUD_LOG_KEY));
  cl.print(F("\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "));
  cl.print(blen);
  cl.print(F("\r\n\r\n"));
  cl.print(body);

  unsigned long t0 = millis();
  while (!cl.available() && millis() - t0 < 2000 && cl.connected());
  cl.stop();

  _clEndAcked = true;
  Serial.print(F("CL: ended #")); Serial.print(_clFiringId);
  Serial.print(F(" reason=")); Serial.println(_clEndReason);
#endif
}

// Mark the current firing as ended. Stops per-point sends immediately and
// attempts the end signal once; cloudLogTick() retries if that attempt fails.
// reason: "completed" | "stopped" | "estop". Safe to call more than once.
void cloudLogEndFiring(const char* reason) {
  if (!_clEnabled) return;
  if (_clEnded)    return;            // already ended for this firing
  _clEnded     = true;               // stop per-point sends from now on
  _clEndReason = reason ? reason : "completed";
  _clEndTryMs  = millis();
  _clSendEnd();
}

// Periodic retry of an undelivered end signal. Call from loop(). Cheap no-op
// unless a firing has ended but its end signal never reached the server.
void cloudLogTick(unsigned long now) {
  if (!_clEnabled || !_clEnded || _clEndAcked) return;
  if (now - _clEndTryMs < CLOUD_LOG_END_RETRY_MS) return;
  _clEndTryMs = now;
  _clSendEnd();
}

void maybeSendCloudPoint(unsigned long now) {
  if (!_clEnabled) return;
  if (!firingStartMs) return;
  if (_clEnded) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (_clLastMs != 0 && now - _clLastMs < CLOUD_LOG_INTERVAL_MS) return;
  _clLastMs = now;

  uint32_t sec  = firingStartMs ? (uint32_t)((now - firingStartMs) / 1000UL) : 0;
  uint8_t  duty = (uint8_t)constrain((int)pidOutput, 0, 100);
  bool     relay = digitalRead(PIN_RELAY) == HIGH;

  const char* seg = "";
  if (profile && segIdx < profile->segCount)
    seg = profile->segments[segIdx].name;

  _clPost(_clFiringId, sec, currentTemp, setpoint, relay, duty, seg, sensorMissing);
}
