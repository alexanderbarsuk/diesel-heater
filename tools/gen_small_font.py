#!/usr/bin/env python3
"""
Generate an Adafruit GFX-compatible bitmap font header from a TTF.
Usage: python3 gen_small_font.py
Outputs: ../FreeSans7pt7b.h
"""
from PIL import Image, ImageDraw, ImageFont
import math, os, sys

FONT_PATH = "/System/Library/Fonts/Supplemental/Arial.ttf"
# Pillow font size in pixels. FreeSans9pt7b cap-height ≈ 10px → target ≈ 7px
# Try sizes until cap height = 7-8px (3px less than FreeSans9pt7b).
FONT_SIZE  = 12          # cap height ~9px (+2px from previous 7px)
OUT_NAME   = "FreeSans7pt7b"
OUT_FILE   = os.path.join(os.path.dirname(__file__), "..", f"{OUT_NAME}.h")
FIRST_CHAR = 0x20
LAST_CHAR  = 0x7E

font = ImageFont.truetype(FONT_PATH, FONT_SIZE)

# --- measure baseline via ascent ---
ascent, descent = font.getmetrics()

# --- helper: render one character, return (bitmap_rows, xOffset, yOffset, xAdvance) ---
def render_char(ch):
    img = Image.new("L", (80, 80), 0)
    draw = ImageDraw.Draw(img)
    draw.text((20, 20), ch, font=font, fill=255)

    # bounding box of non-zero pixels
    arr = img.load()
    xs, ys = [], []
    for y in range(80):
        for x in range(80):
            if arr[x, y] > 64:
                xs.append(x); ys.append(y)

    if not xs:
        # space / invisible char
        advance = int(font.getlength(" ")) if ch == " " else 1
        return [], 0, 0, 0, 1, advance

    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    w = x1 - x0 + 1
    h = y1 - y0 + 1

    # glyph bbox via Pillow
    bbox = font.getbbox(ch)           # (left, top, right, bottom) relative to origin
    x_off = bbox[0]                   # xOffset
    y_off = bbox[1] - ascent + 20 - 20   # adjust relative to our draw origin
    # simpler: use the actual pixel position
    # baseline in image is at y = 20 + ascent (Pillow draws with top-left at given pos)
    baseline_y = 20 + ascent
    glyph_top  = y0
    glyph_xoff = x0 - 20             # xOffset = left edge relative to cursor x

    y_offset_gfx = glyph_top - baseline_y   # negative = above baseline

    # advance
    advance = round(font.getlength(ch))

    # extract rows as list of (pixel_values) for the clipped region
    rows = []
    for y in range(y0, y1 + 1):
        row = []
        for x in range(x0, x1 + 1):
            row.append(1 if arr[x, y] > 64 else 0)
        rows.append(row)

    return rows, w, h, glyph_xoff, y_offset_gfx, advance


# --- pack bits, no per-row padding, glyph padded to byte boundary ---
def pack_glyph(rows, w, h):
    bits = []
    for row in rows:
        bits.extend(row)
    # pad to next byte boundary
    while len(bits) % 8:
        bits.append(0)
    out = []
    for i in range(0, len(bits), 8):
        byte = 0
        for b in range(8):
            if bits[i + b]:
                byte |= (0x80 >> b)
        out.append(byte)
    return out


# --- collect all glyphs ---
all_bitmap_bytes = []
glyphs = []   # (offset, w, h, xAdv, xOff, yOff)

for code in range(FIRST_CHAR, LAST_CHAR + 1):
    ch = chr(code)
    rows, w, h, xoff, yoff, adv = render_char(ch)

    offset = len(all_bitmap_bytes)
    if w == 0 or h == 0:
        glyphs.append((offset, 0, 0, adv, 0, 1))
    else:
        packed = pack_glyph(rows, w, h)
        all_bitmap_bytes.extend(packed)
        glyphs.append((offset, w, h, adv, xoff, yoff))

# yAdvance = ascent + descent + 2
y_advance = ascent + descent + 2

# --- measure cap height for info ---
_, _, cap_h, _, cap_yoff, _ = render_char("H")
print(f"H: cap_height={cap_h}, yOffset={cap_yoff}")
print(f"ascent={ascent}, descent={descent}, yAdvance={y_advance}")
print(f"Total bitmap bytes: {len(all_bitmap_bytes)}")

# --- write header ---
lines = []
lines.append(f"#pragma once")
lines.append(f"#include <Adafruit_GFX.h>")
lines.append(f"")
lines.append(f"const uint8_t {OUT_NAME}Bitmaps[] PROGMEM = {{")
# 12 bytes per line
for i in range(0, len(all_bitmap_bytes), 12):
    chunk = all_bitmap_bytes[i:i+12]
    lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
lines.append(f"}};")
lines.append(f"")
lines.append(f"const GFXglyph {OUT_NAME}Glyphs[] PROGMEM = {{")
for i, (off, w, h, adv, xoff, yoff) in enumerate(glyphs):
    ch = chr(FIRST_CHAR + i)
    comment = f"// 0x{FIRST_CHAR+i:02X} '{ch}'" if ch.isprintable() else f"// 0x{FIRST_CHAR+i:02X}"
    lines.append(f"    {{{off}, {w}, {h}, {adv}, {xoff}, {yoff}}},  {comment}")
lines.append(f"}};")
lines.append(f"")
lines.append(f"const GFXfont {OUT_NAME} PROGMEM = {{")
lines.append(f"    (uint8_t *){OUT_NAME}Bitmaps,")
lines.append(f"    (GFXglyph *){OUT_NAME}Glyphs,")
lines.append(f"    0x{FIRST_CHAR:02X}, 0x{LAST_CHAR:02X}, {y_advance}}};")
lines.append(f"")
lines.append(f"// Approx. {len(all_bitmap_bytes) + len(glyphs)*6 + 8} bytes")

with open(OUT_FILE, "w") as f:
    f.write("\n".join(lines) + "\n")

print(f"Written: {OUT_FILE}")
