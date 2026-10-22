// Board: the browser demo (demo/README.md) - an S3-like device whose hardware is simulated in demo/sim.cpp:
// two local channels (MAX31855 + SSR), the SSR supply relay, E-stop and buzzer; no display, SD card or RS485.
// Selected by -D BOARD_DEMO (demo/build.sh).
#pragma once

#define BOARD_NAME "Demo"
#define BOARD_ID "demo"  // no firmware image fits it
#define BOARD_CHIP 255
#define BOARD_VARIANT ""
#define HAS_RS485 0
#define LOCAL_CHANNELS_MAX 2

#define PIN_TC_SCK 41
#define PIN_TC_SO 40
#define PIN_TC_CS_BOTTOM 39
#define PIN_TC_CS_TOP 38
#define PIN_SSR_BOTTOM 21
#define PIN_SSR_TOP 47
#define PIN_SSR_RELAY 4
#define PIN_ESTOP 5
#define PIN_BUZZER 6

#define SCREEN_W 480
#define SCREEN_H 320
#define BOARD_HAS_LARGE_RAM 1
