# ESP32-S3 SIMD fills and copies for LVGL (S3 only)

The `*_esp32s3.S` files and `lv_macro_memcpy.S` are taken from Espressif's
[esp-bsp](https://github.com/espressif/esp-bsp), `components/esp_lvgl_port/src/lvgl9/simd/`
(commit f259a6a, 2026-10-08), licensed under the Apache License 2.0 (`LICENSE` in this folder).
Changes: a build guard (`#if defined(BOARD_S3)` … `#endif`) and a comment after the SPDX header; the
assembly itself is unchanged. On the CYD (ESP32, no PIE instructions) they assemble to empty objects.

They use the ESP32-S3's 128-bit PIE instructions (8 RGB565 pixels per store) for:

| LVGL hook (include/lv_blend_esp32s3.h) | assembly | used for |
|---|---|---|
| `LV_DRAW_SW_COLOR_BLEND_TO_RGB565` | `lv_color_blend_to_rgb565_esp` | solid fills of the display buffer and the chart picture |
| `LV_DRAW_SW_COLOR_BLEND_TO_ARGB8888` | `lv_color_blend_to_argb8888_esp` | solid fills of ARGB8888 layers (widgets with opacity) |
| `LV_DRAW_SW_RGB565_BLEND_NORMAL_TO_RGB565` | `lv_rgb565_blend_normal_to_rgb565_esp` | RGB565 image copies (the chart picture) |

LVGL calls the hooks only for the simple cases (no mask, full opacity); text, rounded corners, borders and
anything semi-transparent stay in LVGL's C code. `lv_blend_esp32s3.c` translates LVGL 9.6's blend
descriptors into the assembly's argument block (as esp_lvgl_port's `esp_lvgl_port_lv_blend.h`, which targets
LVGL 9.1). Serial `perf simd off` / `perf simd on` switches them at run time for comparisons with the frame
rate meter (`perf on`).
