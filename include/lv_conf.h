// Minimal LVGL 8.3 configuration for the CYD. Anything not defined here uses
// the LVGL default from lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

// LVGL objects live on the normal heap: a fixed LV_MEM_SIZE pool would sit
// in the static DRAM segment, which is nearly full with Wi-Fi + Bluetooth stacks.
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc

#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

// lv_snprintf / lv_label_set_text_fmt / lv_table_set_cell_value_fmt must
// understand %f, otherwise they print a bare "f" instead of the number.
#define LV_SPRINTF_USE_FLOAT 1

#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0

#define LV_USE_CHART 1
#define LV_USE_TABLE 1
#define LV_USE_TABVIEW 1
#define LV_USE_KEYBOARD 1
#define LV_USE_MSGBOX 1
#define LV_USE_LED 1
#define LV_USE_DROPDOWN 1
#define LV_USE_SWITCH 1
#define LV_USE_SLIDER 1
#define LV_USE_BAR 1
#define LV_USE_TEXTAREA 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_LIST 1

#endif
