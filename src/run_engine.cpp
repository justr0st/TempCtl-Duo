#include "run_engine.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const float T_EPS = 1e-3f;
static const float T_INF = 1e30f;
static const int GUARD = 2000;  // transitions per settle / events per tick (safety net)

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

const char* stageName(Stage s) {
  static const char* const N[] = {"none", "ramp", "wait", "hold", "idle", "pause", "done"};
  const unsigned i = (unsigned)s;
  return i < 7 ? N[i] : "?";
}

const char* chStateName(ChState s) {
  static const char* const N[] = {"none", "off", "ramp", "hold", "wait", "jump", "keep", "halt", "pause", "done"};
  const unsigned i = (unsigned)s;
  return i < 10 ? N[i] : "?";
}

void RunEngine::ev(uint8_t type, int ch, int idx) const {
  if (!evFn_) return;
  PlanEv e = {type, (uint8_t)(ch < 0 ? 0xFF : ch), (int16_t)idx, evT_};
  evFn_(e, evUser_);
}

bool RunEngine::fail(char* err, size_t n, const char* fmt, ...) {
  if (err && n) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
  }
  return false;
}

// ------------------------------------------------------------------ generators

float RunEngine::sp(const Gen& g) const {
  if (g.idle) return NAN;
  if (g.dur <= 0 || g.el >= g.dur) return g.to;
  return g.from + (g.to - g.from) * (g.el / g.dur);
}

bool RunEngine::genDone(const Gen& g) const { return g.idle || g.dur <= 0 || g.el >= g.dur - T_EPS; }

float RunEngine::genLeft(const Gen& g) const { return genDone(g) ? 0 : g.dur - g.el; }

// user pause freezes everything; a program pause freezes the stage ramps only
bool RunEngine::canMove(const Gen& g) const { return st_ == RunState::Running && !(progPause_ && g.stageRamp); }

void RunEngine::startRamp(int ch, float target, uint32_t r, int src) {
  Gen& g = g_[ch];
  // first ramp / ramp after Idle: from the measured value, else from the current setpoint
  const float from = g.idle ? (pvOk_[ch] ? lastPv_[ch] : target) : sp(g);
  g.idle = 0;
  g.src = (int16_t)src;
  g.from = from;
  g.to = target;
  g.dur = (float)r;
  g.el = 0;
  g.pre = 0;
  g.slope = r > 0 ? fabsf(target - from) / (float)r : 0;
  g.stageRamp = r > 0;
  bump(ch);
}

void RunEngine::jump(int ch, float target, int src) {
  Gen& g = g_[ch];
  g.idle = 0;
  g.stageRamp = 0;
  g.src = (int16_t)src;
  g.from = g.to = target;
  g.dur = g.el = g.pre = g.slope = 0;
  bump(ch);
}

void RunEngine::setIdle(int ch) {
  Gen& g = g_[ch];
  memset(&g, 0, sizeof(g));
  g.idle = 1;
  g.src = -1;
  bump(ch);
}

// setpoints stay where they are (end of the run)
void RunEngine::freeze() {
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    Gen& g = g_[ch];
    if (g.idle) continue;
    g.from = g.to = sp(g);
    g.dur = g.el = 0;
    g.stageRamp = 0;
  }
  progPause_ = false;
  bumpAll();
}

void RunEngine::finish() {
  freeze();
  st_ = RunState::Finished;
  ev(EV_END, -1, -1);
}

// ------------------------------------------------------------------ Sequence

// the bottom drives the phase (ramp, wait, hold): a Bottom phase, or Both with the top Idle
static bool bDrives(const SeqPhase& f) { return f.kind == PH_B || (f.kind == PH_BT && f.tIdle); }

uint32_t RunEngine::holdOf(int idx) const {
  const SeqPhase& f = p_->ph[idx];
  return bDrives(f) ? f.b.h : f.t.h;
}

static int rampCh(const SeqPhase& f) { return bDrives(f) ? CH_BOTTOM : CH_TOP; }

bool RunEngine::bottomReached() const {
  const Gen& g = g_[CH_BOTTOM];
  return pvOk_[CH_BOTTOM] && !g.idle && fabsf(lastPv_[CH_BOTTOM] - g.to) <= band_;
}

void RunEngine::startPhase(int k) {
  seq_.i = (int16_t)k;
  seq_.el = 0;
  if (k >= p_->nPh) {
    seq_.st = Stage::Done;
    finish();
    return;
  }
  ev(EV_PHASE, -1, k);
  const SeqPhase& f = p_->ph[k];
  switch (f.kind) {
    case PH_B:
      startRamp(CH_BOTTOM, f.b.t, f.b.r, k);
      seq_.st = Stage::Ramp;
      break;
    case PH_T:
      startRamp(CH_TOP, f.t.t, f.t.r, k);
      seq_.st = Stage::Ramp;
      break;
    case PH_BT:
      if (f.tIdle) {  // top off, the bottom ramps and holds (as in B)
        setIdle(CH_TOP);
        startRamp(CH_BOTTOM, f.b.t, f.b.r, k);
        seq_.st = Stage::Ramp;
        break;
      }
      if (f.bIdle) setIdle(CH_BOTTOM);
      else jump(CH_BOTTOM, f.b.t, k);
      if (f.wait && !f.bIdle) {
        seq_.st = Stage::Wait;
      } else {
        startRamp(CH_TOP, f.t.t, f.t.r, k);
        seq_.st = Stage::Ramp;
      }
      break;
    default:
      seq_.st = Stage::Pause;
      progPause_ = true;
      bumpAll();
      ev(EV_PAUSE, -1, k);
      break;
  }
}

// one transition if the current stage is over
bool RunEngine::seqStep() {
  if (seq_.i >= p_->nPh) return false;
  const SeqPhase& f = p_->ph[seq_.i];
  switch (seq_.st) {
    case Stage::Ramp: {
      Gen& g = g_[rampCh(f)];
      if (!genDone(g)) return false;
      g.stageRamp = 0;
      ev(EV_RAMP_END, rampCh(f), seq_.i);
      seq_.st = (bDrives(f) && f.wait) ? Stage::Wait : Stage::Hold;
      seq_.el = 0;
      return true;
    }
    case Stage::Wait:
      if (!forecast_ && !bottomReached()) return false;
      seq_.el = 0;
      if (bDrives(f)) {
        seq_.st = Stage::Hold;
      } else {
        startRamp(CH_TOP, f.t.t, f.t.r, seq_.i);
        seq_.st = Stage::Ramp;
      }
      return true;
    case Stage::Hold:
      if (seq_.el < (float)holdOf(seq_.i) - T_EPS) return false;
      ev(EV_HOLD_END, rampCh(f), seq_.i);
      startPhase(seq_.i + 1);
      return true;
    case Stage::Pause:
      if (!forecast_) return false;
      progPause_ = false;
      ev(EV_CONT, -1, seq_.i);
      startPhase(seq_.i + 1);
      return true;
    default:
      return false;
  }
}

// ------------------------------------------------------------------ Timeline

void RunEngine::updatePause() {
  progPause_ = lane_[0].st == Stage::Pause || lane_[1].st == Stage::Pause;
}

void RunEngine::startStep(int ch, int k) {
  Lane& L = lane_[ch];
  L.i = (int16_t)k;
  L.el = 0;
  L.hold0 = 0;
  if (k >= p_->nSt[ch]) {
    L.st = Stage::Done;  // the channel keeps its last target (or stays idle)
    g_[ch].stageRamp = 0;
    return;
  }
  const TlStep& s = p_->st[ch][k];
  ev(EV_STEP, ch, k);
  switch (s.kind) {
    case ST_TARGET:
      startRamp(ch, s.s.t, s.s.r, k);
      L.st = Stage::Ramp;
      break;
    case ST_IDLE:
      setIdle(ch);
      L.st = Stage::Idle;
      break;
    default:
      L.st = Stage::Pause;
      progPause_ = true;
      bumpAll();
      ev(EV_PAUSE, ch, k);
      break;
  }
}

bool RunEngine::laneStep(int ch) {
  Lane& L = lane_[ch];
  switch (L.st) {
    case Stage::Ramp: {
      Gen& g = g_[ch];
      if (!genDone(g)) return false;
      g.stageRamp = 0;
      ev(EV_RAMP_END, ch, L.i);
      L.st = Stage::Hold;
      L.el = L.hold0;
      return true;
    }
    case Stage::Hold:
    case Stage::Idle:
      if (L.el < (float)p_->st[ch][L.i].s.h - T_EPS) return false;
      ev(EV_HOLD_END, ch, L.i);
      startStep(ch, L.i + 1);
      return true;
    case Stage::Pause:
      if (!forecast_) return false;
      ev(EV_CONT, ch, L.i);
      startStep(ch, L.i + 1);
      updatePause();
      return true;
    default:
      return false;
  }
}

// ------------------------------------------------------------------ time

void RunEngine::settle() {
  for (int guard = 0; guard < GUARD && st_ == RunState::Running; ++guard) {
    bool changed;
    if (p_->type == PROG_SEQ) {
      changed = seqStep();
    } else {
      changed = laneStep(0) | laneStep(1);
      if (lane_[0].st == Stage::Done && lane_[1].st == Stage::Done) {
        finish();
        return;
      }
    }
    if (!changed) return;
  }
}

float RunEngine::timeToEvent() const {
  float ev = T_INF;
  if (p_->type == PROG_SEQ) {
    if (seq_.i >= p_->nPh) return T_INF;
    if (seq_.st == Stage::Ramp) ev = genLeft(g_[rampCh(p_->ph[seq_.i])]);
    else if (seq_.st == Stage::Hold) ev = (float)holdOf(seq_.i) - seq_.el;
  } else {
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      const Lane& L = lane_[ch];
      float e = T_INF;
      if (L.st == Stage::Ramp) e = genLeft(g_[ch]);
      else if (L.st == Stage::Hold || L.st == Stage::Idle) e = (float)p_->st[ch][L.i].s.h - L.el;
      if (e < ev) ev = e;
    }
  }
  return ev < 0 ? 0 : ev;
}

void RunEngine::advance(float d) {
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    Gen& g = g_[ch];
    if (!g.idle && g.dur > 0 && g.el < g.dur && canMove(g)) {
      g.el += d;
      if (g.el > g.dur) g.el = g.dur;
    }
  }
  if (progPause_) return;  // program pause: timers stop
  if (p_->type == PROG_SEQ) {
    if (seq_.st == Stage::Hold || seq_.st == Stage::Wait) seq_.el += d;
  } else {
    for (int ch = 0; ch < NUM_CHANNELS; ++ch)
      if (lane_[ch].st == Stage::Hold || lane_[ch].st == Stage::Idle) lane_[ch].el += d;
  }
}

void RunEngine::begin(DuoProgram* p, const float* pv, const bool* pvOk, float band) {
  const uint32_t g0 = gen_[0], g1 = gen_[1];
  const EvFn efn = evFn_;
  void* const eu = evUser_;
  *this = RunEngine();
  gen_[0] = g0;
  gen_[1] = g1;
  evFn_ = efn;
  evUser_ = eu;
  p_ = p;
  band_ = band;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    pvOk_[ch] = pvOk && pvOk[ch] && pv && !isnan(pv[ch]);
    lastPv_[ch] = pvOk_[ch] ? pv[ch] : 0;
    used_[ch] = progUses(*p, ch);
    setIdle(ch);
  }
  st_ = RunState::Running;
  evT_ = 0;
}

bool RunEngine::start(DuoProgram* p, const float* pv, const bool* pvOk, float band) {
  if (!p || progItems(*p) == 0) return false;
  begin(p, pv, pvOk, band);
  if (p->type == PROG_SEQ) {
    startPhase(0);
  } else {
    startStep(0, 0);
    startStep(1, 0);
    updatePause();
  }
  settle();
  bumpAll();
  return true;
}

bool RunEngine::keptBefore(int ch, int idx, float& t, int& src) const {
  bool has = false;
  if (p_->type == PROG_SEQ) {
    for (int j = 0; j < idx && j < p_->nPh; ++j) {
      const SeqPhase& f = p_->ph[j];
      if (ch == CH_BOTTOM && (f.kind == PH_B || (f.kind == PH_BT && !f.bIdle))) {
        t = f.b.t;
        has = true;
        src = j;
      } else if (ch == CH_BOTTOM && f.kind == PH_BT) {
        has = false;  // idle
      } else if (ch == CH_TOP && (f.kind == PH_T || (f.kind == PH_BT && !f.tIdle))) {
        t = f.t.t;
        has = true;
        src = j;
      } else if (ch == CH_TOP && f.kind == PH_BT) {
        has = false;  // idle
      }
    }
  } else {
    for (int j = 0; j < idx && j < p_->nSt[ch]; ++j) {
      const TlStep& x = p_->st[ch][j];
      if (x.kind == ST_TARGET) {
        t = x.s.t;
        has = true;
        src = j;
      } else if (x.kind == ST_IDLE) {
        has = false;
      }
    }
  }
  return has;
}

bool RunEngine::resumeAt(DuoProgram* p, const RunPos& r, const float* pv, const bool* pvOk, float band, bool edited) {
  if (!p || progItems(*p) == 0) return false;
  if (p->type == PROG_SEQ && (r.i[0] < 0 || r.i[0] >= p->nPh)) return false;
  begin(p, pv, pvOk, band);
  el_ = r.el > 0 ? r.el : 0;
  evT_ = el_;
  edited_ = edited;
  float t;
  int src;
  if (p->type == PROG_SEQ) {
    const int k = r.i[0];
    const SeqPhase& f = p->ph[k];
    // channels the phase starts itself begin at the measured value (idle generator)
    const bool own[NUM_CHANNELS] = {f.kind == PH_B || f.kind == PH_BT,
                                    f.kind == PH_T || (f.kind == PH_BT && (f.tIdle || !(f.wait && !f.bIdle)))};
    for (int ch = 0; ch < NUM_CHANNELS; ++ch)
      if (!own[ch] && keptBefore(ch, k, t, src)) jump(ch, t, src);
    startPhase(k);
    // the phase's ramp starts at the measured value with its planned speed (not in its full time)
    if (seq_.st == Stage::Ramp) {
      const int ch = rampCh(f);
      Gen& g = g_[ch];
      const uint32_t rt = ch == CH_BOTTOM ? f.b.r : f.t.r;
      const float dur = resumedRamp(ch, g.to, plannedRate(ch, k, g.to, rt, r.slope[ch]), (float)rt);
      g.dur = dur;
      g.slope = dur > 0 ? fabsf(g.to - g.from) / dur : 0;
      g.stageRamp = dur > 0;
    }
  } else {
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      Lane& L = lane_[ch];
      const int n = p->nSt[ch];
      const int i = r.i[ch] < 0 ? 0 : r.i[ch];
      const float left = r.left[ch] > 0 ? r.left[ch] : 0;
      if (i >= n) {  // this channel's steps were over: it keeps its last target
        if (keptBefore(ch, n, t, src)) jump(ch, t, src);
        L.i = (int16_t)n;
        L.st = Stage::Done;
        L.el = 0;
        continue;
      }
      const TlStep& x = p->st[ch][i];
      const Stage stg = (Stage)r.st[ch];
      L.i = (int16_t)i;
      L.el = 0;
      L.hold0 = 0;
      if (x.kind == ST_TARGET && stg == Stage::Ramp) {
        // from the measured value with the planned speed; the hold after it takes up the difference,
        // so the later steps keep their run time (both channels stay in step)
        ev(EV_STEP, ch, i);
        const float dur = resumedRamp(ch, x.s.t, plannedRate(ch, i, x.s.t, x.s.r, r.slope[ch]), left);
        startRamp(ch, x.s.t, (uint32_t)lroundf(dur), i);
        L.st = Stage::Ramp;
        L.hold0 = dur - left;
      } else if (x.kind == ST_TARGET && stg == Stage::Hold) {
        jump(ch, x.s.t, i);
        L.st = Stage::Hold;
        L.el = (float)x.s.h - left > 0 ? (float)x.s.h - left : 0;
      } else if (x.kind == ST_IDLE && stg == Stage::Idle) {
        ev(EV_STEP, ch, i);
        L.st = Stage::Idle;
        L.el = (float)x.s.h - left > 0 ? (float)x.s.h - left : 0;
      } else {  // a pause, or a stage that doesn't fit the step: the step from its beginning
        if (keptBefore(ch, i, t, src)) jump(ch, t, src);
        startStep(ch, i);
      }
    }
    updatePause();
  }
  settle();
  bumpAll();
  return true;
}

// Ramp speed of phase / step idx of channel ch (degC/s): from the target before it to this one in its
// ramp time r. Without a target before it (first ramp, from the measured value): the speed the run
// had (saved), 0 = unknown.
float RunEngine::plannedRate(int ch, int idx, float target, uint32_t r, float saved) const {
  float t;
  int src;
  if (r > 0 && keptBefore(ch, idx, t, src) && fabsf(target - t) > 0.05f) return fabsf(target - t) / (float)r;
  return saved > 0 ? saved : 0;
}

// Seconds a resumed ramp needs from the measured value to target at rate (fallback: unknown speed /
// no reading)
float RunEngine::resumedRamp(int ch, float target, float rate, float fallback) const {
  if (!(rate > 0) || !pvOk_[ch] || isnan(lastPv_[ch])) return fallback;
  return fabsf(target - lastPv_[ch]) / rate;
}

RunPos RunEngine::position() const {
  RunPos r;
  memset(&r, 0, sizeof(r));
  r.el = el_;
  if (!p_) return r;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) r.slope[ch] = g_[ch].slope;
  if (p_->type == PROG_SEQ) {
    r.i[0] = seq_.i;
    r.st[0] = (uint8_t)seq_.st;
    return r;
  }
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    const Lane& L = lane_[ch];
    r.i[ch] = L.i;
    r.st[ch] = (uint8_t)L.st;
    if (L.st == Stage::Ramp) r.left[ch] = genLeft(g_[ch]);
    else if ((L.st == Stage::Hold || L.st == Stage::Idle) && L.i < p_->nSt[ch]) r.left[ch] = (float)p_->st[ch][L.i].s.h - L.el;
  }
  return r;
}

void RunEngine::tick(float dt, const float* pv, const bool* pvOk) {
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    pvOk_[ch] = pvOk && pvOk[ch] && pv && !isnan(pv[ch]);
    if (pvOk_[ch]) lastPv_[ch] = pv[ch];
  }
  if (!active() || !(dt > 0)) return;
  const float t0 = el_;
  el_ += dt;
  if (st_ != RunState::Running) return;
  float rem = dt;
  for (int guard = 0; guard < GUARD; ++guard) {
    evT_ = t0 + (dt - rem);
    settle();
    if (st_ != RunState::Running || rem <= 0) break;
    if (progPause_) {  // only free ramps (edited kept targets) move
      advance(rem);
      break;
    }
    const float ev = timeToEvent();
    const float d = rem < ev ? rem : ev;
    advance(d);
    rem -= d;
  }
}

void RunEngine::pause() {
  if (st_ != RunState::Running) return;
  st_ = RunState::Paused;
  bumpAll();
}

void RunEngine::resume() {
  if (st_ != RunState::Paused) return;
  st_ = RunState::Running;
  evT_ = el_;
  bumpAll();
  settle();
}

void RunEngine::stop() {
  if (!active()) return;
  freeze();
  st_ = RunState::Aborted;
  evT_ = el_;
  ev(EV_STOP, -1, -1);
}

bool RunEngine::cont() {
  if (!active() || !progPause_) return false;
  progPause_ = false;
  if (!forecast_) evT_ = el_;
  if (p_->type == PROG_SEQ) {
    ev(EV_CONT, -1, seq_.i);
    startPhase(seq_.i + 1);
  } else {
    for (int ch = 0; ch < NUM_CHANNELS; ++ch)
      if (lane_[ch].st == Stage::Pause) {
        ev(EV_CONT, ch, lane_[ch].i);
        startStep(ch, lane_[ch].i + 1);
      }
    updatePause();
  }
  bumpAll();
  settle();
  return true;
}

bool RunEngine::next() {
  if (!active()) return false;
  if (progPause_) return cont();
  if (p_->type != PROG_SEQ || seq_.i >= p_->nPh) return false;
  const SeqPhase& f = p_->ph[seq_.i];
  evT_ = el_;
  switch (seq_.st) {
    case Stage::Ramp: {  // straight to the hold with the setpoint at the target (a wait is skipped)
      const int c = rampCh(f);
      Gen& g = g_[c];
      g.el = g.dur;
      g.stageRamp = 0;
      bump(c);
      ev(EV_RAMP_END, c, seq_.i);
      seq_.st = Stage::Hold;
      seq_.el = 0;
      break;
    }
    case Stage::Wait:
      seq_.el = 0;
      if (bDrives(f)) {
        seq_.st = Stage::Hold;
      } else {
        startRamp(CH_TOP, f.t.t, f.t.r, seq_.i);
        seq_.st = Stage::Ramp;
      }
      break;
    case Stage::Hold:
      ev(EV_HOLD_END, rampCh(f), seq_.i);
      startPhase(seq_.i + 1);
      break;
    default:
      return false;
  }
  settle();
  return true;
}

// ------------------------------------------------------------------ status

bool RunEngine::pauseMsg() const {
  if (!progPaused()) return false;
  if (p_->type == PROG_SEQ) return seq_.i < p_->nPh && p_->ph[seq_.i].msg;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch)
    if (lane_[ch].st == Stage::Pause && p_->st[ch][lane_[ch].i].msg) return true;
  return false;
}

const char* RunEngine::pauseText() const {
  if (!progPaused()) return "";
  if (p_->type == PROG_SEQ) return seq_.i < p_->nPh ? progText(*p_, p_->ph[seq_.i].text) : "";
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    if (lane_[ch].st != Stage::Pause) continue;
    const char* t = progText(*p_, p_->st[ch][lane_[ch].i].text);
    if (*t) return t;
  }
  return "";
}

bool RunEngine::waiting() const { return active() && p_->type == PROG_SEQ && seq_.st == Stage::Wait; }

StageInfo RunEngine::seq() const {
  StageInfo s = {seq_.i, seq_.st, 0, 0};
  if (!p_ || p_->type != PROG_SEQ || seq_.i >= p_->nPh) return s;
  if (seq_.st == Stage::Ramp) {
    const Gen& g = g_[rampCh(p_->ph[seq_.i])];
    s.el = g.pre + g.el;
    s.dur = g.pre + g.dur;
  } else if (seq_.st == Stage::Hold) {
    s.el = seq_.el;
    s.dur = (float)holdOf(seq_.i);
  } else if (seq_.st == Stage::Wait) {
    s.el = seq_.el;
  }
  return s;
}

StageInfo RunEngine::lane(int ch) const {
  StageInfo s = {0, Stage::None, 0, 0};
  if (!p_ || p_->type != PROG_TL || ch < 0 || ch >= NUM_CHANNELS) return s;
  const Lane& L = lane_[ch];
  s.idx = L.i;
  s.st = L.st;
  if (L.st == Stage::Ramp) {
    s.el = g_[ch].pre + g_[ch].el;
    s.dur = g_[ch].pre + g_[ch].dur;
  } else if (L.st == Stage::Hold || L.st == Stage::Idle) {  // the hold as it is (resume may change it)
    s.el = L.el - L.hold0;
    s.dur = (float)p_->st[ch][L.i].s.h - L.hold0;
    if (s.dur < 0) s.dur = 0;
  }
  return s;
}

ChOut RunEngine::out(int ch) const {
  ChOut o;
  memset(&o, 0, sizeof(o));
  o.sp = o.target = NAN;
  if (ch < 0 || ch >= NUM_CHANNELS) return o;
  o.gen = gen_[ch];
  if (st_ == RunState::Idle) {
    o.idle = true;
    return o;
  }
  const Gen& g = g_[ch];
  o.inProgram = used_[ch];
  o.idle = g.idle;
  if (g.idle) return o;
  o.sp = sp(g);
  o.target = g.to;
  o.moving = active() && !genDone(g) && canMove(g);
  o.rampLeft = o.moving ? genLeft(g) : 0;
  return o;
}

ChState RunEngine::chState(int ch) const {
  if (!active() || ch < 0 || ch >= NUM_CHANNELS || !used_[ch]) return ChState::None;
  if (st_ == RunState::Paused) return ChState::Pause;
  if (progPause_) return ChState::Halt;
  const Gen& g = g_[ch];
  if (p_->type == PROG_TL) {
    switch (lane_[ch].st) {
      case Stage::Ramp: return ChState::Ramp;
      case Stage::Hold: return ChState::Hold;
      case Stage::Idle: return ChState::Off;
      case Stage::Done: return g.idle ? ChState::Off : ChState::Done;
      default: return g.idle ? ChState::Off : ChState::Keep;
    }
  }
  if (g.idle) return ChState::Off;
  if (seq_.i >= p_->nPh) return ChState::Keep;
  const SeqPhase& f = p_->ph[seq_.i];
  const int tc = rampCh(f);
  if (seq_.st == Stage::Ramp && tc == ch) return ChState::Ramp;
  if (seq_.st == Stage::Hold && tc == ch) return ChState::Hold;
  if (seq_.st == Stage::Wait && ch == CH_BOTTOM) return ChState::Wait;
  if (f.kind == PH_BT && !f.tIdle && ch == CH_BOTTOM && g.src == seq_.i) return ChState::Jump;
  return ChState::Keep;
}

bool RunEngine::canExtend(int ch) const {
  if (!active() || ch < 0 || ch >= NUM_CHANNELS) return false;
  if (p_->type == PROG_TL) return lane_[ch].st == Stage::Hold || lane_[ch].st == Stage::Idle;
  return seq_.st == Stage::Hold && seq_.i < p_->nPh && rampCh(p_->ph[seq_.i]) == ch;
}

int RunEngine::keptIdx(int ch) const {
  if (!active() || ch < 0 || ch >= NUM_CHANNELS || g_[ch].idle) return -1;
  return g_[ch].src;
}

// ------------------------------------------------------------------ run edits

// a target whose ramp has not started yet
bool RunEngine::futureTarget(int ch, int idx) const {
  if (p_->type == PROG_SEQ) {
    if (idx > seq_.i) return true;
    return idx == seq_.i && ch == CH_TOP && p_->ph[idx].kind == PH_BT && !p_->ph[idx].tIdle && seq_.st == Stage::Wait;
  }
  return lane_[ch].st != Stage::Done && idx > lane_[ch].i;
}

// target of ch before phase/step idx, if its ramp starts from there (not after Idle / at the start)
bool RunEngine::prevTarget(int ch, int idx, float& prev) const {
  for (int k = idx - 1; k >= 0; --k) {
    if (p_->type == PROG_TL) {
      const TlStep& s = p_->st[ch][k];
      if (s.kind == ST_PAUSE) continue;
      if (s.kind == ST_IDLE) return false;
      prev = s.s.t;
      return true;
    }
    const SeqPhase& f = p_->ph[k];
    if (ch == CH_BOTTOM && f.kind == PH_BT && f.bIdle) return false;
    if (ch == CH_TOP && f.kind == PH_BT && f.tIdle) return false;
    const Seg* s = progSeg(*p_, ch, k);
    if (s) {
      prev = s->t;
      return true;
    }
  }
  return false;
}

// the next phase/step of ch with a ramp that starts from idx's target, -1 = none
int RunEngine::nextRamp(int ch, int idx) const {
  const int n = p_->type == PROG_TL ? p_->nSt[ch] : p_->nPh;
  for (int k = idx + 1; k < n; ++k) {
    if (p_->type == PROG_TL) {
      const TlStep& s = p_->st[ch][k];
      if (s.kind == ST_PAUSE) continue;
      return s.kind == ST_TARGET ? k : -1;
    }
    const SeqPhase& f = p_->ph[k];
    if (f.kind == PH_P) continue;
    if (ch == CH_BOTTOM) {
      if (bDrives(f)) return k;
      if (f.kind == PH_BT) return -1;  // jump or Idle: no ramp
    } else if (f.kind == PH_T || (f.kind == PH_BT && !f.tIdle)) {
      return k;
    } else if (f.kind == PH_BT) {
      return -1;  // Idle
    }
  }
  return -1;
}

// Timeline: seconds from now to the end of step idx's ramp (rampEnd) or hold
float RunEngine::laneEnd(int ch, int idx, bool rampEnd) const {
  const Lane& L = lane_[ch];
  float t = 0;
  for (int k = L.i; k < p_->nSt[ch]; ++k) {
    const TlStep& s = p_->st[ch][k];
    if (k == L.i) {
      if (L.st == Stage::Ramp) t += genLeft(g_[ch]);
      if (k == idx && rampEnd) return t;
      if (L.st == Stage::Ramp) t += fmaxf((float)s.s.h - L.hold0, 0);
      else if (L.st == Stage::Hold || L.st == Stage::Idle) t += (float)s.s.h - L.el;
    } else if (s.kind == ST_TARGET) {
      t += (float)s.s.r;
      if (k == idx && rampEnd) return t;
      t += (float)s.s.h;
    } else if (s.kind == ST_IDLE) {
      t += (float)s.s.h;
    }
    if (k == idx) return t;
  }
  return t;
}

// Timeline (spec 9.3.4): a timing change of one channel shifts the other one
// through its holds/idles at or after `moment` (seconds from now); its ramps
// are never changed. No hold left: nothing is shifted.
void RunEngine::shiftOther(int ch, float delta, float moment) {
  const int o = 1 - ch;
  const Lane& L = lane_[o];
  if (p_->type != PROG_TL || L.st == Stage::Done || fabsf(delta) < 0.5f) return;
  float need = fabsf(delta);
  float t = 0;
  for (int k = L.i; k < p_->nSt[o] && need > 0; ++k) {
    TlStep& s = p_->st[o][k];
    float start, end;
    if (s.kind == ST_PAUSE) continue;
    if (k == L.i) {
      if (L.st == Stage::Pause) continue;
      if (L.st == Stage::Ramp) t += genLeft(g_[o]);
      start = (L.st == Stage::Hold || L.st == Stage::Idle) ? -L.el : t - L.hold0;
      end = start + (float)s.s.h;
    } else {
      if (s.kind == ST_TARGET) t += (float)s.s.r;
      start = t;
      end = t + (float)s.s.h;
    }
    t = end;
    if (end < moment - T_EPS) continue;
    if (delta > 0) {
      s.s.h += (uint32_t)lroundf(delta);
      return;
    }
    float from = start;
    if (from < moment) from = moment;
    if (from < 0) from = 0;
    float avail = end - from;
    if (avail > need) avail = need;
    const uint32_t cut = (uint32_t)lroundf(avail);
    s.s.h = s.s.h > cut ? s.s.h - cut : 0;
    need -= avail;
  }
}

bool RunEngine::setHold(int ch, int idx, float sec, char* err, size_t n) {
  if (!active()) return fail(err, n, "No program is running");
  evT_ = el_;
  if (!(sec >= 0)) sec = 0;
  if (sec > (float)PROG_MAX_SEC) sec = (float)PROG_MAX_SEC;
  if (p_->type == PROG_SEQ) {
    if (idx < 0) idx = seq_.i;
    if (idx >= p_->nPh || p_->ph[idx].kind == PH_P) return fail(err, n, "Phase %d has no hold", idx + 1);
    if (idx < seq_.i) return fail(err, n, "Phase %d is finished", idx + 1);
    SeqPhase& f = p_->ph[idx];
    Seg& s = bDrives(f) ? f.b : f.t;
    uint32_t h = (uint32_t)lroundf(sec);
    if (idx == seq_.i && seq_.st == Stage::Hold && sec <= seq_.el) h = (uint32_t)seq_.el;  // ends now
    s.h = h;
    edited_ = true;
    settle();
    return true;
  }
  if (ch < 0 || ch >= NUM_CHANNELS) return fail(err, n, "Channel missing");
  const Lane& L = lane_[ch];
  if (idx < 0) idx = L.i;
  if (L.st == Stage::Done || idx < L.i) return fail(err, n, "Step %d is finished", idx + 1);
  if (idx >= p_->nSt[ch] || p_->st[ch][idx].kind == ST_PAUSE) return fail(err, n, "Step %d has no hold", idx + 1);
  Seg& s = p_->st[ch][idx].s;
  uint32_t h = (uint32_t)lroundf(sec);
  if (idx == L.i && (L.st == Stage::Hold || L.st == Stage::Idle) && sec <= L.el) h = (uint32_t)L.el;
  const float moment = laneEnd(ch, idx, false);
  const float delta = (float)h - (float)s.h;
  s.h = h;
  edited_ = true;
  shiftOther(ch, delta, moment);
  settle();
  return true;
}

bool RunEngine::extendHold(int ch, float sec, char* err, size_t n) {
  if (!active()) return fail(err, n, "No program is running");
  if (p_->type == PROG_SEQ) {
    if (seq_.st != Stage::Hold) return fail(err, n, "No hold is running");
    return setHold(ch, seq_.i, (float)holdOf(seq_.i) + sec, err, n);
  }
  if (ch < 0 || ch >= NUM_CHANNELS) return fail(err, n, "Channel missing");
  const Lane& L = lane_[ch];
  if (L.st != Stage::Hold && L.st != Stage::Idle) return fail(err, n, "No hold is running on this channel");
  return setHold(ch, L.i, (float)p_->st[ch][L.i].s.h + sec, err, n);
}

bool RunEngine::setRamp(int ch, int idx, float sec, char* err, size_t n) {
  if (!active()) return fail(err, n, "No program is running");
  if (ch < 0 || ch >= NUM_CHANNELS) return fail(err, n, "Channel missing");
  Seg* s = progSeg(*p_, ch, idx);
  if (!s || (p_->type == PROG_SEQ && ch == CH_BOTTOM && p_->ph[idx].kind == PH_BT && !p_->ph[idx].tIdle))
    return fail(err, n, "%s %d has no ramp for this channel", p_->type == PROG_TL ? "Step" : "Phase", idx + 1);
  if (!futureTarget(ch, idx)) return fail(err, n, "The ramp has started already (only later ramps can be changed)");
  if (!(sec >= 0)) sec = 0;
  if (sec > (float)PROG_MAX_SEC) sec = (float)PROG_MAX_SEC;
  const uint32_t r = (uint32_t)lroundf(sec);
  if (p_->type == PROG_TL) shiftOther(ch, (float)r - (float)s->r, laneEnd(ch, idx, true));
  s->r = r;
  edited_ = true;
  return true;
}

bool RunEngine::setTarget(int ch, int idx, float t, char* err, size_t n) {
  if (!active()) return fail(err, n, "No program is running");
  evT_ = el_;
  if (ch < 0 || ch >= NUM_CHANNELS) return fail(err, n, "Channel missing");
  if (!isfinite(t)) return fail(err, n, "Target is not a number");
  if (idx < 0) idx = keptIdx(ch);
  if (idx < 0) return fail(err, n, "The channel has no target now (idle)");
  const char* what = p_->type == PROG_TL ? "Step" : "Phase";
  Seg* s = progSeg(*p_, ch, idx);
  if (!s) return fail(err, n, "%s %d has no target for this channel", what, idx + 1);
  Gen& g = g_[ch];
  const bool here = !g.idle && g.src == idx;  // the setpoint comes from this phase/step (ramping, holding, kept)
  if (!here && !futureTarget(ch, idx)) return fail(err, n, "%s %d is finished", what, idx + 1);
  const float old = s->t;
  if (old == t) return true;
  const bool tl = p_->type == PROG_TL;
  const int nx = nextRamp(ch, idx);
  const float nextMoment = (tl && nx >= 0) ? laneEnd(ch, nx, true) : 0;
  float d1 = 0;
  if (here) {
    const float cur = sp(g);
    if (g.stageRamp && !genDone(g)) {
      // ramping now: continue from the current setpoint with the same slope
      const float oldLeft = genLeft(g);
      const float dur = g.slope > 0 ? fabsf(t - cur) / g.slope : 0;
      g.pre += g.el;
      g.from = cur;
      g.to = t;
      g.el = 0;
      g.dur = dur;
      s->r = (uint32_t)lroundf(g.pre + dur);
      d1 = dur - oldLeft;
      if (tl) shiftOther(ch, d1, oldLeft);
    } else {
      // holding / kept: move there with the slope that set it (a jump if none); the hold keeps running
      g.from = cur;
      g.to = t;
      g.el = g.pre = 0;
      g.dur = g.slope > 0 ? fabsf(t - cur) / g.slope : 0;
      g.stageRamp = 0;
    }
    bump(ch);
  } else {
    // later phase/step: its ramp keeps its slope (if it starts from a known target)
    float prev;
    if (s->r > 0 && prevTarget(ch, idx, prev) && prev != old) {
      const float slope = fabsf(old - prev) / (float)s->r;
      const uint32_t r = (uint32_t)lroundf(fabsf(t - prev) / slope);
      d1 = (float)r - (float)s->r;
      if (tl) shiftOther(ch, d1, laneEnd(ch, idx, true));
      s->r = r;
    }
  }
  s->t = t;
  // the ramp that follows keeps its slope, its time adapts
  if (nx >= 0) {
    Seg* q = progSeg(*p_, ch, nx);
    if (q && q->r > 0 && q->t != old) {
      const float slope = fabsf(q->t - old) / (float)q->r;
      const uint32_t r = (uint32_t)lroundf(fabsf(q->t - t) / slope);
      const float d2 = (float)r - (float)q->r;
      q->r = r;
      if (tl) shiftOther(ch, d2, nextMoment + d1);
    }
  }
  edited_ = true;
  settle();
  return true;
}

// ------------------------------------------------------------------ plan

void RunEngine::emit(PlanFn fn, void* user, float t, float d) const {
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    if (!used_[ch]) continue;
    const Gen& g = g_[ch];
    PlanSeg s;
    s.ch = (uint8_t)ch;
    s.idle = g.idle;
    s.ramp = false;
    s.src = g.idle ? -1 : g.src;
    s.t0 = t;
    s.t1 = t + d;
    if (g.idle) {
      s.v0 = s.v1 = NAN;
      fn(s, user);
      continue;
    }
    s.v0 = sp(g);
    const float left = canMove(g) ? genLeft(g) : 0;
    if (left <= 0) {
      s.v1 = s.v0;
    } else if (left < d) {
      s.t1 = t + left;
      s.v1 = g.to;
      s.ramp = true;
      fn(s, user);
      s.ramp = false;
      s.t0 = s.t1;
      s.t1 = t + d;
      s.v0 = g.to;
      s.v1 = g.to;
    } else {
      s.ramp = true;
      s.v1 = g.from + (g.to - g.from) * ((g.el + d) / g.dur);
    }
    fn(s, user);
  }
}

float RunEngine::forecast(PlanFn fn, void* user, const float* pv, EvFn efn, void* euser) const {
  if (!active()) return 0;
  RunEngine s = *this;
  s.forecast_ = true;
  s.evFn_ = efn;
  s.evUser_ = euser;
  s.evT_ = 0;
  if (pv)
    for (int ch = 0; ch < NUM_CHANNELS; ++ch)
      if (!isnan(pv[ch])) {
        s.lastPv_[ch] = pv[ch];
        s.pvOk_[ch] = true;
      }
  s.st_ = RunState::Running;
  if (s.progPause_) s.cont();
  s.settle();
  float t = 0;
  for (int guard = 0; guard < 4 * GUARD && s.st_ == RunState::Running; ++guard) {
    const float ev = s.timeToEvent();
    if (ev >= T_INF) break;
    if (ev > 0) {
      if (fn) s.emit(fn, user, t, ev);
      s.advance(ev);
      t += ev;
    }
    s.evT_ = t;
    s.settle();
  }
  return t;
}

float programPreview(const DuoProgram& p, const float* pv, PlanFn fn, void* user) {
  RunEngine e;
  bool ok[NUM_CHANNELS];
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) ok[ch] = pv && !isnan(pv[ch]);
  static const float none[NUM_CHANNELS] = {0, 0};
  if (!e.start(const_cast<DuoProgram*>(&p), pv ? pv : none, ok, 5)) return 0;
  return e.forecast(fn, user, pv);
}
