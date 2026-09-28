// LVGL assert handler (included by lv_conf.h, LV_ASSERT_USE_CUSTOM_INCLUDE).
// LVGL's default handler is while(1): the touch screen would freeze silently.
// abort() prints a backtrace on the serial port and restarts the board; LVGL
// logs the failed assertion ("[Error] ... Assertion ... failed") just before.
#pragma once
#include <stdlib.h>

#define LV_ASSERT_HANDLER abort();
