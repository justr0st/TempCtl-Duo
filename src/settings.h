// TempCtl Duo settings: common part + two channels (bottom, top).
//
// Stored as JSON in /duo/settings.json (settings_store.cpp); the same JSON is
// the web API's settings document (GET/POST /api/config). The conversion and
// validation here are pure C++ (no Arduino), so they are host-tested.
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

constexpr uint16_t REG_UNSET = 0xFFFF;
constexpr int NUM_CHANNELS = 2;
constexpr int CH_BOTTOM = 0, CH_TOP = 1;
constexpr uint8_t SETTINGS_FMT = 1;  // "fmt" of /duo/settings.json

enum Profile : uint8_t { PROFILE_PMA_KS4X = 0, PROFILE_GENERIC = 1 };
enum RampMode : uint8_t { RAMP_HOST = 0, RAMP_CONTROLLER = 1 };
enum EndAction : uint8_t { END_KEEP = 0, END_SET_SP = 1, END_OUTPUTS_OFF = 2 };
enum ChannelType : uint8_t { CH_OFF = 0, CH_MODBUS = 1, CH_LOCAL = 2 };
enum SensorType : uint8_t { SENSOR_MAX31855 = 0, SENSOR_MAX6675 = 1 };

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

// Thermal runaway detection (spec 9.2), kept per channel type
struct Runaway {
  uint8_t enable;
  float minOut;       // "heating failed": output >= minOut % ...
  uint16_t heatSec;   // ... for heatSec seconds ...
  float minRise;      // ... but PV rose less than minRise degC
  float holdBand;     // "runaway while holding": |PV - SP| > holdBand ...
  uint16_t holdSec;   // ... for longer than holdSec seconds (after SP was reached)
  // "heating while off" (stuck SSR / controller): outputs off, but the PV rises offRise degC within
  // offSec seconds (checked from 2 min after the outputs went off, not while the other channel heats)
  uint8_t offEnable;
  float offRise;
  uint16_t offSec;
  // "reading frozen": outputs on, the reading did not change at all for frozenSec seconds
  uint8_t frozenEnable;
  uint16_t frozenSec;
};

struct ModbusChannelSettings {
  uint8_t slaveId;
  uint8_t profile;       // Profile
  RegMap reg;
  uint8_t rampMode;      // RampMode
  float minStep;         // host mode: min. SP change before a new write
  uint16_t minWriteSec;  // host mode: min. seconds between SP writes
  uint8_t lockFront;     // set remote=1 while running
  Runaway runaway;
};

struct LocalChannelSettings {
  uint8_t sensor;       // SensorType
  float calOffset;      // true = raw * calGain + calOffset
  float calGain;
  float kp, ki, kd;     // PID (output % per degC, per degC*s, per degC/s)
  float windowSec;      // SSR time-proportioning window
  float maxOut;         // output limit %
  float maxTemp;        // SSR cut-off degC
  uint8_t cjEnable;     // converter too hot: cold junction (MAX31855) above cjMax degC
  float cjMax;
  Runaway runaway;
};

constexpr size_t CH_NAME_LEN = 17;  // 16 characters

struct ChannelSettings {
  char name[CH_NAME_LEN];
  uint8_t type;         // ChannelType
  uint8_t endAction;    // EndAction
  float endSp;
  float spMin, spMax;   // setpoint limits (editors, manual, programs)
  float manualSp;       // last manual setpoint
  ModbusChannelSettings mb;
  LocalChannelSettings loc;
};

struct Settings {
  // RS485 bus (shared by the Modbus channels)
  uint32_t baud;
  uint8_t parity;       // 0 none, 1 even, 2 odd
  uint8_t stopBits;     // 1 or 2
  uint16_t timeoutMs;
  uint16_t pollMs;
  uint16_t commLossSec; // pause the program after this many seconds without link

  // Run
  float bottomBand;     // "bottom reached" band (+-degC) for "wait for bottom"

  // Network
  char ssid[33];
  char pass[65];
  char apPass[33];
  char hostname[24];
  uint8_t staticIp;      // 1: fixed address below instead of DHCP (station)
  uint8_t ip[4], subnet[4], gateway[4], dns1[4], dns2[4];  // a.b.c.d; dns 0.0.0.0 = gateway / none

  // Display
  uint8_t brightness;
  uint8_t rotate180;
  uint16_t tsMinX, tsMaxX, tsMinY, tsMaxY;  // XPT2046 calibration (CYD)
  uint8_t themeDark;     // 1 dark, 0 light, THEME_DARK_DEFAULT = theme_dark in ui/globals.xml
  uint8_t themePalette;  // unused (TempCtl 1.2 palettes, replaced by accentColor); kept in the JSON
  uint32_t accentColor;  // RGB 0xRRGGBB
  uint16_t liveMin;      // live chart period, minutes (at most what the live history holds)
  uint8_t screenCheck;   // 1: watch the display / touch controller, restart one that hangs (default on)

  // Time zone (local time in the SD run logs): "Europe/Berlin", "UTC+02:00" (its rule: tzLookup); empty =
  // not set yet (UTC; the web page sets it from the browser once)
  char tzName[32];

  // Safety (spec 9.2a)
  uint8_t manualIdle;     // 1: manual mode ends after manualIdleMin minutes without user activity
  uint16_t manualIdleMin;
  uint8_t estop;          // 1: E-stop input watched (PIN_ESTOP, NC contact to GND)
  uint8_t sound;          // 1: alarm beeps (PIN_BUZZER)
  uint8_t chipWarn;       // 1: warning above chipMaxC (S3 chip temperature)
  float chipMaxC;

  // Tools: setpoint-address finder range
  uint16_t scanFrom, scanTo;

  uint8_t selProgram;    // program selected for the next run

  ChannelSettings ch[NUM_CHANNELS];
};

constexpr uint8_t THEME_DARK_DEFAULT = 0xFF;

// What the board's wiring allows (board_*.h: HAS_RS485, LOCAL_CHANNELS_MAX)
struct BoardLimits {
  bool rs485;
  uint8_t localMax;
};

void regMapDefaults(RegMap& r, uint8_t profile);
void settingsDefaults(Settings& s, const BoardLimits& lim);

// Makes the settings consistent with the board and the value ranges.
// Channel types the board can't wire are switched to Off. Returns false and a
// message if something had to be changed that the caller asked for explicitly
// (used to reject a POST); the settings are corrected either way.
bool settingsValidate(Settings& s, const BoardLimits& lim, char* msg, size_t n);

const char* channelTypeName(uint8_t t);  // "off" / "modbus" / "local"

// forApi = true: passwords are replaced by "passSet" / "apPassSet"
void settingsToJson(const Settings& s, JsonObject o, bool forApi);
// Merges the keys present in `o` into `s` (missing keys keep their values,
// unknown keys are ignored). Returns false + message on a wrong value type.
bool settingsFromJson(JsonObjectConst o, Settings& s, char* msg, size_t n);

// Time zones: a name from the list ("Europe/Berlin") or "UTC", "UTC+02:00", "UTC-03:30" -> its POSIX rule.
// False if unknown. tzCount / tzName(i): the list (for the web page, the touch screen, serial).
bool tzLookup(const char* name, char* tz, size_t n);
int tzCount();
const char* tzListName(int i);
