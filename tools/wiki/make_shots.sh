#!/bin/bash
# All pictures of the wiki (docs/wiki/img/): wiring diagrams, touch screens (UI host), web pages (mock device +
# Playwright) and the browser demo, dark and light. Run it after a change to the UI or the wiring, then commit
# the pictures.
#   tools/wiki/make_shots.sh [wiring|touch|web|demo]...      (default: all four)
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
WHAT=${*:-wiring touch web demo}
# PNGs -> WebP (about a tenth of the size; GitHub and every current browser show it)
towebp() {
  python3 - "$1" "$2" <<'EOF'
import glob, os, sys
from PIL import Image
src, dst = sys.argv[1:]
for f in sorted(glob.glob(os.path.join(src, '*.png'))):
    Image.open(f).convert('RGB').save(os.path.join(dst, os.path.basename(f)[:-4] + '.webp'), 'WEBP', quality=82, method=6)
print('%d pictures in %s' % (len(glob.glob(os.path.join(src, '*.png'))), os.path.relpath(dst)))
EOF
}

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
  towebp "$TMP/web" "$IMG/web"
  ;;
demo)
  # the browser demo (docs/wiki/Demo.md): needs demo/out/tempctl-demo.wasm (demo/build.sh)
  python3 tools/build_site.py "$TMP/site" --web >/dev/null
  [ -f "$TMP/site/demo/tempctl-demo.wasm" ] || { echo "demo: build it first (demo/build.sh)"; exit 1; }
  python3 -m http.server 8790 --bind 127.0.0.1 --directory "$TMP/site" >/dev/null 2>&1 &
  SRV=$!
  trap 'kill $SRV 2>/dev/null' EXIT
  sleep 1
  mkdir -p "$TMP/demo"
  NODE_PATH=${NODE_PATH:-$(npm root -g)} node tools/wiki/shots_demo.js http://127.0.0.1:8790 "$TMP/demo"
  kill $SRV; trap - EXIT
  towebp "$TMP/demo" "$IMG/web" ;;
*) echo "unknown part: $w (wiring, touch, web, demo)"; exit 1 ;;
esac; done
du -sh "$IMG"
