"""Turn the frames printed by the oled-sim build into PNGs.

Usage: render.py <zmk.exe output> <out dir> (render.sh runs it)

Writes <name>.png for each frame (scaled up, lit pixels white) and all.png
with every frame stacked. Standard library only.
"""

import struct
import sys
import zlib
from pathlib import Path

SCALE = 4
LIT, OFF, BLANK, SEP = (235, 240, 255), (10, 10, 14), (40, 0, 0), (90, 90, 90)


def frames(lines):
    lines = iter(lines)
    for line in lines:
        if not line.startswith("OLED "):
            continue
        _, name, w, h, blanked = line.split()
        w, h = int(w), int(h)
        # the console breaks long lines, so read until each page has all its bytes
        pages = []
        for _ in range(h // 8):
            hexes = ""
            while len(hexes) < 2 * w:
                hexes += next(lines).strip()
            pages.append(bytes.fromhex(hexes))
        # ZMK's light theme draws black on white and displays are set up with inversion-on,
        # so LVGL's black (a 0 bit) is a lit pixel
        pixels = [[not (pages[y // 8][x] >> (y % 8)) & 1 for x in range(w)] for y in range(h)]
        yield name, pixels, blanked == "1"


def scaled(pixels, blanked):
    rows = []
    for row in pixels:
        line = []
        for lit in row:
            color = OFF if blanked or not lit else LIT
            line += [color] * (SCALE - 1) + [OFF if color == LIT else color]
        rows += [line] * (SCALE - 1) + [[OFF] * len(line)]
    if blanked:  # a dark red tint marks a blanked (sleeping) screen
        rows = [[BLANK] * len(r) for r in rows]
    return rows


def write_png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in rows)
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    log, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.png"):
        old.unlink()
    stacked = []
    for name, pixels, blanked in frames(log.read_text().splitlines()):
        rows = scaled(pixels, blanked)
        write_png(out / f"{name}.png", rows)
        stacked += rows + [[SEP] * len(rows[0])] * 3
        print(f"{name}{' (blanked)' if blanked else ''}")
    if not stacked:
        sys.exit("no frames in " + str(log))
    write_png(out / "all.png", stacked[:-3])


main()
