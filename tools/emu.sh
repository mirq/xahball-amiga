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
	stop)
		for f in fsuae xephyr; do
			[ -f "$RUN_DIR/$f.pid" ] && kill "$(cat "$RUN_DIR/$f.pid")" 2>/dev/null || true
			rm -f "$RUN_DIR/$f.pid"
		done
		;;
	*)
		sed -n 2,13p "$0"
		exit 1
		;;
esac
