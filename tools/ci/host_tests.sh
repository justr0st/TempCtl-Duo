#!/bin/bash
# Host tests (no board): test/host_test.cpp + the pure sources, with AddressSanitizer / UBSan.
#   AJ=<ArduinoJson 7 src dir> bash tools/ci/host_tests.sh        (default AJ=/tmp/AJ743/src)
# Same command as docs/wiki/Development.md ("Tests without a board"). A new pure source file the test needs:
# add it to SOURCES below (and to the page).
set -e
cd "$(dirname "$0")/../.."
AJ=${AJ:-/tmp/AJ743/src}
SOURCES="src/duo_program.cpp src/run_engine.cpp src/settings.cpp src/tc_sensor.cpp src/pid.cpp src/runaway.cpp
  src/autotune.cpp src/ota_image.cpp src/release.cpp"
OUT=${TMPDIR:-/tmp}/tempctl-host-test
mkdir -p "$(dirname "$OUT")"
g++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -Isrc -I"$AJ" \
  test/host_test.cpp $SOURCES -o "$OUT"
"$OUT"
