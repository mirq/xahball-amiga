#!/usr/bin/env python3
"""Converts the XAHBALL title master artwork into the 32-color Amiga asset.

  make_title.py "<master>.png"   ->  game/res/logo/title.png + title.gpl

The palette is picked in OCS 12-bit space: the downscaled image is snapped
to 4-bit levels (n * 17) before median-cut, the chosen entries are snapped
again, then the full-precision image is Floyd-Steinberg dithered against
them. Every entry is therefore an exact OCS color - palette_conv's 8->4 bit
round-up (which brightened and hue-shifted the old asset) has nothing to
round. Entry 0 is forced to pure black (letterbox / fade target).

Median-cut beat MAXCOVERAGE/octree on this art: saturated balls and grass
stay vivid, no hue bleeding on the red/blue discs.
"""
import sys

from PIL import Image

W, H = 320, 256
COLORS = 32
OUT_PNG = "game/res/logo/title.png"
OUT_GPL = "game/res/logo/title.gpl"


def snap(v):
    return round(v / 17) * 17


def main():
    src = Image.open(sys.argv[1]).convert("RGB")
    small = src.resize((W, H), Image.LANCZOS)

    q = small.point(snap).quantize(COLORS, method=Image.Quantize.MEDIANCUT, kmeans=4)
    pal = [snap(v) for v in q.getpalette()[:COLORS * 3]]
    cols = []
    for i in range(COLORS):
        c = tuple(pal[i * 3:i * 3 + 3])
        if c not in cols and c != (0, 0, 0):
            cols.append(c)
    cols = [(0, 0, 0)] + cols[:COLORS - 1]
    if len(cols) < COLORS:
        sys.exit(f"ERR: only {len(cols)} unique OCS colors - bitmap_conv needs unique entries")

    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette([v for c in cols for v in c] + [0] * (768 - COLORS * 3))
    out = small.quantize(palette=pal_img, dither=Image.Dither.FLOYDSTEINBERG)
    out.save(OUT_PNG)

    with open(OUT_GPL, "w") as f:
        f.write("GIMP Palette\nName: XAHBall title - Midnight Stadium Showdown\nColumns: 8\n#\n")
        for i, (r, g, b) in enumerate(cols):
            f.write(f"{r:3d} {g:3d} {b:3d}\tc{i:02d}\n")
    print(f"wrote {OUT_PNG} + {OUT_GPL} ({len(cols)} OCS colors)")


if __name__ == "__main__":
    main()
