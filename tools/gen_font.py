#!/usr/bin/env python3
"""
Генерує Adafruit GFX font header з ASCII + українська кирилиця.
Використовує SFNSMono.ttf (SF Mono — моноширинний, є кирилиця).
2-bit antialiasing: 4 рівні (0=bg, 1=33%fg, 2=67%fg, 3=fg), 4px/byte MSB-first.

Розкладка font-позицій:
  0x20-0x7E  — ASCII (95 символів)
  0x7F       — заповнювач
  0x80-0x9F  — А-Я  (Unicode 0x0410-0x042F, 32 символи)
  0xA0-0xBF  — а-я  (Unicode 0x0430-0x044F, 32 символи)
  0xC0       — Є (0x0404)   0xC1 — є (0x0454)
  0xC2       — Ї (0x0407)   0xC3 — ї (0x0457)
  0xC4       — І (0x0406)   0xC5 — і (0x0456)
  0xC6       — Ґ (0x0490)   0xC7 — ґ (0x0491)
"""
from PIL import Image, ImageDraw, ImageFont
import sys, os

FONT_PATH  = "/System/Library/Fonts/SFNSMono.ttf"  # SF Mono — чистий сучасний моноширинний
FONT_SIZE  = 22        # пікселів; при 22pt xAdvance=14
FONT_IDX   = 0
OUT_FILE   = os.path.join(os.path.dirname(__file__), "..", "Ukrainian13pt.h")
FONT_NAME  = "Ukrainian13pt"
BASELINE_Y = 22        # де baseline у render-полотні (22pt потребує більше місця)

pil_font = ImageFont.truetype(FONT_PATH, FONT_SIZE, index=FONT_IDX)

# ─── Таблиця символів: (unicode_cp, font_pos) ───────────────────────────────
char_table = []
for cp in range(0x20, 0x7F):         char_table.append((cp,      cp))
char_table.append((0,        0x7F))  # filler
for i in range(32):                   char_table.append((0x0410+i, 0x80+i))
for i in range(32):                   char_table.append((0x0430+i, 0xA0+i))
for cp, pos in [(0x0404,0xC0),(0x0454,0xC1),(0x0407,0xC2),(0x0457,0xC3),
                (0x0406,0xC4),(0x0456,0xC5),(0x0490,0xC6),(0x0491,0xC7),
                (0x00B0,0xC8)]:  # ° degree sign
    char_table.append((cp, pos))

FIRST = 0x20
LAST  = 0xC8

# ─── Рендер одного символу ───────────────────────────────────────────────────
def render(unicode_cp):
    """Повертає (values_list, w, h, xoffset, yoffset, xadvance)
    values_list: квантовані 2-bit значення (0-3) для кожного пікселя"""
    if unicode_cp == 0:
        return [], 0, 0, 0, 0, 4

    ch = chr(unicode_cp)
    W, H = 60, 45  # ширше полотно для великого шрифту

    img  = Image.new('L', (W, H), 0)
    draw = ImageDraw.Draw(img)
    try:
        draw.text((4, BASELINE_Y), ch, font=pil_font, fill=255, anchor='ls')
    except TypeError:
        draw.text((4, BASELINE_Y), ch, font=pil_font, fill=255)

    pix = img.load()

    # bounding box: поріг > 20 щоб захопити AA-краї
    rows = [y for y in range(H) if any(pix[x,y] > 20 for x in range(W))]
    cols = [x for x in range(W) if any(pix[x,y] > 20 for y in range(H))]

    if not rows or not cols:
        # Невидимий гліф (пробіл тощо): w=0,h=0 → Adafruit GFX пропускає малювання
        try:
            adv = int(pil_font.getlength(ch))
        except AttributeError:
            adv = FONT_SIZE // 2
        return [], 0, 0, 0, 0, adv or 4

    y0, y1 = rows[0],  rows[-1]
    x0, x1 = cols[0],  cols[-1]
    w = x1 - x0 + 1
    h = y1 - y0 + 1

    # 2-bit квантизація: 4 рівні (0=прозорий, 1=33%fg, 2=67%fg, 3=повний fg)
    values = []
    for y in range(y0, y1+1):
        for x in range(x0, x1+1):
            p = pix[x, y]
            values.append(min(3, p // 64))

    try:
        adv = int(pil_font.getlength(ch))
    except AttributeError:
        adv = FONT_SIZE // 2

    xoff = x0 - 4
    yoff = y0 - BASELINE_Y
    return values, w, h, xoff, yoff, adv

def pack_2bit(values):
    """Пакує значення 0-3 по 4 в байт (MSB-first: bits 7-6, 5-4, 3-2, 1-0)"""
    out, cur, cnt = [], 0, 0
    for v in values:
        cur = (cur << 2) | (v & 3)
        cnt += 1
        if cnt == 4:
            out.append(cur); cur = 0; cnt = 0
    if cnt:
        out.append(cur << ((4 - cnt) * 2))
    return out

# ─── Рендер усіх символів ────────────────────────────────────────────────────
glyphs = {}  # font_pos → (packed_bytes, w, h, xoff, yoff, adv)
for cp, pos in char_table:
    values, w, h, xoff, yoff, adv = render(cp)
    glyphs[pos] = (pack_2bit(values), w, h, xoff, yoff, adv)

# ─── Формат Adafruit GFX ─────────────────────────────────────────────────────
bitmap_data = []
glyph_entries = []
offset = 0
y_advance_max = 0

for pos in range(FIRST, LAST+1):
    packed, w, h, xoff, yoff, adv = glyphs[pos]
    glyph_entries.append((offset, w, h, adv, xoff, yoff))
    bitmap_data.extend(packed)
    offset += len(packed)
    if w > 0 and h > 0:
        y_advance_max = max(y_advance_max, h - yoff)

y_advance = y_advance_max + 2

# ─── Метрики для UA_ASCENT / UA_DESCENT ──────────────────────────────────────
valid_entries = [(boff, w, h, adv, xoff, yoff)
                 for (boff, w, h, adv, xoff, yoff) in glyph_entries
                 if w > 0 and h > 0]
min_yoff    = min(yoff for (_, _, _, _, _, yoff) in valid_entries)
max_descent = max(yoff + h - 1 for (_, _, h, _, _, yoff) in valid_entries)
ua_ascent   = -min_yoff
ua_descent  = max(0, max_descent)
ua_advance  = glyphs[ord('A')][5]  # xAdvance моноширинного шрифту

# ─── Запис заголовку ─────────────────────────────────────────────────────────
with open(OUT_FILE, 'w', encoding='utf-8') as f:
    f.write(f"// Auto-generated Adafruit GFX font: {FONT_NAME}\n")
    f.write(f"// Source: SF Mono {FONT_SIZE}pt, ASCII + Ukrainian Cyrillic\n")
    f.write(f"// 2-bit antialiasing: 4 levels per pixel, 4 pixels per byte (MSB-first)\n")
    f.write(f"// Font positions:  0x20-0x7E ASCII | 0x80-0x9F А-Я | 0xA0-0xBF а-я\n")
    f.write(f"//                  0xC0 Є 0xC1 є 0xC2 Ї 0xC3 ї 0xC4 І 0xC5 і 0xC6 Ґ 0xC7 ґ\n")
    f.write(f"// Metrics: UA_ADVANCE={ua_advance}  UA_ASCENT={ua_ascent}  UA_DESCENT={ua_descent}\n\n")
    f.write("#pragma once\n#include <Adafruit_GFX.h>\n\n")

    # bitmap (2-bit packed)
    f.write(f"static const uint8_t {FONT_NAME}Bitmaps[] PROGMEM = {{\n  ")
    for i, b in enumerate(bitmap_data):
        f.write(f"0x{b:02X}")
        if i < len(bitmap_data)-1: f.write(",")
        if (i+1) % 16 == 0: f.write("\n  ")
    f.write("\n};\n\n")

    # glyphs
    f.write(f"static const GFXglyph {FONT_NAME}Glyphs[] PROGMEM = {{\n")
    for i, (boff, w, h, adv, xoff, yoff) in enumerate(glyph_entries):
        pos = FIRST + i
        comment = f"0x{pos:02X}"
        if 0x20 <= pos <= 0x7E:   comment += f" '{chr(pos)}'"
        elif 0x80 <= pos <= 0x9F: comment += f" '{chr(0x0410 + pos - 0x80)}'"
        elif 0xA0 <= pos <= 0xBF: comment += f" '{chr(0x0430 + pos - 0xA0)}'"
        f.write(f"  {{ {boff:5}, {w:3}, {h:3}, {adv:3}, {xoff:4}, {yoff:4} }},  // {comment}\n")
    f.write("};\n\n")

    # font struct (kept for metadata access; bitmap is 2-bit AA, not 1-bit!)
    f.write(f"// NOTE: bitmap is 2-bit AA packed (4px/byte). Use drawGlyphAA(), not tft.write()!\n")
    f.write(f"static const GFXfont {FONT_NAME} PROGMEM = {{\n")
    f.write(f"  (uint8_t  *){FONT_NAME}Bitmaps,\n")
    f.write(f"  (GFXglyph *){FONT_NAME}Glyphs,\n")
    f.write(f"  0x{FIRST:02X}, 0x{LAST:02X}, {y_advance}\n")
    f.write(f"}};\n")

# Виводимо метрики для налаштування коду
print(f"OK → {OUT_FILE}")
print(f"Bitmap size: {len(bitmap_data)} bytes")
print(f"yAdvance: {y_advance}")
print(f"UA_ADVANCE  = {ua_advance}")
print(f"UA_ASCENT   = {ua_ascent}")
print(f"UA_DESCENT  = {ua_descent}")
