// Status LED: on-board WS2812 (S3 board) or RGB LED (CYD, PWM), same colours:
//   green = idle, blue = running, amber blinking = waiting / paused / action
//   required, fault = slowly breathing magenta (for now, Rost 2026-10-01; was red
//   blinking - the status indication gets reworked later), purple = access point mode.
// (STATUS_LED_SINGLE: one LED - on = running, slow blink = waiting, fast
// blink = fault, off = idle.)
// Priority when several apply: fault > waiting > AP > running > idle.
#pragma once
#include <stdint.h>

enum class LedStatus : uint8_t { Off, Idle, Running, Ap, Wait, Fault };

// Sets the LED up and starts its own small task (blinking independent of
// the control task)
void statusLedBegin();
// Picks the status by priority from the flags
LedStatus statusLedPick(bool fault, bool wait, bool ap, bool running);
void statusLedSet(LedStatus s);
