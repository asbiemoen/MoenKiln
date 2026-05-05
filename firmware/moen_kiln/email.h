#pragma once
#include <EEPROM.h>
#include "config.h"

// Globals definert i moen_kiln.ino
extern DataPoint      logBuf[];
extern uint16_t       logHead;
extern uint16_t       logCount;
extern const Profile* profile;
extern unsigned long  firingStartMs;
extern float          currentTemp;

// ── EEPROM-helpers ────────────────────────────────────────────────────────────
static void eepStr(int addr, const char* s, int n) {
  for (int i = 0; i < n - 1; i++) {
    EEPROM.update(addr + i, (uint8_t)s[i]);
    if (!s[i]) break;
  }
  EEPROM.update(addr + n - 1, 0);
}

static void eepRead(int addr, char* buf, int n) {
  for (int i = 0; i < n - 1; i++) {
    uint8_t b = EEPROM.read(addr + i);
    if (b == 0) { buf[i] = 0; break; }
    if (b < 0x20 || b > 0x7E) { buf[0] = 0; break; }  // uinitialisert EEPROM (0xFF e.l.)
    buf[i] = (char)b;
  }
  buf[n - 1] = 0;
}

void saveEmailConfig(const char* key, const char* to, const char* cc, const char* frm) {
  eepStr(EEPROM_API_KEY,    key, 50);
  eepStr(EEPROM_EMAIL_TO,   to,  50);
  eepStr(EEPROM_EMAIL_CC,   cc,  50);
  eepStr(EEPROM_EMAIL_FROM, frm, 50);
}

void loadEmailConfig(char* key, char* to, char* cc, char* frm) {
  eepRead(EEPROM_API_KEY,    key, 50);
  eepRead(EEPROM_EMAIL_TO,   to,  50);
  eepRead(EEPROM_EMAIL_CC,   cc,  50);
  eepRead(EEPROM_EMAIL_FROM, frm, 50);

  // Fall back to defaults if not set
  if (!key[0]) strncpy(key, DEFAULT_API_KEY,    49);
  if (!to[0])  strncpy(to,  DEFAULT_EMAIL_TO,   49);
  if (!frm[0]) strncpy(frm, DEFAULT_EMAIL_FROM, 49);
}

// ── URL-dekoder (for settings POST body) ──────────────────────────────────────
static String urlDecode(const String& s) {
  String r = "";
  for (int i = 0; i < (int)s.length(); i++) {
    if (s[i] == '+') r += ' ';
    else if (s[i] == '%' && i + 2 < (int)s.length()) {
      char hex[3] = { s[i+1], s[i+2], 0 };
      r += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else r += s[i];
  }
  return r;
}

static String formParam(const String& body, const String& key) {
  String search = key + "=";
  int start = body.indexOf(search);
  if (start < 0) return "";
  start += search.length();
  int end = body.indexOf('&', start);
  return urlDecode(body.substring(start, end < 0 ? body.length() : end));
}

// ── Streaming write helpers (to-pass: telle bytes, så sende) ─────────────────
static bool           _stCount = false;
static unsigned long  _stBytes = 0;
static WiFiSSLClient* _stCl    = nullptr;
static char           _stBuf[256];
static int            _stBufN  = 0;

static void _stFlush() {
  if (_stCount || !_stBufN || !_stCl) { _stBufN = 0; return; }
  _stCl->write((const uint8_t*)_stBuf, _stBufN);
  _stBufN = 0;
}
static void _stCh(char c) {
  if (_stCount) { _stBytes++; return; }
  _stBuf[_stBufN++] = c;
  if (_stBufN >= 250) _stFlush();
}
static void _stS(const char* s) { while (*s) _stCh(*s++); }
static void _stN(unsigned int v) { char b[8]; snprintf(b,sizeof(b),"%u",v); _stS(b); }
static void _stEsc(const char* s) {   // JSON-escape
  while (*s) {
    if      (*s == '"')  { _stCh('\\'); _stCh('"'); }
    else if (*s == '\\') { _stCh('\\'); _stCh('\\'); }
    else if (*s == '\n') { _stCh('\\'); _stCh('n');  }
    else                 _stCh(*s);
    s++;
  }
}

// ── Generer hele e-post-body (kalles to ganger: telle + sende) ───────────────
static void _genBody(const char* fromA, const char* toA, const char* ccA,
                     unsigned long totalSec, uint16_t maxT) {
  char buf[32];

  // JSON wrapper
  _stS("{\"from\":\""); _stS(fromA); _stS("\"");
  _stS(",\"to\":[\"");  _stS(toA);   _stS("\"]");
  if (ccA[0]) { _stS(",\"cc\":[\""); _stS(ccA); _stS("\"]"); }
  _stS(",\"subject\":\"Firing complete");
  if (profile) { _stS(" - "); _stS(profile->name); }
  _stS("\",\"html\":\"");

  // HTML start
  _stEsc("<div style='font-family:sans-serif;background:#111;color:#eee;"
         "padding:20px;max-width:640px;margin:0 auto'>");
  _stEsc("<h1 style='color:#ff7700;margin:0 0 14px;font-size:1.3em'>"
         "Moen Kiln &ndash; Firing complete</h1>");

  // Sammendragstabell
  _stEsc("<table cellpadding='6' cellspacing='0' style='border-collapse:collapse;"
         "margin-bottom:18px'>");
  if (profile) {
    _stEsc("<tr><td style='color:#888;padding-right:24px'>Profile</td>"
           "<td><b>"); _stEsc(profile->name); _stEsc("</b></td></tr>");
  }
  snprintf(buf, sizeof(buf), "%dh %dm", (int)(totalSec/3600),(int)((totalSec%3600)/60));
  _stEsc("<tr><td style='color:#888'>Total time</td><td><b>"); _stEsc(buf); _stEsc("</b></td></tr>");
  snprintf(buf, sizeof(buf), "%u", maxT);
  _stEsc("<tr><td style='color:#888'>Max temperature</td><td><b>"); _stEsc(buf);
  _stEsc(" &deg;C</b></td></tr></table>");

  // Table-based bar chart (SVG is stripped by Gmail and unsupported in Outlook)
  if (logCount >= 2) {
    float yMax = 0, yMin = 9999;
    for (int i = 0; i < logCount; i++) {
      uint16_t idx = (logCount == LOG_SIZE) ? (uint16_t)((logHead+i)%LOG_SIZE) : (uint16_t)i;
      if (logBuf[idx].temp > yMax) yMax = logBuf[idx].temp;
      if (logBuf[idx].temp < yMin) yMin = logBuf[idx].temp;
    }
    yMax += 30; yMin = (yMin > 30) ? yMin - 30 : 0;
    float yr = yMax - yMin;

    const int N_BARS  = 30;
    const int CHART_H = 100;

    _stEsc("<table cellpadding='0' cellspacing='1' bgcolor='#222' "
           "style='margin-bottom:18px' width='420'><tr>");
    for (int b = 0; b < N_BARS; b++) {
      int pi  = b * (logCount - 1) / (N_BARS - 1);
      int idx = (logCount == LOG_SIZE) ? (int)((logHead + pi) % LOG_SIZE) : pi;
      int bh  = (yr > 0) ? (int)(((float)logBuf[idx].temp - yMin) / yr * CHART_H) : CHART_H / 2;
      if (bh < 1)       bh = 1;
      if (bh > CHART_H) bh = CHART_H;
      int sh = CHART_H - bh;
      _stEsc("<td valign='bottom' width='13'>"
             "<table cellpadding='0' cellspacing='0'>");
      if (sh > 0) {
        snprintf(buf, sizeof(buf), "%d", sh);
        _stEsc("<tr><td bgcolor='#222' height='"); _stEsc(buf);
        _stEsc("' width='13'>&nbsp;</td></tr>");
      }
      snprintf(buf, sizeof(buf), "%d", bh);
      _stEsc("<tr><td bgcolor='#ff7700' height='"); _stEsc(buf);
      _stEsc("' width='13'>&nbsp;</td></tr>");
      _stEsc("</table></td>");
    }
    _stEsc("</tr></table>");
  }

  // Temperaturlogg-tabell
  if (logCount > 0) {
    _stEsc("<h2 style='color:#ff7700;margin:0 0 8px;font-size:1em'>Temperature log</h2>");
    _stEsc("<table cellpadding='5' cellspacing='0' style='border-collapse:collapse;"
           "width:100%;font-size:12px'>");
    _stEsc("<tr style='background:#333;color:#ff7700'>"
           "<th style='text-align:left'>Segment</th>"
           "<th>Time</th>"
           "<th>Temp</th>"
           "<th>Target</th></tr>");
    for (int i = 0; i < logCount; i++) {
      uint16_t idx=(logCount==LOG_SIZE)?(uint16_t)((logHead+i)%LOG_SIZE):(uint16_t)i;
      const DataPoint& dp = logBuf[idx];
      const char* sn = (profile && (dp.segIdx & 0x7F) < profile->segCount)
                       ? profile->segments[dp.segIdx & 0x7F].name : "?";
      snprintf(buf, sizeof(buf), "%02d:%02d",
               (int)(dp.sec/3600), (int)((dp.sec%3600)/60));
      _stEsc(i%2==0 ? "<tr style='background:#1e1e1e'>" : "<tr style='background:#242424'>");
      _stEsc("<td>"); _stEsc(sn); _stEsc("</td>");
      _stEsc("<td style='text-align:center;font-variant-numeric:tabular-nums'>");
      _stEsc(buf); _stEsc("</td>");
      _stEsc("<td style='text-align:right'>"); _stN(dp.temp); _stEsc("&deg;C</td>");
      _stEsc("<td style='text-align:right'>"); _stN(dp.sp);   _stEsc("&deg;C</td>");
      _stEsc("</tr>");
    }
    _stEsc("</table>");
  }

  _stEsc("</div>");
  _stS("\"}");
}

// ── Rapport-generator ─────────────────────────────────────────────────────────
void sendFiringReport(unsigned long overrideTotalSec = 0) {
  char apiKey[50]={0}, toAddr[50]={0}, ccAddr[50]={0}, fromAddr[50]={0};
  loadEmailConfig(apiKey, toAddr, ccAddr, fromAddr);
  if (!apiKey[0] || !toAddr[0] || !fromAddr[0]) {
    Serial.println(F("Email: not configured")); return;
  }

  unsigned long totalSec = (overrideTotalSec > 0) ? overrideTotalSec
    : (firingStartMs > 0) ? (millis()-firingStartMs)/1000UL : 0;
  uint16_t maxT = 0;
  for (int i = 0; i < logCount; i++) maxT = max(maxT, logBuf[i].temp);

  // Pass 1: tell opp Content-Length uten å sende noe
  _stCount = true; _stBytes = 0;
  _genBody(fromAddr, toAddr, ccAddr, totalSec, maxT);
  unsigned long contentLen = _stBytes;
  Serial.print(F("Email: body ")); Serial.print(contentLen); Serial.println(F(" bytes"));

  // Koble til
  WiFiSSLClient client;
  Serial.println(F("Email: connecting to api.resend.com..."));
  if (!client.connect("api.resend.com", 443)) {
    Serial.println(F("Email: connection failed")); return;
  }

  // Send headers
  client.print(F("POST /emails HTTP/1.1\r\n"));
  client.print(F("Host: api.resend.com\r\n"));
  client.print(F("Content-Type: application/json\r\n"));
  client.print(F("Authorization: Bearer ")); client.print(apiKey); client.print(F("\r\n"));
  client.print(F("Content-Length: ")); client.print(contentLen); client.print(F("\r\n"));
  client.print(F("Connection: close\r\n\r\n"));

  // Pass 2: send body
  _stCount = false; _stCl = &client; _stBufN = 0;
  _genBody(fromAddr, toAddr, ccAddr, totalSec, maxT);
  _stFlush();

  // Les svar
  unsigned long t0 = millis();
  String line = "";
  bool logged = false;
  while (client.connected() && millis()-t0 < 15000) {
    while (client.available()) {
      char c = client.read();
      if (c == '\n') {
        if (!logged && line.length()>0) {
          Serial.print(F("Email response: ")); Serial.println(line); logged=true;
        }
        line = "";
      } else if (c != '\r' && line.length()<100) line += c;
    }
  }
  client.stop();
  Serial.println(F("Email: done"));
}
