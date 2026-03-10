#!/usr/bin/env python3
"""Generate background.h from background.png.
No scaling — crops the source to OUT_W wide and takes the first OUT_H rows at 1:1.
Splits into NUM_PARTS parts of ROWS_PART rows each.
Output arrays use __attribute__((section(".text.bitmap_zz"))) instead of PROGMEM
so they land AFTER Arduino core tables in flash (avr6 linker script places .text.*
AFTER .progmem*, keeping LPM-accessed tables below 64 KB).
"""

from PIL import Image
import struct, sys, os

SRC  = os.path.join(os.path.dirname(__file__), "background.png")
DST  = os.path.join(os.path.dirname(__file__), "..", "background.h")

OUT_W    = 320
ROWS_PART = 34
NUM_PARTS = 4
OUT_H     = ROWS_PART * NUM_PARTS  # 136

img = Image.open(SRC).convert("RGB")
# Crop to OUT_W × OUT_H at top-left (1:1 pixels, no scaling)
img = img.crop((0, 0, OUT_W, OUT_H))
pixels = list(img.getdata())  # [(r,g,b), ...]

def to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

data565 = [to_rgb565(r, g, b) for r, g, b in pixels]

SECTION = '__attribute__((section(".text.bitmap_zz")))'

with open(DST, "w") as f:
    f.write("#pragma once\n")
    f.write("#include <avr/pgmspace.h>\n\n")
    f.write(f"#define BG_IMG_W     {OUT_W}\n")
    f.write(f"#define BG_IMG_H     {OUT_H}\n")
    f.write(f"#define BG_ROWS_PART {ROWS_PART}\n")
    f.write(f"#define BG_NUM_PARTS {NUM_PARTS}\n\n")

    for part in range(NUM_PARTS):
        start = part * ROWS_PART * OUT_W
        end   = start + ROWS_PART * OUT_W
        chunk = data565[start:end]
        f.write(f"const uint16_t backgroundBitmap{part}[{len(chunk)}] {SECTION} = {{\n")
        for i in range(0, len(chunk), 16):
            row = chunk[i:i+16]
            f.write("  " + ", ".join(f"0x{v:04X}" for v in row) + ",\n")
        f.write("};\n\n")

src_size = Image.open(SRC).size
print(f"Written {DST}  (source {src_size[0]}x{src_size[1]}, crop {OUT_W}x{OUT_H}, {NUM_PARTS} parts of {ROWS_PART} rows)")
