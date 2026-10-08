// One run engine for both program types and both channels (spec 9.3).
// Pure C++ (no Arduino), host-tested (test/host_test.cpp).
//
// Each channel has a setpoint generator (ramp from -> to over dur seconds, or
// Idle = outputs off). The program drives the generators:
//   Sequence: one phase after another; stages Ramp -> [Wait] -> Hold, Pause
//   Timeline: two independent step lists; stages Ramp -> Hold, Idle, Pause
// Times advance event by event inside tick(), so the result does not depend
// on the tick length. forecast() runs a copy of the engine to the end (waits
// satisfied at once, pauses take no time) and reports the plan curves.
#pragma once
#include "duo_program.h"

enum class RunState : uint8_t { Idle = 0, Running, Paused, Finished, Aborted };
enum class Stage : uint8_t { None = 0, Ramp, Wait, Hold, Idle, Pause, Done };

const char* runStateName(RunState s);  // "idle" "running" "paused" "finished" "stopped"
const char* stageName(Stage s);        // "none" "ramp" "wait" "hold" "idle" "pause" "done"

// What the control task does with a channel
struct ChOut {
  bool inProgram;   // the program has phases/steps for this channel (else: manual)
  bool idle;        // outputs off (no target yet, Idle phase/step)
  bool moving;      // the setpoint is ramping right now
  float sp;         // current setpoint (NaN when idle)
  float target;     // where the setpoint is going / staying (NaN when idle)
  float rampLeft;   // seconds until sp reaches target (0 if not moving)
  uint32_t gen;     // changes whenever the setpoint course changes (new ramp, jump, pause, resume ...)
};

// Current phase (Sequence) or step of one channel (Timeline)
struct StageInfo {
  int16_t idx;      // phase / step index (0-based), may be == count after the end
  Stage st;
  float el, dur;    // seconds in this stage / its length (Wait, Pause: dur 0)
};

// One piece of a plan curve: from t0 to t1 (seconds from now) the setpoint
// goes linearly from v0 to v1, or the channel is idle. src = phase/step that
// set the target (-1 none), ramp = the setpoint is moving.
struct PlanSeg {
  uint8_t ch;
  bool idle;
  bool ramp;
  int16_t src;
  float t0, t1, v0, v1;
};
typedef void (*PlanFn)(const PlanSeg& s, void* user);

// Transitions of a run (recorded during the run, and in the forecast):
// t = run time (real run) or seconds from now (forecast).
enum PlanEvType : uint8_t {
  EV_PHASE = 1,    // Sequence: phase idx starts
  EV_STEP,         // Timeline: step idx of ch starts
  EV_RAMP_END,     // ramp of phase/step idx (ch) ends
  EV_HOLD_END,     // hold (or Timeline idle) of phase/step idx ends (Sequence: ch = channel of the hold)
  EV_PAUSE,        // program pause (Pause phase idx / pause step idx of ch)
  EV_CONT,         // Continue
  EV_END,          // program finished
  EV_STOP,         // stopped (user, fault)
};
struct PlanEv {
  uint8_t type;
  uint8_t ch;      // 0xFF = not channel specific
  int16_t idx;
  float t;
};
typedef void (*EvFn)(const PlanEv& e, void* user);

// What a channel does right now (status lines, chart lanes)
enum class ChState : uint8_t { None = 0, Off, Ramp, Hold, Wait, Jump, Keep, Halt, Pause, Done };
const char* chStateName(ChState s);  // "none" "off" "ramp" "hold" "wait" "jump" "keep" "halt" "pause" "done"

// Where a run is, to resume it after "Run interrupted by a reset" (spec 9.3.5)
struct RunPos {
  float el;                  // run time
  int16_t i[NUM_CHANNELS];   // Sequence: i[0] = phase; Timeline: step of each channel
  uint8_t st[NUM_CHANNELS];  // Stage (Sequence: st[0])
  float left[NUM_CHANNELS];  // Timeline: seconds left in the channel's ramp / hold / idle
  float slope[NUM_CHANNELS];  // degC/s of the channel's last ramp (0 = none / unknown)
};

class RunEngine {
 public:
  // p = the running copy of the program (run edits change it); it must stay
  // valid while the run is active. pv/pvOk: measured values (first ramps
  // start there). band: "bottom reached" band for "wait for bottom".
  bool start(DuoProgram* p, const float* pv, const bool* pvOk, float band);
  // Resumes an interrupted run (spec 9.3.5) with its running copy p, all channels starting from their
  // measured values (outputs were off): Sequence = phase r.i[0] from its beginning (channels the phase
  // doesn't start keep the target of the phases before it, set at once); Timeline = run time r.el,
  // each channel in its step: a ramp ramps from the measured value in the time it had left, holds
  // and idles go on for their remaining time, a pause asks for Continue again. Run time goes on from
  // r.el. edited: the running copy had run edits.
  bool resumeAt(DuoProgram* p, const RunPos& r, const float* pv, const bool* pvOk, float band, bool edited);
  RunPos position() const;
  void tick(float dt, const float* pv, const bool* pvOk);
  void pause();   // user pause: everything stops (timers, ramps)
  void resume();
  void stop();    // abort: setpoints stay where they are
  bool next();    // "Next phase" (Sequence): one stage further; in a program pause = Continue
  bool cont();    // Continue after a Pause phase / step
  void setBand(float band) { band_ = band; }
  // receives the transitions of the real run (t = run time)
  void setEventSink(EvFn fn, void* user) {
    evFn_ = fn;
    evUser_ = user;
  }

  // Run edits (spec 9.3.4). idx = phase (Sequence) or step of channel ch
  // (Timeline); -1 = current. Sequence holds ignore ch (a phase has one hold).
  // False + message if the edit is not allowed.
  bool setHold(int ch, int idx, float sec, char* err, size_t n);
  bool extendHold(int ch, float sec, char* err, size_t n);
  bool setRamp(int ch, int idx, float sec, char* err, size_t n);
  bool setTarget(int ch, int idx, float t, char* err, size_t n);  // idx -1: current / kept target of ch
  int keptIdx(int ch) const;  // phase/step whose target ch has (or ramps to), -1 = none

  RunState state() const { return st_; }
  bool active() const { return st_ == RunState::Running || st_ == RunState::Paused; }
  uint8_t type() const { return p_ ? p_->type : (uint8_t)PROG_SEQ; }
  bool progPaused() const { return active() && progPause_; }  // waiting for Continue
  bool pauseMsg() const;          // the pause asks for "Action required"
  const char* pauseText() const;  // its custom text ("" if none)
  bool waiting() const;           // Sequence: waiting for the bottom to reach its target
  StageInfo seq() const;          // Sequence: phase + stage
  StageInfo lane(int ch) const;   // Timeline: step + stage of a channel
  float elapsed() const { return el_; }  // run time (includes pauses and waits)
  ChOut out(int ch) const;
  ChState chState(int ch) const;
  bool canExtend(int ch) const;  // a hold (or Timeline idle) of this channel is running
  bool edited() const { return edited_; }  // the running copy was changed
  const DuoProgram* program() const { return p_; }

  // Plan from now to the end (t = 0 is now): waits satisfied at once, pauses
  // take no time, idle channels at pv (may be null) when a ramp starts after
  // Idle. Returns the remaining seconds.
  float forecast(PlanFn fn, void* user, const float* pv, EvFn efn = nullptr, void* euser = nullptr) const;

 private:
  struct Gen {
    uint8_t idle;       // outputs off
    uint8_t stageRamp;  // this ramp is the current stage's ramp (frozen in a program pause)
    int16_t src;        // phase/step that set the target, -1 = none
    float from, to, dur, el;
    float pre;          // ramp seconds before the last restart (target edit while ramping)
    float slope;        // degC/s of the ramp, 0 = jump
  };
  struct Lane {
    int16_t i;
    Stage st;
    float el;
    float hold0;  // hold seconds used up when the hold starts (resumed ramp longer / shorter than planned)
  };

  float plannedRate(int ch, int idx, float target, uint32_t r, float saved) const;
  float resumedRamp(int ch, float target, float rate, float fallback) const;
  float sp(const Gen& g) const;
  bool genDone(const Gen& g) const;
  float genLeft(const Gen& g) const;
  bool canMove(const Gen& g) const;
  void bump(int ch) { ++gen_[ch]; }
  void bumpAll() { ++gen_[0]; ++gen_[1]; }
  void startRamp(int ch, float target, uint32_t r, int src);
  void jump(int ch, float target, int src);
  void setIdle(int ch);
  void freeze();
  void finish();
  void begin(DuoProgram* p, const float* pv, const bool* pvOk, float band);
  bool keptBefore(int ch, int idx, float& t, int& src) const;  // target ch has before phase/step idx
  void startPhase(int k);
  void startStep(int ch, int k);
  bool seqStep();
  bool laneStep(int ch);
  void settle();
  float timeToEvent() const;
  void advance(float d);
  void emit(PlanFn fn, void* user, float t, float d) const;
  void updatePause();
  bool bottomReached() const;
  uint32_t holdOf(int idx) const;
  bool futureTarget(int ch, int idx) const;
  bool prevTarget(int ch, int idx, float& prev) const;
  int nextRamp(int ch, int idx) const;
  float laneEnd(int ch, int idx, bool rampEnd) const;
  void shiftOther(int ch, float delta, float moment);
  static bool fail(char* err, size_t n, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
  void ev(uint8_t type, int ch, int idx) const;

  DuoProgram* p_ = nullptr;
  RunState st_ = RunState::Idle;
  bool progPause_ = false;
  bool forecast_ = false;
  bool edited_ = false;
  bool used_[NUM_CHANNELS] = {false, false};
  Gen g_[NUM_CHANNELS] = {};
  Lane seq_ = {0, Stage::None, 0};
  Lane lane_[NUM_CHANNELS] = {};
  float el_ = 0;
  float band_ = 5;
  float lastPv_[NUM_CHANNELS] = {0, 0};
  bool pvOk_[NUM_CHANNELS] = {false, false};
  uint32_t gen_[NUM_CHANNELS] = {0, 0};
  EvFn evFn_ = nullptr;
  void* evUser_ = nullptr;
  float evT_ = 0;  // time of the transitions being processed
};

// Plan of a program that is not running (t = 0 = start); pv = start values
// (null: first ramps drawn as jumps; the times are the same). Returns the total.
float programPreview(const DuoProgram& p, const float* pv, PlanFn fn, void* user);
