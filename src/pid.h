// PID controller for the local channels (pure C++, host-tested).
// - derivative on the measurement (no kick when the setpoint jumps), low-pass filtered (Td/8, >= 1 s)
// - integrator clamped to the output range and frozen while the output is
//   saturated in the direction of the error (anti-windup)
// - optional feed-forward (spec 9.23): the output the heater model expects is added to P + I + D; the
//   integrator may then go below 0 (it takes back an overestimated feed-forward)
#pragma once
#include <math.h>
#include <stdint.h>

// Heater model for the feed-forward (spec 9.23): first order + dead time around a fixed ambient
constexpr float FF_AMBIENT_C = 25.0f;
struct FfModel {
  float k;     // degC per % output at steady state
  float tau;   // time constant s
  float dead;  // dead time s (the preview looks this far ahead)
};
// holding power at spAhead + ramp power for rate degC/s (not clamped; NaN if spAhead is NaN)
float ffOutput(const FfModel& m, float spAhead, float rate);
// the share of a feed-forward that is used: 0 ... outMax (more can't reach the heater, and the integrator,
// clamped to -outMax ... outMax, can then always take it back: bumpless switching, a too-low K correctable)
inline float clampFf(float ff, float outMax) { return ff < 0 ? 0 : (ff > outMax ? outMax : ff); }
// how old a preview may be: 2.5 control cycles (cycleMs = the time between its updates), at least 2 s
inline uint32_t ffMaxAgeMs(uint32_t cycleMs) { return cycleMs * 5 / 2 > 2000 ? cycleMs * 5 / 2 : 2000; }
// the preview (spAhead, rate) while it is at most maxAgeMs old, else the channel's own sp with no ramp power
float ffInput(const FfModel& m, float sp, float spAhead, float rate, uint32_t ageMs, uint32_t maxAgeMs = 2000);

struct PidParams {
  float kp;      // % output per degC error
  float ki;      // % output per degC*s
  float kd;      // % output per degC/s (on the measurement)
  float outMax;  // output limit %, 0..100
};

class Pid {
 public:
  void reset() { first_ = true; i_ = 0; d_ = 0; out_ = 0; ff_ = 0; }
  // One step: returns the output 0..outMax %. ff = feed-forward % (NaN = none: integrator 0..outMax as before).
  // spRate = the setpoint's ramp rate at this moment (degC/s), used only with ff: the derivative then acts on
  // (PV rate - spRate), so it doesn't brake a ramp the PV follows (a jump still gives no kick)
  float update(const PidParams& p, float sp, float pv, float dtSec, float ff = NAN, float spRate = 0);
  // feed-forward switched on / off: move its share into / out of the integrator (no output jump)
  void shiftIntegral(float delta);
  float integral() const { return i_; }
  float output() const { return out_; }
  float lastFf() const { return ff_; }  // feed-forward of the last update (0 = none)
  bool running() const { return !first_; }  // updated at least once since the last reset
  // the measurement stepped to a new level (a jump accepted by the input filter): the derivative starts from it
  void rebase(float pv) { if (!first_) prevPv_ = pv; }

 private:
  bool first_ = true;
  float i_ = 0, d_ = 0, prevPv_ = 0, out_ = 0, ff_ = 0;
};
