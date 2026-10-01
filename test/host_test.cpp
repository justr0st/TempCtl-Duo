// Host-side tests. Build & run on a PC (ArduinoJson 7 headers, e.g. from .pio/libdeps/<env>/ArduinoJson/src):
//   g++ -std=c++17 -I../src -I<ArduinoJson>/src host_test.cpp ../src/program.cpp ../src/duo_program.cpp
//       ../src/run_engine.cpp ../src/settings.cpp ../src/tc_sensor.cpp ../src/pid.cpp ../src/runaway.cpp
//       ../src/autotune.cpp -o host_test
//   ./host_test
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "modbus_frame.h"
#include "program.h"
#include "duo_program.h"
#include "run_engine.h"
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

// ------------------------------------------------------------------ programs (JSON, limits)

static std::string progJson(const DuoProgram& p) {
  JsonDocument d;
  progToJson(p, d.to<JsonObject>());
  std::string s;
  serializeJson(d, s);
  return s;
}

static bool progParse(const char* json, DuoProgram& p, char* err, size_t n) {
  JsonDocument d;
  if (deserializeJson(d, json) != DeserializationError::Ok) return false;
  return progFromJson(d.as<JsonObjectConst>(), p, err, n);
}

static DuoProgram P1, P2;  // static: large

static const char* pauseTextOf(const DuoProgram& p, int phase) { return progText(p, p.ph[phase].text); }

static void testProgJson() {
  char err[120];
  for (int i = 0; i < PROG_EXAMPLES; ++i) {
    progExample(i, P1);
    const std::string j = progJson(P1);
    CHECK(progParse(j.c_str(), P2, err, sizeof(err)));
    CHECK(progJson(P2) == j);
  }
  progExample(0, P1);
  CHECK(P1.type == PROG_SEQ && P1.nPh == 7 && P1.ph[1].kind == PH_P && P1.ph[1].msg && P1.ph[6].bIdle);
  CHECK(progUses(P1, CH_BOTTOM) && progUses(P1, CH_TOP));
  // the sketch from spec 9.3.5
  CHECK(progParse(R"({"fmt":1,"name":" Lead-free BGA ","type":"seq","phases":[
    {"kind":"B","wait":true,"b":{"t":150,"r":60,"h":60}},
    {"kind":"P","msg":true,"text":"Place the IR head"},
    {"kind":"BT","wait":true,"b":{"t":160},"t":{"t":150,"r":60,"h":20}},
    {"kind":"BT","b":{"idle":true},"t":{"t":150,"r":40,"h":0}}],"extra":1})", P1, err, sizeof(err)));
  CHECK(!strcmp(P1.name, "Lead-free BGA") && P1.nPh == 4 && !strcmp(pauseTextOf(P1, 1), "Place the IR head"));
  CHECK(P1.ph[2].kind == PH_BT && P1.ph[2].wait && near(P1.ph[2].b.t, 160) && P1.ph[3].bIdle && !P1.ph[3].wait);
  CHECK(progParse(R"({"type":"tl","name":"x","B":[{"t":150,"r":60,"h":150},{"idle":true,"h":60}],
    "T":[{"pause":true,"msg":true,"text":"Check flux"},{"t":200}]})", P1, err, sizeof(err)));
  CHECK(P1.type == PROG_TL && P1.nSt[0] == 2 && P1.nSt[1] == 2 && P1.st[0][1].kind == ST_IDLE && P1.st[0][1].s.h == 60);
  CHECK(P1.st[1][0].kind == ST_PAUSE && P1.st[1][1].s.r == 0 && progItems(P1) == 4);
  // errors
  CHECK(!progParse(R"({"type":"seq","phases":[{"kind":"X"}]})", P1, err, sizeof(err)) && strstr(err, "kind"));
  CHECK(!progParse(R"({"type":"seq","phases":[{"kind":"B","b":{"r":5}}]})", P1, err, sizeof(err)) &&
        strstr(err, "Phase 1: Bottom target missing"));
  CHECK(!progParse(R"({"type":"seq","phases":[{"kind":"T","t":{"t":100,"r":-5}}]})", P1, err, sizeof(err)) &&
        strstr(err, "ramp"));
  CHECK(!progParse(R"({"name":"x"})", P1, err, sizeof(err)) && strstr(err, "type"));
  CHECK(!progParse(R"({"fmt":2,"type":"seq"})", P1, err, sizeof(err)) && strstr(err, "newer"));
  CHECK(!progParse(R"({"type":"seq","name":"0123456789012345678901234567890123"})", P1, err, sizeof(err)) &&
        strstr(err, "31"));
  CHECK(progParse(R"({"type":"seq","name":"0123456789012345678901234567890"})", P1, err, sizeof(err)));
  CHECK(!progParse(R"({"type":"tl","T":[{"pause":true,"text":"01234567890123456789012345678901234567890"}]})", P1, err,
                   sizeof(err)) && strstr(err, "40"));
  std::string many = R"({"type":"seq","phases":[)";
  for (int i = 0; i < 101; ++i) many += std::string(i ? "," : "") + R"({"kind":"T","t":{"t":100}})";
  CHECK(!progParse((many + "]}").c_str(), P1, err, sizeof(err)) && strstr(err, "Too many"));
  std::string texts = R"({"type":"seq","phases":[)";
  for (int i = 0; i < 17; ++i) texts += std::string(i ? "," : "") + R"({"kind":"P","text":"t)" + std::to_string(i) + "\"}";
  CHECK(!progParse((texts + "]}").c_str(), P1, err, sizeof(err)) && strstr(err, "texts"));
  // limits
  progExample(0, P1);
  const float lo[2] = {0, 0}, hi[2] = {400, 240};
  CHECK(!progCheckLimits(P1, lo, hi, err, sizeof(err)) && strstr(err, "Phase 6: Top target 245.0"));
  const float hi2[2] = {400, 400};
  CHECK(progCheckLimits(P1, lo, hi2, err, sizeof(err)));
}

// ------------------------------------------------------------------ run engine

// value of each channel's plan at time t
struct Probe {
  float t;
  float v[NUM_CHANNELS];
  bool idle[NUM_CHANNELS];
};
static void probeFn(const PlanSeg& s, void* u) {
  Probe& p = *(Probe*)u;
  if (p.t < s.t0 || p.t >= s.t1) return;
  p.idle[s.ch] = s.idle;
  p.v[s.ch] = s.idle ? NAN : s.v0 + (s.v1 - s.v0) * (p.t - s.t0) / (s.t1 - s.t0);
}
static Probe planAt(const DuoProgram& p, float t, float pv = 25) {
  Probe r{t, {NAN, NAN}, {false, false}};
  const float v[2] = {pv, pv};
  programPreview(p, v, probeFn, &r);
  return r;
}

// the heaters follow their setpoints at once (idle: the temperature stays)
struct Sim {
  RunEngine e;
  float pv[NUM_CHANNELS] = {25, 25};
  bool ok[NUM_CHANNELS] = {true, true};
  float t = 0;
  bool autoCont = false;
  bool start(DuoProgram& p) {
    t = 0;
    return e.start(&p, pv, ok, 5);
  }
  void step(float dt) {
    if (autoCont && e.progPaused()) e.cont();
    e.tick(dt, pv, ok);
    t += dt;
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      const ChOut o = e.out(ch);
      if (!o.idle) pv[ch] = o.sp;
    }
  }
  void runTo(float until, float dt = 1) {
    while (t < until - 1e-4f && e.active()) step(dt);
  }
  float sp(int ch) const { return e.out(ch).sp; }
};

static float remainingOf(const RunEngine& e) { return e.forecast(nullptr, nullptr, nullptr); }

static void testPreview() {
  const float tot[PROG_EXAMPLES] = {420, 375, 450};
  for (int i = 0; i < PROG_EXAMPLES; ++i) {
    progExample(i, P1);
    CHECK(near(programPreview(P1, nullptr, nullptr, nullptr), tot[i]));
  }
  progExample(0, P1);
  Probe a = planAt(P1, 30);
  CHECK(near(a.v[0], 87.5f) && a.idle[1]);
  a = planAt(P1, 150);  // Both phase: bottom jumped to 160, top ramps from 25 to 150 in 60 s
  CHECK(near(a.v[0], 160) && near(a.v[1], 87.5f));
  a = planAt(P1, 400);  // last phase: bottom Idle, top ramps 245 -> 150 in 40 s from 380
  CHECK(a.idle[0] && near(a.v[1], 245 - 95 * 0.5f));
  progExample(2, P1);
  a = planAt(P1, 130);  // Timeline: top idle for 120 s, then 25 -> 150 in 40 s
  CHECK(near(a.v[0], 150) && near(a.v[1], 25 + 125 * 0.25f));
  a = planAt(P1, 420);  // top done at 390, keeps 150; bottom idle 390..450
  CHECK(a.idle[0] && near(a.v[1], 150));
  // legacy view (old UIs): bottom plan as TempCtl steps
  progExample(0, P1);
  static Program L;
  programLegacyView(P1, L);
  CHECK(L.count == 2 && near(L.s[0].target, 150) && L.s[0].rampSec == 60 && L.s[0].holdSec == 60);
  CHECK(near(L.s[1].target, 160) && L.s[1].rampSec == 0 && L.s[1].holdSec == 300 && near(programTotal(L), 420));
}

static void testSeqRun() {
  static DuoProgram p;
  progExample(0, p);
  Sim s;
  CHECK(s.start(p));
  CHECK(near(remainingOf(s.e), 420));
  s.runTo(30);
  CHECK(near(s.sp(0), 87.5f) && s.e.out(1).idle && s.e.out(1).inProgram);
  CHECK(s.e.seq().idx == 0 && s.e.seq().st == Stage::Ramp && near(s.e.seq().el, 30) && near(s.e.seq().dur, 60));
  CHECK(s.e.out(0).moving && near(s.e.out(0).rampLeft, 30) && near(s.e.out(0).target, 150));
  s.runTo(61);  // ramp done, the bottom is there: no wait
  CHECK(s.e.seq().st == Stage::Hold && near(s.sp(0), 150));
  s.runTo(125);
  CHECK(s.e.progPaused() && s.e.pauseMsg() && s.e.seq().idx == 1 && s.e.seq().st == Stage::Pause);
  s.runTo(200);  // nothing moves during the pause
  CHECK(s.e.progPaused() && near(s.e.elapsed(), 200) && near(s.sp(0), 150));
  CHECK(s.e.cont());
  CHECK(s.e.seq().idx == 2 && s.e.waiting() && near(s.sp(0), 160) && s.e.out(1).idle);
  s.step(1);  // the bottom heater gets to 160 during this tick
  CHECK(s.e.waiting());
  s.step(1);  // it is there -> the top ramp starts from its measured 25
  CHECK(!s.e.waiting() && s.e.seq().st == Stage::Ramp && s.e.out(1).moving);
  s.runTo(231);  // ramp from 201
  CHECK(near(s.sp(1), 25 + 125 * 30 / 60.0f, 0.1f));
  s.runTo(900);
  CHECK(s.e.state() == RunState::Finished && s.e.out(0).idle && near(s.sp(1), 150));
  // total: 120 + pause 80 + wait 1 + 300
  CHECK(near(s.e.elapsed(), 501, 1.01f));

  // wait for bottom: a slow bottom heater keeps the top waiting
  progExample(0, p);
  Sim w;
  w.autoCont = true;
  w.start(p);
  w.runTo(119);
  w.ok[0] = false;  // no bottom temperature: waits
  w.runTo(160);
  CHECK(w.e.waiting() && w.e.seq().idx == 2 && near(w.e.seq().el, 40, 1.01f));
  CHECK(near(remainingOf(w.e), 300));  // the forecast treats the wait as done
  w.ok[0] = true;
  w.runTo(162);
  CHECK(!w.e.waiting());
}

static void testNextPause() {
  static DuoProgram p;
  progExample(0, p);
  Sim s;
  s.start(p);
  s.runTo(10);
  CHECK(s.e.next());  // ramp -> hold, setpoint at the target
  CHECK(s.e.seq().st == Stage::Hold && near(s.sp(0), 150));
  CHECK(s.e.next() && s.e.progPaused());   // hold -> pause phase
  CHECK(s.e.next() && s.e.waiting());      // Next = Continue; Both phase waits for the bottom (at 150)
  CHECK(s.e.next() && s.e.seq().st == Stage::Ramp && s.e.out(1).moving);  // wait skipped: top ramp
  CHECK(s.e.next() && s.e.seq().st == Stage::Hold && near(s.sp(1), 150));
  CHECK(s.e.next() && s.e.seq().idx == 3);
  // user pause freezes the ramp
  s.runTo(s.t + 10);
  const float sp = s.sp(1);
  s.e.pause();
  CHECK(s.e.state() == RunState::Paused && !s.e.out(1).moving);
  const uint32_t g = s.e.out(1).gen;
  s.runTo(s.t + 20);
  CHECK(near(s.sp(1), sp) && s.e.seq().st == Stage::Ramp);
  s.e.resume();
  CHECK(s.e.out(1).gen != g);
  s.runTo(s.t + 30);  // 40 s ramp: 10 before + 30 after the pause
  CHECK(s.e.seq().st == Stage::Hold && near(s.sp(1), 190));
  s.e.stop();
  CHECK(s.e.state() == RunState::Aborted && near(s.sp(1), 190) && !s.e.next());
  // Timeline: no "Next phase"
  progExample(2, p);
  s.start(p);
  CHECK(!s.e.next());
}

static void testTimelineRun() {
  static DuoProgram p;
  progExample(2, p);
  Sim s;
  s.start(p);
  CHECK(near(remainingOf(s.e), 450));
  s.runTo(30);
  CHECK(near(s.sp(0), 87.5f) && s.e.out(1).idle && s.e.lane(1).st == Stage::Idle);
  s.runTo(130);
  CHECK(near(s.sp(1), 25 + 125 * 10 / 40.0f) && s.e.lane(0).st == Stage::Hold && s.e.lane(1).idx == 1);
  s.runTo(400);
  CHECK(s.e.lane(1).st == Stage::Done && near(s.sp(1), 150) && s.e.out(0).idle && s.e.active());
  s.runTo(460);
  CHECK(s.e.state() == RunState::Finished && near(s.e.elapsed(), 450));
  // a pause step on the top pauses both channels
  progExample(2, p);
  TlStep ps = {ST_PAUSE, 1, NO_TEXT, {0, 0, 0}};
  progAddText(p, "Check flux", ps.text);
  memmove(&p.st[1][3], &p.st[1][2], sizeof(TlStep) * 4);
  p.st[1][2] = ps;  // after 150/40/20: reached at 180
  ++p.nSt[1];
  s.start(p);
  s.runTo(185);
  CHECK(s.e.progPaused() && s.e.pauseMsg() && !strcmp(s.e.pauseText(), "Check flux"));
  const float el0 = s.e.lane(0).el;
  s.runTo(215);
  CHECK(near(s.e.lane(0).el, el0) && s.e.lane(0).st == Stage::Hold);
  CHECK(s.e.cont() && !s.e.progPaused());
  s.runTo(900);
  CHECK(s.e.state() == RunState::Finished && near(s.e.elapsed(), 450 + 35, 1.01f));
  // the result does not depend on the tick length
  const float dts[] = {0.25f, 1, 7.3f};
  for (float dt : dts) {
    progExample(2, p);
    Sim q;
    q.start(p);
    q.runTo(1000, dt);
    CHECK(q.e.state() == RunState::Finished && q.e.elapsed() >= 450 - 1e-3f && q.e.elapsed() < 450 + dt + 1e-3f);
  }
}

struct EvRec {
  PlanEv e[64];
  int n = 0;
};
static void evFn(const PlanEv& e, void* u) {
  EvRec& r = *(EvRec*)u;
  if (r.n < 64) r.e[r.n++] = e;
}
static const PlanEv* findEv(const EvRec& r, uint8_t type, int idx, int ch = -2) {
  for (int i = 0; i < r.n; ++i)
    if (r.e[i].type == type && r.e[i].idx == idx && (ch == -2 || r.e[i].ch == ch)) return &r.e[i];
  return nullptr;
}

static void testEvents() {
  static DuoProgram p;
  progExample(0, p);
  Sim s;
  s.start(p);
  EvRec f;
  CHECK(near(s.e.forecast(nullptr, nullptr, nullptr, evFn, &f), 420));
  const PlanEv* e = findEv(f, EV_PHASE, 2);
  CHECK(e && near(e->t, 120) && findEv(f, EV_PAUSE, 1) && findEv(f, EV_CONT, 1));
  e = findEv(f, EV_RAMP_END, 2, CH_TOP);
  CHECK(e && near(e->t, 180));
  e = findEv(f, EV_HOLD_END, 5, CH_TOP);
  CHECK(e && near(e->t, 380));
  e = findEv(f, EV_END, -1);
  CHECK(e && near(e->t, 420));
  // the real run reports its transitions with the run time
  EvRec r;
  progExample(0, p);
  Sim q;
  q.e.setEventSink(evFn, &r);
  q.start(p);
  CHECK(r.n >= 1 && r.e[0].type == EV_PHASE && r.e[0].idx == 0);
  q.runTo(150);
  e = findEv(r, EV_PAUSE, 1);
  CHECK(e && near(e->t, 120) && q.e.chState(0) == ChState::Halt);
  q.e.cont();
  e = findEv(r, EV_CONT, 1);
  CHECK(e && near(e->t, 150) && q.e.chState(0) == ChState::Wait && q.e.chState(1) == ChState::Off);
  q.runTo(155);
  CHECK(q.e.chState(1) == ChState::Ramp && q.e.chState(0) == ChState::Jump && !q.e.canExtend(1));
  q.runTo(215);
  CHECK(q.e.chState(1) == ChState::Hold && q.e.canExtend(1) && !q.e.canExtend(0));
  q.runTo(250);
  CHECK(q.e.chState(0) == ChState::Keep);
  q.e.pause();
  CHECK(q.e.chState(1) == ChState::Pause);
  q.e.stop();
  CHECK(r.e[r.n - 1].type == EV_STOP && near(r.e[r.n - 1].t, 250));
  // forecast segments carry the source phase and the ramp flag
  progExample(2, p);
  Sim t;
  t.start(p);
  CHECK(t.e.chState(1) == ChState::Off && t.e.chState(0) == ChState::Ramp);
}

static void testSeqEdits() {
  static DuoProgram p;
  char err[100];
  progExample(0, p);
  Sim s;
  s.autoCont = true;
  s.start(p);
  s.runTo(30);
  // target change while ramping: same slope (125/60 per s) from the current 87.5
  CHECK(s.e.setTarget(0, -1, 200, err, sizeof(err)));
  CHECK(near(s.e.out(0).rampLeft, 54) && p.ph[0].b.r == 84 && near(p.ph[0].b.t, 200) && s.e.edited());
  s.runTo(84);
  CHECK(near(s.sp(0), 200, 0.1f));
  s.runTo(90);
  CHECK(s.e.seq().st == Stage::Hold);
  // target change while holding: ramps with the phase's slope, the hold keeps running
  CHECK(s.e.setTarget(0, 0, 175, err, sizeof(err)));
  s.runTo(96);
  CHECK(near(s.sp(0), 200 - 6 * 125 / 60.0f, 0.1f));
  // extend the hold
  CHECK(s.e.extendHold(0, 30, err, sizeof(err)) && p.ph[0].b.h == 90);
  const float rem = remainingOf(s.e);
  s.runTo(173);  // hold 84 .. 174
  CHECK(s.e.seq().st == Stage::Hold && near(s.sp(0), 175));
  // shorten below the elapsed time: the hold ends at once
  CHECK(s.e.setHold(0, 0, 5, err, sizeof(err)) && s.e.seq().idx == 1 && p.ph[0].b.h == 89);
  CHECK(rem > 300);
  s.runTo(180);  // Continue (auto) -> Both phase: bottom jumps to 160, waits, top ramps
  // finished phase: locked
  CHECK(!s.e.setTarget(0, 0, 150, err, sizeof(err)) && strstr(err, "finished"));
  // future ramp: can change; the running one can't
  CHECK(!s.e.setRamp(1, 2, 10, err, sizeof(err)));
  CHECK(s.e.setRamp(1, 4, 60, err, sizeof(err)) && p.ph[4].t.r == 60);
  // future target: its ramp keeps its slope (225 -> 245 in 20 s), the next one too (245 -> 150 in 40 s)
  CHECK(s.e.setTarget(1, 5, 255, err, sizeof(err)) && p.ph[5].t.r == 30 && p.ph[6].t.r == 44);
  s.runTo(265);  // phase 2 175 .. 255; phase 3: top ramps 150 -> 190 (1 C/s); the bottom keeps 160 (phase 2)
  CHECK(s.e.seq().idx == 3 && s.e.keptIdx(0) == 2 && s.e.keptIdx(1) == 3);
  CHECK(s.e.setTarget(0, -1, 170, err, sizeof(err)) && near(s.sp(0), 170) && near(p.ph[2].b.t, 170));  // jump target
  // current top ramp -> 200: continues at 1 C/s; the next ramp (190 -> 225 in 30 s) keeps its slope
  CHECK(s.e.seq().st == Stage::Ramp && s.e.setTarget(1, 3, 200, err, sizeof(err)));
  CHECK(p.ph[4].t.r == 43);  // setRamp above made it 60 s for 35 C; from 200: 25 C -> 42.9 s
  // Idle / Pause have no target
  CHECK(!s.e.setTarget(0, 6, 100, err, sizeof(err)) && !s.e.setHold(0, 1, 10, err, sizeof(err)));
}

static void testTimelineEdits() {
  static DuoProgram p;
  char err[100];
  progExample(2, p);
  Sim s;
  s.start(p);
  s.runTo(100);  // bottom holds 150 until 210; top idle until 120
  // bottom hold +30: the top is shifted through its first hold at/after 210 (step 2: hold 220..250)
  CHECK(s.e.extendHold(0, 30, err, sizeof(err)) && p.st[0][0].s.h == 180 && p.st[1][2].s.h == 60);
  CHECK(near(remainingOf(s.e), 380));
  // and back: the same hold gets shorter again
  CHECK(s.e.setHold(0, 0, 150, err, sizeof(err)) && p.st[0][0].s.h == 150 && p.st[1][2].s.h == 30);
  s.runTo(130);  // top ramps 25 -> 150 (3.125 C/s), 56.25 now
  CHECK(s.e.setTarget(1, -1, 175, err, sizeof(err)));
  CHECK(p.st[1][1].s.r == 48 && p.st[1][2].s.r == 15);   // current ramp +8 s, next ramp 150->190 (1 C/s) from 175
  CHECK(p.st[0][0].s.h == 158 && p.st[0][1].s.h == 125);  // bottom: +8 in the current hold, -25 in the next one
  // a finished channel keeps its last target, which stays adjustable
  s.runTo(395);
  CHECK(s.e.lane(1).st == Stage::Done && s.e.keptIdx(1) == 5);
  CHECK(s.e.setTarget(1, -1, 120, err, sizeof(err)) && s.e.out(1).moving && near(p.st[1][5].s.t, 120));
  CHECK(!s.e.extendHold(1, 10, err, sizeof(err)));
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
  a.liveMin = 120;
  roundTrip(a, b, false);
  CHECK(!strcmp(b.ssid, "Workshop") && !strcmp(b.pass, "secret-pw"));
  CHECK(b.ch[1].type == CH_LOCAL && !strcmp(b.ch[1].name, "IR head"));
  CHECK(b.ch[1].loc.sensor == SENSOR_MAX6675 && near(b.ch[1].loc.kp, 7.5f));
  CHECK(b.ch[0].mb.reg.alarm == REG_UNSET && b.ch[0].mb.reg.pv == 13294);
  CHECK(b.accentColor == 0xD65D0E && b.themeDark == 0 && b.liveMin == 120);
  CHECK(memcmp(&a.ch[0], &b.ch[0], sizeof(ChannelSettings)) == 0);
  // API form: no passwords
  JsonDocument d;
  settingsToJson(a, d.to<JsonObject>(), true);
  CHECK(d["wifi"]["pass"].isNull() && d["wifi"]["passSet"] == true);
  roundTrip(a, b, true);
  CHECK(b.pass[0] == 0);  // defaults kept (not overwritten)
}

static void testStaticIp() {
  Settings s;
  char msg[200];
  settingsDefaults(s, S3);
  CHECK(!s.staticIp && s.subnet[0] == 255 && s.subnet[3] == 0);
  JsonDocument d;
  deserializeJson(d, R"({"wifi":{"staticIp":true,"ip":"192.168.1.60","gateway":"192.168.1.1","dns1":"1.1.1.1"}})");
  CHECK(settingsFromJson(d.as<JsonObjectConst>(), s, msg, sizeof(msg)));
  CHECK(settingsValidate(s, S3, msg, sizeof(msg)) && s.staticIp && s.ip[3] == 60 && s.dns1[0] == 1 && s.dns2[0] == 0);
  JsonDocument o;
  settingsToJson(s, o.to<JsonObject>(), true);
  CHECK(o["wifi"]["ip"] == "192.168.1.60" && o["wifi"]["subnet"] == "255.255.255.0" && o["wifi"]["dns2"] == "");
  // gateway outside the subnet / bad mask / missing address: rejected, back to DHCP
  deserializeJson(d, R"({"wifi":{"gateway":"192.168.2.1"}})");
  CHECK(settingsFromJson(d.as<JsonObjectConst>(), s, msg, sizeof(msg)));
  CHECK(!settingsValidate(s, S3, msg, sizeof(msg)) && strstr(msg, "gateway") && !s.staticIp);
  deserializeJson(d, R"({"wifi":{"staticIp":true,"gateway":"192.168.1.1","subnet":"255.0.255.0"}})");
  CHECK(settingsFromJson(d.as<JsonObjectConst>(), s, msg, sizeof(msg)));
  CHECK(!settingsValidate(s, S3, msg, sizeof(msg)) && strstr(msg, "mask"));
  deserializeJson(d, R"({"wifi":{"ip":"192.168.1.300"}})");
  CHECK(!settingsFromJson(d.as<JsonObjectConst>(), s, msg, sizeof(msg)) && strstr(msg, "wifi.ip"));
  deserializeJson(d, R"({"wifi":{"ip":"192.168.1.5x"}})");
  CHECK(!settingsFromJson(d.as<JsonObjectConst>(), s, msg, sizeof(msg)));
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
  testProgJson();
  testPreview();
  testSeqRun();
  testNextPause();
  testTimelineRun();
  testSeqEdits();
  testTimelineEdits();
  testEvents();
  testSettingsDefaults();
  testSettingsJson();
  testSettingsMerge();
  testStaticIp();
  testSettingsVariants();
  testThermocouple();
  testPid();
  testRunaway();
  testAutotune();
  printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
  return fails ? 1 : 0;
}
