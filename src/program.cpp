#include "program.h"
#include <math.h>

const char* runStateName(RunState s) {
  switch (s) {
    case RunState::Idle: return "idle";
    case RunState::Running: return "running";
    case RunState::Paused: return "paused";
    case RunState::Finished: return "finished";
    case RunState::Aborted: return "stopped";
  }
  return "?";
}

const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Ramp: return "ramp";
    case Phase::Hold: return "hold";
    default: return "none";
  }
}

static float clampMin(float m) { return (m > 0 && !isnan(m)) ? m * 60.0f : 0.0f; }

float ProgramEngine::rampSec(int i) const { return (i >= 0 && i < NUM_STEPS) ? clampMin(p_.s[i].rampMin) : 0; }
float ProgramEngine::holdSec(int i) const { return (i >= 0 && i < NUM_STEPS) ? clampMin(p_.s[i].holdMin) : 0; }

int ProgramEngine::nextEnabled(int from) const {
  for (int i = from; i < NUM_STEPS; ++i)
    if (p_.s[i].enabled) return i;
  return -1;
}

int ProgramEngine::enabledCount(const Program& p) {
  int n = 0;
  for (int i = 0; i < NUM_STEPS; ++i) n += p.s[i].enabled ? 1 : 0;
  return n;
}

float ProgramEngine::plannedTotal(const Program& p) {
  float t = 0;
  for (int i = 0; i < NUM_STEPS; ++i)
    if (p.s[i].enabled) t += clampMin(p.s[i].rampMin) + clampMin(p.s[i].holdMin);
  return t;
}

float ProgramEngine::plannedAt(const Program& p, float startTemp, float t) {
  float prev = startTemp;
  if (t < 0) return startTemp;
  for (int i = 0; i < NUM_STEPS; ++i) {
    if (!p.s[i].enabled) continue;
    const float r = clampMin(p.s[i].rampMin), h = clampMin(p.s[i].holdMin);
    const float tgt = p.s[i].target;
    if (t < r) return prev + (tgt - prev) * (t / r);
    t -= r;
    if (t < h) return tgt;
    t -= h;
    prev = tgt;
  }
  return prev;
}

bool ProgramEngine::start(const Program& p, float startTemp) {
  p_ = p;
  if (enabledCount(p_) == 0) return false;
  st_ = RunState::Running;
  ph_ = Phase::None;
  step_ = -1;
  phEl_ = 0;
  done_ = 0;
  start_ = startTemp;
  prev_ = startTemp;
  total_ = plannedTotal(p_);
  soakWait_ = false;
  advance();
  return true;
}

void ProgramEngine::advance() {
  ++gen_;
  soakWait_ = false;
  for (;;) {
    if (ph_ == Phase::Ramp) {
      done_ += rampSec(step_);
      ph_ = Phase::Hold;
      phEl_ = 0;
      if (holdSec(step_) > 0) return;
      // no hold: fall through to next step
    } else {
      if (ph_ == Phase::Hold) done_ += holdSec(step_);
      const int n = nextEnabled(step_ + 1);
      if (n < 0) { finish(); return; }
      if (step_ >= 0) prev_ = p_.s[step_].target;
      step_ = n;
      ph_ = Phase::Ramp;
      phEl_ = 0;
      if (rampSec(step_) > 0) return;
      // zero ramp: jump to target, continue with hold
    }
  }
}

void ProgramEngine::finish() {
  lastSp_ = (step_ >= 0) ? p_.s[step_].target : start_;
  st_ = RunState::Finished;
  ph_ = Phase::None;
  phEl_ = 0;
  done_ = total_;
  soakWait_ = false;
}

void ProgramEngine::pause() {
  if (st_ == RunState::Running) { st_ = RunState::Paused; ++gen_; }
}

void ProgramEngine::resume() {
  if (st_ == RunState::Paused) { st_ = RunState::Running; ++gen_; }
}

void ProgramEngine::stop() {
  if (st_ == RunState::Running || st_ == RunState::Paused) {
    lastSp_ = setpoint();
    st_ = RunState::Aborted;
    ph_ = Phase::None;
    soakWait_ = false;
    ++gen_;
  }
}

void ProgramEngine::skip() {
  if (st_ == RunState::Running || st_ == RunState::Paused) {
    const RunState keep = st_;
    advance();
    if (st_ != RunState::Finished) st_ = keep;
  }
}

void ProgramEngine::tick(float dt, float pv, bool pvValid, bool guaranteedSoak, float band) {
  if (st_ != RunState::Running || dt <= 0) return;
  if (ph_ == Phase::Ramp) {
    phEl_ += dt;
    if (phEl_ >= rampSec(step_)) advance();
  } else if (ph_ == Phase::Hold) {
    const bool inBand = pvValid && fabsf(pv - target()) <= band;
    if (guaranteedSoak && !inBand) {
      soakWait_ = true;
    } else {
      soakWait_ = false;
      phEl_ += dt;
    }
    if (phEl_ >= holdSec(step_)) advance();
  }
}

float ProgramEngine::target() const {
  if (step_ < 0) return start_;
  return p_.s[step_].target;
}

float ProgramEngine::setpoint() const {
  switch (st_) {
    case RunState::Running:
    case RunState::Paused:
      if (ph_ == Phase::Ramp) {
        const float r = rampSec(step_);
        const float f = (r > 0) ? fminf(1.0f, phEl_ / r) : 1.0f;
        return prev_ + (target() - prev_) * f;
      }
      return target();
    case RunState::Finished:
    case RunState::Aborted:
      return lastSp_;
    default:
      return NAN;
  }
}

float ProgramEngine::phaseDuration() const {
  if (ph_ == Phase::Ramp) return rampSec(step_);
  if (ph_ == Phase::Hold) return holdSec(step_);
  return 0;
}

float ProgramEngine::phaseRemaining() const {
  const float r = phaseDuration() - phEl_;
  return r > 0 ? r : 0;
}

float ProgramEngine::elapsed() const {
  const float d = phaseDuration();
  return done_ + (phEl_ < d ? phEl_ : d);
}
