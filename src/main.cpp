// TempCtl Duo: two-channel temperature profile controller (bottom + top
// heater of a BGA rework station) for PMA KS 4x-1 / generic Modbus RTU
// controllers. Boards: ESP32-S3-DevKitC-1 + 4.0" ST7796 (env s3) and the
// ESP32-2432S028R "CYD" (env cyd), see src/board_*.h.
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "app_state.h"
#include "config.h"
#include "local_ctrl.h"
#include "net.h"
#include "ui.h"
#include "web.h"

// setup() and loop() run in the Arduino loop task, whose default stack is 8 kB.
// LVGL (creating the screen, drawing the keyboards and the chart) needs about
// 14 kB; an overflow reboots the board. The stack is taken from the heap.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

void setup() {
  localCtrlEarlyInit();  // SSR outputs low (off) before anything else
  Serial.begin(115200);
  Serial.printf("\n[boot] " PRODUCT_NAME " %s (%s%s%s), free heap %u bytes", FW_VERSION, BOARD_NAME,
                BOARD_VARIANT[0] ? ", " : "", BOARD_VARIANT, ESP.getFreeHeap());
#ifdef BOARD_HAS_LARGE_RAM
  Serial.printf(", PSRAM %u bytes", ESP.getPsramSize());
#endif
  Serial.println();
  appInit();          // settings, shared state
  Serial.println("[boot] ui");
  uiInit();           // display, touch, LVGL screens
  Serial.printf("[boot] net (free heap %u bytes)\n", ESP.getFreeHeap());
  netBegin();         // Wi-Fi STA (or AP fallback)
  webBegin();         // HTTP UI + JSON API
  controlTaskStart(); // Modbus + program engine on core 0
  Serial.printf("[mem] free heap %u bytes, largest block %u, loop stack unused %u bytes\n", ESP.getFreeHeap(),
                ESP.getMaxAllocHeap(), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  // Watchdog for the loop: if it gets stuck (frozen touch screen) the board
  // prints a backtrace and restarts. 15 s because the longest intended wait
  // in the loop is a 5 s bus request from the touch UI.
  esp_task_wdt_init(15, true);
  enableLoopWDT();
}

void loop() {
  uiLoop();
  webLoop();
  netLoop();
  delay(2);
}
