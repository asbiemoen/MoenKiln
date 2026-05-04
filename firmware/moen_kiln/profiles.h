#pragma once
#include <stdint.h>

struct Segment {
  const char* name;
  float       targetTemp;   // °C
  float       ratePerHour;  // °C/time; 0 = fri avkjoeling
  uint16_t    holdMin;      // Minutter ved maaltemperatur
};

struct Profile {
  const char*    id;
  const char*    name;
  uint8_t        segCount;
  const Segment* segments;
};

// ── Glaze firing ──────────────────────────────────────────────────────────────
static const Segment SEG_GLAZE[] = {
  { "Glaze 1",  105,  55,  0  },
  { "Glaze 2",  1080, 200, 0  },
  { "Glaze 3",  1222, 85,  15 },
  { "Glaze 4",  1000, 275, 0  },
  { "Glaze 5",  760,  70,  0  },
};

// ── Bisque firing ─────────────────────────────────────────────────────────────
static const Segment SEG_BISQUE[] = {
  { "Bisque 1", 600,  100, 0  },
  { "Bisque 2", 995,  150, 15 },
};

// ── Config test: max speed to 1220 °C, then free cool to room temp ────────────
static const Segment SEG_CONFIGTEST[] = {
  { "Ramp Up",    800, 9999, 0 },
  { "Free Cool",   25,    0, 0 },
};

static const Profile PROFILES[] = {
  { "glaze",      "Glaze",       5, SEG_GLAZE      },
  { "bisque",     "Bisque",      2, SEG_BISQUE      },
  { "configtest", "Config Test", 2, SEG_CONFIGTEST  },
};

#define PROFILE_COUNT (sizeof(PROFILES) / sizeof(PROFILES[0]))
