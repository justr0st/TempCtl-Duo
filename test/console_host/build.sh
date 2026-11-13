#!/bin/bash
# Serial console (src/console.cpp) on a PC: types scripted commands and prints the console's output.
#   AJ=<ArduinoJson src> ./build.sh && ./conhost | cat -v
#   BOARD=s3 ./build.sh && ./conhost_s3 | cat -v   the S3 build (SD card backup commands; console on Serial)
set -e
cd "$(dirname "$0")"
R=../..
AJ=${AJ:-/tmp/AJ743/src}
if [ "$BOARD" = s3 ]; then BD="-DBOARD_S3 -DBOARD_HAS_PSRAM -DARDUINO_USB_MODE=1"; OUT=conhost_s3; else BD="-DBOARD_CYD -DCYD_1XLOCAL_1X485"; OUT=conhost; fi
g++ -std=gnu++17 -g -fsanitize=address,undefined -Wall -Ishim -I$R/src -I$AJ $BD main.cpp \
  $R/src/console.cpp $R/src/release.cpp $R/src/settings.cpp $R/src/duo_program.cpp $R/src/run_engine.cpp $R/src/tc_sensor.cpp -o $OUT
