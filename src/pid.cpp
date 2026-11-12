#include "pid.h"
#include <math.h>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float ffOutput(const FfModel& m, float spAhead, float rate) {
  return (spAhead - FF_AMBIENT_C) / m.k + m.tau * rate / m.k;
}

float ffInput(const FfModel& m, float sp, float spAhead, float rate, uint32_t ageMs, uint32_t maxAgeMs) {
  if (ageMs <= maxAgeMs && !isnan(spAhead)) return ffOutput(m, spAhead, rate);
  return ffOutput(m, sp, 0);
}

void Pid::shiftIntegral(float delta) { i_ = clampf(i_ + delta, -100.0f, 100.0f); }

float Pid::update(const PidParams& p, float sp, float pv, float dt, float ff, float spRate) {
  const float outMax = clampf(p.outMax, 0.0f, 100.0f);
  if (!(dt > 0) || isnan(sp) || isnan(pv)) return out_;
  const bool hasFf = !isnan(ff);
  ff_ = hasFf ? clampFf(ff, outMax) : 0;
  const float e = sp - pv;
  // derivative on the measurement
  if (first_) {
    prevPv_ = pv;
    d_ = 0;
    first_ = false;
  }
  // low-pass on the derivative: time constant Td/8 (at least 1 s), so the
  // 0.25 degC steps of the converters don't turn into output spikes
  const float dRaw = -p.kd * ((pv - prevPv_) / dt - (hasFf && !isnan(spRate) ? spRate : 0));
  float tauD = p.kp > 0 ? p.kd / p.kp / 8.0f : 1.0f;
  if (tauD < 1.0f) tauD = 1.0f;
  d_ += (dRaw - d_) * (dt / (tauD + dt));
  prevPv_ = pv;
  const float pTerm = p.kp * e;
  // conditional integration: not while saturated in the same direction
  const float unsat = ff_ + pTerm + i_ + d_;
  const bool satHigh = unsat >= outMax && e > 0;
  const bool satLow = unsat <= 0 && e < 0;
  if (!satHigh && !satLow) i_ += p.ki * e * dt;
  i_ = clampf(i_, hasFf ? -outMax : 0.0f, outMax);
  out_ = clampf(ff_ + pTerm + i_ + d_, 0.0f, outMax);
  return out_;
}
