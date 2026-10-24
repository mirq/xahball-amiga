#!/usr/bin/env python3
"""Snap an indexed PNG + its .gpl to exact OCS 12-bit levels (n * 17).

ACE's palette_conv rounds 8-bit channels UP when writing .plt (189 -> 0xC
instead of 0xB), so art whose palette sits between OCS levels shows up
brighter and hue-shifted on the Amiga - worst on dark colors (8,68,74 became
0x145 instead of 0x044). Snapping the source palette to n*17 makes the
conversion lossless: the screen shows exactly what the PNG shows.

Pixel indices are kept; only palette values change. Fails if two entries
would collapse onto the same OCS color (bitmap_conv matches pixels by value).

Usage: snap_ocs_palette.py image.png palette.gpl
"""
import sys

from PIL import Image


def snap(v):
    return round(v / 17) * 17


def main():
    png_path, gpl_path = sys.argv[1], sys.argv[2]

    lines = open(gpl_path).read().splitlines()
    out = []
    colors = []
    for line in lines:
        parts = line.split()
        if len(parts) >= 3 and all(p.isdigit() for p in parts[:3]):
            rgb = tuple(snap(int(p)) for p in parts[:3])
            colors.append(rgb)
            name = " ".join(parts[3:])
            out.append(f"{rgb[0]:3d} {rgb[1]:3d} {rgb[2]:3d}\t{name}".rstrip())
        else:
            out.append(line)
    if len(set(colors)) != len(colors):
        sys.exit("ERR: snapped palette has duplicate entries - requantize instead")
    open(gpl_path, "w").write("\n".join(out) + "\n")

    img = Image.open(png_path)
    if img.mode != "P":
        sys.exit("ERR: image must be indexed (mode P)")
    pal = img.getpalette()
    n = len(pal) // 3
    for i in range(n):
        pal[i * 3:i * 3 + 3] = [snap(v) for v in pal[i * 3:i * 3 + 3]]
    img.putpalette(pal)
    img.save(png_path)
    print(f"snapped {len(colors)} gpl entries, {n} png entries")


if __name__ == "__main__":
    main()
