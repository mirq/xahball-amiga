#!/bin/sh
# XAHBall emulator test rig: FS-UAE (Bartman's build) inside a nested Xephyr
# display. Keys and screenshots target that display only - the desktop
# session keeps its focus and no stray key ever lands in another app.
#
# Usage:
#   tools/emu.sh deploy           copy game-build exe + data -> Dev:xahball
#   tools/emu.sh start            Xephyr :5 + FS-UAE (boots straight into the game)
#   tools/emu.sh keys "<seq>"     xkey.py sequence, e.g. "return sleep:1 down return"
#   tools/emu.sh shot <out.png>   screenshot of the emulator window
#   tools/emu.sh grab <out.png>   pixel-exact 320x256 game screen (FS-UAE's
#                                 own F12+S screenshot, cropped + halved)
#   tools/emu.sh stop             kill FS-UAE + Xephyr
#
# Host arrows + right ctrl are the Amiga joystick in port 2 (ACE JOY1) - see
# a500-xephyr.fs-uae; letters/return/shift reach the Amiga keyboard.
set -e
cd "$(dirname "$0")/.."

DPY=${EMU_DISPLAY:-:5}
FSUAE=${FSUAE_BIN:-$HOME/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/linux/fs-uae/fs-uae}
DEV_DIR=${EMU_DEV_DIR:-$HOME/Amiberry/share/Dev}
RUN_DIR=${XDG_RUNTIME_DIR:-/tmp}/xahball-emu
mkdir -p "$RUN_DIR"

case "$1" in
	deploy)
		mkdir -p "$DEV_DIR/xahball"
		# From the build dir, not dist/: the dist copy is a POST_BUILD step of
		# the exe, so asset-only changes (a glyph, a sprite) never reach it
		cp game-build/xahball.exe "$DEV_DIR/xahball/"
		chmod +x "$DEV_DIR/xahball/xahball.exe"
		cp -r game-build/data "$DEV_DIR/xahball/"
		echo "deployed to $DEV_DIR/xahball"
		;;
	start)
		if ! DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
			Xephyr "$DPY" -screen 1152x864 -ac -nolisten tcp >"$RUN_DIR/xephyr.log" 2>&1 &
			echo $! >"$RUN_DIR/xephyr.pid"
			sleep 2
		fi
		DISPLAY=$DPY "$FSUAE" tools/a500-xephyr.fs-uae >"$RUN_DIR/fsuae.log" 2>&1 &
		echo $! >"$RUN_DIR/fsuae.pid"
		echo "fs-uae pid $(cat "$RUN_DIR/fsuae.pid") on $DPY"
		;;
	keys)
		DISPLAY=$DPY python3 tools/xkey.py --pid "$(cat "$RUN_DIR/fsuae.pid")" keys "$2"
		;;
	shot)
		# The Xephyr screen holds nothing but the emulator window
		DISPLAY=$DPY import -window root "${2:-$RUN_DIR/shot.png}"
		echo "${2:-$RUN_DIR/shot.png}"
		;;
	grab)
		# FS-UAE saves the full PAL frame at 2x (754x576); the 320x256
		# display window sits at (74,36). Crop and halve with nearest
		# neighbour -> exactly the Amiga's pixels, no scaler blur.
		SHOTS=$HOME/FS-UAE/Screenshots
		BEFORE=$(ls -t "$SHOTS" 2>/dev/null | head -1)
		DISPLAY=$DPY python3 - <<'PY'
import time
from Xlib import X, XK, display
from Xlib.ext import xtest
d = display.Display()
f12 = d.keysym_to_keycode(XK.string_to_keysym("F12"))
s = d.keysym_to_keycode(XK.string_to_keysym("s"))
for code, ev, wait in ((f12, X.KeyPress, 0.15), (s, X.KeyPress, 0.1),
		(s, X.KeyRelease, 0.1), (f12, X.KeyRelease, 0)):
	xtest.fake_input(d, ev, code)
	d.sync()
	time.sleep(wait)
PY
		for _ in 1 2 3 4 5 6 7 8 9 10; do
			LATEST=$(ls -t "$SHOTS" | head -1)
			[ "$LATEST" != "$BEFORE" ] && break
			sleep 0.5
		done
		python3 -c "
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB').crop((74, 36, 714, 548))
im.resize((320, 256), Image.NEAREST).save(sys.argv[2])
" "$SHOTS/$LATEST" "${2:-$RUN_DIR/grab.png}"
		rm -f "$SHOTS/$LATEST"
		echo "${2:-$RUN_DIR/grab.png}"
		;;
	stop)
		for f in fsuae xephyr; do
			[ -f "$RUN_DIR/$f.pid" ] && kill "$(cat "$RUN_DIR/$f.pid")" 2>/dev/null || true
			rm -f "$RUN_DIR/$f.pid"
		done
		;;
	*)
		sed -n 2,15p "$0"
		exit 1
		;;
esac
