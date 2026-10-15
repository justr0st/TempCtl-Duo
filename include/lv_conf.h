/* LVGL 9.6 configuration of TempCtl Duo: only what differs from LVGL's defaults (lv_conf_internal.h
 * fills in everything else; the full list of options with their documentation is lv_conf_template.h in
 * the LVGL package). */
/* clang-format off */
#ifndef LV_CONF_H
#define LV_CONF_H

/*---- memory, strings, printf ----*/
/* With PSRAM (S3) LVGL's objects, styles and draw tasks live in PSRAM (src/lv_mem_psram.c), so the
 * internal RAM stays free for Wi-Fi, the display's DMA buffers and the tasks. CYD: the C library. */
#if defined(BOARD_HAS_PSRAM) && defined(ESP_PLATFORM)
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM
#else
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#endif
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB  /* (floats in printf) */

/*---- display refresh ----*/
#define LV_DEF_REFR_PERIOD 30  /* ms */

/*---- display controllers (lcd.cpp) ----*/
#define LV_USE_GENERIC_MIPI 1
#define LV_USE_ST7796 1   /* S3: 4" 480x320 */
#define LV_USE_ILI9341 1  /* CYD: 2.8" 320x240 */

/*---- logging: warnings and errors through printf (the serial log) ----*/
#define LV_USE_LOG 1
#define LV_LOG_PRINTF 1

/*---- asserts: NULL and malloc checks; abort() with a backtrace instead of freezing (lv_assert_custom.h) ----*/
#define LV_USE_ASSERT 1
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_ASSERT_USE_CUSTOM_INCLUDE 1
#define LV_ASSERT_CUSTOM_INCLUDE "lv_assert_custom.h"

/*---- fonts: Montserrat only as the fallback for LVGL's symbols (the UI fonts are in src/fonts) ----*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1

/*---- theme ----*/
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0  /* no grow effect on press */

/*---- objects ----*/
#define LV_USE_OBJ_NAME 1
#define LV_USE_TRANSLATION 1

#endif /*LV_CONF_H*/
