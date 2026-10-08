// Board: ESP32-2432S028R "Cheap Yellow Display" (ESP32, ILI9341 320x240,
// XPT2046 resistive touch). Selected by -D BOARD_CYD (envs cyd-1xlocal-1x485, cyd-2xlocal, cyd-2xlocal-2x485).
// Second supported device, see claude/tempctl-duo-spec.md, 2b.
#pragma once

#define BOARD_NAME "CYD"  // + BOARD_VARIANT below

// ---- Display: ILI9341 on SPI2 (HSPI, IO_MUX pins 12-15) ----
#define LCD_CTRL_ILI9341 1
#define LCD_NATIVE_W 240
#define LCD_NATIVE_H 320
#define LCD_PIN_SCK 14
#define LCD_PIN_MOSI 13
#define LCD_PIN_MISO -1   // GPIO12 is wired but not read
#define LCD_PIN_CS 15
#define LCD_PIN_DC 2
#define LCD_PIN_RST -1
#define LCD_SPI_HZ 40000000
#define LCD_BGR 1
#define LCD_INVERT 0
// Draw buffers (2 x, internal DMA RAM; the CYD has no PSRAM). 10 lines = 2 x 6.4 kB: the heap is
// tight with Wi-Fi + web server + two channels (20 lines took 12.8 kB more)
#define LCD_BUF_LINES 10

#define PIN_BACKLIGHT 21

// ---- Touch: XPT2046 on its own SPI bus (VSPI) ----
#define TOUCH_XPT2046 1
#define PIN_TOUCH_CLK 25
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39
#define PIN_TOUCH_CS 33
#define PIN_TOUCH_IRQ 36

// ---- Free GPIOs on the CYD (schematic ESP32-2432S028 V0.1 + randomnerdtutorials CYD pinout) ----
// Connectors: P3 = GND, IO35, IO22, IO21 (IO21 = backlight, not usable);
//             CN1 = GND, IO22, IO27, 3V3 (IO27 has a 10 k pull-up, R18; IO22 on CN1 pin 3
//             is not drawn in schematic V0.1 but present on the board - checked by Rost);
//             P1 = VIN, IO1 (TX0), IO3 (RX0), GND - UART0, shared with the CH340 (log / console).
// Only IO22, IO27 and IO35 (input only) are free on connectors. The micro-SD slot pins
// (IO5 CS, IO18 CLK, IO19 MISO, IO23 MOSI) are free because the SD card is not used; reach them
// with a micro-SD breakout ("sniffer") adapter. The SD lines have 10 k pull-ups (R13 / RN2) except,
// per the schematic, IO18 (CLK) - check with an ohmmeter before wiring an SSR to it.
// IO6..IO11 are NOT usable: they are the module's internal SPI flash (the empty U4 footprint on the
// CYD is only an optional second flash chip on the same lines).
// Taken by the board: IO0 BOOT button (strapping), IO4/16/17 RGB LED (freed in cyd-2xlocal-2x485), IO26 speaker amplifier,
// IO34 light sensor (LDR, input only), IO2/12/13/14/15 display, IO25/32/33/36/39 touch, IO21 backlight.

// ---- Variant (build flag of the env): which channel types the wiring allows ----
// The CYD has too few free pins for RS485 plus two local channels, so there are three wirings
// (claude/wiring/cyd-wiring-a.png / -b.png / -c.png):
//   env cyd-1xlocal-1x485 (-D CYD_1XLOCAL_1X485): RS485 on P3/CN1 + one local channel on the SD
//       slot pins. Either channel can be the local one; the other one (or both) Modbus.
//   env cyd-2xlocal (-D CYD_2XLOCAL): two local channels (bottom on the SD slot pins, top on
//       P3/CN1), no RS485 - both channels must be of type local.
//   env cyd-2xlocal-2x485 (-D CYD_2XLOCAL_2X485): RS485 + two local channels, every channel can be
//       local or Modbus. Needs the on-board RGB LED removed: top channel on its pads (IO4, IO16),
//       status LED = external WS2812 on IO17.
#if (defined(CYD_1XLOCAL_1X485) + defined(CYD_2XLOCAL) + defined(CYD_2XLOCAL_2X485)) != 1
#error "CYD: build with exactly one of -D CYD_1XLOCAL_1X485, -D CYD_2XLOCAL, -D CYD_2XLOCAL_2X485 (envs cyd-...)"
#endif

// Thermocouple bus on the micro-SD slot pins (both variants)
#define PIN_TC_SCK 23  // SD MOSI (pull-up is harmless for a clock output)
#define PIN_TC_SO 19   // SD MISO (input, pull-up fine)

#if defined(CYD_1XLOCAL_1X485) || defined(CYD_2XLOCAL_2X485)
// ---- RS485 transceiver on UART2 ----
#define HAS_RS485 1
#define RS485_SERIAL Serial2
#define PIN_RS485_RX 35   // P3-3 - input-only pin, ideal for RX (RO of transceiver)
#define PIN_RS485_TX 22   // P3-2 / CN1-3 - DI of transceiver
#define PIN_RS485_DE 27   // CN1-2 - DE+/RE of transceiver. Set to -1 for auto-direction modules.
#else
#define HAS_RS485 0
#endif

#if defined(CYD_1XLOCAL_1X485)
#define BOARD_VARIANT "1x local + 1x RS485"
#define LOCAL_CHANNELS_MAX 1
// ---- The one local channel (bottom or top, whichever is set to local) ----
#define PIN_TC_CS_LOCAL 5  // SD CS: pull-up keeps the converter deselected at boot (strapping pin, high = ok)
#define PIN_SSR_LOCAL 18   // SD CLK: no pull-up -> SSR stays off at boot with the driver's 10 k pull-down

#elif defined(CYD_2XLOCAL_2X485)
#define BOARD_VARIANT "2x local + RS485"
#define LOCAL_CHANNELS_MAX 2
// ---- Bottom channel on the SD slot pins, top channel on the pads of the removed RGB LED ----
#define PIN_TC_CS_BOTTOM 5  // SD CS (pull-up = deselected at boot)
#define PIN_SSR_BOTTOM 18   // SD CLK (no pull-up)
#define PIN_TC_CS_TOP 4     // LED pad "R"; floating at boot -> external 10 k pull-up to 3V3
#define PIN_SSR_TOP 16      // LED pad "G"; no pull-up (SSR off at boot with the driver's pull-down)

#else  // CYD_2XLOCAL
#define BOARD_VARIANT "2x local"
#define LOCAL_CHANNELS_MAX 2
// ---- Bottom channel on the SD slot pins, top channel on P3 / CN1 ----
#define PIN_TC_CS_BOTTOM 5  // SD CS (pull-up = deselected at boot)
#define PIN_SSR_BOTTOM 18   // SD CLK (no pull-up)
#define PIN_TC_CS_TOP 27    // CN1-2, board pull-up R18 = deselected at boot
#define PIN_SSR_TOP 22      // P3-2 / CN1-3, no pull-up
// IO35 (P3-3, input only) stays free, e.g. for a door / E-stop input.
#endif

#if defined(CYD_2XLOCAL_2X485)
// ---- Status LED: external WS2812 on the RGB LED's "B" pad, powered from 3V3 ----
#define STATUS_LED_WS2812 1
#define PIN_STATUS_LED 17
#define STATUS_LED_LEVEL 40
#else
// ---- Status LED: on-board RGB LED (common anode, active low, 1 k resistors) ----
// Same colours as the S3's WS2812 (PWM on LEDC channels 1..3; channel 0 = backlight).
#define STATUS_LED_RGB_PWM 1
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17
#define STATUS_LED_LEVEL 120  // brightness of a full colour channel (0..255)
#endif

// ---- Other on-board parts (not used yet) ----
#define PIN_SPEAKER 26     // SC8002B amplifier input (P4 speaker connector) - e.g. alarm beeps
#define PIN_LDR 34         // light sensor (input only) - e.g. automatic backlight
#define PIN_BOOT_BUTTON 0  // BOOT button, 10 k pull-up (strapping pin)

// ---- Screen used by the UI ----
#define SCREEN_W 320
#define SCREEN_H 240
// Touch UI: the hand-written LVGL screens (src/tui_*.cpp, compact 320x240 set)
#define UI_V2 1
