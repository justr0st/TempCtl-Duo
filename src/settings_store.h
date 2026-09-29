// Settings on the device: /duo/settings.json on LittleFS (safe save: temp
// file + rename), the board's limits and the serial format of the bus.
#pragma once
#include "settings.h"

BoardLimits boardLimits();
// Loads and validates /duo/settings.json. Returns false if the file was
// missing or unreadable (then `s` holds the defaults).
bool settingsLoad(Settings& s);
bool settingsSave(const Settings& s);
uint32_t serialConfigFor(const Settings& s);
