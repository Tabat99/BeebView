#!/usr/bin/env python3
"""Generate BeebView packaging icons from the embedded 12x20 Mode 7 bitmap.

No external font or imaging library is required. The glyph source is the same
bitmap table used by the SDL frontend, so the application icon stays visually
consistent with BeebView itself.
"""
from __future__ import annotations

import argparse
import binascii
import re
import struct
import zlib
from pathlib import Path

WIDTH = 256
HEIGHT = 256
FONT_W = 12
FONT_H = 20
SCALE = 4
TEXT_LINES = ("Beeb", "View")


def load_glyphs(source: Path) -> dict[int, list[int]]:
    text = source.read_text(encoding="utf-8")
    pattern = re.compile(
        r"/\*\s*(\d+)\s+.*?\*/\s*\{\s*([^}]*)\}", re.DOTALL
    )
    glyphs: dict[int, list[int]] = {}
    for match in pattern.finditer(text):
        code = int(match.group(1))
        if not 32 <= code <= 126:
            continue
        rows = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{1,4})", match.group(2))]
        if len(rows) == FONT_H:
            glyphs[code] = rows
    missing = sorted({ord(c) for line in TEXT_LINES for c in line} - glyphs.keys())
    if missing:
        raise RuntimeError(f"embedded bitmap font is missing glyphs: {missing}")
    return glyphs


def text_origin(line: str, y: int) -> tuple[int, int]:
    width = len(line) * FONT_W * SCALE
    return ((WIDTH - width) // 2, y)


def pixel_rects(glyphs: dict[int, list[int]]):
    y_positions = (34, 138)
    for line, y in zip(TEXT_LINES, y_positions):
        x, _ = text_origin(line, y)
        for index, char in enumerate(line):
            rows = glyphs[ord(char)]
            char_x = x + index * FONT_W * SCALE
            for gy, bits in enumerate(rows):
                for gx in range(FONT_W):
                    if bits & (0x8000 >> gx):
                        yield (
                            char_x + gx * SCALE,
                            y + gy * SCALE,
                            SCALE,
                            SCALE,
                        )


def write_svg(path: Path, glyphs: dict[int, list[int]]) -> None:
    rects = "\n".join(
        f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#ffffff"/>'
        for x, y, w, h in pixel_rects(glyphs)
    )
    svg = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" '
        f'viewBox="0 0 {WIDTH} {HEIGHT}" shape-rendering="crispEdges">\n'
        f'  <rect width="{WIDTH}" height="{HEIGHT}" fill="#000000"/>\n'
        f'{rects}\n'
        '</svg>\n'
    )
    path.write_text(svg, encoding="utf-8")


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(
        ">I", binascii.crc32(kind + payload) & 0xFFFFFFFF
    )


def write_png(path: Path, glyphs: dict[int, list[int]]) -> None:
    # RGB image. Start black, then paint white bitmap pixels.
    pixels = bytearray(WIDTH * HEIGHT * 3)
    for x, y, w, h in pixel_rects(glyphs):
        for py in range(y, min(y + h, HEIGHT)):
            for px in range(x, min(x + w, WIDTH)):
                off = (py * WIDTH + px) * 3
                pixels[off : off + 3] = b"\xff\xff\xff"

    scanlines = bytearray()
    stride = WIDTH * 3
    for y in range(HEIGHT):
        scanlines.append(0)  # PNG filter type 0
        start = y * stride
        scanlines.extend(pixels[start : start + stride])

    data = bytearray(b"\x89PNG\r\n\x1a\n")
    data.extend(png_chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)))
    data.extend(png_chunk(b"IDAT", zlib.compress(bytes(scanlines), level=9)))
    data.extend(png_chunk(b"IEND", b""))
    path.write_bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate the BeebView Mode 7 application icon")
    parser.add_argument("--source", required=True, help="path to src/frontend/sdl3/bitmap_font.c")
    parser.add_argument("--png", required=True, help="output PNG path")
    parser.add_argument("--svg", required=True, help="output SVG path")
    args = parser.parse_args()

    source = Path(args.source)
    png = Path(args.png)
    svg = Path(args.svg)
    png.parent.mkdir(parents=True, exist_ok=True)
    svg.parent.mkdir(parents=True, exist_ok=True)

    glyphs = load_glyphs(source)
    write_png(png, glyphs)
    write_svg(svg, glyphs)
    print(f"Generated {png}")
    print(f"Generated {svg}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
