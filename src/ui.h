#pragma once
#include <stdint.h>
void uiInit();
void uiLoop();
void uiSetBrightness(uint8_t b);
// Restart of the screen hardware without restarting the device: touch controller and display
// controller set up again, everything redrawn. Asked from any task, done in the next uiLoop().
// uiScreenRestartPoll(): number of restarts done so far (+ the result of the last one).
void uiScreenRestart();
uint32_t uiScreenRestartPoll(bool& lcdOk, bool& touchOk);
// Touch calibration (resistive touch, CYD): 4 crosses on the screen, tapped one after the other.
// uiTouchCalStart() returns false where there is nothing to calibrate (S3) or no display.
// uiTouchCalPoll(): 0 = running (step = cross 1..4), 1 = done (raw range in the four values), -1 = cancelled.
bool uiTouchCalStart();
int uiTouchCalPoll(int& step, uint16_t& minX, uint16_t& maxX, uint16_t& minY, uint16_t& maxY);
void uiTouchCalCancel();
// colour palettes of the touch UI (names separated by '\n', index = Settings::themePalette)
const char* uiPaletteNames();
int uiPaletteCount();
