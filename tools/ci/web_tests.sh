#!/bin/bash
# Web page tests (Playwright against the browser demo on :8790). The demo needs demo/out/tempctl-demo.wasm
# (demo/build.sh) before this runs; Node, and "playwright" with its chromium, must be installed.
#   bash tools/ci/web_tests.sh
set -e
cd "$(dirname "$0")/../.."
SITE=${TMPDIR:-/tmp}/tempctl-site
rm -rf "$SITE"
python3 tools/build_site.py "$SITE" --web >/dev/null
[ -f "$SITE/demo/tempctl-demo.wasm" ] || { echo "demo/tempctl-demo.wasm missing: run demo/build.sh first"; exit 1; }
python3 -m http.server 8790 --bind 127.0.0.1 --directory "$SITE" >/dev/null 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
sleep 1
export NODE_PATH=${NODE_PATH:-$(npm root -g)}
rc=0
for t in web_jumps web_reorder web_chart_hold; do
  echo "== $t"
  out=$(node test/$t.js) || rc=1
  echo "$out"
  # the tests print FAIL lines (reorder, chart_hold) or what moved (jumps: anything but "stable")
  if echo "$out" | grep -q '^FAIL'; then rc=1; fi
  if [ "$t" = web_jumps ]; then
    if echo "$out" | grep ': ' | grep -vqE ': stable$'; then echo "layout jumps found"; rc=1; fi
    echo "$out" | grep -qx 'no errors' || { echo "page errors"; rc=1; }
  fi
done
exit $rc
