// C side of the ESP32-S3 SIMD fills / copies (include/lv_blend_esp32s3.h): LVGL's blend descriptors
// translated into the argument block of Espressif's assembly (asm_dsc_t, as in esp_lvgl_port's
// esp_lvgl_port_lv_blend.h, Apache-2.0), plus a run-time switch for measuring (serial "perf simd on|off").
#if defined(BOARD_S3) && defined(ESP_PLATFORM)
#include <lvgl.h>
#include "lvgl_private.h"  // lv_draw_sw_blend_fill_dsc_t / lv_draw_sw_blend_image_dsc_t (LVGL 9.6)
#include "lv_blend_esp32s3.h"
#include <stddef.h>

typedef struct {  // the assembly reads these by offset: keep the order
  uint32_t opa;
  void* dst_buf;
  uint32_t dst_w;
  uint32_t dst_h;
  uint32_t dst_stride;
  const void* src_buf;  // fills: the lv_color_t (blue, green, red; the assembly reads 4 bytes from it)
  uint32_t src_stride;
  const lv_opa_t* mask_buf;
  uint32_t mask_stride;
} asm_dsc_t;

// the fills hand the assembly a pointer to the colour, which it reads as one aligned 32-bit word
_Static_assert(offsetof(lv_draw_sw_blend_fill_dsc_t, color) % 4 == 0, "colour not 32-bit aligned");
_Static_assert(sizeof(lv_color_t) == 3, "lv_color_t: blue, green, red expected");

extern int lv_color_blend_to_rgb565_esp(asm_dsc_t* d);
extern int lv_color_blend_to_argb8888_esp(asm_dsc_t* d);
extern int lv_rgb565_blend_normal_to_rgb565_esp(asm_dsc_t* d);

volatile int lvSimdOn = 1;

int lvSimdFillRgb565(void* p) {
  const lv_draw_sw_blend_fill_dsc_t* d = (const lv_draw_sw_blend_fill_dsc_t*)p;
  if (!lvSimdOn || d->dest_w <= 0 || d->dest_h <= 0) return LV_RESULT_INVALID;
  asm_dsc_t a = {0};
  a.dst_buf = d->dest_buf;
  a.dst_w = (uint32_t)d->dest_w;
  a.dst_h = (uint32_t)d->dest_h;
  a.dst_stride = (uint32_t)d->dest_stride;
  a.src_buf = &d->color;
  return lv_color_blend_to_rgb565_esp(&a);
}

int lvSimdFillArgb8888(void* p) {
  const lv_draw_sw_blend_fill_dsc_t* d = (const lv_draw_sw_blend_fill_dsc_t*)p;
  if (!lvSimdOn || d->dest_w <= 0 || d->dest_h <= 0) return LV_RESULT_INVALID;
  asm_dsc_t a = {0};
  a.dst_buf = d->dest_buf;
  a.dst_w = (uint32_t)d->dest_w;
  a.dst_h = (uint32_t)d->dest_h;
  a.dst_stride = (uint32_t)d->dest_stride;
  a.src_buf = &d->color;
  return lv_color_blend_to_argb8888_esp(&a);
}

int lvSimdCopyRgb565(void* p) {
  const lv_draw_sw_blend_image_dsc_t* d = (const lv_draw_sw_blend_image_dsc_t*)p;
  if (!lvSimdOn || d->dest_w <= 0 || d->dest_h <= 0) return LV_RESULT_INVALID;
  asm_dsc_t a = {0};
  a.dst_buf = d->dest_buf;
  a.dst_w = (uint32_t)d->dest_w;
  a.dst_h = (uint32_t)d->dest_h;
  a.dst_stride = (uint32_t)d->dest_stride;
  a.src_buf = d->src_buf;
  a.src_stride = (uint32_t)d->src_stride;
  return lv_rgb565_blend_normal_to_rgb565_esp(&a);
}
#endif
