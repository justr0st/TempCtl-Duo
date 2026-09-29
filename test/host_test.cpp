// Host-side tests. Build & run on a PC (ArduinoJson 7 headers, e.g. from .pio/libdeps/<env>/ArduinoJson/src):
//   g++ -std=c++17 -I../src -I<ArduinoJson>/src host_test.cpp ../src/program.cpp ../src/settings.cpp 
//       ../src/tc_sensor.cpp ../src/pid.cpp ../src/runaway.cpp ../src/autotune.cpp -o host_test
//   ./host_test
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "modbus_frame.h"
#include "program.h"
#include "autotune.h"
#include "pid.h"
#include "runaway.h"
#include "settings.h"
#include "tc_sensor.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static bool near(float a, float b, float e = 0.01f) { return fabsf(a - b) <= e; }

static void testCrc() {
  // Classic reference frame: 01 03 00 00 00 02 -> CRC C4 0B
  uint8_t f[8];
  mbBuildRead(f, 1, 3, 0, 2);
  CHECK(f[6] == 0xC4 && f[7] == 0x0B);
  CHECK(mbCheckCrc(f, 8));
  // PMA manual example: read addr 650 (0x028A), 2 words from slave 17
  mbBuildRead(f, 0x11, 3, 650, 2);
  CHECK(f[2] == 0x02 && f[3] == 0x8A && f[5] == 0x02);
}

static void testParse() {
  // Response: 11 03 04 00 DE 01 4D + CRC  -> 222, 333 (from PMA manual)
  uint8_t r[9] = {0x11, 0x03, 0x04, 0x00, 0xDE, 0x01, 0x4D};
  mbAppendCrc(r, 7);
  int16_t v[2];
  CHECK(mbParse(r, 9, 0x11, 3, 2, v, nullptr) == MbResult::Ok);
  CHECK(v[0] == 222 && v[1] == 333);
  r[4] ^= 1;
  CHECK(mbParse(r, 9, 0x11, 3, 2, v, nullptr) == MbResult::CrcError);
  // Exception: 11 90 02
  uint8_t e[5] = {0x11, 0x90, 0x02};
  mbAppendCrc(e, 3);
  uint8_t ex = 0;
  CHECK(mbParse(e, 5, 0x11, 16, 1, nullptr, &ex) == MbResult::Exception && ex == 2);
  // Negative value -31000 (sensor fault)
  uint8_t n[7] = {1, 3, 2, 0x86, 0xE8};
  mbAppendCrc(n, 5);
  CHECK(mbParse(n, 7, 1, 3, 1, v, nullptr) == MbResult::Ok && v[0] == -31000);
  // FC6 echo
  uint8_t w[8];
  mbBuildWrite6(w, 0x11, 650, 123);
  CHECK(w[4] == 0x00 && w[5] == 0x7B);
  CHECK(mbParse(w, 8, 0x11, 6, 1, nullptr, nullptr) == MbResult::Ok);
  uint8_t w16[11];
  mbBuildWrite16(w16, 1, 100, -5);
  CHECK(w16[1] == 16 && w16[6] == 2 && w16[7] == 0xFF && w16[8] == 0xFB && mbCheckCrc(w16, 11));
}

static Step st(float t, uint32_t r, uint32_t h) { return Step{t, r, h}; }

static Program demo() {
  Program p;
  programClear(p, "demo");
  programInsertStep(p, 0, st(100, 600, 300));   // ramp 10 min to 100, hold 5
  programInsertStep(p, 1, st(200, 0, 600));     // jump to 200, hold 10
  programInsertStep(p, 2, st(50, 900, 0));      // ramp down 15 min, no hold
  return p;
}

static void testEdit() {
  Program p;
  programClear(p, "edit");
  CHECK(p.count == 0 && strcmp(p.name, "edit") == 0);
  CHECK(!programInsertStep(p, 1, st(1, 0, 0)));  // beyond end
  for (int i = 0; i < MAX_STEPS; ++i) CHECK(programInsertStep(p, p.count, st((float)i, 0, 0)));
  CHECK(p.count == MAX_STEPS);
  CHECK(!programInsertStep(p, 0, st(-1, 0, 0)));  // full
  CHECK(programRemoveStep(p, 0) && p.count == MAX_STEPS - 1 && p.s[0].target == 1);
  CHECK(!programRemoveStep(p, p.count));
  Program q;
  programClear(q, "q");
  for (int i = 0; i < 4; ++i) programInsertStep(q, q.count, st((float)i, 0, 0));  // 0 1 2 3
  CHECK(programMoveStep(q, 0, 3));   // 1 2 3 0
  CHECK(q.s[0].target == 1 && q.s[3].target == 0);
  CHECK(programMoveStep(q, 3, 1));   // 1 0 2 3
  CHECK(q.s[0].target == 1 && q.s[1].target == 0 && q.s[2].target == 2 && q.s[3].target == 3);
  CHECK(!programMoveStep(q, 0, 4));
  CHECK(programInsertStep(q, 1, st(9, 0, 0)) && q.s[1].target == 9 && q.s[2].target == 0 && q.count == 5);
  q.s[0].target = 5000;
  q.s[1].rampSec = 0xFFFFFFFF;
  programSanitize(q, 0, 900);
  CHECK(q.s[0].target == 900 && q.s[1].rampSec == MAX_STEP_SEC);
}

static void testPlan() {
  Program p = demo();
  CHECK(near(ProgramEngine::plannedTotal(p), 600 + 300 + 0 + 600 + 900));
  CHECK(near(ProgramEngine::plannedAt(p, 20, 0), 20));
  CHECK(near(ProgramEngine::plannedAt(p, 20, 300), 60));      // halfway ramp 20->100
  CHECK(near(ProgramEngine::plannedAt(p, 20, 700), 100));     // hold
  CHECK(near(ProgramEngine::plannedAt(p, 20, 900), 200));     // step 2 hold
  CHECK(near(ProgramEngine::plannedAt(p, 20, 1500 + 450), 125));  // halfway 200->50
  CHECK(near(ProgramEngine::plannedAt(p, 20, 99999), 50));
  // seconds resolution
  Program s;
  programClear(s, "sec");
  programInsertStep(s, 0, st(30, 10, 5));
  CHECK(near(ProgramEngine::plannedTotal(s), 15));
  CHECK(near(ProgramEngine::plannedAt(s, 20, 5), 25));
}

static void testRun() {
  Program p = demo();
  ProgramEngine e;
  CHECK(e.start(p, 20));
  CHECK(e.state() == RunState::Running && e.step() == 0 && e.phase() == Phase::Ramp);
  CHECK(near(e.setpoint(), 20));
  for (int i = 0; i < 300; ++i) e.tick(1, 0, true, false, 5);
  CHECK(near(e.setpoint(), 60));
  e.pause();
  uint32_t g = e.generation();
  for (int i = 0; i < 100; ++i) e.tick(1, 0, true, false, 5);
  CHECK(near(e.setpoint(), 60));  // frozen
  e.resume();
  CHECK(e.generation() != g);
  for (int i = 0; i < 300; ++i) e.tick(1, 0, true, false, 5);
  CHECK(e.phase() == Phase::Hold && e.step() == 0 && near(e.setpoint(), 100));
  // guaranteed soak: PV far away -> hold time doesn't advance
  for (int i = 0; i < 1000; ++i) e.tick(1, 40, true, true, 5);
  CHECK(e.phase() == Phase::Hold && e.step() == 0 && e.soakWaiting());
  for (int i = 0; i < 300; ++i) e.tick(1, 98, true, true, 5);
  // step 1 has zero ramp -> directly hold at 200
  CHECK(e.step() == 1 && e.phase() == Phase::Hold && near(e.setpoint(), 200));
  CHECK(near(e.elapsed(), 900, 1.5f));
  e.skip();  // skip the hold -> step 2 ramp
  CHECK(e.step() == 2 && e.phase() == Phase::Ramp && near(e.setpoint(), 200));
  for (int i = 0; i < 450; ++i) e.tick(1, 0, true, false, 5);
  CHECK(near(e.setpoint(), 125, 0.5f));
  for (int i = 0; i < 460; ++i) e.tick(1, 0, true, false, 5);
  CHECK(e.state() == RunState::Finished && near(e.setpoint(), 50));
  CHECK(near(e.elapsed(), e.total()));

  // abort keeps the last setpoint
  ProgramEngine a;
  a.start(p, 20);
  for (int i = 0; i < 150; ++i) a.tick(1, 0, true, false, 5);
  a.stop();
  CHECK(a.state() == RunState::Aborted && near(a.setpoint(), 40));

  // empty program refuses to start
  Program none;
  programClear(none, "");
  ProgramEngine z;
  CHECK(!z.start(none, 20) && z.state() == RunState::Idle);

  // a 100-step program with second-resolution steps runs to the end
  Program big;
  programClear(big, "big");
  for (int i = 0; i < MAX_STEPS; ++i) programInsertStep(big, big.count, st((float)(i % 2 ? 50 : 60), 3, 2));
  ProgramEngine b;
  CHECK(b.start(big, 20) && near(b.total(), MAX_STEPS * 5));
  for (int i = 0; i < MAX_STEPS * 5 * 2; ++i) b.tick(0.5f, 0, true, false, 5);
  CHECK(b.state() == RunState::Finished && near(b.setpoint(), 50));
}


// ------------------------------------------------------------------ settings (JSON, validation)

static const BoardLimits S3{true, 2}, CYD_1L485{true, 1}, CYD_2L{false, 2};

static void roundTrip(const Settings& in, Settings& out, bool forApi) {
  JsonDocument d;
  settingsToJson(in, d.to<JsonObject>(), forApi);
  std::string txt;
  serializeJson(d, txt);
  JsonDocument e;
  CHECK(deserializeJson(e, txt) == DeserializationError::Ok);
  char msg[160];
  settingsDefaults(out, S3);
  CHECK(settingsFromJson(e.as<JsonObjectConst>(), out, msg, sizeof(msg)));
}

static void testSettingsDefaults() {
  Settings s;
  char msg[200];
  settingsDefaults(s, S3);
  CHECK(s.ch[0].type == CH_MODBUS && s.ch[1].type == CH_OFF);
  CHECK(s.ch[0].mb.slaveId == 1 && s.ch[1].mb.slaveId == 2);
  CHECK(!strcmp(s.ch[0].name, "Bottom") && !strcmp(s.ch[1].name, "Top"));
  CHECK(s.ch[0].mb.runaway.heatSec == 300 && s.ch[1].mb.runaway.heatSec == 120);
  CHECK(s.ch[0].mb.reg.pv == 13294 && s.ch[0].mb.reg.sp == 11372);
  CHECK(!strcmp(s.hostname, "tempctl-duo"));
  CHECK(settingsValidate(s, S3, msg, sizeof(msg)) && msg[0] == 0);
  settingsDefaults(s, CYD_2L);
  CHECK(s.ch[0].type == CH_LOCAL && s.ch[1].type == CH_LOCAL);
  CHECK(settingsValidate(s, CYD_2L, msg, sizeof(msg)));
  settingsDefaults(s, CYD_1L485);
  CHECK(s.ch[0].type == CH_MODBUS && s.ch[1].type == CH_OFF);
}

static void testSettingsJson() {
  Settings a, b;
  settingsDefaults(a, S3);
  strcpy(a.ssid, "Workshop");
  strcpy(a.pass, "secret-pw");
  a.ch[1].type = CH_LOCAL;
  strcpy(a.ch[1].name, "IR head");
  a.ch[1].loc.sensor = SENSOR_MAX6675;
  a.ch[1].loc.kp = 7.5f;
  a.ch[0].mb.reg.alarm = REG_UNSET;
  a.accentColor = 0xD65D0E;
  a.themeDark = 0;
  roundTrip(a, b, false);
  CHECK(!strcmp(b.ssid, "Workshop") && !strcmp(b.pass, "secret-pw"));
  CHECK(b.ch[1].type == CH_LOCAL && !strcmp(b.ch[1].name, "IR head"));
  CHECK(b.ch[1].loc.sensor == SENSOR_MAX6675 && near(b.ch[1].loc.kp, 7.5f));
  CHECK(b.ch[0].mb.reg.alarm == REG_UNSET && b.ch[0].mb.reg.pv == 13294);
  CHECK(b.accentColor == 0xD65D0E && b.themeDark == 0);
  CHECK(memcmp(&a.ch[0], &b.ch[0], sizeof(ChannelSettings)) == 0);
  // API form: no passwords
  JsonDocument d;
  settingsToJson(a, d.to<JsonObject>(), true);
  CHECK(d["wifi"]["pass"].isNull() && d["wifi"]["passSet"] == true);
  roundTrip(a, b, true);
  CHECK(b.pass[0] == 0);  // defaults kept (not overwritten)
}

static void testSettingsMerge() {
  Settings s;
  char msg[200];
  settingsDefaults(s, S3);
  // partial update: only the top channel's type and address, unknown keys ignored
  JsonDocument d;
  CHECK(deserializeJson(d, R"({"channels":[null,{"type":"modbus","modbus":{"addr":5}}],"foo":1,"bus":{"baud":19200}})") ==
        DeserializationError::Ok);
  CHECK(settingsFromJson(d.as<JsonObjectConst>(), s, msg, sizeof(msg)));
  CHECK(s.ch[1].type == CH_MODBUS && s.ch[1].mb.slaveId == 5 && s.ch[0].type == CH_MODBUS);
  CHECK(s.baud == 19200 && s.parity == 1);
  CHECK(settingsValidate(s, S3, msg, sizeof(msg)));
  // same address on both Modbus channels
  s.ch[1].mb.slaveId = 1;
  CHECK(!settingsValidate(s, S3, msg, sizeof(msg)) && strstr(msg, "same Modbus address"));
  // wrong types are rejected with the path
  JsonDocument e;
  deserializeJson(e, R"({"channels":[{"spMax":"hot"}]})");
  CHECK(!settingsFromJson(e.as<JsonObjectConst>(), s, msg, sizeof(msg)) && strstr(msg, "channels[0].spMax"));
  deserializeJson(e, R"({"channels":[{"type":"gas"}]})");
  CHECK(!settingsFromJson(e.as<JsonObjectConst>(), s, msg, sizeof(msg)) && strstr(msg, "type"));
  deserializeJson(e, R"({"display":{"accent":"blue"}})");
  CHECK(!settingsFromJson(e.as<JsonObjectConst>(), s, msg, sizeof(msg)) && strstr(msg, "accent"));
  // a new profile loads its register map
  deserializeJson(e, R"({"channels":[{"modbus":{"profile":"generic"}}]})");
  CHECK(settingsFromJson(e.as<JsonObjectConst>(), s, msg, sizeof(msg)));
  CHECK(s.ch[0].mb.profile == PROFILE_GENERIC && s.ch[0].mb.reg.pv == REG_UNSET);
}

static void testSettingsVariants() {
  Settings s;
  char msg[200];
  // CYD 2x local: no RS485 -> Modbus channels are switched off
  settingsDefaults(s, CYD_2L);
  s.ch[0].type = CH_MODBUS;
  CHECK(!settingsValidate(s, CYD_2L, msg, sizeof(msg)) && strstr(msg, "no RS485"));
  CHECK(s.ch[0].type == CH_OFF && s.ch[1].type == CH_LOCAL);
  // CYD 1x local + RS485: only one local channel
  settingsDefaults(s, CYD_1L485);
  s.ch[0].type = CH_LOCAL;
  s.ch[1].type = CH_LOCAL;
  CHECK(!settingsValidate(s, CYD_1L485, msg, sizeof(msg)) && strstr(msg, "only 1 local"));
  CHECK(s.ch[0].type == CH_LOCAL && s.ch[1].type == CH_OFF);
  // local: spMax stays 10 degC below the cut-off
  settingsDefaults(s, S3);
  s.ch[1].type = CH_LOCAL;
  s.ch[1].loc.maxTemp = 300;
  s.ch[1].spMax = 350;
  CHECK(settingsValidate(s, S3, msg, sizeof(msg)));
  CHECK(near(s.ch[1].spMax, 290));
  // ranges
  s.pollMs = 5;
  s.baud = 1234;
  s.hostname[0] = 0;
  s.ch[0].name[0] = 0;
  settingsValidate(s, S3, msg, sizeof(msg));
  CHECK(s.pollMs == 250 && s.baud == 9600 && !strcmp(s.hostname, "tempctl-duo") && !strcmp(s.ch[0].name, "Bottom"));
}

// ------------------------------------------------------------------ thermocouple frames

static void testThermocouple() {
  // MAX31855: TC +25.00 degC (100 << 18), internal +25.0 degC (400 << 4)
  TcReading r = tcDecode31855(0x01901900u);
  CHECK(r.fault == TC_OK && near(r.tempC, 25.0f) && near(r.coldC, 25.0f));
  // TC -250 degC (14-bit two's complement 0x3C18), internal -0.0625 degC (0xFFF)
  r = tcDecode31855((0x3C18u << 18) | (0xFFFu << 4));
  CHECK(r.fault == TC_OK && near(r.tempC, -250.0f) && near(r.coldC, -0.0625f));
  // TC +1000.75 degC
  r = tcDecode31855((4003u << 18) | (400u << 4));
  CHECK(r.fault == TC_OK && near(r.tempC, 1000.75f));
  // faults: fault bit D16 + OC / SCG / SCV
  CHECK(tcDecode31855(0x00011901u).fault == TC_OPEN);
  CHECK(tcDecode31855(0x00011902u).fault == TC_SHORT_GND);
  CHECK(tcDecode31855(0x00011904u).fault == TC_SHORT_VCC);
  CHECK(near(tcDecode31855(0x00011901u).coldC, 25.0f));
  // nothing on the bus / reserved bits set
  CHECK(tcDecode31855(0xFFFFFFFFu).fault == TC_NO_DEVICE);
  CHECK(tcDecode31855(0).fault == TC_NO_DEVICE);
  CHECK(tcDecode31855(0x01901908u).fault == TC_NO_DEVICE);  // D3 set
  CHECK(tcDecode31855(0x01921900u).fault == TC_NO_DEVICE);  // D17 set
  // MAX6675: 100.25 degC = 401 << 3
  TcReading m = tcDecode6675(401 << 3);
  CHECK(m.fault == TC_OK && near(m.tempC, 100.25f) && std::isnan(m.coldC));
  CHECK(tcDecode6675((401 << 3) | 4).fault == TC_OPEN);
  CHECK(tcDecode6675(0xFFFF).fault == TC_NO_DEVICE);
  CHECK(tcDecode6675(0).fault == TC_NO_DEVICE);
  CHECK(tcDecode6675((401 << 3) | 2).fault == TC_NO_DEVICE);  // device id bit must be 0
}

// ------------------------------------------------------------------ PID

// First-order heater with dead time: dT/dt = (gain * out% - (T - ambient)) / tau
struct Heater {
  float T = 25, amb = 25, gain = 3.0f, tau = 120, dead = 8;
  float hist[400] = {};
  int n = 0;
  float step(float out, float dt) {
    const int d = (int)(dead / dt);
    for (int i = 399; i > 0; --i) hist[i] = hist[i - 1];
    hist[0] = out;
    const float u = n >= d ? hist[d] : 0;
    ++n;
    T += (gain * u - (T - amb)) / tau * dt;
    return T;
  }
};

static void testPid() {
  Pid pid;
  PidParams p{4, 0, 0, 100};
  CHECK(near(pid.update(p, 100, 90, 0.25f), 40));   // P only
  CHECK(near(pid.update(p, 100, 50, 0.25f), 100));  // clamped
  pid.reset();
  CHECK(near(pid.update(p, 100, 120, 0.25f), 0));   // never negative
  // output limit
  PidParams lim{4, 0, 0, 60};
  pid.reset();
  CHECK(near(pid.update(lim, 100, 50, 0.25f), 60));
  // no derivative kick on a setpoint jump (derivative on the measurement)
  PidParams pd{0, 0, 50, 100};
  pid.reset();
  pid.update(pd, 100, 100, 0.25f);
  CHECK(near(pid.update(pd, 200, 100, 0.25f), 0));
  // anti-windup: 10 min saturated far below the setpoint, then the setpoint drops below PV
  PidParams pi{2, 0.05f, 0, 100};
  pid.reset();
  for (int i = 0; i < 2400; ++i) pid.update(pi, 300, 25, 0.25f);
  CHECK(pid.integral() <= 100.0f);
  float o = 100;
  for (int i = 0; i < 8; ++i) o = pid.update(pi, 20, 25, 0.25f);  // error -5
  CHECK(o < 100);  // leaves saturation at once (integral not wound far above the limit)
  // closed loop: reaches 200 degC from 25 degC, small overshoot, settles
  Heater h;
  PidParams cl{3.0f, 0.03f, 20, 100};
  pid.reset();
  float T = h.T, peak = 0;
  for (int i = 0; i < 4 * 1800; ++i) {  // 30 min at 250 ms, readings in 0.25 degC steps like the converters
    const float out = pid.update(cl, 200, roundf(T * 4) / 4, 0.25f);
    T = h.step(out, 0.25f);
    if (T > peak) peak = T;
  }
  CHECK(fabsf(T - 200) < 1.0f);
  CHECK(peak < 215);
}

// ------------------------------------------------------------------ runaway

static void testRunaway() {
  Runaway c{1, 80, 120, 5, 15, 60};
  RunawayMonitor m;
  // heating failed: 100 % for 120 s, PV rises only 2 degC
  uint8_t f = FAULT_NONE;
  float pv = 25;
  for (int i = 0; i < 125 && !f; ++i) f = m.update(c, 1, true, 200, pv += 2.0f / 120, 100);
  CHECK(f == FAULT_HEATING);
  // heats fine: 0.5 degC/s at 100 % -> no fault for 10 min
  m.reset();
  pv = 25;
  f = FAULT_NONE;
  for (int i = 0; i < 600 && !f; ++i) f = m.update(c, 1, true, 400, pv += 0.5f, 100);
  CHECK(f == FAULT_NONE);
  // output below minOut: no heating check
  m.reset();
  f = FAULT_NONE;
  for (int i = 0; i < 600 && !f; ++i) f = m.update(c, 1, true, 30, 25, 50);
  CHECK(f == FAULT_NONE);
  // unknown output (NAN): no heating check
  m.reset();
  f = FAULT_NONE;
  for (int i = 0; i < 600 && !f; ++i) f = m.update(c, 1, true, 30, 25, NAN);
  CHECK(f == FAULT_NONE);
  // holding: reached 200, then 20 degC below for 61 s -> fault; 50 s -> recovers
  m.reset();
  f = m.update(c, 1, true, 200, 199.5f, 30);
  for (int i = 0; i < 50; ++i) f |= m.update(c, 1, true, 200, 180, 30);
  CHECK(f == FAULT_NONE);
  f |= m.update(c, 1, true, 200, 200, 30);  // back in the band: timer restarts
  for (int i = 0; i < 62 && !f; ++i) f = m.update(c, 1, true, 200, 180, 30);
  CHECK(f == FAULT_HOLD);
  // a moving setpoint (ramp) is not "holding"
  m.reset();
  f = FAULT_NONE;
  float sp = 100;
  for (int i = 0; i < 300 && !f; ++i) f = m.update(c, 1, true, sp += 1.0f, 100, 30);
  CHECK(f == FAULT_NONE);
  // not reached yet: far below a new setpoint is no hold fault
  m.reset();
  f = FAULT_NONE;
  for (int i = 0; i < 300 && !f; ++i) f = m.update(c, 1, true, 250, 100, 30);
  CHECK(f == FAULT_NONE);
  // outputs off / disabled: never a fault
  m.reset();
  f = FAULT_NONE;
  for (int i = 0; i < 300 && !f; ++i) f = m.update(c, 1, false, 200, 25, 100);
  Runaway off = c;
  off.enable = 0;
  for (int i = 0; i < 300 && !f; ++i) f = m.update(off, 1, true, 200, 25, 100);
  CHECK(f == FAULT_NONE);
}

// ------------------------------------------------------------------ autotune

static bool runTune(Heater& h, RelayAutotune& at, const TuneConfig& c) {
  at.start(c);
  float T = h.T;
  for (int i = 0; i < 4 * 7200 && at.state() == TuneState::Running; ++i) T = h.step(at.update(roundf(T * 4) / 4, 0.25f), 0.25f);
  return at.state() == TuneState::Done;
}

static void testAutotune() {
  // heater gain 3 degC/%, tau 120 s, dead time 8 s; test at 200 degC
  Heater h;
  RelayAutotune at;
  TuneConfig c;
  c.sp = 200;
  c.outMax = 100;
  CHECK(runTune(h, at, c));
  const TuneResult& r = at.result();
  CHECK(r.pu > 25 && r.pu < 45);
  CHECK(r.kp > 1 && r.kp < 6 && r.ki > 0 && r.kd > 0);
  CHECK(near(r.ki, r.kp / (2.2f * r.pu), 1e-4f) && near(r.kd, r.kp * r.pu / 6.3f, 1e-3f));
  // the proposed PID: from 25 degC to 200 degC without real overshoot
  Heater h2;
  Pid pid;
  PidParams p{r.kp, r.ki, r.kd, 100};
  float T = h2.T, peak = 0;
  for (int i = 0; i < 4 * 1800; ++i) {
    T = h2.step(pid.update(p, 200, roundf(T * 4) / 4, 0.25f), 0.25f);
    if (T > peak) peak = T;
  }
  CHECK(fabsf(T - 200) < 1.0f && peak < 203);
  // low setpoint (the heater needs ~25 %): the relay re-centres, still converges
  Heater h3;
  c.sp = 100;
  CHECK(runTune(h3, at, c));
  // output limit is respected
  Heater h4;
  c.sp = 150;
  c.outMax = 70;
  at.start(c);
  T = h4.T;
  bool within = true;
  for (int i = 0; i < 4 * 3600 && at.state() == TuneState::Running; ++i) {
    const float o = at.update(roundf(T * 4) / 4, 0.25f);
    within &= o >= 0 && o <= 70.001f;
    T = h4.step(o, 0.25f);
  }
  CHECK(within && at.state() == TuneState::Done);
  // too weak to reach the setpoint (max 25 + 1 * 100 = 125 degC): time limit
  Heater weak;
  weak.gain = 1;
  c.sp = 200;
  c.outMax = 100;
  c.maxSec = 900;
  CHECK(!runTune(weak, at, c) && at.state() == TuneState::Failed && strstr(at.message(), "time"));
  // user stop, missing reading
  at.start(c);
  at.update(25, 0.25f);
  at.stop();
  CHECK(at.state() == TuneState::Failed && at.update(25, 0.25f) == 0);
  at.start(c);
  at.update(NAN, 0.25f);
  CHECK(at.state() == TuneState::Failed);
}

int main() {
  testCrc();
  testParse();
  testEdit();
  testPlan();
  testRun();
  testSettingsDefaults();
  testSettingsJson();
  testSettingsMerge();
  testSettingsVariants();
  testThermocouple();
  testPid();
  testRunaway();
  testAutotune();
  printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
  return fails ? 1 : 0;
}
