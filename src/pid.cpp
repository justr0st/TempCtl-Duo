#include "pid.h"
#include <math.h>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float Pid::update(const PidParams& p, float sp, float pv, float dt) {
  const float outMax = clampf(p.outMax, 0.0f, 100.0f);
  if (!(dt > 0) || isnan(sp) || isnan(pv)) return out_;
  const float e = sp - pv;
  // derivative on the measurement
  if (first_) {
    prevPv_ = pv;
    d_ = 0;
    first_ = false;
  }
  // low-pass on the derivative: time constant Td/8 (at least 1 s), so the
  // 0.25 degC steps of the converters don't turn into output spikes
  const float dRaw = -p.kd * (pv - prevPv_) / dt;
  float tauD = p.kp > 0 ? p.kd / p.kp / 8.0f : 1.0f;
  if (tauD < 1.0f) tauD = 1.0f;
  d_ += (dRaw - d_) * (dt / (tauD + dt));
  prevPv_ = pv;
  const float pTerm = p.kp * e;
  // conditional integration: not while saturated in the same direction
  const float unsat = pTerm + i_ + d_;
  const bool satHigh = unsat >= outMax && e > 0;
  const bool satLow = unsat <= 0 && e < 0;
  if (!satHigh && !satLow) i_ += p.ki * e * dt;
  i_ = clampf(i_, 0.0f, outMax);
  out_ = clampf(pTerm + i_ + d_, 0.0f, outMax);
  return out_;
}
