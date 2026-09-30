// Legacy single-channel program view for the TempCtl 1.2 touch UI and web
// page, which stay in use until the new UIs (steps 6 and 7). They show a
// program read-only as the bottom channel's plan in TempCtl steps (target,
// ramp, hold). The Duo programs are in duo_program.h, the engine in
// run_engine.h. Pure C++ (host-tested).
#pragma once
#include <stdint.h>
#include "duo_program.h"
#include "run_engine.h"

constexpr int MAX_PROGRAMS = PROG_MAX;  // stored programs
constexpr int MAX_STEPS = 100;          // steps of the legacy view

struct Step {
  float target;       // target temperature
  uint32_t rampSec;   // seconds to ramp from the previous value to target (0 = jump)
  uint32_t holdSec;   // seconds to hold at target (0 = no hold)
};

struct Program {
  char name[PROG_NAME_LEN];
  uint8_t count;      // number of used steps (0..MAX_STEPS)
  Step s[MAX_STEPS];
};

void programClear(Program& p, const char* name);
bool programInsertStep(Program& p, int at, const Step& st);  // at = 0..count
float programTotal(const Program& p);                        // ramps + holds

// The bottom channel's plan of a Duo program as TempCtl steps (Idle parts
// become holds, first ramps start at 25 C). A program without bottom phases
// shows the top channel.
void programLegacyView(const DuoProgram& d, Program& out);
