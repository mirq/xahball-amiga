#!/usr/bin/env python3
"""Menu backdrop: the title master artwork dithered into the 16-color MENU
palette (g_pMenuPalette in game.c - keep both in sync!).

  make_menu_bg.py "<master>.png"  ->  game/res/menu/menubg.png + menu.gpl

The menu palette keeps the flag colors fixed (team select draws flags with
them), so the art gets a region-restricted Floyd-Steinberg: sky, logo and
grass may only use the "scenic" entries (greens, night blues, grays) - with
the full palette everywhere the chrome logo and the grass fill up with
red/orange/cyan specks. The crowd band gets all 16 colors, where colored
specks read as fans. Error diffusion is damped (0.75) to keep flat areas calm.
All entries are exact OCS levels, so palette_conv has nothing to round.
"""
import sys

from PIL import Image

W, H = 320, 256
OUT_PNG = "game/res/menu/menubg.png"
OUT_GPL = "game/res/menu/menu.gpl"

# Must match g_pMenuPalette (game.c)
PALETTE12 = [
	0x000, 0xFFF, 0x391, 0x271, 0x125, 0x778, 0xF00, 0xC03,
	0xF80, 0xFF0, 0x0D0, 0x013, 0x06F, 0x009, 0xBBC, 0xFA0,
]
NAMES = [
	"black", "white", "grass", "grass dark", "night blue", "steel gray",
	"red", "maroon", "orange", "yellow", "green", "panel", "azure", "navy",
	"accent silver", "amber",
]
SCENIC = [0, 1, 2, 3, 4, 5, 11, 13, 14]
CROWD_Y0, CROWD_Y1 = 120, 168
DAMP = 0.75


def rgb(v):
	return tuple(((v >> s) & 0xF) * 17 for s in (8, 4, 0))


def main():
	cols = [rgb(v) for v in PALETTE12]
	small = Image.open(sys.argv[1]).convert("RGB").resize((W, H), Image.LANCZOS)
	px = [[list(small.getpixel((x, y))) for x in range(W)] for y in range(H)]
	out = Image.new("P", (W, H))
	out.putpalette([v for c in cols for v in c] + [0] * (768 - 48))
	all_idx = list(range(16))
	for y in range(H):
		allowed = all_idx if CROWD_Y0 <= y < CROWD_Y1 else SCENIC
		for x in range(W):
			r, g, b = px[y][x]
			best = min(allowed, key=lambda i: (
				(cols[i][0] - r) ** 2 * 3 + (cols[i][1] - g) ** 2 * 4 + (cols[i][2] - b) ** 2 * 2
			))
			out.putpixel((x, y), best)
			er, eg, eb = (r - cols[best][0], g - cols[best][1], b - cols[best][2])
			for dx, dy, k in ((1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1)):
				xx, yy = x + dx, y + dy
				if 0 <= xx < W and yy < H:
					f = k / 16 * DAMP
					p = px[yy][xx]
					p[0] += er * f
					p[1] += eg * f
					p[2] += eb * f
	out.save(OUT_PNG)
	with open(OUT_GPL, "w") as f:
		f.write("GIMP Palette\nName: XAHBall menu\nColumns: 4\n#\n")
		for (r, g, b), name in zip(cols, NAMES):
			f.write(f"{r:3d} {g:3d} {b:3d}\t{name}\n")
	print(f"wrote {OUT_PNG} + {OUT_GPL}")


if __name__ == "__main__":
	main()
