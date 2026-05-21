#pragma once
#include <stdint.h>
#include <string.h>
#include <EEPROM.h>
#include "config.h"
#include "profiles.h"

// ── Stored profile structs (mutable char arrays, EEPROM-serialisable) ─────────

struct StoredSegment {
  char     name[SEG_NAME_LEN + 1];  // 12 bytes
  uint16_t targetTemp;               //  2 bytes
  uint16_t ratePerHour;              //  2 bytes
  uint16_t holdMin;                  //  2 bytes
};                                   // 18 bytes total

struct StoredProfile {
  char          id  [PROF_ID_LEN   + 1];  // 16 bytes
  char          name[PROF_NAME_LEN + 1];  // 16 bytes
  uint8_t       segCount;                 //  1 byte
  StoredSegment segs[MAX_SEGS_PER_PROFILE]; // 144 bytes
};                                          // 177 bytes total

// ── Runtime storage ────────────────────────────────────────────────────────────
// Custom profiles live here; Profile/Segment wrappers point into these arrays.

static StoredProfile  _customStore[MAX_CUSTOM_PROFILES];
static Segment        _customSegArrays[MAX_CUSTOM_PROFILES][MAX_SEGS_PER_PROFILE];
static Profile        _customProfileArr[MAX_CUSTOM_PROFILES];
static uint8_t        _customCount = 0;

// Public accessors (updated by rebuild functions below)
const Profile* customProfiles     = nullptr;
uint8_t        customProfileCount = 0;

// ── Build Profile/Segment wrapper arrays from _customStore ────────────────────
static void rebuildWrappers() {
  for (uint8_t pi = 0; pi < _customCount; pi++) {
    StoredProfile& sp = _customStore[pi];
    for (uint8_t si = 0; si < sp.segCount; si++) {
      _customSegArrays[pi][si].name        = sp.segs[si].name;
      _customSegArrays[pi][si].targetTemp  = (float)sp.segs[si].targetTemp;
      _customSegArrays[pi][si].ratePerHour = (float)sp.segs[si].ratePerHour;
      _customSegArrays[pi][si].holdMin     = sp.segs[si].holdMin;
    }
    _customProfileArr[pi].id       = sp.id;
    _customProfileArr[pi].name     = sp.name;
    _customProfileArr[pi].segCount = sp.segCount;
    _customProfileArr[pi].segments = _customSegArrays[pi];
  }
  customProfiles     = (_customCount > 0) ? _customProfileArr : nullptr;
  customProfileCount = _customCount;
}

// ── EEPROM persistence ─────────────────────────────────────────────────────────
void saveCustomProfiles() {
  EEPROM.update(EEPROM_PROF_CNT, _customCount);
  for (uint8_t i = 0; i < _customCount; i++)
    EEPROM.put(EEPROM_PROF_DATA + (int)i * (int)sizeof(StoredProfile), _customStore[i]);
  EEPROM.update(EEPROM_PROF_FLAG, 0x46);
}

void loadCustomProfiles() {
  if (EEPROM.read(EEPROM_PROF_FLAG) != 0x46) { _customCount = 0; rebuildWrappers(); return; }
  uint8_t cnt = EEPROM.read(EEPROM_PROF_CNT);
  if (cnt > MAX_CUSTOM_PROFILES) cnt = 0;
  _customCount = cnt;
  for (uint8_t i = 0; i < cnt; i++)
    EEPROM.get(EEPROM_PROF_DATA + (int)i * (int)sizeof(StoredProfile), _customStore[i]);
  rebuildWrappers();
}

// ── JSON serialiser ────────────────────────────────────────────────────────────
// Writes the full profiles JSON (default + custom) to a WiFiClient.
// Default profiles are marked "builtin":true so the GUI can lock them.
template<typename TClient>
void sendProfilesJSON(TClient& client) {
  client.print('[');
  bool first = true;

  // Built-in (read-only) profiles
  for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
    if (!first) client.print(',');
    first = false;
    const Profile& p = PROFILES[i];
    client.print(F("{\"id\":\""));   client.print(p.id);
    client.print(F("\",\"name\":\"")); client.print(p.name);
    client.print(F("\",\"builtin\":true,\"segments\":["));
    for (uint8_t s = 0; s < p.segCount; s++) {
      if (s) client.print(',');
      const Segment& seg = p.segments[s];
      client.print(F("{\"name\":\""));        client.print(seg.name);
      client.print(F("\",\"targetTemp\":"));   client.print((int)seg.targetTemp);
      client.print(F(",\"ratePerHour\":"));    client.print((int)seg.ratePerHour);
      client.print(F(",\"holdMin\":"));        client.print(seg.holdMin);
      client.print('}');
    }
    client.print(F("]}"));
  }

  // Custom (editable) profiles
  for (uint8_t i = 0; i < _customCount; i++) {
    client.print(',');
    StoredProfile& sp = _customStore[i];
    client.print(F("{\"id\":\""));   client.print(sp.id);
    client.print(F("\",\"name\":\"")); client.print(sp.name);
    client.print(F("\",\"builtin\":false,\"segments\":["));
    for (uint8_t s = 0; s < sp.segCount; s++) {
      if (s) client.print(',');
      StoredSegment& seg = sp.segs[s];
      client.print(F("{\"name\":\""));        client.print(seg.name);
      client.print(F("\",\"targetTemp\":"));   client.print(seg.targetTemp);
      client.print(F(",\"ratePerHour\":"));    client.print(seg.ratePerHour);
      client.print(F(",\"holdMin\":"));        client.print(seg.holdMin);
      client.print('}');
    }
    client.print(F("]}"));
  }

  client.print(']');
}

// ── Minimal JSON parser ────────────────────────────────────────────────────────
// Parses only the custom-profiles subset (builtin profiles are never sent back).
// Returns nullptr on success, or a short human-readable error string.

static const char* skipWs(const char* p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  return p;
}

// Read a quoted string into buf (max len including null). Returns ptr after closing '"'.
static const char* readStr(const char* p, char* buf, uint8_t maxLen) {
  p = skipWs(p);
  if (*p != '"') return nullptr;
  p++;
  uint8_t i = 0;
  while (*p && *p != '"') {
    if (i < maxLen) buf[i++] = *p;
    p++;
  }
  if (*p != '"') return nullptr;
  buf[i] = '\0';
  return p + 1;
}

// Read an integer. Returns ptr after digits.
static const char* readInt(const char* p, int32_t& out) {
  p = skipWs(p);
  bool neg = (*p == '-'); if (neg) p++;
  if (*p < '0' || *p > '9') return nullptr;
  out = 0;
  while (*p >= '0' && *p <= '9') { out = out * 10 + (*p - '0'); p++; }
  if (neg) out = -out;
  return p;
}

// Skip a JSON value (string, number, object, array) — used to skip unknown keys.
static const char* skipValue(const char* p);
static const char* skipValue(const char* p) {
  p = skipWs(p);
  if (*p == '"') {
    p++;
    while (*p && *p != '"') { if (*p == '\\') p++; p++; }
    return *p ? p + 1 : p;
  }
  if (*p == '{' || *p == '[') {
    char open = *p, close = (*p == '{') ? '}' : ']';
    p++; int depth = 1;
    while (*p && depth > 0) {
      if (*p == '"') { p++; while (*p && *p != '"') { if (*p == '\\') p++; p++; } if (*p) p++; continue; }
      if (*p == open)  depth++;
      if (*p == close) depth--;
      p++;
    }
    return p;
  }
  while (*p && *p != ',' && *p != '}' && *p != ']') p++;
  return p;
}

// Expect a specific character (after whitespace). Returns ptr after it, or nullptr.
static const char* expect(const char* p, char c) {
  p = skipWs(p);
  return (*p == c) ? p + 1 : nullptr;
}

const char* parseAndSaveCustomProfiles(const char* json) {
  const char* p = skipWs(json);
  p = expect(p, '['); if (!p) return "Expected '['";

  StoredProfile  tmp[MAX_CUSTOM_PROFILES];
  uint8_t        cnt = 0;

  p = skipWs(p);
  while (*p && *p != ']') {
    if (cnt >= MAX_CUSTOM_PROFILES) return "Too many profiles (max 5)";
    p = expect(p, '{'); if (!p) return "Expected '{'";

    StoredProfile& pr = tmp[cnt];
    memset(&pr, 0, sizeof(pr));

    p = skipWs(p);
    while (*p && *p != '}') {
      char key[20] = {};
      p = readStr(p, key, 19); if (!p) return "Bad key";
      p = skipWs(p);
      p = expect(p, ':');      if (!p) return "Expected ':'";
      p = skipWs(p);

      if (strcmp(key, "id") == 0) {
        p = readStr(p, pr.id, PROF_ID_LEN); if (!p) return "Bad id";
      } else if (strcmp(key, "name") == 0) {
        p = readStr(p, pr.name, PROF_NAME_LEN); if (!p) return "Bad name";
      } else if (strcmp(key, "segments") == 0) {
        p = expect(p, '['); if (!p) return "Expected segments array";
        p = skipWs(p);
        while (*p && *p != ']') {
          if (pr.segCount >= MAX_SEGS_PER_PROFILE) return "Too many segments (max 8)";
          p = expect(p, '{'); if (!p) return "Expected segment object";
          StoredSegment& seg = pr.segs[pr.segCount];
          memset(&seg, 0, sizeof(seg));
          p = skipWs(p);
          while (*p && *p != '}') {
            char sk[20] = {};
            p = readStr(p, sk, 19); if (!p) return "Bad segment key";
            p = skipWs(p);
            p = expect(p, ':');     if (!p) return "Expected ':'";
            p = skipWs(p);
            int32_t v = 0;
            if (strcmp(sk, "name") == 0) {
              p = readStr(p, seg.name, SEG_NAME_LEN); if (!p) return "Bad segment name";
            } else if (strcmp(sk, "targetTemp") == 0) {
              p = readInt(p, v); if (!p) return "Bad targetTemp";
              if (v < 100 || v > 1400) return "targetTemp out of range (100-1400)";
              seg.targetTemp = (uint16_t)v;
            } else if (strcmp(sk, "ratePerHour") == 0) {
              p = readInt(p, v); if (!p) return "Bad ratePerHour";
              if (v < 0 || v > 9999) return "ratePerHour out of range (0-9999)";
              seg.ratePerHour = (uint16_t)v;
            } else if (strcmp(sk, "holdMin") == 0) {
              p = readInt(p, v); if (!p) return "Bad holdMin";
              if (v < 0 || v > 999) return "holdMin out of range (0-999)";
              seg.holdMin = (uint16_t)v;
            } else {
              p = skipValue(p);
            }
            p = skipWs(p);
            if (*p == ',') p++;
            p = skipWs(p);
          }
          if (seg.name[0] == '\0') return "Segment missing name";
          if (seg.targetTemp == 0) return "Segment missing targetTemp";
          pr.segCount++;
          p = expect(p, '}'); if (!p) return "Expected '}' after segment";
          p = skipWs(p);
          if (*p == ',') p++;
          p = skipWs(p);
        }
        p = expect(p, ']'); if (!p) return "Expected ']' after segments";
      } else {
        // Skip unknown keys (e.g. "builtin" sent back from GUI)
        p = skipValue(p);
      }
      p = skipWs(p);
      if (*p == ',') p++;
      p = skipWs(p);
    }

    if (pr.id[0] == '\0')   return "Profile missing id";
    if (pr.name[0] == '\0') return "Profile missing name";
    if (pr.segCount == 0)   return "Profile has no segments";

    p = expect(p, '}'); if (!p) return "Expected '}' after profile";
    cnt++;
    p = skipWs(p);
    if (*p == ',') p++;
    p = skipWs(p);
  }

  // Commit
  memcpy(_customStore, tmp, cnt * sizeof(StoredProfile));
  _customCount = cnt;
  rebuildWrappers();
  saveCustomProfiles();
  return nullptr; // success
}
