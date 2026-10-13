// Safety extras (spec 9.2a): E-stop input, alarm sound, user activity, chip temperature.
//  - E-stop (PIN_ESTOP, S3 GPIO5): NC contact to GND, internal pull-up. Open contact or broken wire =
//    E-stop: the SSRs and the SSR supply relay go off at once (SSR timer, local task), the control task
//    stops the run, ends manual mode and switches the Modbus outputs off. Nothing can be switched on
//    while it is active. Read every 50 ms (on after 2 readings, released after 10).
//  - Alarm sound (PIN_BUZZER: S3 GPIO6, CYD IO26 = speaker amplifier): fault / E-stop: three long beeps
//    every 3 s; action required: two short beeps every 10 s; run finished: three short beeps once.
//    Any user activity (touch, web, serial) silences the current alarm until a new one comes up.
//  - User activity: for the sound and the manual-mode idle timeout.
//  - Chip temperature (S3 internal sensor).
#pragma once
#include <stdint.h>

void safetyBegin();                       // pins + 50 ms timer (once, early in setup)
void safetyConfigure(bool estop, bool sound);
bool safetyEstopWired();                  // the board has an E-stop input
bool safetyEstop();                       // E-stop active (enabled and contact open)
bool safetyEstopRaw();                    // contact open now (also when not enabled; for the settings page)

bool safetySoundWired();
enum SoundLevel : uint8_t { SOUND_NONE = 0, SOUND_ACTION = 1, SOUND_ALARM = 2 };
void safetySound(SoundLevel level);       // the current condition (control task, every cycle)
void safetySoundFinished();               // three short beeps once
bool safetySoundMuted();
void safetyBeepTest();                    // settings page: one short beep

void safetyActivity();                    // the user did something (touch, web action, serial command)
uint32_t safetyLastActivityMs();

bool safetyChipTempWired();               // S3: internal temperature sensor
float safetyChipTemp();                   // degC, NAN unknown (read by the control task every 30 s)
void safetyChipTempPoll();
