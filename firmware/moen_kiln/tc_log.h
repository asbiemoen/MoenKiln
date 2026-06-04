#pragma once
#include "config.h"

#define TC_BATCH_SIZE        180
#define TC_BATCH_INTERVAL_MS 180000UL  // 3 minutes

extern bool  tcLogActive;
extern bool  tcLogError;
extern float currentTemp;

const char* stateStr();

static uint16_t      _tcBuf[TC_BATCH_SIZE];
static uint8_t       _tcBufN        = 0;
static unsigned long _tcLastBatchMs = 0;
static char          _tcBody[1024];

static void _sendTcBatch() {
  if (WiFi.status() != WL_CONNECTED) { tcLogError = true; return; }
  if (!TC_LOG_HOST[0]) { return; }

  // Build JSON body
  int pos = 0;
  pos += snprintf(_tcBody + pos, (int)sizeof(_tcBody) - pos,
                  "{\"state\":\"%s\",\"readings\":[", stateStr());
  for (uint8_t i = 0; i < _tcBufN && pos < (int)sizeof(_tcBody) - 8; i++) {
    if (i) _tcBody[pos++] = ',';
    pos += snprintf(_tcBody + pos, (int)sizeof(_tcBody) - pos, "%u", _tcBuf[i]);
  }
  if (pos < (int)sizeof(_tcBody) - 2) { _tcBody[pos++] = ']'; _tcBody[pos++] = '}'; }
  _tcBody[pos] = 0;

  WiFiSSLClient cl;
  if (!cl.connect(TC_LOG_HOST, 443)) { tcLogError = true; return; }

  cl.print(F("POST ")); cl.print(F(TC_LOG_PATH)); cl.println(F(" HTTP/1.1"));
  cl.print(F("Host: ")); cl.println(F(TC_LOG_HOST));
  cl.println(F("Content-Type: application/json"));
  cl.print(F("x-functions-key: ")); cl.println(F(TC_LOG_KEY));
  cl.print(F("Content-Length: ")); cl.println(pos);
  cl.println(F("Connection: close"));
  cl.println();
  cl.print(_tcBody);
  cl.flush();
  cl.stop();  // fire and forget – do not wait for response

  tcLogError = false;
  Serial.print(F("TC batch: ")); Serial.print(_tcBufN); Serial.println(F(" samples sent"));
}

void tcLogTick(unsigned long now) {
  if (!tcLogActive) return;

  if (_tcBufN < TC_BATCH_SIZE)
    _tcBuf[_tcBufN++] = (uint16_t)constrain((int)(currentTemp + 0.5f), 0, 65534);

  if (_tcBufN >= TC_BATCH_SIZE ||
      (now - _tcLastBatchMs >= TC_BATCH_INTERVAL_MS && _tcBufN > 0)) {
    _sendTcBatch();
    _tcBufN = 0;
    _tcLastBatchMs = now;
  }
}

void tcLogSetActive(bool on) {
  tcLogActive = on;
  if (on) { _tcBufN = 0; _tcLastBatchMs = millis(); tcLogError = false; }
  Serial.print(F("TC log: ")); Serial.println(on ? F("ON") : F("OFF"));
}
