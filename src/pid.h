// PID controller for the local channels (pure C++, host-tested).
// - derivative on the measurement (no kick when the setpoint jumps), low-pass filtered (Td/8, >= 1 s)
// - integrator clamped to the output range and frozen while the output is
//   saturated in the direction of the error (anti-windup)
#pragma once

struct PidParams {
  float kp;      // % output per degC error
  float ki;      // % output per degC*s
  float kd;      // % output per degC/s (on the measurement)
  float outMax;  // output limit %, 0..100
};

class Pid {
 public:
  void reset() { first_ = true; i_ = 0; d_ = 0; out_ = 0; }
  // One step: returns the output 0..outMax %
  float update(const PidParams& p, float sp, float pv, float dtSec);
  float integral() const { return i_; }
  float output() const { return out_; }

 private:
  bool first_ = true;
  float i_ = 0, d_ = 0, prevPv_ = 0, out_ = 0;
};
