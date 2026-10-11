#!/usr/bin/env python3
"""Generates sprite PNGs + GIMP palette for XAHBall.

Sprites use PALETTE COLORS as roles so one sprite serves all teams. The
palette is the MATCH palette (must stay in sync with matchPaletteLoad in
game.c).

IMPORTANT: bitmap_conv maps PNG pixel colors to palette entries by VALUE,
so every dynamic slot has a UNIQUE placeholder RGB in the palette - plain
black/white would collapse onto the static slots. Draw here always uses
rgb12() tuples (never raw indices - the PNG palette carries the mask color
at index 0, which shifts everything by one).

  teamA.png        fill=0xE10 (kit A slot 4), outline=black
  teamB.png        fill=0x18E (kit B slot 5), outline=black
  teamA-flash.png  fill=0xE10, outline=amber (kick flash)
  teamB-flash.png  fill=0x18E, outline=amber
  teamA-ready.png  fill=0xE10, outline=white (kick armed indicator)
  teamB-ready.png  fill=0x18E, outline=white
  ball.png         classic football: white base, black outline, black
                   pentagon-style patches (Telstar look - NOT a flag)
Transparent areas use mask color #FF00FF (stripped by bitmap_conv -mc).

At 320x256 lores the pixel aspect is ~1:1, so discs are proper circles.
"""
import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "game", "res", "sprites")

# 12-bit palette values (same table as matchPaletteLoad in game.c)
PALETTE12 = [
    0x000, 0xFFF, 0x785, 0x453, 0xE10, 0x18E, 0x666, 0xFA0,
    0xF00, 0xB8C, 0x7A4, 0x4A7, 0x2CE, 0x111, 0x888, 0x222,
]
MASK_COLOR = (0xFF, 0x00, 0xFF)

KIT_A = 0xE10
KIT_B = 0x18E
BLACK = 0x000
WHITE = 0xFFF
AMBER = 0xFA0


def rgb12(v):
    r = (v >> 8) & 0xF
    g = (v >> 4) & 0xF
    b = v & 0xF
    return (r * 17, g * 17, b * 17)


def write_gpl(path):
    names = ["black", "white", "pitch", "stadium",
             "kitA-fill", "kitB-fill", "hud-gray", "amber",
             "post-red", "band1", "band2", "band3",
             "accent", "gray-d", "gray-l", "dark"]
    with open(path, "w") as f:
        f.write("GIMP Palette\nName: XAHBall\nColumns: 4\n#\n")
        for i, v in enumerate(PALETTE12):
            r, g, b = rgb12(v)
            f.write(f"{r:3d} {g:3d} {b:3d}\t{i:2d} {names[i]}\n")
    print(path)


def new_indexed(size=16):
    img = Image.new("P", (size, size), 0)  # index 0 = mask color
    pal = [MASK_COLOR] + [rgb12(v) for v in PALETTE12]
    img.putpalette([c for rgb in pal for c in rgb])
    return img


def make_disc(path, idx_fill, idx_outline, radius=8, digit=None):
    """Circle disc, 1px outline, centered in a 16x16 indexed PNG.
    Interleaved .bm requires width divisible by 16, so all sprites are 16px.
    digit: 1..3 - player number, 3x5 pixels, white with a 1px black shadow
    (readable on any kit fill)."""
    img = new_indexed()
    d = ImageDraw.Draw(img)
    c = 8
    d.ellipse([c - radius, c - radius, c + radius - 1, c + radius - 1],
              fill=rgb12(idx_outline))
    d.ellipse([c - radius + 1, c - radius + 1, c + radius - 2, c + radius - 2],
              fill=rgb12(idx_fill))
    if digit is not None:
        # 3x5 digit font for 1..3
        glyphs = {
            1: ["010", "110", "010", "010", "111"],
            2: ["111", "001", "111", "100", "111"],
            3: ["111", "001", "011", "001", "111"],
        }
        rows = glyphs[digit]
        ox, oy = 6, 5  # 3x5 block centered-ish in the 16px box
        for gy in range(5):
            for gx in range(3):
                if rows[gy][gx] == '1':
                    # black shadow first, white core on top
                    if 0 <= ox + gx + 1 < 16 and 0 <= oy + gy + 1 < 16:
                        img.putpixel((ox + gx + 1, oy + gy + 1), rgb12(BLACK))
                    img.putpixel((ox + gx, oy + gy), rgb12(WHITE))
    img.save(path)
    print(path)


def make_ball(path, radius=5):
    """Classic football (Telstar look): white base, black outline and black
    pentagon-style patches - one center patch plus four ring patches."""
    img = new_indexed()
    c = 8
    r_out2 = radius * radius          # outline ring outer edge
    r_in2 = (radius - 1) * (radius - 1)  # fill area edge
    # patch centers: one in the middle + four on the rim (half-clipped by
    # the circle edge - the classic Telstar silhouette at this size)
    patches = [(c, c), (c, 4), (c, 12), (4, c), (12, c)]
    for py in range(16):
        for px in range(16):
            dx = px + 0.5 - c
            dy = py + 0.5 - c
            d2 = dx * dx + dy * dy
            if d2 > r_out2:
                continue  # transparent
            color = rgb12(WHITE)
            if d2 > r_in2:
                color = rgb12(BLACK)  # outline
            else:
                for (pcx, pcy) in patches:
                    pdx = px + 0.5 - pcx
                    pdy = py + 0.5 - pcy
                    if abs(pdx) <= 1.0 and abs(pdy) <= 1.0:
                        color = rgb12(BLACK)
                        break
            img.putpixel((px, py), color)
    img.save(path)
    print(path)


def main():
    os.makedirs(OUT, exist_ok=True)
    write_gpl(os.path.join(os.path.dirname(OUT), "game.gpl"))
    # Player discs carry their slot number (1..3 per side) so players are
    # distinguishable; all three variants (normal/flash/ready) are numbered
    for side, fill in (("A", KIT_A), ("B", KIT_B)):
        for num in (1, 2, 3):
            make_disc(os.path.join(OUT, f"team{side}{num}.png"), fill, BLACK, digit=num)
            make_disc(os.path.join(OUT, f"team{side}{num}-flash.png"), fill, AMBER, digit=num)
            make_disc(os.path.join(OUT, f"team{side}{num}-ready.png"), fill, WHITE, digit=num)
    make_ball(os.path.join(OUT, "ball.png"))
    print("done")


if __name__ == "__main__":
    main()
