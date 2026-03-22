#include "smallprint.h"
#include "display.h"
#include <avr/pgmspace.h>

// ─── 2-bit AA colour blend (RGB565) ──────────────────────────────────────────
// level 1 ≈ 33% fg, level 2 ≈ 67% fg  (same formula as uaprint.cpp)
static uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t level) {
  uint8_t bgR = (bg >> 11) & 0x1F, bgG = (bg >> 5) & 0x3F, bgB = bg & 0x1F;
  uint8_t fgR = (fg >> 11) & 0x1F, fgG = (fg >> 5) & 0x3F, fgB = fg & 0x1F;
  uint8_t r, g, b;
  if (level == 1) {
    r = (2*bgR + fgR) / 3;  g = (2*bgG + fgG) / 3;  b = (2*bgB + fgB) / 3;
  } else {
    r = (bgR + 2*fgR) / 3;  g = (bgG + 2*fgG) / 3;  b = (bgB + 2*fgB) / 3;
  }
  return ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
}

// ─── printSmall ──────────────────────────────────────────────────────────────
int16_t printSmall(const SmallFont* f, const char* s, int16_t x, int16_t baseline,
                   uint16_t fg, uint16_t bg) {
  while (*s) {
    uint8_t c = (uint8_t)*s++;
    if (c < f->first || c > f->last) continue;
    uint8_t gi = c - f->first;

    uint16_t bo  = pgm_read_word(&f->glyphs[gi].bitmapOffset);
    uint8_t  w   = pgm_read_byte(&f->glyphs[gi].width);
    uint8_t  h   = pgm_read_byte(&f->glyphs[gi].height);
    uint8_t  adv = pgm_read_byte(&f->glyphs[gi].xAdvance);
    int8_t   xo  = (int8_t)pgm_read_byte(&f->glyphs[gi].xOffset);
    int8_t   yo  = (int8_t)pgm_read_byte(&f->glyphs[gi].yOffset);

    if (w > 0 && h > 0) {
      int16_t gx = x + xo;
      int16_t gy = baseline + yo;
      // fill glyph bbox with bg first (clean AA edges)
      tft.fillRect(gx, gy, w, h, bg);
      // stream pixels via address window
      tft.startWrite();
      tft.setAddrWindow(gx, gy, w, h);
      uint16_t total = (uint16_t)w * h;
      for (uint16_t pi = 0; pi < total; pi++) {
        uint8_t shift = 6 - ((pi & 3) << 1);
        uint8_t level = (pgm_read_byte(&f->bitmap[bo + (pi >> 2)]) >> shift) & 0x03;
        uint16_t color = (level == 3) ? fg
                       : (level == 0) ? bg
                       : blend565(bg, fg, level);
        tft.writeColor(color, 1);
      }
      tft.endWrite();
    }
    x += adv;
  }
  return x;
}

// ─── smallTextWidth ───────────────────────────────────────────────────────────
uint16_t smallTextWidth(const SmallFont* f, const char* s) {
  uint16_t w = 0;
  while (*s) {
    uint8_t c = (uint8_t)*s++;
    if (c < f->first || c > f->last) continue;
    w += pgm_read_byte(&f->glyphs[c - f->first].xAdvance);
  }
  return w;
}
