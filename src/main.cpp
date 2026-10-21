// TempCtl Duo: two-channel temperature profile controller (bottom + top
// heater of a BGA rework station) for PMA KS 4x-1 / generic Modbus RTU
// controllers. Boards: ESP32-S3-DevKitC-1 + 4.0" ST7796 (env s3) and the
// ESP32-2432S028R "CYD" (env cyd), see src/board_*.h.
#include "syslog.h"
#include "sdlog.h"
#include "crumbs.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include "app_state.h"
#include "config.h"
#include "local_ctrl.h"
#include "safety.h"
#include "ota.h"
#include "net.h"
#include "ui.h"
#include "web.h"
#include "console.h"

// setup() and loop() run in the Arduino loop task, whose default stack is 8 kB.
// An overflow reboots the board. The stack is taken from the internal heap (it
// cannot be in PSRAM: the loop writes flash). S3: 16 kB (was 24; Rost's log,
// 1.0.54: at most ~6 kB used, `[mem] stack free: loop 18436`); the minute's
// [mem] line shows the lowest free space, and below 2 kB an event warns.
#ifdef BOARD_HAS_PSRAM
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
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
#ifdef BOARD_HAS_PSRAM
  // malloc() of 256 bytes or more: PSRAM first (the core's limit is 4 kB). Web answers, JSON documents, strings,
  // lists, the update buffers go there; the internal RAM stays for the display's DMA buffers, task stacks,
  // Wi-Fi and the system, which ask for it explicitly. Nothing of ours touches heap memory from an IRAM interrupt.
  heap_caps_malloc_extmem_enable(256);
#endif
  safetyBegin();         // E-stop input, buzzer (spec 9.2a)
  otaBoot();             // a new firmware on probation? (confirmed by otaLoop after 30 s)
#if !(defined(BOARD_S3) && ARDUINO_USB_MODE == 0)
  // CYD: the console shares this port; the api mode (Web Serial page) sends whole programs in one line
  // and gets answers of several kB: larger buffers, so neither side waits on the 115200 baud line
  Serial.setRxBufferSize(4096);
  Serial.setTxBufferSize(2048);
#endif
  Serial.begin(115200);
  syslogBegin();      // system log: boot number, RAM ring (before the first log line)
  LogSerial.printf("\n[boot] " PRODUCT_NAME " %s (%s%s%s), free heap %u bytes", FW_VERSION, BOARD_NAME,
                BOARD_VARIANT[0] ? ", " : "", BOARD_VARIANT, ESP.getFreeHeap());
#ifdef BOARD_HAS_LARGE_RAM
  LogSerial.printf(", PSRAM %u bytes", ESP.getPsramSize());
#endif
  LogSerial.println();
  appInit();          // settings, shared state
  otaBootReport();    // the result of the last firmware update (event log)
  // the control task first: it installs the RS485 UART driver while the heap is still unfragmented
  // (on the CYD the driver allocation failed once Wi-Fi, web and the first screen draw were running)
  controlTaskStart(); // Modbus + program engine on core 0
  delay(50);
  memLog("after settings/programs/control");
  LogSerial.println("[boot] ui");
  uiInit();           // display, touch, LVGL screens
  sdlogBegin();       // run logs on the SD card (S3; shares the display's SPI bus)
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
  crumb(CR_LOOP, L_SYSLOG);
  syslogLoop();  // new events to flash (at most every 30 s)
  // heap check once a minute (internal RAM: free, largest block, lowest so far)
  {
    static uint32_t memAt = 0;
    if (millis() - memAt > 60000) {
      memAt = millis();
      memLog("running");
      const unsigned loopFree = (unsigned)uxTaskGetStackHighWaterMark(nullptr);
      LogSerial.printf("[mem] stack free: loop %u, control %u bytes\n", loopFree, controlTaskStackFree());
      static bool stackWarned = false;
      if (loopFree < 2048 && !stackWarned) {  // (once per start: the event log keeps it)
        stackWarned = true;
        logWarn("Loop task stack nearly used up: %u bytes were left at the lowest", loopFree);
      }
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
      syslogFlush();  // the last events to flash
      Serial.flush();
      ESP.restart();
    }
    delay(10);
    return;
  }
  otaLoop();
  crumb(CR_LOOP, L_UI);
  uiLoop();
  crumb(CR_LOOP, L_CONSOLE);
  consoleLoop();
  crumb(CR_LOOP, L_WEB);
  webLoop();
  crumb(CR_LOOP, L_END);
  netLoop();
  delay(2);
}
