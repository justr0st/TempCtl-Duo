// Host-side tests. Build & run on a PC:
//   g++ -std=c++17 -I../src host_test.cpp ../src/program.cpp -o host_test && ./host_test
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "modbus_frame.h"
#include "program.h"

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

int main() {
  testCrc();
  testParse();
  testEdit();
  testPlan();
  testRun();
  printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
  return fails ? 1 : 0;
}
