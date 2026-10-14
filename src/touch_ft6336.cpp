// FT6336U capacitive touch controller (I2C 0x38) on the 4.0" ST7796 module.
//
// Registers (FocalTech FT6x36 register map): 0x02 TD_STATUS (bits 3:0 =
// number of touch points), 0x03 P1_XH (bits 7:6 event flag: 0 press down,
// 1 lift up, 2 contact, 3 no event; bits 3:0 = X[11:8]), 0x04 P1_XL,
// 0x05 P1_YH (bits 3:0 = Y[11:8]), 0x06 P1_YL; 0xA3 chip id, 0xA6 firmware,
// 0xA4 G_MODE. The raw coordinates are in the panel's native portrait
// orientation (LCD_NATIVE_W x LCD_NATIVE_H).
#include "config.h"
#include "console.h"
#if defined(TOUCH_FT6336)
#include <Arduino.h>
#include <Wire.h>
#include "touch.h"

#ifndef TOUCH_USE_INT
// CTP_INT is low while the panel is touched (G_MODE 0): while it is high and nothing was pressed, the I2C
// read is skipped. Checked at run time: every 25th read (~0.75 s) is done anyway; a touch found while the line
// is high (3 times) switches back to reading every time ("[touch] INT line ..." in the log).
#define TOUCH_USE_INT 1
#endif

static const uint8_t ADDR = 0x38;
static bool ok = false, rotated = false, wasPressed = false;
static bool foundOnce = false;  // found since the start (else no module: nothing to check)
static uint8_t chipId = 0;
static int32_t lastRawX = -1, lastRawY = -1;
static int8_t intOk = -1;    // INT line: -1 not seen working yet, 1 works, 0 does not (read every time)
static uint8_t intMiss = 0;  // touches found while the line was high
static uint8_t intSkip = 0;  // reads skipped since the last real one

static bool writeReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

static bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; ++i) buf[i] = Wire.read();
  return true;
}

// Gaps in a held touch: the FT6336 sometimes reports no touch point for a read or two while the finger stays
// (noise; seen all over the 4" panel, most in one corner). Up to GAP_READS such reads (LVGL reads every 30 ms)
// keep the press at its last point, so a held button is not let go; "lift-up" from the controller and a
// longer gap release it. Diagnosis: bridged gaps by length, and drop-outs that got through (release + new
// press within 150 ms, by the reason of the release); a line once a minute when there were some, totals
// for "screen check". (The log texts avoid the words the system log reads as warnings / errors.)
static constexpr uint8_t GAP_READS = 2;
enum { DR_I2C, DR_NONE, DR_LIFT };
static uint8_t gapReads = 0;
static uint32_t relAt = 0;
static uint8_t relWhy = 0;
static int32_t relX = 0, relY = 0, lastX = 0, lastY = 0, noteX = 0, noteY = 0;
static uint32_t bridgedMin[GAP_READS], bridgedAll[GAP_READS], dropMin[3], dropAll[3];
static uint32_t reads = 0, readErrs = 0, minuteAt = 0;

static void released(uint8_t why) {
  if (wasPressed) {
    relAt = millis();
    if (!relAt) relAt = 1;
    relWhy = why;
    relX = lastX, relY = lastY;
  }
  wasPressed = false;
  gapReads = 0;
}

static void gapReport() {
  const uint32_t now = millis();
  if (!minuteAt) minuteAt = now;
  if (now - minuteAt < 60000) return;
  minuteAt = now;
  const uint32_t b = bridgedMin[0] + bridgedMin[1], d = dropMin[0] + dropMin[1] + dropMin[2];
  if (b || d)
    LogSerial.printf("[touch] last minute: %lu short gap%s in held touches bridged (%lu of 1 read, %lu of 2 reads), %lu "
                     "longer (%lu no point, %lu lift-up, %lu I2C); last at x %ld y %ld\n",
                     (unsigned long)b, b == 1 ? "" : "s", (unsigned long)bridgedMin[0], (unsigned long)bridgedMin[1],
                     (unsigned long)d, (unsigned long)dropMin[DR_NONE], (unsigned long)dropMin[DR_LIFT],
                     (unsigned long)dropMin[DR_I2C], (long)noteX, (long)noteY);
  memset(bridgedMin, 0, sizeof(bridgedMin));
  memset(dropMin, 0, sizeof(dropMin));
}

void touchStats(char* info, size_t n) {
  snprintf(info, n, "%lu reads, %lu I2C read problems; gaps bridged: %lu of 1 read, %lu of 2 reads; longer (release + "
                    "press within 150 ms): %lu no point, %lu lift-up, %lu I2C; INT line %s",
           (unsigned long)reads, (unsigned long)readErrs, (unsigned long)bridgedAll[0], (unsigned long)bridgedAll[1],
           (unsigned long)dropAll[DR_NONE], (unsigned long)dropAll[DR_LIFT], (unsigned long)dropAll[DR_I2C],
           !TOUCH_USE_INT || TOUCH_PIN_INT < 0 ? "not used" : intOk > 0 ? "used" : intOk == 0 ? "NOT working, polled" : "not seen yet");
}

// no touch point / I2C problem / lift-up: bridged (press kept at its last point) or released
static bool noTouch(uint8_t why, int32_t* x, int32_t* y) {
  if (wasPressed && why != DR_LIFT && gapReads < GAP_READS) {
    ++gapReads;
    *x = lastX;
    *y = lastY;
    return true;
  }
  released(why);
  return false;
}

bool touchInit(const TouchCal&) {
  if (TOUCH_PIN_RST >= 0) {  // reset pulse: low >= 5 ms, then ~300 ms start-up
    pinMode(TOUCH_PIN_RST, OUTPUT);
    digitalWrite(TOUCH_PIN_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_PIN_RST, HIGH);
    delay(300);
  }
  if (TOUCH_PIN_INT >= 0) pinMode(TOUCH_PIN_INT, INPUT_PULLUP);
  Wire.begin(TOUCH_PIN_SDA, TOUCH_PIN_SCL, TOUCH_I2C_HZ);
  Wire.setTimeOut(20);  // ms; a stuck bus must not block the UI for long
  uint8_t id = 0, fw = 0;
  ok = readRegs(0xA3, &id, 1);
  readRegs(0xA6, &fw, 1);
  if (ok) writeReg(0xA4, 0);  // G_MODE 0: INT low while touched
  if (ok) {
    foundOnce = true;
    chipId = id;
  }
  LogSerial.printf("[touch] FT6336 %s, chip id 0x%02X, firmware 0x%02X\n", ok ? "found" : "NOT FOUND", id, fw);
  return ok;
}

// A device that was cut off in the middle of a byte can hold SDA low: up to 9 clocks let it finish,
// then a STOP; afterwards the reset pulse and a new start of the I2C driver (touchInit).
bool touchReset(const TouchCal& cal) {
  Wire.end();
  pinMode(TOUCH_PIN_SDA, INPUT_PULLUP);
  pinMode(TOUCH_PIN_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(TOUCH_PIN_SCL, HIGH);
  delayMicroseconds(10);
  int clocks = 0;
  while (digitalRead(TOUCH_PIN_SDA) == LOW && clocks < 9) {
    digitalWrite(TOUCH_PIN_SCL, LOW);
    delayMicroseconds(10);
    digitalWrite(TOUCH_PIN_SCL, HIGH);
    delayMicroseconds(10);
    ++clocks;
  }
  pinMode(TOUCH_PIN_SDA, OUTPUT_OPEN_DRAIN);  // STOP: SDA low -> high while SCL is high
  digitalWrite(TOUCH_PIN_SDA, LOW);
  delayMicroseconds(10);
  digitalWrite(TOUCH_PIN_SDA, HIGH);
  delayMicroseconds(10);
  if (clocks) LogSerial.printf("[touch] I2C bus freed (%d clocks)\n", clocks);
  wasPressed = false;
  relAt = 0;
  gapReads = 0;
  return touchInit(cal);
}

int touchCheck(char* info, size_t n) {
  if (!foundOnce) {
    snprintf(info, n, "FT6336 not found at the start - no check");
    return -1;
  }
  uint8_t id = 0;
  const bool answers = readRegs(0xA3, &id, 1) && id == chipId;
  if (answers) snprintf(info, n, "FT6336 answers, chip id 0x%02X", id);
  else snprintf(info, n, "FT6336 does NOT answer (read 0x%02X, chip id 0x%02X)", id, chipId);
  return answers ? 1 : 0;
}

void touchSetRotation(bool rotate180) { rotated = rotate180; }

void touchSetCal(const TouchCal&) {}  // capacitive: no calibration

void touchLastRaw(int32_t* rx, int32_t* ry) {
  *rx = lastRawX;
  *ry = lastRawY;
}

bool touchRead(int32_t* x, int32_t* y) {
  if (!ok) return false;  // not found at boot (display module not connected): no I2C traffic, no error log
  gapReport();
  bool intHigh = false;
  if (TOUCH_USE_INT && TOUCH_PIN_INT >= 0 && intOk != 0 && !wasPressed) {
    intHigh = digitalRead(TOUCH_PIN_INT);
    if (intHigh && ++intSkip < 25) return false;  // nothing touched (every 25th read checks the line)
  }
  intSkip = 0;
  uint8_t b[5];
  ++reads;
  if (!readRegs(0x02, b, sizeof(b))) {
    ++readErrs;
    return noTouch(DR_I2C, x, y);
  }
  const uint8_t points = b[0] & 0x0F;
  const uint8_t event = b[1] >> 6;
  if (points < 1 || points > 2 || event == 1) return noTouch(event == 1 && points >= 1 && points <= 2 ? DR_LIFT : DR_NONE, x, y);
  if (TOUCH_USE_INT && TOUCH_PIN_INT >= 0 && intOk != 0 && !wasPressed) {  // a new touch: did the line show it?
    if (!intHigh && intOk < 0) {
      intOk = 1;
      LogSerial.println("[touch] INT line works: I2C read only while touched");
    } else if (intHigh && ++intMiss >= 3) {
      intOk = 0;
      LogSerial.println("[touch] INT line stays high while touched - reading over I2C every time");
    }
  }
  const int32_t rx = ((b[1] & 0x0F) << 8) | b[2];
  const int32_t ry = ((b[3] & 0x0F) << 8) | b[4];
  lastRawX = rx;
  lastRawY = ry;
  int32_t px = TOUCH_SWAP_XY ? ry : rx;
  int32_t py = TOUCH_SWAP_XY ? rx : ry;
  if (TOUCH_INVERT_X) px = SCREEN_W - 1 - px;
  if (TOUCH_INVERT_Y) py = SCREEN_H - 1 - py;
  if (rotated) {
    px = SCREEN_W - 1 - px;
    py = SCREEN_H - 1 - py;
  }
  *x = constrain(px, 0, SCREEN_W - 1);
  *y = constrain(py, 0, SCREEN_H - 1);
  if (gapReads) {  // a gap bridged
    ++bridgedMin[gapReads - 1];
    ++bridgedAll[gapReads - 1];
    noteX = lastX, noteY = lastY;
    gapReads = 0;
  }
  if (!wasPressed && relAt && millis() - relAt < 150) {  // a gap that got through
    ++dropMin[relWhy];
    ++dropAll[relWhy];
    noteX = relX, noteY = relY;
  }
  relAt = 0;
  lastX = *x, lastY = *y;
  return wasPressed = true;
}
#endif
