#!/bin/bash
# All pictures of the wiki (docs/wiki/img/): wiring diagrams, touch screens (UI host) and web pages (demo
# device + Playwright), dark and light. Run it after a change to the UI or the wiring, then commit the pictures.
#   tools/wiki/make_shots.sh [wiring|touch|web]...      (default: all three)
# Needs (Linux / WSL): g++, Python 3 + Pillow, Node + Playwright (npm i -g playwright; npx playwright install
# chromium), LVGL 9.6 source and ArduinoJson 7 source for the UI host:
#   LVGL=<lvgl dir> AJ=<ArduinoJson>/src tools/wiki/make_shots.sh
set -e
cd "$(dirname "$0")/../.."
R=$PWD
IMG=$R/docs/wiki/img
LVGL=${LVGL:-/tmp/lvgl}
AJ=${AJ:-/tmp/AJ743/src}
TMP=${TMPDIR:-/tmp}/tempctl-wiki
mkdir -p "$TMP" "$IMG"/wiring "$IMG"/touch "$IMG"/web
WHAT=${*:-wiring touch web}

for w in $WHAT; do case $w in
wiring)
  python3 tools/wiki/wiring.py "$IMG/wiring" ;;
touch)
  (cd test/ui_host
   LVGL=$LVGL AJ=$AJ ./build.sh >/dev/null
   LVGL=$LVGL AJ=$AJ BOARD=cyd ./build.sh >/dev/null
   for b in uihost uihost_cyd; do ./$b >/dev/null; UIHOST_THEME=light ./$b >/dev/null; done)
  python3 tools/wiki/touch_png.py "$IMG/touch" ;;
web)
  g++ -std=c++17 -O1 -Isrc -I"$AJ" tools/wiki/demo_run.cpp src/run_engine.cpp src/duo_program.cpp src/settings.cpp -o "$TMP/demo_run"
  DEMO_RUN=$TMP/demo_run python3 tools/wiki/mock/server.py 8770 &
  SRV=$!
  trap 'kill $SRV 2>/dev/null' EXIT
  sleep 1.5
  NODE_PATH=${NODE_PATH:-$(npm root -g)} node tools/wiki/shots_web.js http://127.0.0.1:8770 "$TMP/web"
  kill $SRV; trap - EXIT
  # WebP (about a tenth of the PNG size; GitHub and every current browser show it)
  python3 - "$TMP/web" "$IMG/web" <<'EOF'
import glob, os, sys
from PIL import Image
src, dst = sys.argv[1:]
for f in sorted(glob.glob(os.path.join(src, '*.png'))):
    Image.open(f).convert('RGB').save(os.path.join(dst, os.path.basename(f)[:-4] + '.webp'), 'WEBP', quality=82, method=6)
print('web: %d pictures in %s' % (len(glob.glob(os.path.join(src, '*.png'))), os.path.relpath(dst)))
EOF
  ;;
*) echo "unknown part: $w (wiring, touch, web)"; exit 1 ;;
esac; done
du -sh "$IMG"
