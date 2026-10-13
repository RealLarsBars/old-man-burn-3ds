#!/bin/sh
# Runs the Wii's self-playing test build (cd wii && make AUTOPLAY=1) in Dolphin and saves its screenshots as PNGs.
#   test/wii-shots.sh [out dir] [extra Dolphin args]     e.g. test/wii-shots.sh shots43 "-C SYSCONF.IPL.AR=0" for a 4:3 Wii
# Needs dolphin-emu-nogui (or DOLPHIN=path), xvfb-run, mtools (mcopy) and Python with Pillow.
set -e
cd "$(dirname "$0")/.."
OUT=${1:-wii/shots}; EXTRA=${2:-}; DOLPHIN=${DOLPHIN:-dolphin-emu-nogui}
command -v "$DOLPHIN" >/dev/null || { echo "no $DOLPHIN (set DOLPHIN=path)"; exit 1; }
U=$(mktemp -d); mkdir -p "$U/Config"
printf '[Core]\nWiiSDCard = True\nGFXBackend = OGL\n[Interface]\nConfirmStop = False\n[DSP]\nBackend = No Audio Output\n' > "$U/Config/Dolphin.ini"
printf '[Hacks]\nXFBToTextureEnable = False\nEFBToTextureEnable = False\n' > "$U/Config/GFX.ini"
printf '[Logs]\nOSREPORT = True\nOSREPORT_HLE = True\n[Options]\nVerbosity = 4\nWriteToConsole = True\n' > "$U/Config/Logger.ini"
xvfb-run -a -s '-screen 0 1280x720x24' "$DOLPHIN" $EXTRA -u "$U" -p headless -e wii/boot-autoplay.dol > "$U/log.txt" 2>&1 &
i=0; while [ $i -lt 300 ] && ! grep -aq 'AUTOPLAY DONE' "$U/log.txt"; do sleep 1; i=$((i + 1)); done
grep -a 'AUTOPLAY DONE' "$U/log.txt" || { echo "the test build didn't finish"; tail -20 "$U/log.txt"; }
kill $(pidof dolphin-emu-nogui) 2>/dev/null; sleep 5; kill -9 $(pidof dolphin-emu-nogui) 2>/dev/null || true
mkdir -p "$OUT/ppm"
MTOOLS_SKIP_CHECK=1 mcopy -n -i "$U/Load/WiiSD.raw" '::/apps/oldmanburn/shots/*' "$OUT/ppm/"
for f in "$OUT"/ppm/*.ppm; do python3 -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])' "$f" "$OUT/$(basename "$f" .ppm).png"; done
rm -rf "$OUT/ppm" "$U"
ls "$OUT"
