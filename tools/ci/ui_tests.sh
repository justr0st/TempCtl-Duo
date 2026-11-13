#!/bin/bash
# Touch UI on the PC (test/ui_host): builds both boards, runs the self-checks and the random-tap test.
#   LVGL=<lvgl 9.6 src> AJ=<ArduinoJson src> bash tools/ci/ui_tests.sh
set -e
cd "$(dirname "$0")/../../test/ui_host"
# (not set: ~/deps/lvgl and ~/deps/AJ743/src if they exist, else /tmp/lvgl and /tmp/AJ743/src, see docs/wiki/Development.md)
if [ -z "$LVGL" ]; then if [ -d ~/deps/lvgl ]; then LVGL=~/deps/lvgl; else LVGL=/tmp/lvgl; fi; fi
if [ -z "$AJ" ]; then if [ -d ~/deps/AJ743/src ]; then AJ=~/deps/AJ743/src; else AJ=/tmp/AJ743/src; fi; fi
export LVGL AJ
./build.sh
BOARD=cyd ./build.sh
echo "== stepper";  ./uihost stepper
echo "== chan";     ./uihost chan
echo "== lowmem";   ./uihost_cyd lowmem
echo "== monkey S3"; ./uihost monkey 4000
echo "== monkey CYD"; ./uihost_cyd monkey 4000
echo "UI tests: ok"
