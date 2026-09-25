// Ramp/soak profile engine with 6 steps. Pure C++ (no Arduino deps) so it
// can be unit-tested on a PC (see test/host_test.cpp).
#pragma once
#include <stdint.h>

constexpr int NUM_STEPS = 6;

struct Step {
  uint8_t enabled;   // 0/1
  float   target;    // target temperature
  float   rampMin;   // minutes to ramp from previous value to target (0 = jump)
  float   holdMin;   // minutes to hold at target (0 = no hold)
};

struct Program {
  Step s[NUM_STEPS];
};

enum class RunState : uint8_t { Idle = 0, Running, Paused, Finished, Aborted };
enum class Phase : uint8_t { None = 0, Ramp, Hold };

const char* runStateName(RunState s);
const char* phaseName(Phase p);

class ProgramEngine {
 public:
  // Start a program. The first ramp starts at startTemp (normally the current PV).
  // Returns false if no step is enabled.
  bool start(const Program& p, float startTemp);
  void pause();
  void resume();
  void stop();   // abort
  void skip();   // jump to next phase

  // Advance program time by dt seconds.
  // guaranteedSoak: hold time only counts while |pv - target| <= band.
  void tick(float dtSec, float pv, bool pvValid, bool guaranteedSoak, float band);

  RunState state() const { return st_; }
  Phase phase() const { return ph_; }
  int step() const { return step_; }              // 0..5, -1 if none
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
  static int enabledCount(const Program& p);

 private:
  void advance();
  void finish();
  int nextEnabled(int from) const;
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
