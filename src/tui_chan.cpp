// Touch UI channel settings: one tab per channel (general, Modbus controller or thermocouple +
// PID + autotune, runaway protection), the RS485 bus and (HAS_MONITOR) the monitor channel. Edits go to a copy; Save validates and
// stores it (the same rules as the web page). Register addresses and the two-point calibration
// are on the web page.
#include "tui.h"
#include "psram.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "local_ctrl.h"
#include "settings_store.h"

static PSRAM_VAR(Settings, E);  // edited copy
static bool eDirty = false;
static uint32_t eGen = 0;  // settingsGen() of the copy in E (a save elsewhere - web, serial - reloads it)
// control sizes (480x320 / 320x240)
// (with the monitor channel's tab four tabs share the header: narrower)
constexpr int CW = TUI_SMALL ? 128 : 150, CW2 = TUI_SMALL ? 90 : 110, BH = TUI_SMALL ? 32 : 36,
              TAB_W = TUI_SMALL ? 66 : HAS_MONITOR ? 76 : 104;
constexpr int TAB_BUS = 2, TAB_MON = 3;
static int tab = 0;  // 0, 1 = channel, 2 = bus, 3 = monitor channel
static lv_obj_t *cScr, *cTabs[4], *cBody, *cSave, *tuneLbl, *tuneRow, *tuneSw;  // (tuneSw: rule, off while tuning)

enum FKind : uint8_t { F_NUM, F_SEL, F_SW, F_HEAD, F_NOTE };
struct Field {
  FKind k;
  const char* label;
  const char* hint;
  float lo, hi;
  uint8_t dec;
  const char* unit;
  const char* opts;  // F_SEL: '\n'-separated
  float (*get)(const Settings&, int);
  void (*set)(Settings&, int, float);
  bool (*show)(const Settings&, int);
};

static Runaway& ra(Settings& s, int c) { return s.ch[c].type == CH_LOCAL ? s.ch[c].loc.runaway : s.ch[c].mb.runaway; }
static const Runaway& ra(const Settings& s, int c) {
  return s.ch[c].type == CH_LOCAL ? s.ch[c].loc.runaway : s.ch[c].mb.runaway;
}
static bool isOn(const Settings& s, int c) { return s.ch[c].type != CH_OFF; }
static bool isMb(const Settings& s, int c) { return s.ch[c].type == CH_MODBUS; }
static bool isLoc(const Settings& s, int c) { return s.ch[c].type == CH_LOCAL; }
static bool isHostRamp(const Settings& s, int c) { return isMb(s, c) && s.ch[c].mb.rampMode == RAMP_HOST; }
static bool raOn(const Settings& s, int c) { return isOn(s, c) && ra(s, c).enable; }
static bool offOn(const Settings& s, int c) { return isOn(s, c) && ra(s, c).offEnable; }
static bool frozenOn(const Settings& s, int c) { return isOn(s, c) && ra(s, c).frozenEnable; }
static bool cjOn(const Settings& s, int c) { return isLoc(s, c) && s.ch[c].loc.cjEnable; }
static bool medOn(const Settings& s, int c) { return isLoc(s, c) && s.ch[c].loc.median; }
// local: read interval, median filter, autotune time limit
static float gReadMs(const Settings& s, int c) { return s.ch[c].loc.readMs; }
static void sReadMs(Settings& s, int c, float v) { s.ch[c].loc.readMs = (uint16_t)v; }
static float gMedian(const Settings& s, int c) { return s.ch[c].loc.median; }
static void sMedian(Settings& s, int c, float v) { s.ch[c].loc.median = v != 0; }
static float gMedianN(const Settings& s, int c) { return (s.ch[c].loc.medianN - 3) / 2; }  // 3, 5, 7, 9 -> 0 ... 3
static void sMedianN(Settings& s, int c, float v) { s.ch[c].loc.medianN = (uint8_t)(3 + 2 * (int)v); }
static float gTuneMax(const Settings& s, int c) { return s.ch[c].loc.tuneMaxMin; }
static void sTuneMax(Settings& s, int c, float v) { s.ch[c].loc.tuneMaxMin = (uint16_t)v; }
static float gTuneFast(const Settings& s, int c) { return s.ch[c].loc.tuneFast; }
static void sTuneFast(Settings& s, int c, float v) { s.ch[c].loc.tuneFast = v != 0; }

static const uint32_t BAUDS[] = {2400, 4800, 9600, 19200, 38400};

// field accessors (named functions, not lambdas: with C++11 the table could then not be a constant and
// would be built in RAM at start)
static float fg1(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].type);
}
static void fs2(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].type = (uint8_t)v;
}
static float fg3(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].endAction);
}
static void fs4(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].endAction = (uint8_t)v;
}
static float fg5(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].endSp);
}
static void fs6(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].endSp = (float)v;
}
static bool fl7(const Settings& s, int c) { return isOn(s, c) && s.ch[c].endAction == END_SET_SP; }
static float fg8(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].spMin);
}
static void fs9(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].spMin = (float)v;
}
static float fg10(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].spMax);
}
static void fs11(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].spMax = (float)v;
}
static float fg12(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].mb.slaveId);
}
static void fs13(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].mb.slaveId = (uint8_t)v;
}
static float fg14(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].mb.profile);
}
static void fl15(Settings& s, int c, float v) {  // a new type loads its register defaults
  if ((uint8_t)v != s.ch[c].mb.profile) regMapDefaults(s.ch[c].mb.reg, (uint8_t)v);
  s.ch[c].mb.profile = (uint8_t)v;
}
static float fg16(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].mb.rampMode);
}
static void fs17(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].mb.rampMode = (uint8_t)v;
}
static float fg18(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].mb.minStep);
}
static void fs19(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].mb.minStep = (float)v;
}
static float fg20(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].mb.minWriteSec);
}
static void fs21(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].mb.minWriteSec = (uint16_t)v;
}
static float fg22(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].mb.lockFront);
}
static void fs23(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].mb.lockFront = (uint8_t)v;
}
static float fg24(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].loc.sensor);
}
static void fs25(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].loc.sensor = (uint8_t)v;
}
static float fg26(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].loc.calOffset);
}
static void fs27(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].loc.calOffset = (float)v;
}
static float fg28(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].loc.calGain);
}
static void fs29(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].loc.calGain = (float)v;
}
static float fg30(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].loc.kp);
}
static void fs31(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].loc.kp = (float)v;
}
static float fg32(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].loc.ki);
}
static void fs33(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].loc.ki = (float)v;
}
static float fg34(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].loc.kd);
}
static void fs35(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].loc.kd = (float)v;
}
static float fg36(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].loc.windowSec);
}
static void fs37(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].loc.windowSec = (float)v;
}
static float fg38(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].loc.maxOut);
}
static void fs39(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].loc.maxOut = (float)v;
}
static float fg40(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].loc.maxTemp);
}
static void fs41(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].loc.maxTemp = (float)v;
}
static float fg42(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].loc.cjEnable);
}
static void fs43(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].loc.cjEnable = (uint8_t)v;
}
static float fg44(const Settings& s, int c) {
  (void)c;
  return (float)(s.ch[c].loc.cjMax);
}
static void fs45(Settings& s, int c, float v) {
  (void)c;
  s.ch[c].loc.cjMax = (float)v;
}
static float fg46(const Settings& s, int c) {
  (void)c;
  return (float)(ra(s, c).enable);
}
static void fs47(Settings& s, int c, float v) {
  (void)c;
  ra(s, c).enable = (uint8_t)v;
}
static float fg48(const Settings& s, int c) {
  (void)c;
  return (float)(ra(s, c).minOut);
}
static void fs49(Settings& s, int c, float v) {
  (void)c;
  ra(s, c).minOut = (float)v;
}
static float fg50(const Settings& s, int c) {
  (void)c;
  return (float)(ra(s, c).heatSec);
}
static void fs51(Settings& s, int c, float v) {
  (void)c;
  ra(s, c).heatSec = (uint16_t)v;
}
static float fg52(const Settings& s, int c) {
  (void)c;
  return (float)(ra(s, c).minRise);
}
static void fs53(Settings& s, int c, float v) {
  (void)c;
  ra(s, c).minRise = (float)v;
}
static float fg54(const Settings& s, int c) {
  (void)c;
  return (float)(ra(s, c).holdBand);
}
static void fs55(Settings& s, int c, float v) {
  (void)c;
  ra(s, c).holdBand = (float)v;
}
static float fg56(const Settings& s, int c) {
  (void)c;
  return (float)(ra(s, c).holdSec);
}
static void fs57(Settings& s, int c, float v) {
  (void)c;
  ra(s, c).holdSec = (uint16_t)v;
}
static float fg58(const Settings& s, int c) {
  (void)c;
  return (float)(ra(s, c).offEnable);
}
static void fs59(Settings& s, int c, float v) {
  (void)c;
  ra(s, c).offEnable = (uint8_t)v;
}
static float fg60(const Settings& s, int c) {
  (void)c;
  return (float)(ra(s, c).offRise);
}
static void fs61(Settings& s, int c, float v) {
  (void)c;
  ra(s, c).offRise = (float)v;
}
static float fg62(const Settings& s, int c) {
  (void)c;
  return (float)(ra(s, c).offSec);
}
static void fs63(Settings& s, int c, float v) {
  (void)c;
  ra(s, c).offSec = (uint16_t)v;
}
static float fg64(const Settings& s, int c) {
  (void)c;
  return (float)(ra(s, c).frozenEnable);
}
static void fs65(Settings& s, int c, float v) {
  (void)c;
  ra(s, c).frozenEnable = (uint8_t)v;
}
static float fg66(const Settings& s, int c) {
  (void)c;
  return (float)(ra(s, c).frozenSec);
}
static void fs67(Settings& s, int c, float v) {
  (void)c;
  ra(s, c).frozenSec = (uint16_t)v;
}
static float fl68(const Settings& s, int) {
  for (int i = 0; i < 5; ++i)
    if (BAUDS[i] == s.baud) return i;
  return 2;
}
static void fl69(Settings& s, int, float v) { s.baud = BAUDS[(int)v]; }
static float fg70(const Settings& s, int c) {
  (void)c;
  return (float)(s.parity);
}
static void fs71(Settings& s, int c, float v) {
  (void)c;
  s.parity = (uint8_t)v;
}
static float fg72(const Settings& s, int c) {
  (void)c;
  return (float)(s.stopBits - 1);
}
static void fl73(Settings& s, int, float v) { s.stopBits = (uint8_t)v + 1; }
static float fg74(const Settings& s, int c) {
  (void)c;
  return (float)(s.timeoutMs);
}
static void fs75(Settings& s, int c, float v) {
  (void)c;
  s.timeoutMs = (uint16_t)v;
}
static float fg76(const Settings& s, int c) {
  (void)c;
  return (float)(s.commLossSec);
}
static void fs77(Settings& s, int c, float v) {
  (void)c;
  s.commLossSec = (uint16_t)v;
}

static const Field CH_FIELDS[] = {
    {F_SEL, "Type", nullptr, 0, 0, 0, nullptr, "Off (not used)\nModbus controller (beta)\nLocal (thermocouple + SSR)",
     fg1, fs2, nullptr},
    {F_SEL, "When the program ends or stops", nullptr, 0, 0, 0, nullptr,
     "Keep the last setpoint\nGo to the end setpoint\nSwitch the outputs off", fg3,
     fs4, isOn},
    {F_NUM, "End setpoint", nullptr, -200, 1500, 1, "\xC2\xB0" "C", nullptr, fg5, fs6,
     fl7},
    {F_NUM, "Lowest setpoint", nullptr, -200, 1500, 1, "\xC2\xB0" "C", nullptr, fg8, fs9, isOn},
    {F_NUM, "Highest setpoint", "Local: at most the cut-off temperature \xE2\x88\x92 10 \xC2\xB0" "C", -200, 1500, 1,
     "\xC2\xB0" "C", nullptr, fg10, fs11, isOn},
    // Modbus
    {F_HEAD, "Modbus controller (beta)", nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, isMb},
    {F_NUM, "Slave address", nullptr, 1, 247, 0, "", nullptr, fg12, fs13, isMb},
    {F_SEL, "Controller type", "A new type loads its register defaults", 0, 0, 0, nullptr,
     "PMA KS 40-1 / 41-1 / 42-1\nGeneric Modbus RTU", fg14,
     fl15,
     isMb},
    {F_SEL, "Setpoint ramps", nullptr, 0, 0, 0, nullptr, "ESP32 writes setpoint steps\nController ramps (r.SP)",
     fg16, fs17, isMb},
    {F_NUM, "Min. step between writes", nullptr, 0.1f, 100, 1, "\xC2\xB0" "C", nullptr, fg18,
     fs19, isHostRamp},
    {F_NUM, "Min. time between writes", nullptr, 1, 3600, 0, "s", nullptr, fg20,
     fs21, isHostRamp},
    {F_SW, "Lock the front panel while running", nullptr, 0, 0, 0, nullptr, nullptr, fg22,
     fs23, isMb},
    {F_NOTE, "Register addresses: web page, Channels.", nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, isMb},
    // local
    {F_HEAD, "Thermocouple", nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, isLoc},
    {F_SEL, "Converter", nullptr, 0, 0, 0, nullptr, "MAX31855\nMAX6675", fg24, fs25,
     isLoc},
    {F_NUM, "Calibration offset", "Calibrated = raw \xC3\x97 gain + offset", -50, 50, 2, "\xC2\xB0" "C", nullptr,
     fg26, fs27, isLoc},
    {F_NUM, "Calibration gain", nullptr, 0.5f, 2, 4, "", nullptr, fg28, fs29, isLoc},
    {F_NUM, "Read every", "MAX6675: at least 250 ms (its conversion time)", 100, 1000, 0, "ms", nullptr, gReadMs, sReadMs,
     isLoc},
    {F_SW, "Median filter", "Removes single-reading spikes; a change shows about half the window later", 0, 0, 0,
     nullptr, nullptr, gMedian, sMedian, isLoc},
    {F_SEL, "\xE2\x80\xA6 over the last", nullptr, 0, 0, 0, nullptr, "3 readings\n5 readings\n7 readings\n9 readings", gMedianN,
     sMedianN, medOn},
    {F_HEAD, "PID control", nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, isLoc},
    {F_NUM, "Autotune time limit", "The autotune gives up after this time", 10, 240, 0, "min", nullptr, gTuneMax, sTuneMax,
     isLoc},
    {F_SW, "Autotune: faster", "Off: slow, no overshoot. On: reaches the setpoint 2-3x sooner, a little overshoot",
     0, 0, 0, nullptr, nullptr, gTuneFast, sTuneFast, isLoc},
    {F_NUM, "Kp", "% output per \xC2\xB0" "C", 0, 1000, 2, "", nullptr, fg30, fs31, isLoc},
    {F_NUM, "Ki", "% per \xC2\xB0" "C\xC2\xB7s", 0, 100, 4, "", nullptr, fg32, fs33, isLoc},
    {F_NUM, "Kd", "%\xC2\xB7s per \xC2\xB0" "C", 0, 10000, 1, "", nullptr, fg34, fs35, isLoc},
    {F_NUM, "SSR window", "The SSR is on for the output share of every window", 0.2f, 10, 1, "s", nullptr,
     fg36, fs37, isLoc},
    {F_NUM, "Max. output", nullptr, 0, 100, 0, "%", nullptr, fg38, fs39, isLoc},
    {F_NUM, "Cut-off temperature", "Outputs off above", 50, 600, 0, "\xC2\xB0" "C", nullptr, fg40,
     fs41, isLoc},
    {F_SW, "Converter too hot: fault", "MAX31855 cold junction (the converter's own temperature)", 0, 0, 0, nullptr, nullptr,
     fg42, fs43, isLoc},
    {F_NUM, "\xE2\x80\xA6 above", nullptr, 30, 125, 0, "\xC2\xB0" "C", nullptr, fg44, fs45, cjOn},
    // runaway (values of the channel's type)
    {F_HEAD, "Thermal runaway protection", nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, isOn},
    {F_SW, "Runaway detection", nullptr, 0, 0, 0, nullptr, nullptr, fg46, fs47, isOn},
    {F_NUM, "Heating failed: output at least", nullptr, 0, 100, 0, "%", nullptr, fg48, fs49, raOn},
    {F_NUM, "\xE2\x80\xA6 for", nullptr, 10, 3600, 0, "s", nullptr, fg50, fs51, raOn},
    {F_NUM, "\xE2\x80\xA6 with a rise below", nullptr, 0.5f, 100, 1, "\xC2\xB0" "C", nullptr, fg52,
     fs53, raOn},
    {F_NUM, "Holding: PV off the setpoint by more than \xC2\xB1", nullptr, 1, 200, 0, "\xC2\xB0" "C", nullptr,
     fg54, fs55, raOn},
    {F_NUM, "\xE2\x80\xA6 for longer than", nullptr, 5, 3600, 0, "s", nullptr, fg56, fs57, raOn},
    {F_SW, "Heating while off (stuck SSR)", "Checked from 2 min after off, not while the other channel heats", 0, 0, 0,
     nullptr, nullptr, fg58, fs59, isOn},
    {F_NUM, "\xE2\x80\xA6 PV rises by", nullptr, 2, 100, 1, "\xC2\xB0" "C", nullptr, fg60, fs61, offOn},
    {F_NUM, "\xE2\x80\xA6 within", nullptr, 10, 3600, 0, "s", nullptr, fg62, fs63, offOn},
    {F_SW, "Reading frozen", "Outputs on, but the reading does not change at all", 0, 0, 0, nullptr, nullptr,
     fg64, fs65, isOn},
    {F_NUM, "\xE2\x80\xA6 for", nullptr, 10, 3600, 0, "s", nullptr, fg66, fs67, frozenOn},
};

static const Field BUS_FIELDS[] = {
    {F_NOTE, "Shared by both Modbus channels. Modbus is beta: not tested with a controller yet.", nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr},
    {F_SEL, "Baud rate", nullptr, 0, 0, 0, nullptr, "2400\n4800\n9600\n19200\n38400",
     fl68,
     fl69, nullptr},
    {F_SEL, "Parity", nullptr, 0, 0, 0, nullptr, "None\nEven\nOdd", fg70, fs71, nullptr},
    {F_SEL, "Stop bits", nullptr, 0, 0, 0, nullptr, "1\n2", fg72, fl73,
     nullptr},
    {F_NUM, "Response timeout", nullptr, 50, 2000, 0, "ms", nullptr, fg74, fs75, nullptr},
    {F_NUM, "Pause the run after no answer for", "0 = never pause", 0, 3600, 0, "s", nullptr, fg76,
     fs77, nullptr},
};

// monitor channel (the channel argument is not used)
static bool monOn(const Settings& s, int) { return s.mon.on; }
static bool monMedOn(const Settings& s, int) { return s.mon.on && s.mon.median; }
static float mgOn(const Settings& s, int) { return s.mon.on; }
static void msOn(Settings& s, int, float v) { s.mon.on = v != 0; }
static float mgSensor(const Settings& s, int) { return s.mon.sensor; }
static void msSensor(Settings& s, int, float v) { s.mon.sensor = (uint8_t)v; }
static float mgOff(const Settings& s, int) { return s.mon.calOffset; }
static void msOff(Settings& s, int, float v) { s.mon.calOffset = v; }
static float mgGain(const Settings& s, int) { return s.mon.calGain; }
static void msGain(Settings& s, int, float v) { s.mon.calGain = v; }
static float mgRead(const Settings& s, int) { return s.mon.readMs; }
static void msRead(Settings& s, int, float v) { s.mon.readMs = (uint16_t)v; }
static float mgMed(const Settings& s, int) { return s.mon.median; }
static void msMed(Settings& s, int, float v) { s.mon.median = v != 0; }
static float mgMedN(const Settings& s, int) { return (s.mon.medianN - 3) / 2; }
static void msMedN(Settings& s, int, float v) { s.mon.medianN = (uint8_t)(3 + 2 * (int)v); }

static const Field MON_FIELDS[] = {
    {F_NOTE, "A third thermocouple, measured only: its reading is shown at the bottom right of the chart and logged. "
             "No output, no alarm. CS on GPIO 3.", nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr},
    {F_SW, "Converter fitted", "Read, charted and logged while on", 0, 0, 0, nullptr, nullptr, mgOn, msOn, nullptr},
    {F_SEL, "Converter", nullptr, 0, 0, 0, nullptr, "MAX31855\nMAX6675", mgSensor, msSensor, monOn},
    {F_NUM, "Calibration offset", "Calibrated = raw \xC3\x97 gain + offset", -50, 50, 2, "\xC2\xB0" "C", nullptr, mgOff, msOff, monOn},
    {F_NUM, "Calibration gain", nullptr, 0.5f, 2, 4, "", nullptr, mgGain, msGain, monOn},
    {F_NUM, "Read every", "MAX6675: at least 250 ms (its conversion time)", 100, 1000, 0, "ms", nullptr, mgRead, msRead, monOn},
    {F_SW, "Median filter", "Removes single-reading spikes", 0, 0, 0, nullptr, nullptr, mgMed, msMed, monOn},
    {F_SEL, "\xE2\x80\xA6 over the last", nullptr, 0, 0, 0, nullptr, "3 readings\n5 readings\n7 readings\n9 readings", mgMedN, msMedN,
     monMedOn},
};

static const Field* fieldsOf(int& n) {
  if (tab == TAB_BUS) {
    n = sizeof(BUS_FIELDS) / sizeof(BUS_FIELDS[0]);
    return BUS_FIELDS;
  }
  if (tab == TAB_MON) {
    n = sizeof(MON_FIELDS) / sizeof(MON_FIELDS[0]);
    return MON_FIELDS;
  }
  n = sizeof(CH_FIELDS) / sizeof(CH_FIELDS[0]);
  return CH_FIELDS;
}

static void loadE() {
  settingsCopy(E);
  eGen = settingsGen();
}

static void build();

static void changed() {
  eDirty = true;
  build();
}

static void onNum(float v, int i) {
  int n;
  const Field* F = fieldsOf(n);
  F[i].set(E, tab, v);
  changed();
}
static void onSel(int v, int i) {
  int n;
  const Field* F = fieldsOf(n);
  F[i].set(E, tab, v);
  changed();
}
static void onField(lv_event_t* e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  int n;
  const Field& f = fieldsOf(n)[i];
  if (f.k == F_NUM) tNumPad(f.label, f.get(E, tab), f.lo, f.hi, f.dec, f.unit, onNum, i);
  else if (f.k == F_SEL) tChoice(f.label, f.opts, (int)f.get(E, tab), onSel, i);
}
static void onSw(lv_event_t* e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  int n;
  const Field& f = fieldsOf(n)[i];
  f.set(E, tab, lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
  changed();
}
static char* nameOf(int t) { return t == TAB_MON ? E.mon.name : E.ch[t].name; }
static void onName(const char* t, int) {
  if (!*t) return tToast("The channel needs a name.", true);
  strlcpy(nameOf(tab), t, CH_NAME_LEN);
  changed();
}
static void onNameBtn(lv_event_t*) { tKeyboard("Channel name", nameOf(tab), CH_NAME_LEN - 1, false, false, onName, 0); }

// ---- autotune (local channels)
static void onTuneSp(float v, int) { sendCmd(CMD_TUNE_START, tab, v); }
static void onTune(lv_event_t* e) {
  const int a = (int)(intptr_t)lv_event_get_user_data(e);
  LocalTune t;
  localCtrlTuneGet(tab, t);
  if (a == 0) {
    if (eDirty) return tToast("Save the settings first.", true);
    tNumPad("Autotune test temperature", 200, 50, E.ch[tab].loc.maxTemp - 20, 0, "\xC2\xB0" "C", onTuneSp, 0);
  } else if (a == 1) {
    sendCmd(CMD_TUNE_STOP, tab);
  } else if (a == 2 && t.state == TuneState::Done) {  // accept
    {
      Guard g;
      LocalChannelSettings& l = gSettings.ch[tab].loc;
      l.kp = t.res.kp;
      l.ki = t.res.ki;
      l.kd = t.res.kd;
    }
    saveSettingsAndNotify();
    localCtrlTuneClear(tab);
    loadE();
    eDirty = false;
    build();
    tToast("PID values taken over.");
  } else if (a == 3) {
    localCtrlTuneClear(tab);
  }
}

static void tuneUpdate() {
  if (!tuneLbl) return;
  LocalTune t;
  localCtrlTuneGet(tab, t);
  char b[160], el[16];
  fmtTime(t.elapsed, el, sizeof(el));
  switch (t.state) {
    case TuneState::Running:
      snprintf(b, sizeof(b), "Running at %.0f \xC2\xB0" "C (%s), cycle %d, %s. %s", t.sp, t.res.fast ? "faster" : "slow", t.cycle, el, t.msg);
      break;
    case TuneState::Done:
      snprintf(b, sizeof(b), "Done: Kp %.2f, Ki %.4f, Kd %.1f. Take them over?", t.res.kp, t.res.ki, t.res.kd);
      break;
    case TuneState::Failed: snprintf(b, sizeof(b), "Failed: %s", t.msg); break;
    default: strlcpy(b, "Finds PID values: the heater is switched on and off around a test temperature.", sizeof(b)); break;
  }
  tSetText(tuneLbl, b);
  if (tuneSw) tSetEnabled(tuneSw, t.state != TuneState::Running);
  for (int i = 0; i < 4; ++i) {
    lv_obj_t* btn = lv_obj_get_child(tuneRow, i);
    const bool show = i == 0 ? t.state == TuneState::Idle || t.state == TuneState::Failed
                     : i == 1 ? t.state == TuneState::Running
                              : t.state == TuneState::Done || (i == 3 && t.state == TuneState::Failed);
    tSetHidden(btn, !show);
  }
}

// ---- test link (Modbus)
static void onTest(lv_event_t*) {
  BusRequest r{};
  r.op = BUS_READ;
  r.ch = tab;
  r.addr = E.ch[tab].mb.reg.pv;
  r.count = 1;
  char msg[160];
  if (r.addr == REG_UNSET) strlcpy(msg, "No PV register set (web page, Channels).", sizeof(msg));
  else if (busRequest(r, 5000)) snprintf(msg, sizeof(msg), "Answer: register %u = %d", r.addr, r.values[0]);
  else snprintf(msg, sizeof(msg), "No answer: %s", r.err);
  tDialog("Test link", msg, "", "", "", nullptr, 0);
}

// ------------------------------------------------------------ layout
static lv_obj_t* row(const char* label, const char* hint) {
  lv_obj_t* r = lv_obj_create(cBody);  // no tile (Rost): label left, control right
  lv_obj_remove_style_all(r);
  lv_obj_set_scrollable(r, false);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(r, 4, 0);
  lv_obj_set_style_pad_ver(r, 1, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(r, 8, 0);
  lv_obj_t* col = tCol(r, 0);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_t* l = tLabel(col, &tfont_14, label);
  lv_obj_set_width(l, TUI_SMALL ? LV_PCT(100) : 260);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
  if (hint) {
    lv_obj_t* h = tLabel(col, &tfont_12, hint);
    lv_obj_add_style(h, &S_.muted, 0);
    lv_obj_set_width(h, TUI_SMALL ? LV_PCT(100) : 260);
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
  }
  return r;
}

static void selText(const char* opts, int i, char* b, size_t n) {
  const char* p = opts;
  for (int k = 0; k < i && p; ++k) {
    p = strchr(p, '\n');
    if (p) ++p;
  }
  if (!p) {
    b[0] = 0;
    return;
  }
  const char* e = strchr(p, '\n');
  snprintf(b, n, "%.*s", (int)(e ? e - p : (long)strlen(p)), p);
}

static void build() {
  // the RS485 tab only when a channel is a Modbus channel (as edited)
  const bool anyMb = isMb(E, 0) || isMb(E, 1);
  if (cTabs[TAB_BUS]) tSetHidden(cTabs[TAB_BUS], !anyMb);
  if (tab == TAB_BUS && !anyMb) tab = 0;
  if (tab == TAB_MON && !cTabs[TAB_MON]) tab = 0;
  // rebuilt after every change: keep the scroll position (a value entered further down stays in view)
  const int32_t sy = lv_obj_get_scroll_y(cBody);
  tuneLbl = tuneRow = tuneSw = nullptr;
  lv_obj_clean(cBody);
  for (int i = 0; i < 4; ++i)
    if (cTabs[i]) tSetChecked(cTabs[i], i == tab);
  if (cTabs[0]) tButtonText(cTabs[0], E.ch[0].name);
  if (cTabs[1]) tButtonText(cTabs[1], E.ch[1].name);
  if (cTabs[TAB_MON]) tButtonText(cTabs[TAB_MON], E.mon.name);
  tSetEnabled(cSave, eDirty);
  if (eDirty) lv_obj_add_style(cSave, &S_.btnPri, 0);
  else lv_obj_remove_style(cSave, &S_.btnPri, 0);
  if (tab < 2 || tab == TAB_MON) {
    lv_obj_t* r = row("Name", tab == TAB_MON ? "Chart, CSV and logs" : "Shown on all screens");
    tButton(r, nameOf(tab), onNameBtn, nullptr, CW, BH);
  }
  int n;
  const Field* F = fieldsOf(n);
  for (int i = 0; i < n; ++i) {
    const Field& f = F[i];
    if (f.show && !f.show(E, tab)) continue;
    if (f.k == F_HEAD) {
      lv_obj_t* h = tLabel(cBody, &tfont_16b, f.label);
      lv_obj_set_style_pad_top(h, 6, 0);
      lv_obj_add_style(h, &S_.acc, 0);
      if (!strcmp(f.label, "PID control")) {  // autotune under the PID heading
        lv_obj_t* r = row("Autotune", nullptr);
        lv_obj_t* col = lv_obj_get_child(r, 0);
        tuneLbl = lv_obj_get_child(col, 0);
        lv_obj_set_style_text_font(tuneLbl, &tfont_12, 0);
        tuneRow = tCol(r, 4);
        static const char* const B[] = {"Start", "Stop", "Take over", "Discard"};
        for (int k = 0; k < 4; ++k) tButton(tuneRow, B[k], onTune, (void*)(intptr_t)k, TUI_SMALL ? 86 : 100, TUI_SMALL ? 30 : 34);
        tuneUpdate();
      }
      continue;
    }
    if (f.k == F_NOTE) {
      lv_obj_t* nt = tLabel(cBody, &tfont_12, f.label);
      lv_obj_add_style(nt, &S_.muted, 0);
      lv_obj_set_width(nt, LV_PCT(100));
      lv_label_set_long_mode(nt, LV_LABEL_LONG_MODE_WRAP);
      if (tab < 2 && isMb(E, tab) && !strncmp(f.label, "Register", 8)) tButton(cBody, "Test link", onTest, nullptr, 120, BH);
      continue;
    }
    lv_obj_t* r = row(f.label, f.hint);
    char b[64];
    if (f.k == F_SW) {
      lv_obj_t* sw = tSwitch(r, onSw, (void*)(intptr_t)i);
      tSetChecked(sw, f.get(E, tab) != 0);
      if (f.get == gTuneFast) {  // the autotune rule: greyed out while an autotune runs
        tuneSw = sw;
        tuneUpdate();
      }
    } else if (f.k == F_SEL) {
      selText(f.opts, (int)f.get(E, tab), b, sizeof(b));
      lv_obj_t* btn = tButton(r, b, onField, (void*)(intptr_t)i, CW, BH);
      lv_obj_t* l = lv_obj_get_child(btn, 0);
      lv_obj_set_width(l, CW - 10);
      tOneLine(l);
    } else {
      snprintf(b, sizeof(b), "%.*f%s%s", f.dec, f.get(E, tab), f.unit && *f.unit ? " " : "", f.unit ? f.unit : "");
      tButton(r, b, onField, (void*)(intptr_t)i, CW2, BH);
    }
  }
  lv_obj_update_layout(cBody);
  lv_obj_scroll_to_y(cBody, sy, LV_ANIM_OFF);
}

static void onTab(lv_event_t* e) {
  tab = (int)(intptr_t)lv_event_get_user_data(e);
  build();
  lv_obj_scroll_to_y(cBody, 0, LV_ANIM_OFF);
}

static void onSave(lv_event_t*) {
  Settings s;
  settingsCopy(s);
  for (int c = 0; c < NUM_CHANNELS; ++c) {
    const float manual = s.ch[c].manualSp;
    s.ch[c] = E.ch[c];
    s.ch[c].manualSp = manual;  // may have changed meanwhile (main screen, web)
  }
  s.baud = E.baud;
  s.parity = E.parity;
  s.stopBits = E.stopBits;
  s.timeoutMs = E.timeoutMs;
  s.commLossSec = E.commLossSec;
  s.mon = E.mon;
  char msg[160];
  const uint16_t poll0 = s.pollMs;
  if (!settingsValidate(s, boardLimits(), msg, sizeof(msg))) {
    tToast(msg, true);
    return;
  }
  {
    Guard g;
    gSettings = s;
  }
  saveSettingsAndNotify();
  loadE();
  eDirty = false;
  build();
  if (s.pollMs != poll0) {  // (settings.cpp pollMinMs: a MAX6675 or Modbus channel needs at least 250 ms)
    char t[96];
    snprintf(t, sizeof(t), "Settings saved. Polling interval raised to %u ms.", (unsigned)s.pollMs);
    tToast(t);
  } else {
    tToast("Settings saved.");
  }
}

static void onLeave(int b, int) {
  if (b == 3) onSave(nullptr);
  if (b == 2 || (b == 3 && eDirty)) return;
  eDirty = false;
  tShow(SCR_MENU);
}
static void onBack(lv_event_t*) {
  if (eDirty) {
    tDialog("Unsaved changes", "Save the channel settings?", "Discard", "Cancel", "Save", onLeave, 0);
    return;
  }
  tShow(SCR_MENU);
}

lv_obj_t* chanCreate() {
  TPage p = tPageCreate("Channels", onBack);
  cScr = p.scr;
  cBody = p.body;
  lv_obj_set_style_pad_gap(cBody, 2, 0);
  lv_label_set_text(p.title, "");
  lv_obj_set_flex_grow(p.title, 0);
  lv_obj_set_width(p.title, 0);
  const bool bus = boardLimits().rs485, mon = boardLimits().monitor;
  // tabs: bottom, top, (monitor), (RS485) - the monitor next to the channels
  static const int ORDER[] = {0, 1, TAB_MON, TAB_BUS};
  for (int i = 0; i < 4; ++i) cTabs[i] = nullptr;
  for (int i : ORDER) {
    if ((i == TAB_BUS && !bus) || (i == TAB_MON && !mon)) continue;
    cTabs[i] = tButton(p.header, i == TAB_BUS ? (TUI_SMALL || HAS_MONITOR ? "RS485" : "RS485 (beta)") : "", onTab, (void*)(intptr_t)i,
                       TAB_W, T_HDR - 8);
    lv_obj_t* l = lv_obj_get_child(cTabs[i], 0);
    lv_obj_set_width(l, TAB_W - 8);
    tOneLine(l);
  }
  lv_obj_t* sp = lv_obj_create(p.header);
  lv_obj_remove_style_all(sp);
  lv_obj_set_flex_grow(sp, 1);
  cSave = tButton(p.header, "Save", onSave, nullptr, TUI_SMALL ? 52 : 64, T_HDR - 8);
  return p.scr;
}

void chanShow() {
  loadE();
  eDirty = false;
  build();
  lv_obj_scroll_to_y(cBody, 0, LV_ANIM_OFF);  // (opened: from the top)
}

void chanUpdate() {
  static uint32_t at = 0;
  if (at && lv_tick_elaps(at) < 1000) return;
  at = tTickNow();
  // saved elsewhere (web, serial) while nothing is being edited here: show it
  if (!eDirty && !tModalOpen() && settingsGen() != eGen) {
    loadE();
    build();
  }
  if (tab < 2 && isLoc(E, tab)) tuneUpdate();
}
