// Display glue for LVGL's MIPI panel drivers (ST7796 on the S3 board,
// ILI9341 on the CYD): ESP-IDF spi_master with DMA, backlight PWM.
#pragma once
#include <lvgl.h>
#include <stdint.h>

// Creates the LVGL display (landscape SCREEN_W x SCREEN_H; call after
// lv_init() and lv_tick_set_cb()). Returns NULL if the SPI bus or the draw
// buffers could not be set up.
lv_display_t* lcdInit(bool rotate180);
// Landscape or landscape turned by 180 degrees (MADCTL, no redraw cost)
void lcdSetRotation(bool rotate180);
// Backlight 0..255
void lcdSetBrightness(uint8_t b);
// Sets the panel controller up again (reset + init sequence) and redraws; keeps the UI as it is.
// Call from the UI task. False without a display.
bool lcdReinit();
