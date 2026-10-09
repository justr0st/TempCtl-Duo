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
// CTP_INT is low while the panel is touched (G_MODE 0). Reading it saves the
// I2C transfer when nothing is touched; off until checked on the hardware.
#define TOUCH_USE_INT 0
#endif

static const uint8_t ADDR = 0x38;
static bool ok = false, rotated = false, wasPressed = false;
static int32_t lastRawX = -1, lastRawY = -1;

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
  return touchInit(cal);
}

void touchSetRotation(bool rotate180) { rotated = rotate180; }

void touchSetCal(const TouchCal&) {}  // capacitive: no calibration

void touchLastRaw(int32_t* rx, int32_t* ry) {
  *rx = lastRawX;
  *ry = lastRawY;
}

bool touchRead(int32_t* x, int32_t* y) {
  if (!ok) return false;  // not found at boot (display module not connected): no I2C traffic, no error log
  if (TOUCH_USE_INT && TOUCH_PIN_INT >= 0 && !wasPressed && digitalRead(TOUCH_PIN_INT)) return false;
  uint8_t b[5];
  if (!readRegs(0x02, b, sizeof(b))) return wasPressed = false;
  const uint8_t points = b[0] & 0x0F;
  const uint8_t event = b[1] >> 6;
  if (points < 1 || points > 2 || event == 1) return wasPressed = false;
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
  return wasPressed = true;
}
#endif
