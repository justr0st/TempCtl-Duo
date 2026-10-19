#!/bin/bash
# Touch UI preview on a PC (no board): builds LVGL and the tui_*.cpp screens with stand-ins for the
# firmware (host_app.cpp, shim/) and renders screenshots (topng.py / sheet.py make PNGs).
#   LVGL=<lvgl 9.6 source> AJ=<ArduinoJson src> ./build.sh && ./uihost && python3 sheet.py
#   BOARD=cyd ./build.sh && ./uihost_cyd && python3 sheet.py cyd   the 320x240 set (CYD), shots_cyd/
#   ./uihost monkey 4000   random taps on every screen (with AddressSanitizer)
#   ./uihost stepper   tile + / - while the device takes the new setpoint over late (exit code 0 = ok)
#   ./uihost chan   Channels screen: reload after a web save, RS485 tab, scroll position (exit code 0 = ok)
#   MEM=1 BOARD=cyd ./build.sh && ./uihost_mem mem   heap per screen, malloc tracking (memtrack.cpp)
set -e
cd "$(dirname "$0")"
R=../..
LVGL=${LVGL:-/tmp/lvgl}
AJ=${AJ:-/tmp/AJ743/src}
if [ "$BOARD" = cyd ]; then
  BD="-DBOARD_CYD -DCYD_1XLOCAL_1X485"; OUT=uihost_cyd; FO=obj/cyd
else
  BD="-DBOARD_S3 -DBOARD_HAS_PSRAM"; OUT=uihost; FO=obj/s3
fi
INC="-Ishim -I$R/src -I$R/include -I$LVGL -I$AJ -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE"
SAN="-fsanitize=address,undefined"; EXTRA=""
if [ "$MEM" = 1 ]; then
  SAN="-rdynamic"; OUT=uihost_mem; BD="$BD -DMEMTEST"
  EXTRA="memtrack.cpp -Wl,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc"
fi
mkdir -p obj $FO shots shots_cyd
if [ ! -f obj/liblvgl.a ]; then  # LVGL itself is the same for both boards
  find $LVGL/src -name "*.c" | xargs -P 8 -I{} sh -c 'gcc -O1 -w '"$INC"' -c {} -o obj/lv_$(echo {} | md5sum | cut -c1-12).o'
  ar rcs obj/liblvgl.a obj/lv_*.o
fi
for f in $R/src/fonts/*.c $R/src/tui_icons.c; do o=$FO/$(basename $f .c).o; [ $o -nt $f ] || gcc -O1 -w $INC $BD -c $f -o $o; done
g++ -std=gnu++17 -O0 -g $SAN -Wall -Wno-unused-function $INC $BD host_app.cpp \
  $R/src/tui.cpp $R/src/tui_theme.cpp $R/src/tui_widgets.cpp $R/src/tui_main.cpp $R/src/tui_chart.cpp \
  $R/src/tui_menu.cpp $R/src/tui_prog.cpp $R/src/tui_chan.cpp $R/src/settings.cpp $R/src/duo_program.cpp \
  $R/src/run_engine.cpp $FO/tfont_*.o $FO/tui_icons.o obj/liblvgl.a -lm $EXTRA -o $OUT
