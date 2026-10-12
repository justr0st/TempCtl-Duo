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
// Reads the panel's state back (power mode, MADCTL, pixel format) and compares it with the setup:
// 1 = as set up, 0 = lost its setup, -1 = cannot tell (no display; status not readable on this
// hardware: decided by the first read right after the init). info: the values, for the log / console.
// Call from the UI task (or the same task as lv_timer_handler()).
int lcdCheck(char* info, size_t n);
// Test only: switches the display off and puts it to sleep (the screen check must notice it)
bool lcdTestBreak();
// The SPI bus shared with the SD card (S3): the screen check frees the bus for its status read, so the SD
// card is only mounted while this lock is held (sdlog.cpp); the check skips a turn ("busy") meanwhile.
bool lcdBusLock(uint32_t waitMs);
void lcdBusUnlock();
