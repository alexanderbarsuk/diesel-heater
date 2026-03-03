#include "uaprint.h"
#include "Ukrainian13pt.h"

// ─── UTF-8 decoder ───────────────────────────────────────────────────────────
// Reads one codepoint from *p, advances *p past the consumed bytes.
// Returns 0 at end-of-string, '?' (0x3F) on invalid sequence.
static uint32_t utf8Decode(const char** p) {
  uint8_t c = (uint8_t)**p;
  if (!c) return 0;
  if (c < 0x80) { (*p)++; return c; }

  uint32_t cp;
  uint8_t  extra;
  if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
  else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
  else                          { (*p)++; return 0x3F; }
  (*p)++;

  while (extra--) {
    uint8_t b = (uint8_t)**p;
    if ((b & 0xC0) != 0x80) return 0x3F;
    cp = (cp << 6) | (b & 0x3F);
    (*p)++;
  }
  return cp;
}

// ─── Unicode → Ukrainian13pt font position ───────────────────────────────────
// Layout: 0x20-0x7E ASCII | 0x80-0x9F А-Я | 0xA0-0xBF а-я
//         0xC0 Є  0xC1 є  0xC2 Ї  0xC3 ї  0xC4 І  0xC5 і  0xC6 Ґ  0xC7 ґ  0xC8 °
static uint8_t cpToFont(uint32_t cp) {
  if (cp >= 0x0020 && cp <= 0x007E) return (uint8_t)cp;
  if (cp >= 0x0410 && cp <= 0x042F) return 0x80 + (uint8_t)(cp - 0x0410);
  if (cp >= 0x0430 && cp <= 0x044F) return 0xA0 + (uint8_t)(cp - 0x0430);
  switch (cp) {
    case 0x0404: return 0xC0;  // Є
    case 0x0454: return 0xC1;  // є
    case 0x0407: return 0xC2;  // Ї
    case 0x0457: return 0xC3;  // ї
    case 0x0406: return 0xC4;  // І
    case 0x0456: return 0xC5;  // і
    case 0x0490: return 0xC6;  // Ґ
    case 0x0491: return 0xC7;  // ґ
    case 0x00B0: return 0xC8;  // °
  }
  return 0x3F;  // '?'
}

// ─── 2-bit AA colour blend ────────────────────────────────────────────────────
// level 1 ≈ 33% fg (faint AA edge), level 2 ≈ 67% fg (strong AA edge)
static uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t level) {
  uint8_t bgR = (bg >> 11) & 0x1F, bgG = (bg >> 5) & 0x3F, bgB = bg & 0x1F;
  uint8_t fgR = (fg >> 11) & 0x1F, fgG = (fg >> 5) & 0x3F, fgB = fg & 0x1F;
  uint8_t r, g, b;
  if (level == 1) {
    r = (2*bgR + fgR) / 3;  g = (2*bgG + fgG) / 3;  b = (2*bgB + fgB) / 3;
  } else {  // level == 2
    r = (bgR + 2*fgR) / 3;  g = (bgG + 2*fgG) / 3;  b = (bgB + 2*fgB) / 3;
  }
  return ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
}

// ─── 2-bit AA glyph renderer ──────────────────────────────────────────────────
// Reads glyph metadata from Ukrainian13ptGlyphs (PROGMEM GFXglyph structs) and
// 2-bit packed pixel data from Ukrainian13ptBitmaps. 4 pixels per byte, MSB-first.
static void drawGlyphAA(uint8_t fontChar, int16_t cx, int16_t baseline,
                         uint16_t fg, uint16_t bg) {
  if (fontChar < 0x20 || fontChar > 0xC8) return;
  uint8_t  gi = fontChar - 0x20;
  uint16_t bo = pgm_read_word(&Ukrainian13ptGlyphs[gi].bitmapOffset);
  uint8_t  w  = pgm_read_byte(&Ukrainian13ptGlyphs[gi].width);
  uint8_t  h  = pgm_read_byte(&Ukrainian13ptGlyphs[gi].height);
  int8_t   xo = (int8_t)pgm_read_byte(&Ukrainian13ptGlyphs[gi].xOffset);
  int8_t   yo = (int8_t)pgm_read_byte(&Ukrainian13ptGlyphs[gi].yOffset);

  if (w == 0 || h == 0) return;

  int16_t x0 = cx + xo;
  int16_t y0 = baseline + yo;

  tft.startWrite();
  uint16_t pi = 0;
  for (uint8_t row = 0; row < h; row++) {
    for (uint8_t col = 0; col < w; col++) {
      uint8_t shift = 6 - ((pi & 3) << 1);
      uint8_t level = (pgm_read_byte(&Ukrainian13ptBitmaps[bo + (pi >> 2)]) >> shift) & 0x03;
      pi++;
      if (level == 0) continue;
      uint16_t color = (level == 3) ? fg : blend565(bg, fg, level);
      tft.writePixel(x0 + col, y0 + row, color);
    }
  }
  tft.endWrite();
}

// ─── printUA ─────────────────────────────────────────────────────────────────
// Draws a UTF-8 string at the current cursor (= baseline) using 2-bit AA.
// Each UA_ADVANCE-wide cell is erased with 'bg' before drawing the glyph.
void printUA(const char* str, uint16_t fg, uint16_t bg) {
  int16_t baseline = tft.getCursorY();
  int16_t top      = baseline - UA_ASCENT;

  while (*str) {
    uint32_t cp = utf8Decode(&str);
    if (!cp) break;
    int16_t cx = tft.getCursorX();
    tft.fillRect(cx, top, UA_ADVANCE, UA_ASCENT + UA_DESCENT + 1, bg);
    if (cp != 0x20) {
      drawGlyphAA(cpToFont(cp), cx, baseline, fg, bg);
    }
    tft.setCursor(cx + UA_ADVANCE, baseline);
  }
}

// ─── printUAn ────────────────────────────────────────────────────────────────
// Draws exactly n codepoints from str starting at codepoint offset skip.
// Pads with spaces if the string has fewer than skip+n codepoints.
void printUAn(const char* str, uint8_t skip, uint8_t n, uint16_t fg, uint16_t bg) {
  int16_t baseline = tft.getCursorY();
  int16_t top      = baseline - UA_ASCENT;

  // Skip the first 'skip' codepoints
  while (skip > 0 && *str) {
    utf8Decode(&str);
    skip--;
  }

  // Draw exactly 'n' codepoints (pad with spaces if string is exhausted)
  for (uint8_t i = 0; i < n; i++) {
    uint32_t cp = *str ? utf8Decode(&str) : 0x20;
    int16_t cx = tft.getCursorX();
    tft.fillRect(cx, top, UA_ADVANCE, UA_ASCENT + UA_DESCENT + 1, bg);
    if (cp != 0x20) {
      drawGlyphAA(cpToFont(cp), cx, baseline, fg, bg);
    }
    tft.setCursor(cx + UA_ADVANCE, baseline);
  }
}
