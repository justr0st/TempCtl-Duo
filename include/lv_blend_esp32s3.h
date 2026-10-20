/* LVGL's hooks for its software renderer's fills and copies (LV_USE_DRAW_SW_ASM = LV_DRAW_SW_ASM_CUSTOM,
 * include/lv_conf.h; S3 only): solid fills of RGB565 and ARGB8888 buffers and RGB565 -> RGB565 copies go to
 * Espressif's ESP32-S3 SIMD assembly (src/simd/, from esp_lvgl_port, Apache-2.0) through the C wrappers in
 * src/simd/lv_blend_esp32s3.c. LVGL calls them only for the simple cases (no mask, full opacity); a wrapper
 * that returns LV_RESULT_INVALID (SIMD switched off: serial "perf simd off") lets LVGL's own C code do it. */
#ifndef LV_BLEND_ESP32S3_H
#define LV_BLEND_ESP32S3_H

#ifdef __cplusplus
extern "C" {
#endif

int lvSimdFillRgb565(void* dsc);    /* lv_draw_sw_blend_fill_dsc_t*: lv_result_t */
int lvSimdFillArgb8888(void* dsc);  /* lv_draw_sw_blend_fill_dsc_t* */
int lvSimdCopyRgb565(void* dsc);    /* lv_draw_sw_blend_image_dsc_t* */

#ifdef __cplusplus
}
#endif

#define LV_DRAW_SW_COLOR_BLEND_TO_RGB565(dsc) ((lv_result_t)lvSimdFillRgb565((void*)(dsc)))
#define LV_DRAW_SW_COLOR_BLEND_TO_ARGB8888(dsc) ((lv_result_t)lvSimdFillArgb8888((void*)(dsc)))
#define LV_DRAW_SW_RGB565_BLEND_NORMAL_TO_RGB565(dsc) ((lv_result_t)lvSimdCopyRgb565((void*)(dsc)))

#endif /*LV_BLEND_ESP32S3_H*/
