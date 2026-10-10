#!/bin/bash
# Serial console (src/console.cpp) on a PC: types scripted commands and prints the console's output.
#   AJ=<ArduinoJson src> ./build.sh && ./conhost | cat -v
set -e
cd "$(dirname "$0")"
R=../..
AJ=${AJ:-/tmp/AJ743/src}
g++ -std=gnu++17 -g -fsanitize=address,undefined -Wall -Ishim -I$R/src -I$AJ -DBOARD_CYD -DCYD_1XLOCAL_1X485 main.cpp \
  $R/src/console.cpp $R/src/settings.cpp $R/src/duo_program.cpp $R/src/run_engine.cpp -o conhost
