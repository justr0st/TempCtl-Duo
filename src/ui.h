#pragma once
#include <stdint.h>
void uiInit();
void uiLoop();
// a firmware upload runs inside one web request: it calls this so the touch screen shows the progress
void uiOtaRefresh();
void uiSetBrightness(uint8_t b);
// Restart of the screen hardware without restarting the device: touch controller and display
// controller set up again, everything redrawn. Asked from any task, done in the next uiLoop().
// uiScreenRestartPoll(): number of restarts done so far (+ the result of the last one).
void uiScreenRestart(const char* from);  // from: "from the web page" ... (a static text, for the event log)
uint32_t uiScreenRestartPoll(bool& lcdOk, bool& touchOk);
// Automatic screen check (setting display.screenCheck): the state now, as lines for the console
void uiScreenCheckInfo(void (*line)(const char*));
// Test: switches the display off (sleep) so that the check restarts it. 1 done; -1 check off;
// -2 display status not readable; -3 no display
int uiScreenCheckTest();
// Touch calibration (resistive touch, CYD): 4 crosses on the screen, tapped one after the other.
// uiTouchCalStart() returns false where there is nothing to calibrate (S3) or no display.
// uiTouchCalPoll(): 0 = running (step = cross 1..4), 1 = done (raw range in the four values), -1 = cancelled.
bool uiTouchCalStart();
int uiTouchCalPoll(int& step, uint16_t& minX, uint16_t& maxX, uint16_t& minY, uint16_t& maxY);
void uiTouchCalCancel();
