#include "program.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

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

float programTotal(const Program& p) {
  float t = 0;
  for (int i = 0; i < p.count && i < MAX_STEPS; ++i) t += (float)p.s[i].rampSec + (float)p.s[i].holdSec;
  return t;
}

namespace {
struct Flat {
  Program* p;
  int ch;
  float lastV;      // value at the end of the last segment (start value before the first target)
  float lastSlope;  // slope of the last step if it is a ramp without hold, else NaN
};

// Segments come in pieces (split at the other channel's events): merge them.
void addTime(Flat& f, bool ramp, float target, float sec, float slope) {
  Program& p = *f.p;
  Step* last = p.count > 0 ? &p.s[p.count - 1] : nullptr;
  if (!ramp && last && last->target == target) {
    last->holdSec = (uint32_t)lroundf((float)last->holdSec + sec);
    f.lastSlope = NAN;
    return;
  }
  if (ramp && last && !isnan(f.lastSlope) && fabsf(slope - f.lastSlope) < 1e-3f) {
    last->target = target;
    last->rampSec = (uint32_t)lroundf((float)last->rampSec + sec);
    return;
  }
  if (p.count >= MAX_STEPS) return;
  const uint32_t s = (uint32_t)lroundf(sec);
  p.s[p.count++] = Step{target, ramp ? s : 0, ramp ? 0 : s};
  f.lastSlope = ramp ? slope : NAN;
}

void flatSeg(const PlanSeg& s, void* user) {
  Flat& f = *(Flat*)user;
  if (s.ch != f.ch || s.t1 <= s.t0) return;
  const float d = s.t1 - s.t0;
  if (s.idle) {  // no Idle in TempCtl steps: hold the previous value
    addTime(f, false, f.lastV, d, 0);
    return;
  }
  if (s.v1 != s.v0) addTime(f, true, s.v1, d, (s.v1 - s.v0) / d);
  else addTime(f, false, s.v1, d, 0);
  f.lastV = s.v1;
}
}  // namespace

void programLegacyView(const DuoProgram& d, Program& out) {
  programClear(out, d.name);
  static const float START = 25;  // first ramps start at room temperature in this view
  const float pv[NUM_CHANNELS] = {START, START};
  Flat f{&out, progUses(d, CH_BOTTOM) ? CH_BOTTOM : CH_TOP, START, NAN};
  programPreview(d, pv, flatSeg, &f);
}
