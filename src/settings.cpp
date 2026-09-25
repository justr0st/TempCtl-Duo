#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

static constexpr uint32_t SETTINGS_MAGIC = 0x54435331;  // "TCS1"
static constexpr uint16_t SETTINGS_VERSION = 2;

// PMA KS 40-1/41-1/42-1 addresses from interface description 9499 040 63511.
// Address layout: base + 8192 = integer with 1 decimal ("1dP"), which we use
// for all temperatures (value = raw / 10).
static constexpr uint16_t DP1 = 8192;

void regMapDefaults(RegMap& r, uint8_t profile) {
  if (profile == PROFILE_PMA_KS4X) {
    r.pv = 5102 + DP1;       // Cntr/Signal In.1   (13294) measured value after correction
    r.spEff = 5101 + DP1;    // Cntr/Signal SP.EF  (13293) effective setpoint
    r.out = 5103 + DP1;      // Cntr/Signal Ypid   (13295) controller output %
    r.status = 5100;         // Cntr/Signal St.Cntr (bit 0 heating, bits 12-15 mode)
    r.alarm = 250;           // ohnE/Signal St.Ala
    r.sp = 3180 + DP1;       // SEtP/Signal SP     (11372) interface setpoint. RAM is updated
                             // immediately, EEPROM storage is timed (max ~1 per 30 min)
    r.spLo = 3100 + DP1;     // SEtP/PArA SP.LO    (11292) lower setpoint limit
    r.spHi = 3101 + DP1;     // SEtP/PArA SP.Hi    (11293) upper setpoint limit
    r.rampRate = 3103 + DP1; // SEtP/PArA r.SP     (11295) setpoint gradient per minute
    r.ctrlOff = 1086;        // LOGI/Signal C.Off  (1 = all controller outputs off)
    r.remote = 1080;         // LOGI/Signal L-R    (1 = front panel blocked)
    r.scale = 10.0f;
    r.readFn = 3;
    r.writeFn = 6;
    r.offRaw = -32000;
  } else {
    // Generic Modbus RTU controller (e.g. LCTC TE-M226): fill in from its manual.
    r.pv = REG_UNSET;
    r.spEff = REG_UNSET;
    r.out = REG_UNSET;
    r.status = REG_UNSET;
    r.alarm = REG_UNSET;
    r.sp = REG_UNSET;
    r.spLo = REG_UNSET;
    r.spHi = REG_UNSET;
    r.rampRate = REG_UNSET;
    r.ctrlOff = REG_UNSET;
    r.remote = REG_UNSET;
    r.scale = 10.0f;
    r.readFn = 3;
    r.writeFn = 6;
    r.offRaw = -32000;
  }
}

void settingsDefaults(Settings& s) {
  memset(&s, 0, sizeof(s));
  s.magic = SETTINGS_MAGIC;
  s.version = SETTINGS_VERSION;
  // KS 4x-1 factory defaults: address 1, 9600 baud, even parity (PrtY = 1)
  s.slaveId = 1;
  s.baud = 9600;
  s.parity = 1;
  s.stopBits = 1;
  s.timeoutMs = 300;
  s.pollMs = 1000;
  s.profile = PROFILE_PMA_KS4X;
  regMapDefaults(s.reg, s.profile);

  s.rampMode = RAMP_CONTROLLER;  // falls back to host mode if rampRate is unset
  s.minStep = 0.5f;
  s.minWriteSec = 5;
  s.guaranteedSoak = 0;
  s.soakBand = 5.0f;
  s.endAction = END_KEEP;
  s.endSp = 0;
  s.lockFront = 0;
  s.commLossSec = 30;
  s.spMin = 0;
  s.spMax = 900;   // KS 4x-1 default SP.Hi

  strcpy(s.apPass, "tempctl123");
  strcpy(s.hostname, "tempctl");
  s.brightness = 200;
  s.tsMinX = 200; s.tsMaxX = 3700;
  s.tsMinY = 240; s.tsMaxY = 3800;

  s.scanFrom = 3100 + DP1;
  s.scanTo = 3199 + DP1;

  // Example program: 6 steps (last two disabled)
  s.program.s[0] = {1, 100, 30, 30};
  s.program.s[1] = {1, 250, 60, 60};
  s.program.s[2] = {1, 400, 60, 120};
  s.program.s[3] = {1, 200, 90, 0};
  s.program.s[4] = {0, 100, 60, 0};
  s.program.s[5] = {0, 50, 30, 0};
}

bool settingsLoad(Settings& s) {
  Preferences p;
  p.begin("tempctl", true);
  Settings tmp;
  const size_t n = p.getBytes("cfg", &tmp, sizeof(tmp));
  p.end();
  if (n != sizeof(tmp) || tmp.magic != SETTINGS_MAGIC || tmp.version != SETTINGS_VERSION) {
    settingsDefaults(s);
    return false;
  }
  s = tmp;
  // make sure strings are terminated
  s.ssid[sizeof(s.ssid) - 1] = 0;
  s.pass[sizeof(s.pass) - 1] = 0;
  s.apPass[sizeof(s.apPass) - 1] = 0;
  s.hostname[sizeof(s.hostname) - 1] = 0;
  return true;
}

void settingsSave(const Settings& s) {
  Preferences p;
  p.begin("tempctl", false);
  p.putBytes("cfg", &s, sizeof(s));
  p.end();
}

uint32_t serialConfigFor(const Settings& s) {
  if (s.parity == 1) return SERIAL_8E1;
  if (s.parity == 2) return SERIAL_8O1;
  return s.stopBits == 2 ? SERIAL_8N2 : SERIAL_8N1;
}
