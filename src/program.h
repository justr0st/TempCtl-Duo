// Ramp/soak profile engine. Pure C++ (no Arduino deps) so it can be
// unit-tested on a PC (see test/host_test.cpp).
#pragma once
#include <stdint.h>

constexpr int MAX_PROGRAMS = 10;    // stored programs
constexpr int MAX_STEPS = 100;      // steps per program
constexpr int PROG_NAME_LEN = 24;   // including the terminating 0
constexpr uint32_t MAX_STEP_SEC = 9999UL * 60UL;  // longest ramp/hold

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

// Program editing helpers (bounds-checked; return false if not possible)
void programClear(Program& p, const char* name);
bool programInsertStep(Program& p, int at, const Step& st);  // at = 0..count
bool programRemoveStep(Program& p, int idx);
bool programMoveStep(Program& p, int from, int to);
void programSanitize(Program& p, float spMin, float spMax);  // clamp values, fix count/name

enum class RunState : uint8_t { Idle = 0, Running, Paused, Finished, Aborted };
enum class Phase : uint8_t { None = 0, Ramp, Hold };

const char* runStateName(RunState s);
const char* phaseName(Phase p);

class ProgramEngine {
 public:
  // Start a program. The first ramp starts at startTemp (normally the current PV).
  // Returns false if the program has no steps.
  bool start(const Program& p, float startTemp);
  void pause();
  void resume();
  void stop();   // abort
  void skip();   // jump to next phase

  // Advance program time by dt seconds.
  // guaranteedSoak: hold time only counts while |pv - target| <= band.
  void tick(float dtSec, float pv, bool pvValid, bool guaranteedSoak, float band);

  RunState state() const { return st_; }
  bool active() const { return st_ == RunState::Running || st_ == RunState::Paused; }
  Phase phase() const { return ph_; }
  int step() const { return step_; }              // 0..count-1, -1 if none
  int stepCount() const { return p_.count; }
  float setpoint() const;                          // current (host-computed) setpoint
  float target() const;                            // target of current step
  float phaseElapsed() const { return phEl_; }     // seconds
  float phaseDuration() const;                     // seconds
  float phaseRemaining() const;                    // seconds
  float elapsed() const;                           // program seconds (planned timeline)
  float total() const { return total_; }           // planned total seconds
  bool soakWaiting() const { return soakWait_; }
  uint32_t generation() const { return gen_; }     // increments on every phase/state change
  float startTemp() const { return start_; }
  const Program& program() const { return p_; }

  static float plannedTotal(const Program& p);
  static float plannedAt(const Program& p, float startTemp, float tSec);

 private:
  void advance();
  void finish();
  float rampSec(int i) const;
  float holdSec(int i) const;

  Program p_{};
  RunState st_ = RunState::Idle;
  Phase ph_ = Phase::None;
  int step_ = -1;
  float phEl_ = 0;     // elapsed in current phase
  float done_ = 0;     // planned seconds of completed phases
  float start_ = 0;
  float prev_ = 0;     // value the current ramp starts from
  float total_ = 0;
  float lastSp_ = 0;   // setpoint after finish/abort
  bool soakWait_ = false;
  uint32_t gen_ = 0;
};
