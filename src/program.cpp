#include "program.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

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

// ------------------------------------------------------------ editing helpers

void programClear(Program& p, const char* name) {
  memset(&p, 0, sizeof(p));
  snprintf(p.name, sizeof(p.name), "%s", name ? name : "");
}

bool programInsertStep(Program& p, int at, const Step& st) {
  if (p.count >= MAX_STEPS || at < 0 || at > p.count) return false;
  memmove(&p.s[at + 1], &p.s[at], sizeof(Step) * (p.count - at));
  p.s[at] = st;
  ++p.count;
  return true;
}

bool programRemoveStep(Program& p, int idx) {
  if (idx < 0 || idx >= p.count) return false;
  memmove(&p.s[idx], &p.s[idx + 1], sizeof(Step) * (p.count - idx - 1));
  --p.count;
  memset(&p.s[p.count], 0, sizeof(Step));
  return true;
}

bool programMoveStep(Program& p, int from, int to) {
  if (from < 0 || from >= p.count || to < 0 || to >= p.count) return false;
  if (from == to) return true;
  const Step st = p.s[from];
  if (from < to) memmove(&p.s[from], &p.s[from + 1], sizeof(Step) * (to - from));
  else memmove(&p.s[to + 1], &p.s[to], sizeof(Step) * (from - to));
  p.s[to] = st;
  return true;
}

void programSanitize(Program& p, float spMin, float spMax) {
  p.name[PROG_NAME_LEN - 1] = 0;
  if (p.count > MAX_STEPS) p.count = MAX_STEPS;
  for (int i = 0; i < p.count; ++i) {
    Step& s = p.s[i];
    if (isnan(s.target)) s.target = spMin;
    if (s.target < spMin) s.target = spMin;
    if (s.target > spMax) s.target = spMax;
    if (s.rampSec > MAX_STEP_SEC) s.rampSec = MAX_STEP_SEC;
    if (s.holdSec > MAX_STEP_SEC) s.holdSec = MAX_STEP_SEC;
  }
}

// ------------------------------------------------------------ engine

float ProgramEngine::rampSec(int i) const { return (i >= 0 && i < p_.count) ? (float)p_.s[i].rampSec : 0.0f; }
float ProgramEngine::holdSec(int i) const { return (i >= 0 && i < p_.count) ? (float)p_.s[i].holdSec : 0.0f; }

float ProgramEngine::plannedTotal(const Program& p) {
  float t = 0;
  const int n = p.count > MAX_STEPS ? MAX_STEPS : p.count;
  for (int i = 0; i < n; ++i) t += (float)p.s[i].rampSec + (float)p.s[i].holdSec;
  return t;
}

float ProgramEngine::plannedAt(const Program& p, float startTemp, float t) {
  float prev = startTemp;
  if (t < 0) return startTemp;
  const int n = p.count > MAX_STEPS ? MAX_STEPS : p.count;
  for (int i = 0; i < n; ++i) {
    const float r = (float)p.s[i].rampSec, h = (float)p.s[i].holdSec;
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
  if (p.count == 0 || p.count > MAX_STEPS) return false;
  p_ = p;
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
      const int n = step_ + 1;
      if (n >= p_.count) { finish(); return; }
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
  if (active()) {
    lastSp_ = setpoint();
    st_ = RunState::Aborted;
    ph_ = Phase::None;
    soakWait_ = false;
    ++gen_;
  }
}

void ProgramEngine::skip() {
  if (active()) {
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
