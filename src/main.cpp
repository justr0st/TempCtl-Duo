// TempCtl Duo: two-channel temperature profile controller (bottom + top
// heater of a BGA rework station) for PMA KS 4x-1 / generic Modbus RTU
// controllers. Boards: ESP32-S3-DevKitC-1 + 4.0" ST7796 (env s3) and the
// ESP32-2432S028R "CYD" (env cyd), see src/board_*.h.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include "app_state.h"
#include "config.h"
#include "local_ctrl.h"
#include "net.h"
#include "ui.h"
#include "web.h"
#include "console.h"

// setup() and loop() run in the Arduino loop task, whose default stack is 8 kB.
// LVGL (creating the screen, drawing the keyboards and the chart) needs about
// 14 kB; an overflow reboots the board. The stack is taken from the heap.
#ifdef BOARD_HAS_PSRAM
SET_LOOP_TASK_STACK_SIZE(24 * 1024);
#else
SET_LOOP_TASK_STACK_SIZE(16 * 1024);  // CYD: ~7.5 kB used (measured, incl. Wi-Fi scan list); DRAM is short
#endif

// Internal byte-addressable RAM (DRAM): free, largest block, lowest so far; plus the IRAM-only heap,
// which only takes 32-bit data (chart and live history on the CYD). ESP.getFreeHeap() counts both, so it looked
// better than it was on the CYD (no PSRAM).
static void memLog(const char* where) {
  const uint32_t caps = MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL;
  LogSerial.printf("[mem] %s: DRAM free %u, largest %u, min %u; IRAM-only free %u\n", where,
                (unsigned)heap_caps_get_free_size(caps), (unsigned)heap_caps_get_largest_free_block(caps),
                (unsigned)heap_caps_get_minimum_free_size(caps), (unsigned)iramPureFree());
}

void setup() {
  localCtrlEarlyInit();  // SSR outputs low (off) before anything else
  Serial.begin(115200);
  LogSerial.printf("\n[boot] " PRODUCT_NAME " %s (%s%s%s), free heap %u bytes", FW_VERSION, BOARD_NAME,
                BOARD_VARIANT[0] ? ", " : "", BOARD_VARIANT, ESP.getFreeHeap());
#ifdef BOARD_HAS_LARGE_RAM
  LogSerial.printf(", PSRAM %u bytes", ESP.getPsramSize());
#endif
  LogSerial.println();
  appInit();          // settings, shared state
  // the control task first: it installs the RS485 UART driver while the heap is still unfragmented
  // (on the CYD the driver allocation failed once Wi-Fi, web and the first screen draw were running)
  controlTaskStart(); // Modbus + program engine on core 0
  delay(50);
  memLog("after settings/programs/control");
  LogSerial.println("[boot] ui");
  uiInit();           // display, touch, LVGL screens
  memLog("after ui");
  netBegin();         // Wi-Fi STA (or AP fallback)
  memLog("after wifi");
  webBegin();         // HTTP UI + JSON API
  memLog("after web");
  consoleBegin();     // serial commands (S3: native USB port, CYD: the one serial port)
  memLog("setup done");
  // Watchdog for the loop: if it gets stuck (frozen touch screen) the board
  // prints a backtrace and restarts. 15 s because the longest intended wait
  // in the loop is a 5 s bus request from the touch UI.
  esp_task_wdt_init(15, true);
  enableLoopWDT();
}

void loop() {
  // heap check once a minute (internal RAM: free, largest block, lowest so far)
  {
    static uint32_t memAt = 0;
    if (millis() - memAt > 60000) {
      memAt = millis();
      memLog("running");
      LogSerial.printf("[mem] stack free: loop %u, control %u bytes\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                    controlTaskStackFree());
#ifdef BOARD_HAS_PSRAM
      LogSerial.printf("[mem] PSRAM free %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
      if (WiFi.status() == WL_CONNECTED) LogSerial.printf("[wifi] RSSI %d dBm\n", (int)WiFi.RSSI());
    }
  }
  if (appRestartPending()) {  // requested restart: no more drawing, backlight off, then restart
    static uint32_t at = 0;
    if (!at) {
      at = millis();
      if (!at) at = 1;  // (0 = not yet; "| 1" could be ahead of millis())
      uiSetBrightness(0);
    }
    if (millis() - at > 400) {
      Serial.flush();
      ESP.restart();
    }
    delay(10);
    return;
  }
  uiLoop();
  consoleLoop();
  webLoop();
  netLoop();
  delay(2);
}
