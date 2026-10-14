// XPT2046 resistive touch (CYD) on its own SPI bus, through the
// XPT2046_Touchscreen library. Raw values are mapped with the calibration
// range from the settings (tsMinX..tsMaxY).
#include "config.h"
#if defined(TOUCH_XPT2046)
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "touch.h"
#include "console.h"

static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen ts(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
static TouchCal cal;
static bool rotated = false;
static int32_t lastRawX = -1, lastRawY = -1;

bool touchInit(const TouchCal& c) {
  cal = c;
  touchSpi.begin(PIN_TOUCH_CLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);  // raw orientation; 180 degree rotation is handled in touchRead()
  return true;
}

bool touchReset(const TouchCal& c) {
  detachInterrupt(digitalPinToInterrupt(PIN_TOUCH_IRQ));  // attached again by begin()
  touchSpi.end();
  touchInit(c);
  LogSerial.println("[touch] XPT2046 SPI set up again");
  return true;
}

int touchCheck(char* info, size_t n) {
  snprintf(info, n, "XPT2046: cannot be asked (only a press stuck in one place is noticed)");
  return -1;
}

void touchStats(char* info, size_t n) {
  if (n) info[0] = 0;
}

void touchSetRotation(bool rotate180) { rotated = rotate180; }

void touchSetCal(const TouchCal& c) { cal = c; }

void touchLastRaw(int32_t* rx, int32_t* ry) {
  *rx = lastRawX;
  *ry = lastRawY;
}

bool touchRead(int32_t* x, int32_t* y) {
  if (!(ts.tirqTouched() && ts.touched())) return false;
  TS_Point p = ts.getPoint();
  lastRawX = p.x;
  lastRawY = p.y;
  long px = constrain(map(p.x, cal.minX, cal.maxX, 0, SCREEN_W - 1), 0, SCREEN_W - 1);
  long py = constrain(map(p.y, cal.minY, cal.maxY, 0, SCREEN_H - 1), 0, SCREEN_H - 1);
  if (rotated) {  // display turned by 180 degrees: mirror both axes
    px = SCREEN_W - 1 - px;
    py = SCREEN_H - 1 - py;
  }
  *x = px;
  *y = py;
  return true;
}
#endif
