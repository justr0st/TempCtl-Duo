// Board: ESP32-S3-DevKitC-1 v1.0 N16R8 + 4.0" SPI module MSP4031
// (ST7796S 320x480, used as 480x320 landscape; FT6336U capacitive touch).
// Selected by -D BOARD_S3 (env s3). Pinout: claude/tempctl-duo-spec.md, 2a.
// Kept free: GPIO 35-37 (octal PSRAM), 19/20 (native USB), 43/44 (UART0,
// flashing + log), strapping pins 0, 3, 45, 46.
#pragma once

#define BOARD_NAME "ESP32-S3"

// ---- Display: ST7796S on SPI2 (FSPI, IO_MUX pins 10-13) ----
#define LCD_CTRL_ST7796 1
#define LCD_NATIVE_W 320  // panel in its native portrait orientation
#define LCD_NATIVE_H 480
#define LCD_PIN_SCK 12
#define LCD_PIN_MOSI 11
#define LCD_PIN_MISO 13   // shared with the SD card (kept off the bus); read for the screen check
#define LCD_PIN_CS 10
#define LCD_PIN_DC 9      // LCD_RS
#define LCD_PIN_RST 14
#define LCD_SPI_HZ 40000000
#define LCD_BGR 1         // panel colour order (MADCTL BGR bit)
#define LCD_INVERT 0      // TN panel: no colour inversion. Set 1 if colours look inverted.
// Draw buffers (2 x, internal DMA RAM). One SPI transfer on the S3 is at
// most 32 kB: 480 px * 32 lines * 2 bytes = 30 kB.
#define LCD_BUF_LINES 32

#define PIN_BACKLIGHT 8   // LED pin of the module, LEDC PWM
#define PIN_SD_CS 18      // SD card not used: held high (deselected)

// ---- Touch: FT6336U, I2C 0x38 ----
#define TOUCH_FT6336 1
#define TOUCH_PIN_SDA 16
#define TOUCH_PIN_SCL 15
#define TOUCH_PIN_RST 17
#define TOUCH_PIN_INT 7   // -1 if not wired
#define TOUCH_I2C_HZ 400000
// Mapping of the raw touch coordinates (native portrait, 320 x 480) to the
// landscape screen: x = raw y, y = 319 - raw x. Change here if touches land
// in the wrong place (the bring-up screen shows the raw and mapped values).
#define TOUCH_SWAP_XY 1
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 1

#define BOARD_VARIANT ""
#define HAS_RS485 1
#define LOCAL_CHANNELS_MAX 2  // fixed pins per channel (below); any mix of Modbus and local

// ---- RS485 transceiver (3.3 V part) on UART1 ----
#define RS485_SERIAL Serial1
#define PIN_RS485_RX 2    // RO
#define PIN_RS485_TX 1    // DI
#define PIN_RS485_DE 42   // DE + /RE

// ---- Local channels (step 3; defined here, not used yet) ----
#define PIN_TC_SCK 41
#define PIN_TC_SO 40
#define PIN_TC_CS_BOTTOM 39
#define PIN_TC_CS_TOP 38  // v1.1 board (WS2812 on GPIO38): swap 38 <-> 48
#define PIN_SSR_BOTTOM 21
#define PIN_SSR_TOP 47

// ---- Status LED: on-board WS2812 (v1.0: GPIO48, v1.1: GPIO38) ----
#define STATUS_LED_WS2812 1
#define PIN_STATUS_LED 48
#define STATUS_LED_LEVEL 40  // brightness of a full colour channel (0..255, ~20 %)

// ---- Screen used by the UI ----
#define SCREEN_W 480
#define SCREEN_H 320
// The new touch UI (tui_*.cpp, step 6); the old exported 320x240 UI (ui.cpp) is not built
#define UI_V2 1

#define BOARD_HAS_LARGE_RAM 1  // 8 MB PSRAM
