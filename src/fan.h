// Cooling fan output (spec 9.25): the optional pin with the function "fan" (on / off, active high: a MOSFET
// or relay module) or "fanPwm" (a 4-wire fan's control wire, 25 kHz, duty = speed), and "fanTach" for its
// speed. When it runs: fan_rule.h (control task, app_state.cpp).
#pragma once
#include <stdint.h>

void fanSet(bool on, uint8_t speedPct);  // every control cycle (speed: PWM fan only, 0 ... 100 %)
bool fanWired();                         // a fan output pin exists now
bool fanOn();                            // switched on (and wired)
int fanRpm();                            // -1 without a tach pin; else turns per minute (2 pulses per turn, ~1 s)
