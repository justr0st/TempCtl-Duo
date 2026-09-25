// Board configuration for the ESP32-2432S028R ("Cheap Yellow Display", CYD).
#pragma once

#define FW_VERSION "1.0.0"

// ---- RS485 transceiver (UART2) ----
// CYD free pins: CN1 = GPIO22, GPIO27 (+3.3V, GND); P3 = GPIO21(backlight!), GPIO22, GPIO35.
#define PIN_RS485_RX 35   // P3  - input-only pin, ideal for RX (RO of transceiver)
#define PIN_RS485_TX 22   // CN1 - DI of transceiver
#define PIN_RS485_DE 27   // CN1 - DE+/RE of transceiver. Set to -1 for auto-direction modules.

// ---- Display / touch (fixed on the CYD) ----
#define PIN_BACKLIGHT 21
#define PIN_TOUCH_CLK 25
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39
#define PIN_TOUCH_CS 33
#define PIN_TOUCH_IRQ 36

// ---- On-board RGB LED (active low) used as link status ----
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17

#define SCREEN_W 320
#define SCREEN_H 240

// Number of points in the run/live history (chart resolution)
#define HIST_N 240
// Live (idle) chart sample interval in seconds -> 240 * 5 s = 20 min window
#define LIVE_SAMPLE_SEC 5
