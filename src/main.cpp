// ESP32-CYD temperature profile controller for PMA KS 4x-1 (Modbus RTU)
// and other Modbus RTU temperature controllers.
#include <Arduino.h>
#include "app_state.h"
#include "net.h"
#include "ui.h"
#include "web.h"

void setup() {
  Serial.begin(115200);
  appInit();          // settings, shared state
  uiInit();           // display, touch, LVGL screens
  netBegin();         // Wi-Fi STA (or AP fallback)
  webBegin();         // HTTP UI + JSON API
  controlTaskStart(); // Modbus + program engine on core 0
}

void loop() {
  uiLoop();
  webLoop();
  netLoop();
  delay(2);
}
