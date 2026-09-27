// ESP32-CYD temperature profile controller for PMA KS 4x-1 (Modbus RTU)
// and other Modbus RTU temperature controllers.
#include <Arduino.h>
#include "app_state.h"
#include "config.h"
#include "net.h"
#include "ui.h"
#include "web.h"

// setup() and loop() run in the Arduino loop task, whose default stack is 8 kB.
// LVGL (creating the screen, drawing the keyboards and the chart) needs about
// 14 kB; an overflow reboots the board. The stack is taken from the heap.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[boot] TempCtl %s, free heap %u bytes\n", FW_VERSION, ESP.getFreeHeap());
  appInit();          // settings, shared state
  Serial.println("[boot] ui");
  uiInit();           // display, touch, LVGL screens
  Serial.printf("[boot] net (free heap %u bytes)\n", ESP.getFreeHeap());
  netBegin();         // Wi-Fi STA (or AP fallback)
  webBegin();         // HTTP UI + JSON API
  controlTaskStart(); // Modbus + program engine on core 0
  Serial.printf("[mem] free heap %u bytes, largest block %u, loop stack unused %u bytes\n", ESP.getFreeHeap(),
                ESP.getMaxAllocHeap(), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}

void loop() {
  uiLoop();
  webLoop();
  netLoop();
  delay(2);
}
