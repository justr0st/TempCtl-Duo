// Touch input, one driver per board: own FT6336U driver (S3 board,
// touch_ft6336.cpp) or XPT2046_Touchscreen (CYD, touch_xpt2046.cpp).
#pragma once
#include <stdint.h>

struct TouchCal {  // XPT2046 raw range (resistive touch only)
  uint16_t minX, maxX, minY, maxY;
};

bool touchInit(const TouchCal& cal);
// Display turned by 180 degrees (same setting as the display)
void touchSetRotation(bool rotate180);
// Reads the touch point in screen coordinates (0..SCREEN_W-1, 0..SCREEN_H-1).
// Returns false when not touched (and on any bus error: no stuck presses).
bool touchRead(int32_t* x, int32_t* y);
// Last raw controller coordinates (for the bring-up screen / calibration)
void touchLastRaw(int32_t* rx, int32_t* ry);
// New calibration range, used at once (XPT2046; the FT6336 needs none)
void touchSetCal(const TouchCal& cal);
// Touch controller started again (FT6336: reset pin + I2C bus freed and set up again; XPT2046: its
// SPI bus set up again). True if the controller answers (XPT2046: always, it cannot be asked).
bool touchReset(const TouchCal& cal);
// Asks the controller whether it is still there: 1 answers, 0 does not, -1 cannot tell (XPT2046 has
// nothing to ask; FT6336 not found at boot). info: chip id etc. for the console.
int touchCheck(char* info, size_t n);
