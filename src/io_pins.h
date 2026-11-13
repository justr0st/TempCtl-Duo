// Optional pins (spec 9.25): pins of the board that are not tied to one job (S3: GPIO 4, 5, 6, IO_PINS_* in
// board_s3.h). Each one gets its function from the settings (Settings::ioFn, IoFn in settings.h): SSR supply
// relay, E-stop input, buzzer, cooling fan (on / off or 25 kHz PWM) or the fan's tach input. The CYD has none;
// its buzzer is fixed on PIN_BUZZER (speaker), which ioPin(IO_BUZZER) returns.
//
// Safe default: from ioEarlyInit() until the settings are applied every optional pin is an input with
// pull-down, nothing is driven (an SSR supply relay input stays off). ioConfigure() only touches pins whose
// function changes: a relay or fan that stays on its pin is not switched.
#pragma once
#include <stdint.h>

// LEDC channels: 0 = backlight, 1..3 = status LED (CYD), 6 = buzzer (safety.cpp). Channels n and n + 1
// share a timer (frequency): the fan has 4 (and 5) for itself, 7 stays unused (the buzzer changes its timer).
constexpr uint8_t IO_LEDC_FAN = 4;
constexpr uint8_t IO_LEDC_BUZZER = 6;
constexpr uint32_t IO_FAN_PWM_HZ = 25000;  // 4-wire fans: 21 ... 28 kHz

void ioEarlyInit();                  // first thing in setup(): every optional pin an input with pull-down
void ioConfigure(const uint8_t* fn);  // the settings' functions (IO_PINS_MAX entries; control task)
int ioPin(uint8_t fn);                // GPIO with that function now, -1 none (any task)
bool ioWrite(uint8_t fn, bool high);  // drive the output of that function (relay, fan); false: no pin has it
uint32_t ioTachPulses();              // falling edges on the tach pin since boot
