#!/bin/sh
# XAHBall dist builder: builds the game, assembles the Amiga-ready dist
# folder, packs LHA + ZIP and pushes both to the FTP server.
# Usage: tools/dist.sh [--no-build] [--no-upload]
set -e
cd "$(dirname "$0")/.."   # repo root

FTP_HOST="szajb.us"
FTP_USER="082B72E6F7"
FTP_PASS="082B72E6F7"
FTP_DIR="PUBLIC"

DO_BUILD=1
DO_UPLOAD=1
for arg in "$@"; do
	case "$arg" in
		--no-build) DO_BUILD=0 ;;
		--no-upload) DO_UPLOAD=0 ;;
		*) echo "unknown option: $arg"; exit 1 ;;
	esac
done

if [ "$DO_BUILD" = 1 ]; then
	cmake --build game-build -j 8
fi

# Assemble the dist folder
rm -rf dist
mkdir -p dist/xahball
cp game-build/xahball.exe dist/xahball/
cp -r game-build/data dist/xahball/data
cp tools/dist-README.txt dist/xahball/README-INSTALL.txt
chmod +x dist/xahball/xahball.exe

# Pack LHA (Amiga-native) + ZIP
cd dist
rm -f xahball.lha xahball.zip
lha a xahball.lha xahball > /dev/null
zip -rq xahball.zip xahball
echo "packed: xahball.lha $(du -h xahball.lha | cut -f1), xahball.zip $(du -h xahball.zip | cut -f1)"

# Push to FTP
if [ "$DO_UPLOAD" = 1 ]; then
	for f in xahball.lha xahball.zip; do
		curl -s --ftp-pasv -T "$f" "ftp://$FTP_USER:$FTP_PASS@$FTP_HOST/$FTP_DIR/$f" \
			&& echo "uploaded: $FTP_DIR/$f"
	done
fi
