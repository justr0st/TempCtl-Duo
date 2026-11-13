// Safety extras (spec 9.2a): E-stop input, sounds, user activity, chip temperature.
//  - E-stop (the optional pin with the function "estop", io_pins.h; S3 default GPIO5): NC contact to GND,
//    internal pull-up. Open contact or broken wire = E-stop: the SSRs and the SSR supply relay go off at once
//    (SSR timer, local task), the control task stops the run, ends manual mode and switches the Modbus outputs
//    off. Nothing can be switched on while it is active. Read every 50 ms (on after 2 readings, released after 10).
//  - Sounds (the pin with the function "buzzer": S3 default GPIO6, CYD IO26 = speaker amplifier, fixed): each
//    sound is a list of notes (tone in Hz, length), played by a 10 ms timer through LEDC; the volume is the
//    PWM duty (settings.volume 1 ... 5). Fault / E-stop: three long beeps every 3 s, always at volume 5;
//    action required: two short beeps every 10 s; run finished: three rising notes once. Any user activity
//    (touch, web, serial) silences the current alarm until a new one comes up. One-shot sounds (key click,
//    confirm, refused, test) never interrupt an alarm.
//  - User activity: for the sound and the manual-mode idle timeout.
//  - Chip temperature (S3 internal sensor).
#pragma once
#include <stdint.h>

void safetyBegin();                       // buzzer channel + 10 ms timer (once, early in setup)
// from the settings (after ioConfigure: the E-stop and buzzer pins are known): E-stop watched, sounds on,
// volume 1 ... 5, a click on every touch-screen button
void safetyConfigure(bool estop, bool sound, uint8_t volume, bool keyBeep);
bool safetyEstopWired();                  // an optional pin has the E-stop function now
bool safetyEstop();                       // E-stop active (enabled and contact open)
bool safetyEstopRaw();                    // contact open now (also when not enabled; for the settings page)

bool safetySoundWired();                  // a buzzer pin exists now (CYD: always)
enum SoundLevel : uint8_t { SOUND_NONE = 0, SOUND_ACTION = 1, SOUND_ALARM = 2 };
void safetySound(SoundLevel level);       // the current condition (control task, every cycle)
void safetySoundFinished();               // three rising notes once (not over an alarm)
bool safetySoundMuted();

// One-shot sounds, any task. Nothing while the sound is off (safety.sound), FX_KEY also only with keyBeep on;
// FX_TEST plays even with the sound off (the settings page tries the buzzer before switching it on).
enum SoundFx : uint8_t {
  FX_KEY,      // key click (very short)
  FX_OK,       // confirm: two rising notes
  FX_REFUSED,  // refused: one low note
  FX_TEST,     // test: one beep at the set volume
};
void safetyFx(SoundFx fx);
void safetyBeepTest();                    // = safetyFx(FX_TEST)

void safetyActivity();                    // the user did something (touch, web action, serial command)
uint32_t safetyLastActivityMs();

bool safetyChipTempWired();               // S3: internal temperature sensor
float safetyChipTemp();                   // degC, NAN unknown (read by the control task every 30 s)
void safetyChipTempPoll();
