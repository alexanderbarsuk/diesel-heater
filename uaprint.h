#pragma once
#include "display.h"

// ─── Font metrics for Ukrainian13pt (SF Mono 22pt, 2-bit AA) ─────────────────
// Update these values with the output of tools/gen_font.py when regenerating.
#define UA_ADVANCE  14   // xAdvance per character (monospace)
#define UA_ASCENT   21   // pixels above baseline
#define UA_DESCENT   4   // pixels below baseline

// Draws a UTF-8 string at the current cursor position (= baseline).
// Each character cell (UA_ADVANCE × (UA_ASCENT+UA_DESCENT+1) px) is filled
// with 'bg' before drawing — flicker-free, no Adafruit GFX bg colour needed.
// Uses 2-bit antialiased rendering (4 levels: bg / 33%fg / 67%fg / fg).
void printUA(const char* str, uint16_t fg, uint16_t bg);
