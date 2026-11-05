#!/bin/sh
# XAHBall bootable floppy: packs an assembled dist folder into a DD ADF that
# boots straight into the game (OFS + 1.3 bootblock, so it also boots on a
# stock Kickstart 1.3 A500).
#
# Usage: tools/make_adf.sh <dist folder with xahball.exe + data/> <out.adf>
# Needs xdftool from amitools (pip install amitools).
set -e

SRC=$1
OUT=$2
if [ -z "$SRC" ] || [ -z "$OUT" ] || [ ! -f "$SRC/xahball.exe" ]; then
	sed -n 2,8p "$0"
	exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# The game loads data/... relative to the current dir - on a floppy boot
# that is the disk root, so the exe and data/ sit at the top level
printf 'xahball.exe\n' >"$TMP/startup-sequence"

rm -f "$OUT"
xdftool "$OUT" create size=880Ki + format XAHBALL ofs + boot install boot1x
xdftool "$OUT" makedir s
xdftool "$OUT" write "$TMP/startup-sequence" s/startup-sequence
xdftool "$OUT" write "$SRC/xahball.exe" xahball.exe
xdftool "$OUT" protect xahball.exe rwed
xdftool "$OUT" write "$SRC/data" data
[ -f "$SRC/README-INSTALL.txt" ] && xdftool "$OUT" write "$SRC/README-INSTALL.txt" README.txt
xdftool "$OUT" info
