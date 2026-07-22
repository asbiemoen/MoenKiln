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

void saveEmailConfig(const char* usr, const char* pwd, const char* to,
                     const char* cc, const char* frm) {
  eepStr(EEPROM_SMTP_USER,  usr, 50);
  eepStr(EEPROM_SMTP_PASS,  pwd, 50);
  eepStr(EEPROM_EMAIL_TO,   to,  50);
  eepStr(EEPROM_EMAIL_CC,   cc,  50);
  eepStr(EEPROM_EMAIL_FROM, frm, 50);
}

void loadEmailConfig(char* usr, char* pwd, char* to, char* cc, char* frm) {
  eepRead(EEPROM_SMTP_USER,  usr, 50);
  eepRead(EEPROM_SMTP_PASS,  pwd, 50);
  eepRead(EEPROM_EMAIL_TO,   to,  50);
  eepRead(EEPROM_EMAIL_CC,   cc,  50);
  eepRead(EEPROM_EMAIL_FROM, frm, 50);

  // EEPROM_SMTP_PASS held the Resend API key before #97. A leftover key is not
  // an SMTP password, so drop it and let the default take over.
  if (!strncmp(pwd, "re_", 3)) pwd[0] = 0;

  // Fall back to defaults if not set
  if (!usr[0]) strncpy(usr, DEFAULT_SMTP_USER,  49);
  if (!pwd[0]) strncpy(pwd, DEFAULT_SMTP_PASS,  49);
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

// ── SMTP-dialog ───────────────────────────────────────────────────────────────
#define SMTP_TIMEOUT_MS 15000UL

// Leser ett komplett svar; "250-" markerer at flere linjer følger.
// Returnerer svarkoden, eller -1 ved timeout/brudd.
static int _smtpReply(WiFiSSLClient& cl) {
  char line[80];
  for (;;) {
    int n = 0;
    unsigned long t0 = millis();
    for (;;) {
      if (cl.available()) {
        char c = cl.read();
        if (c == '\n') break;
        if (c != '\r' && n < (int)sizeof(line) - 1) line[n++] = c;
        t0 = millis();
      } else if (!cl.connected() || millis() - t0 > SMTP_TIMEOUT_MS) {
        Serial.println(F("SMTP: timeout waiting for reply"));
        return -1;
      }
    }
    line[n] = 0;
    if (n < 4 || line[3] != '-') {           // siste linje i svaret
      Serial.print(F("SMTP < ")); Serial.println(line);
      return (n >= 3) ? atoi(line) : -1;
    }
  }
}

// Sender én kommandolinje og krever svarkode want.
// secret=true holder linja ute av serielogget (brukernavn/passord).
static bool _smtpCmd(WiFiSSLClient& cl, const char* line, int want,
                     bool secret = false) {
  Serial.print(F("SMTP > "));
  Serial.println(secret ? "(credential)" : line);
  cl.print(line);
  cl.print(F("\r\n"));
  int code = _smtpReply(cl);
  if (code != want) {
    Serial.print(F("SMTP: expected ")); Serial.print(want);
    Serial.print(F(", got "));          Serial.println(code);
    return false;
  }
  return true;
}

static void _b64(const char* in, char* out, int outN) {
  static const char TBL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int n = strlen(in), o = 0;
  for (int i = 0; i < n; i += 3) {
    if (o + 5 > outN) break;
    uint32_t v = (uint32_t)(uint8_t)in[i] << 16;
    if (i + 1 < n) v |= (uint32_t)(uint8_t)in[i+1] << 8;
    if (i + 2 < n) v |= (uint32_t)(uint8_t)in[i+2];
    out[o++] = TBL[(v >> 18) & 63];
    out[o++] = TBL[(v >> 12) & 63];
    out[o++] = (i + 1 < n) ? TBL[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < n) ? TBL[v & 63]        : '=';
  }
  out[o] = 0;
}

// ── Streaming write helpers ──────────────────────────────────────────────────
static WiFiSSLClient* _stCl   = nullptr;
static char           _stBuf[256];
static int            _stBufN = 0;
static bool           _stBol  = true;     // ved linjestart → punktum må dobles

static void _stFlush() {
  if (_stBufN && _stCl) _stCl->write((const uint8_t*)_stBuf, _stBufN);
  _stBufN = 0;
}
static void _stRaw(char c) {
  _stBuf[_stBufN++] = c;
  if (_stBufN >= 250) _stFlush();
}
static void _stCh(char c) {
  if (_stBol && c == '.') _stRaw('.');    // dot-stuffing (RFC 5321 §4.5.2)
  _stRaw(c);
  _stBol = (c == '\n');
}
static void _stS(const char* s) { while (*s) _stCh(*s++); }
static void _stN(unsigned int v) { char b[8]; snprintf(b,sizeof(b),"%u",v); _stS(b); }
static void _stBr() { _stS("\r\n"); }     // holder linjene under 998-oktetts-grensa
static void _stLn(const char* s) { _stS(s); _stBr(); }

// ── Date-header (RFC 5322) fra modemets NTP-tid ──────────────────────────────
// Tom streng hvis modemet ikke har synket; da setter mottakende server sin egen.
static void _dateHeader(char* out, int n) {
  unsigned long t = WiFi.getTime();
  out[0] = 0;
  if (t < 1700000000UL) return;

  static const char* const WD[7] = {"Thu","Fri","Sat","Sun","Mon","Tue","Wed"};
  static const char* const MO[12]= {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
  unsigned long days = t / 86400UL, rem = t % 86400UL;
  int hh = (int)(rem / 3600), mi = (int)((rem % 3600) / 60), ss = (int)(rem % 60);

  // days-from-civil, invertert (Howard Hinnant)
  long z = (long)days + 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned long doe = (unsigned long)(z - era * 146097);
  unsigned long yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
  long y = (long)yoe + era * 400;
  unsigned long doy = doe - (365*yoe + yoe/4 - yoe/100);
  unsigned long mp = (5*doy + 2) / 153;
  unsigned long d  = doy - (153*mp + 2)/5 + 1;
  unsigned long m  = mp + (mp < 10 ? 3 : -9);
  if (m <= 2) y++;

  snprintf(out, n, "Date: %s, %02lu %s %ld %02d:%02d:%02d +0000",
           WD[days % 7], d, MO[m-1], y, hh, mi, ss);
}

// ── Generer hele meldingen: MIME-headere + HTML-body ─────────────────────────
// Alt går gjennom _stCh, som dot-stuffer og teller linjestart. Linjeskift settes
// bare på steder der HTML tåler blanktegn – aldri inne i en attributtverdi.
static void _genBody(const char* fromA, const char* toA, const char* ccA,
                     unsigned long totalSec, uint16_t maxT) {
  char buf[64];

  // Headere
  _stS("From: Moen Kiln <"); _stS(fromA); _stLn(">");
  _stS("To: "); _stLn(toA);
  if (ccA[0]) { _stS("Cc: "); _stLn(ccA); }
  _stS("Subject: Firing complete");
  if (profile) { _stS(" - "); _stS(profile->name); }
  _stBr();
  _dateHeader(buf, sizeof(buf));
  if (buf[0]) _stLn(buf);
  _stLn("MIME-Version: 1.0");
  _stLn("Content-Type: text/html; charset=UTF-8");
  _stBr();                                  // blank linje avslutter headerne

  // HTML start
  _stLn("<div style='font-family:sans-serif;background:#111;color:#eee;"
        "padding:20px;max-width:640px;margin:0 auto'>");
  _stLn("<h1 style='color:#ff7700;margin:0 0 14px;font-size:1.3em'>"
        "Moen Kiln &ndash; Firing complete</h1>");

  // Sammendragstabell
  _stLn("<table cellpadding='6' cellspacing='0' style='border-collapse:collapse;"
        "margin-bottom:18px'>");
  if (profile) {
    _stS("<tr><td style='color:#888;padding-right:24px'>Profile</td>"
         "<td><b>"); _stS(profile->name); _stLn("</b></td></tr>");
  }
  snprintf(buf, sizeof(buf), "%dh %dm", (int)(totalSec/3600),(int)((totalSec%3600)/60));
  _stS("<tr><td style='color:#888'>Total time</td><td><b>"); _stS(buf); _stLn("</b></td></tr>");
  snprintf(buf, sizeof(buf), "%u", maxT);
  _stS("<tr><td style='color:#888'>Max temperature</td><td><b>"); _stS(buf);
  _stLn(" &deg;C</b></td></tr></table>");

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

    _stLn("<table cellpadding='0' cellspacing='1' bgcolor='#222' "
          "style='margin-bottom:18px' width='420'><tr>");
    for (int b = 0; b < N_BARS; b++) {
      int pi  = b * (logCount - 1) / (N_BARS - 1);
      int idx = (logCount == LOG_SIZE) ? (int)((logHead + pi) % LOG_SIZE) : pi;
      int bh  = (yr > 0) ? (int)(((float)logBuf[idx].temp - yMin) / yr * CHART_H) : CHART_H / 2;
      if (bh < 1)       bh = 1;
      if (bh > CHART_H) bh = CHART_H;
      int sh = CHART_H - bh;
      _stS("<td valign='bottom' width='13'>"
           "<table cellpadding='0' cellspacing='0'>");
      if (sh > 0) {
        snprintf(buf, sizeof(buf), "%d", sh);
        _stS("<tr><td bgcolor='#222' height='"); _stS(buf);
        _stS("' width='13'>&nbsp;</td></tr>");
      }
      snprintf(buf, sizeof(buf), "%d", bh);
      _stS("<tr><td bgcolor='#ff7700' height='"); _stS(buf);
      _stS("' width='13'>&nbsp;</td></tr>");
      _stLn("</table></td>");        // linjeskift bare mellom cellene
    }
    _stLn("</tr></table>");
  }

  // Temperaturlogg-tabell
  if (logCount > 0) {
    _stLn("<h2 style='color:#ff7700;margin:0 0 8px;font-size:1em'>Temperature log</h2>");
    _stLn("<table cellpadding='5' cellspacing='0' style='border-collapse:collapse;"
          "width:100%;font-size:12px'>");
    _stLn("<tr style='background:#333;color:#ff7700'>"
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
      _stS(i%2==0 ? "<tr style='background:#1e1e1e'>" : "<tr style='background:#242424'>");
      _stS("<td>"); _stS(sn); _stS("</td>");
      _stS("<td style='text-align:center;font-variant-numeric:tabular-nums'>");
      _stS(buf); _stS("</td>");
      _stS("<td style='text-align:right'>"); _stN(dp.temp); _stS("&deg;C</td>");
      _stS("<td style='text-align:right'>"); _stN(dp.sp);   _stS("&deg;C</td>");
      _stLn("</tr>");
    }
    _stLn("</table>");
  }

  _stLn("</div>");
}

// ── Rapport-generator ─────────────────────────────────────────────────────────
void sendFiringReport(unsigned long overrideTotalSec = 0) {
  char user[50]={0}, pass[50]={0}, toAddr[50]={0}, ccAddr[50]={0}, fromAddr[50]={0};
  loadEmailConfig(user, pass, toAddr, ccAddr, fromAddr);
  if (!user[0] || !pass[0] || !toAddr[0] || !fromAddr[0]) {
    Serial.println(F("Email: not configured")); return;
  }

  unsigned long totalSec = (overrideTotalSec > 0) ? overrideTotalSec
    : (firingStartMs > 0) ? (millis()-firingStartMs)/1000UL : 0;
  uint16_t maxT = 0;
  for (int i = 0; i < logCount; i++) maxT = max(maxT, logBuf[i].temp);

  WiFiSSLClient client;
  Serial.print(F("Email: connecting to ")); Serial.println(F(DEFAULT_SMTP_HOST));
  if (!client.connect(DEFAULT_SMTP_HOST, DEFAULT_SMTP_PORT)) {
    Serial.println(F("Email: connection failed")); return;
  }

  char line[120], b64[80];
  bool ok = false;

  do {
    if (_smtpReply(client) != 220) break;                      // tjenerhilsen
    if (!_smtpCmd(client, "EHLO moenkiln", 250)) break;
    if (!_smtpCmd(client, "AUTH LOGIN", 334)) break;

    _b64(user, b64, sizeof(b64));
    if (!_smtpCmd(client, b64, 334, true)) break;
    _b64(pass, b64, sizeof(b64));
    if (!_smtpCmd(client, b64, 235, true)) break;

    snprintf(line, sizeof(line), "MAIL FROM:<%s>", fromAddr);
    if (!_smtpCmd(client, line, 250)) break;
    snprintf(line, sizeof(line), "RCPT TO:<%s>", toAddr);
    if (!_smtpCmd(client, line, 250)) break;
    if (ccAddr[0]) {
      snprintf(line, sizeof(line), "RCPT TO:<%s>", ccAddr);
      if (!_smtpCmd(client, line, 250))                        // CC er ikke kritisk
        Serial.println(F("Email: CC rejected, sending anyway"));
    }
    if (!_smtpCmd(client, "DATA", 354)) break;

    _stCl = &client; _stBufN = 0; _stBol = true;
    _genBody(fromAddr, toAddr, ccAddr, totalSec, maxT);
    _stFlush();
    _stCl = nullptr;

    if (!_stBol) client.print(F("\r\n"));                      // sikre egen linje
    client.print(F(".\r\n"));                                  // avslutter DATA
    if (_smtpReply(client) != 250) break;
    ok = true;
  } while (0);

  if (client.connected()) _smtpCmd(client, "QUIT", 221);
  client.stop();
  Serial.println(ok ? F("Email: sent") : F("Email: FAILED"));
}
