#include "autotune.h"
#include <math.h>
#include <stdio.h>

static constexpr int SETTLE_CYCLES = 2;

void RelayAutotune::start(const TuneConfig& c) {
  c_ = c;
  if (!(c_.outMax > 0)) c_.outMax = 100;
  if (c_.outMax > 100) c_.outMax = 100;
  if (!(c_.hyst > 0)) c_.hyst = 0.5f;
  if (c_.cycles < 3) c_.cycles = 3;
  state_ = TuneState::Running;
  res_ = TuneResult{};
  res_.fast = c_.fast;
  snprintf(msg_, sizeof(msg_), "heating up to %.0f C", c_.sp);
  t_ = 0;
  high_ = true;
  crossedOnce_ = false;
  bias_ = d_ = c_.outMax / 2;
  tHighStart_ = tLowStart_ = tHigh_ = tLow_ = cycStart_ = 0;
  pvMax_ = -1e9f;
  pvMin_ = 1e9f;
  cycle_ = 0;
  sumPeriod_ = sumAmp_ = 0;
  minPeriod_ = 1e9f;
  maxPeriod_ = 0;
  counted_ = 0;
}

void RelayAutotune::stop() {
  if (state_ == TuneState::Running) fail("stopped");
}

void RelayAutotune::fail(const char* m) {
  state_ = TuneState::Failed;
  snprintf(msg_, sizeof(msg_), "%s", m);
}

// called at the switch high -> low (PV rose above sp + h): one full cycle ended
void RelayAutotune::finishCycle() {
  const float period = t_ - cycStart_;
  const float amp = (pvMax_ - pvMin_) / 2;
  // re-centre the relay so heating and cooling take equally long (Marlin)
  const float oldBias = bias_;
  if (tHigh_ > 0 && tLow_ > 0) {
    bias_ += d_ * (tHigh_ - tLow_) / (tHigh_ + tLow_);
    const float lo = c_.outMax * 0.1f, hi = c_.outMax * 0.9f;
    if (bias_ < lo) bias_ = lo;
    if (bias_ > hi) bias_ = hi;
    d_ = bias_ > c_.outMax / 2 ? c_.outMax - bias_ : bias_;
  }
  ++cycle_;
  // measure only once the relay has settled (bias moved < 2 % of the range);
  // a larger correction restarts the measurement
  if (cycle_ > SETTLE_CYCLES && fabsf(bias_ - oldBias) >= 0.02f * c_.outMax) {
    sumPeriod_ = sumAmp_ = 0;
    minPeriod_ = 1e9f;
    maxPeriod_ = 0;
    counted_ = 0;
  } else if (cycle_ > SETTLE_CYCLES) {
    sumPeriod_ += period;
    sumAmp_ += amp;
    if (period < minPeriod_) minPeriod_ = period;
    if (period > maxPeriod_) maxPeriod_ = period;
    ++counted_;
  }
  snprintf(msg_, sizeof(msg_), "cycle %d of %d", cycle_, SETTLE_CYCLES + c_.cycles);
  cycStart_ = t_;
  pvMax_ = -1e9f;
  pvMin_ = 1e9f;
  if (counted_ < c_.cycles) return;

  const float pu = sumPeriod_ / counted_;
  const float a = sumAmp_ / counted_;
  if (maxPeriod_ > 1.3f * minPeriod_) return fail("oscillation not steady (period varies > 30 %)");
  if (a <= c_.hyst * 1.2f) return fail("oscillation too small");
  const float ku = 4 * d_ / (3.14159265f * sqrtf(a * a - c_.hyst * c_.hyst));
  res_.ku = ku;
  res_.pu = pu;
  res_.amplitude = a;
  res_.bias = bias_;
  res_.d = d_;
  res_.kp = ku / 2.2f;
  const float ti = (c_.fast ? 0.7f : 2.2f) * pu, td = pu / 6.3f;
  res_.ki = res_.kp / ti;
  res_.kd = res_.kp * td;
  state_ = TuneState::Done;
  snprintf(msg_, sizeof(msg_), "done: Ku %.2f, Pu %.0f s%s", ku, pu, c_.fast ? ", faster" : "");
}

float RelayAutotune::update(float pv, float dt) {
  if (state_ != TuneState::Running) return 0;
  if (isnan(pv)) {
    fail("no temperature reading");
    return 0;
  }
  t_ += dt;
  if (t_ > c_.maxSec) {
    fail("time limit reached");
    return 0;
  }
  if (pv > c_.sp + c_.maxOver) {
    fail("temperature too far above the test setpoint");
    return 0;
  }
  if (pv > pvMax_) pvMax_ = pv;
  if (pv < pvMin_) pvMin_ = pv;
  if (high_ && pv > c_.sp + c_.hyst) {
    high_ = false;
    tHigh_ = t_ - tHighStart_;
    tLowStart_ = t_;
    if (!crossedOnce_) {  // end of the initial heat-up: cycles start here
      crossedOnce_ = true;
      cycStart_ = t_;
      pvMax_ = pvMin_ = pv;
      snprintf(msg_, sizeof(msg_), "cycle 1 of %d", SETTLE_CYCLES + c_.cycles);
    } else {
      finishCycle();
      if (state_ != TuneState::Running) return 0;
    }
  } else if (!high_ && pv < c_.sp - c_.hyst) {
    high_ = true;
    tLow_ = t_ - tLowStart_;
    tHighStart_ = t_;
  }
  if (!crossedOnce_) return c_.outMax;  // heat up at full (limited) output
  return high_ ? bias_ + d_ : bias_ - d_;
}
