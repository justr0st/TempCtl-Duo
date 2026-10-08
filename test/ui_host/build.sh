#!/bin/bash
# Touch UI preview on a PC (no board): builds LVGL and the tui_*.cpp screens with stand-ins for the
# firmware (host_app.cpp, shim/) and renders screenshots into shots/ (topng.py makes PNGs).
#   LVGL=<lvgl 9.6 source> AJ=<ArduinoJson src> ./build.sh && ./uihost && python3 topng.py
#   ./uihost monkey 4000   random taps on every screen (with AddressSanitizer)
set -e
cd "$(dirname "$0")"
R=../..
LVGL=${LVGL:-/tmp/lvgl}
AJ=${AJ:-/tmp/AJ743/src}
INC="-Ishim -I$R/src -I$R/include -I$LVGL -I$AJ -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DBOARD_S3 -DBOARD_HAS_PSRAM"
mkdir -p obj shots
if [ ! -f obj/liblvgl.a ]; then
  find $LVGL/src -name "*.c" | xargs -P 8 -I{} sh -c 'gcc -O1 -w '"$INC"' -c {} -o obj/lv_$(echo {} | md5sum | cut -c1-12).o'
  ar rcs obj/liblvgl.a obj/lv_*.o
fi
for f in $R/src/fonts/*.c; do o=obj/$(basename $f .c).o; [ $o -nt $f ] || gcc -O1 -w $INC -c $f -o $o; done
g++ -std=gnu++17 -O0 -g -fsanitize=address,undefined -Wall -Wno-unused-function $INC host_app.cpp \
  $R/src/tui.cpp $R/src/tui_theme.cpp $R/src/tui_widgets.cpp $R/src/tui_main.cpp $R/src/tui_chart.cpp \
  $R/src/tui_menu.cpp $R/src/tui_prog.cpp $R/src/tui_chan.cpp $R/src/settings.cpp $R/src/duo_program.cpp \
  $R/src/run_engine.cpp obj/tfont_*.o obj/liblvgl.a -lm -o uihost
