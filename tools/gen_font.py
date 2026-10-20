#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate a 16x16 bitmap font (SSD1306 column format) from Noto Sans CJK SC.

Output: main/font16.bin
  header:  "HZ16" (4B) + count (uint16 LE)
  entries: count x [codepoint (uint16 LE) + bitmap (32B)]
           (sorted by codepoint, for binary search on the device)
  bitmap:  16 columns x 2 bytes;
           low byte  = rows 0..7   (bit 0 = topmost row of that byte)
           high byte = rows 8..15

Char set: ASCII 0x20..0x7E + all GB2312 characters (≈6763 hanzi + punctuation).

Usage: python3 tools/gen_font.py
"""
import struct
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
FONT_INDEX = 2  # Noto Sans CJK SC (Simplified Chinese)
SIZE = 16
OUT = "main/font16.bin"


def collect_codepoints():
    """ASCII + every character reachable via GB2312 (2-byte qu/wei space)."""
    cps = set(range(0x20, 0x7F))  # printable ASCII
    for qu in range(0xB0, 0xF8):
        for wei in range(0xA1, 0xFF):
            try:
                ch = bytes([qu, wei]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            cp = ord(ch)
            if cp <= 0xFFFF:  # BMP only (uint16 table)
                cps.add(cp)
    return sorted(cps)


def render(font, ch):
    """Render one character to a 32-byte column-major 16x16 bitmap."""
    img = Image.new("1", (SIZE, SIZE), 0)
    d = ImageDraw.Draw(img)
    d.text((0, 0), ch, font=font, fill=1, anchor="lt")
    bmp = bytearray(32)
    px = img.load()
    for col in range(SIZE):
        lo = hi = 0
        for row in range(8):
            if px[col, row]:
                lo |= 1 << row
        for row in range(8, 16):
            if px[col, row]:
                hi |= 1 << (row - 8)
        bmp[col * 2] = lo
        bmp[col * 2 + 1] = hi
    return bytes(bmp)


def main():
    cps = collect_codepoints()
    font = ImageFont.truetype(FONT_PATH, SIZE, index=FONT_INDEX)
    print("font:", font.getname(), "chars:", len(cps))

    with open(OUT, "wb") as f:
        f.write(b"HZ16")
        f.write(struct.pack("<H", len(cps)))
        for cp in cps:
            f.write(struct.pack("<H", cp))
            f.write(render(font, chr(cp)))

    import os
    print("wrote", OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
