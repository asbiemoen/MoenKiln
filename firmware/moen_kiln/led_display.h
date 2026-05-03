#pragma once
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"

extern ArduinoLEDMatrix ledMatrix;

static char          _lastText[16]  = "";
static unsigned long _lastTextMs    = 0;
static bool          _scrollActive  = false;

static void stopScroll() {
  if (_scrollActive) {
    ledMatrix.beginDraw();
    ledMatrix.endDraw();
    _scrollActive = false;
    _lastText[0] = 0;
  }
}

inline void matrixBegin() { ledMatrix.begin(); }

inline void matrixClear() {
  stopScroll();
  byte f[8][12] = {};
  ledMatrix.renderBitmap(f, 8, 12);
}

// 3×5 siffer-font
static const uint8_t DIGIT_3x5[10][5] = {
  {0b111,0b101,0b101,0b101,0b111},  // 0
  {0b011,0b001,0b001,0b001,0b011},  // 1
  {0b111,0b001,0b111,0b100,0b111},  // 2
  {0b111,0b001,0b111,0b001,0b111},  // 3
  {0b101,0b101,0b111,0b001,0b001},  // 4
  {0b111,0b100,0b111,0b001,0b111},  // 5
  {0b111,0b100,0b111,0b101,0b111},  // 6
  {0b111,0b001,0b001,0b001,0b001},  // 7
  {0b111,0b101,0b111,0b101,0b111},  // 8
  {0b111,0b101,0b111,0b001,0b111},  // 9
};

// 3×5 bokstav-font (A–Z)
static const uint8_t LETTER_3x5[26][5] = {
  {0b010,0b101,0b111,0b101,0b101}, // A
  {0b110,0b101,0b110,0b101,0b110}, // B
  {0b011,0b100,0b100,0b100,0b011}, // C
  {0b110,0b101,0b101,0b101,0b110}, // D
  {0b111,0b100,0b110,0b100,0b111}, // E
  {0b111,0b100,0b110,0b100,0b100}, // F
  {0b011,0b100,0b101,0b101,0b011}, // G
  {0b101,0b101,0b111,0b101,0b101}, // H
  {0b111,0b010,0b010,0b010,0b111}, // I
  {0b011,0b001,0b001,0b101,0b011}, // J
  {0b101,0b101,0b110,0b101,0b101}, // K
  {0b100,0b100,0b100,0b100,0b111}, // L
  {0b101,0b111,0b101,0b101,0b101}, // M
  {0b101,0b110,0b101,0b011,0b101}, // N
  {0b111,0b101,0b101,0b101,0b111}, // O
  {0b111,0b101,0b111,0b100,0b100}, // P
  {0b010,0b101,0b101,0b111,0b001}, // Q
  {0b111,0b101,0b110,0b101,0b101}, // R
  {0b111,0b100,0b111,0b001,0b111}, // S
  {0b111,0b010,0b010,0b010,0b010}, // T
  {0b101,0b101,0b101,0b101,0b111}, // U
  {0b101,0b101,0b101,0b010,0b010}, // V
  {0b101,0b101,0b101,0b111,0b010}, // W
  {0b101,0b101,0b010,0b101,0b101}, // X
  {0b101,0b101,0b010,0b010,0b010}, // Y
  {0b111,0b001,0b010,0b100,0b111}, // Z
};

// 3×5 kolon: to prikker på rad 1 og 3
static const uint8_t COLON_3x5[5] = {0b000, 0b010, 0b000, 0b010, 0b000};

// Viser opptil 4 tegn statisk på matrisen (ingen scroll), 3×5-font, rad 1–5
inline void matrixWord4(const char* word, bool dot = false) {
  stopScroll();
  byte f[8][12] = {};
  if (dot) f[7][0] = 1;
  for (int ci = 0; ci < 4 && word[ci]; ci++) {
    char c = word[ci];
    const uint8_t* bm = nullptr;
    if      (c == ':')             bm = COLON_3x5;
    else if (c >= 'A' && c <= 'Z') bm = LETTER_3x5[c - 'A'];
    else if (c >= 'a' && c <= 'z') bm = LETTER_3x5[c - 'a'];
    else if (c >= '0' && c <= '9') bm = DIGIT_3x5[c - '0'];
    if (!bm) continue;
    int sc = ci * 3;
    for (int r = 0; r < 5; r++) {
      uint8_t bits = bm[r];
      for (int col = 0; col < 3; col++)
        if (bits & (1 << (2 - col))) f[1 + r][sc + col] = 1;
    }
  }
  ledMatrix.renderBitmap(f, 8, 12);
}

// Temperatur med 3×5-sifre og "°"-punkt øverst til høyre
inline void matrixTemp(int t, bool dot = false) {
  t = (t < 0) ? 0 : (t > 1399 ? 1399 : t);
  stopScroll();

  byte f[8][12] = {};
  f[0][11] = 1;
  if (dot) f[7][0] = 1;

  if (t >= 1000) {
    int d[4] = {1, (t % 1000) / 100, (t % 100) / 10, t % 10};
    static const int S4[4] = {-2, 2, 5, 8};
    for (int di = 0; di < 4; di++) {
      for (int row = 0; row < 5; row++) {
        uint8_t bits = DIGIT_3x5[d[di]][row];
        for (int col = 0; col < 3; col++) {
          int ac = S4[di] + col;
          if (ac >= 0 && ac < 11 && (bits & (1 << (2 - col))))
            f[1 + row][ac] = 1;
        }
      }
    }
  } else {
    int d[3] = {t / 100, (t % 100) / 10, t % 10};
    static const int S3[3] = {0, 4, 8};
    for (int di = 0; di < 3; di++) {
      for (int row = 0; row < 5; row++) {
        uint8_t bits = DIGIT_3x5[d[di]][row];
        for (int col = 0; col < 3; col++)
          if (bits & (1 << (2 - col))) f[1 + row][S3[di] + col] = 1;
      }
    }
  }
  ledMatrix.renderBitmap(f, 8, 12);
}

// Nedtellingsfyll venstre→høyre (0.0–1.0) – brukes ved hold-avbryt
inline void drawCancelCountdown(float progress) {
  stopScroll();
  byte f[8][12] = {};
  int cols = constrain((int)(progress * 12.0f + 0.5f), 0, 12);
  for (int c = 0; c < cols; c++)
    for (int r = 0; r < 8; r++) f[r][c] = 1;
  ledMatrix.renderBitmap(f, 8, 12);
}

// Fremdriftslinje med temperaturdelta-indikator
inline void drawProgressBar(float progress, float delta) {
  stopScroll();
  byte f[8][12] = {};
  int cols = constrain((int)(progress * 10.0f + 0.5f), 0, 10);
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < cols; c++) f[r][c] = 1;
  static bool pulse = false;
  pulse = !pulse;
  if (cols < 10 && pulse)
    for (int r = 0; r < 8; r++) f[r][cols] = 1;
  int top;
  if      (delta >  0.05f)  top = 0;
  else if (delta >  0.005f) top = 2;
  else if (delta < -0.05f)  top = 6;
  else if (delta < -0.005f) top = 4;
  else                      top = 3;
  f[top][11] = 1;
  if (top < 7) f[top + 1][11] = 1;
  ledMatrix.renderBitmap(f, 8, 12);
}

inline void matrixText(const char* text) {
  unsigned long now = millis();
  if (strncmp(text, _lastText, 15) == 0 && now - _lastTextMs < 4000) return;
  strncpy(_lastText, text, 15);
  _lastTextMs = now;
  _scrollActive = true;
  ledMatrix.beginDraw();
  ledMatrix.stroke(0xFFFFFFFF);
  ledMatrix.textScrollSpeed(100);
  ledMatrix.textFont(Font_5x7);
  ledMatrix.beginText(0, 1, 0xFFFFFFFF);
  ledMatrix.print(text);
  ledMatrix.endText(SCROLL_LEFT);
  ledMatrix.endDraw();
}
