#pragma once
#include <stdint.h>
#include "program.h"

constexpr uint16_t REG_UNSET = 0xFFFF;

enum Profile : uint8_t { PROFILE_PMA_KS4X = 0, PROFILE_GENERIC = 1 };
enum RampMode : uint8_t { RAMP_HOST = 0, RAMP_CONTROLLER = 1 };
enum EndAction : uint8_t { END_KEEP = 0, END_SET_SP = 1, END_OUTPUTS_OFF = 2 };

// Register map. All "analog" registers (pv, spEff, out, sp, rampRate) are
// int16 divided by `scale`. status/alarm/ctrlOff/remote are raw integers.
struct RegMap {
  uint16_t pv;        // process value (read)
  uint16_t spEff;     // effective setpoint (read, optional)
  uint16_t out;       // controller output % (read, optional)
  uint16_t status;    // controller status word (read, optional)
  uint16_t alarm;     // alarm status word (read, optional)
  uint16_t sp;        // setpoint (write)            -- REQUIRED to run programs
  uint16_t spLo;      // controller's lower setpoint limit (read, optional)
  uint16_t spHi;      // controller's upper setpoint limit (read, optional)
  uint16_t rampRate;  // setpoint gradient per minute (write, optional)
  uint16_t ctrlOff;   // write 1 = controller outputs off (optional)
  uint16_t remote;    // write 1 = front panel locked / remote (optional)
  float scale;        // 10 = one decimal
  uint8_t readFn;     // 3 or 4
  uint8_t writeFn;    // 6 or 16
  int16_t offRaw;     // raw value meaning "function switched off"
};

struct Settings {
  uint32_t magic;
  uint16_t version;

  // Modbus link
  uint8_t slaveId;
  uint32_t baud;
  uint8_t parity;      // 0 none, 1 even, 2 odd
  uint8_t stopBits;    // 1 or 2
  uint16_t timeoutMs;
  uint16_t pollMs;

  uint8_t profile;     // Profile
  RegMap reg;

  // Program behaviour
  uint8_t rampMode;    // RampMode
  float minStep;       // host mode: min. SP change before a new write
  uint16_t minWriteSec;// host mode: min. seconds between SP writes
  uint8_t guaranteedSoak;
  float soakBand;
  uint8_t endAction;   // EndAction
  float endSp;
  uint8_t lockFront;   // set remote=1 while running
  uint16_t commLossSec;// pause program after this many seconds without link
  float spMin, spMax;  // editor limits

  // Network
  char ssid[33];
  char pass[65];
  char apPass[33];
  char hostname[24];

  // Display
  uint8_t brightness;
  uint16_t tsMinX, tsMaxX, tsMinY, tsMaxY;

  // Setpoint-address finder range
  uint16_t scanFrom, scanTo;

  // ---- added in version 3 (everything above keeps the version 2 layout) ----
  uint8_t rotate180;   // display turned upside down
  uint8_t selProgram;  // program selected on the Main screen (0..MAX_PROGRAMS-1)
  float manualSp;      // last manual setpoint

  // ---- added in version 4 ----
  uint8_t themePalette;  // colour palette (ui/globals.xml, System > Display)
  uint8_t themeDark;     // 1 dark, 0 light, THEME_DARK_DEFAULT = theme_dark in ui/globals.xml
};

constexpr uint8_t THEME_DARK_DEFAULT = 0xFF;

void regMapDefaults(RegMap& r, uint8_t profile);
void settingsDefaults(Settings& s);
// Returns false if defaults were loaded. If an older (version 2) settings
// blob was migrated, its 6-step program is converted into *legacy and
// *haveLegacy is set.
bool settingsLoad(Settings& s, Program* legacy = nullptr, bool* haveLegacy = nullptr);
void settingsSave(const Settings& s);
uint32_t serialConfigFor(const Settings& s);
