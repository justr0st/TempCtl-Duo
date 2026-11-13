// Run report figures: fed once per control cycle while a run is running (not during a user
// pause), read at its end. Pure C++, header-only, host-tested (test/host_test.cpp).
//   per channel (bottom, top): highest PV and the run time it happened; largest |PV - SP| while the channel
//     holds (ChState::Hold), counted from HOLD_SKIP s into the hold on (the end of the ramp, the overshoot
//     right after it, does not count); a setpoint that moves during a hold (a target edit) starts the count
//     again; steepest rise in degC/s over a RATE_WIN s window (one sample per run-time second; windows with
//     an invalid PV, or stretched by a user pause, are skipped)
//   monitor channel (index 2): highest PV and its time
// NaN where nothing was measured.
#pragma once
#include <math.h>
#include "run_engine.h"  // ChState, NUM_CHANNELS

constexpr int RS_N = NUM_CHANNELS + 1;  // the channels, then the monitor
constexpr int RS_MON = NUM_CHANNELS;

class RunStats {
 public:
  static constexpr float HOLD_SKIP = 5;      // s of each hold not counted
  static constexpr int RATE_WIN = 5;         // s: the rise is measured over this
  static constexpr float RATE_MAX_SPAN = 8;  // a window longer than this (a user pause in it) is skipped

  RunStats() { reset(); }

  void reset() {
    for (int i = 0; i < RS_N; ++i) peak[i] = peakT[i] = NAN;
    for (int c = 0; c < NUM_CHANNELS; ++c) {
      dev[c] = rate[c] = NAN;
      inHold_[c] = false;
      holdT0_[c] = 0;
      holdSp_[c] = NAN;
    }
    n_ = head_ = 0;
    lastSec_ = -1;
  }

  // t = run time (s); pv / pvOk / sp / st: the channels (sp = the program setpoint, NaN = none);
  // mon / monOk: the monitor channel (monOk false when the board has none)
  void update(float t, const float* pv, const bool* pvOk, const float* sp, const ChState* st, float mon, bool monOk) {
    for (int c = 0; c < NUM_CHANNELS; ++c) {
      const bool ok = pvOk[c] && !isnan(pv[c]);
      if (ok) peakAt(c, pv[c], t);
      const bool hold = st[c] == ChState::Hold && !isnan(sp[c]);
      if (hold && (!inHold_[c] || fabsf(sp[c] - holdSp_[c]) > 0.01f)) holdT0_[c] = t;  // new hold / moved setpoint
      inHold_[c] = hold;
      holdSp_[c] = hold ? sp[c] : NAN;
      if (hold && ok && t - holdT0_[c] >= HOLD_SKIP) {
        const float d = fabsf(pv[c] - sp[c]);
        if (isnan(dev[c]) || d > dev[c]) dev[c] = d;
      }
    }
    if (monOk && !isnan(mon)) peakAt(RS_MON, mon, t);
    // rise: one sample per run-time second
    const long sec = t >= 0 ? (long)t : 0;
    if (sec == lastSec_) return;
    lastSec_ = sec;
    T_[head_] = t;
    for (int c = 0; c < NUM_CHANNELS; ++c) P_[c][head_] = (pvOk[c] && !isnan(pv[c])) ? pv[c] : NAN;
    const int newest = head_;
    head_ = (head_ + 1) % RING;
    if (n_ < RING) ++n_;
    if (n_ < RING) return;
    const int oldest = head_;  // (the slot written next)
    const float span = T_[newest] - T_[oldest];
    if (!(span > 0) || span > RATE_MAX_SPAN) return;
    for (int c = 0; c < NUM_CHANNELS; ++c) {
      bool all = true;
      for (int k = 0; k < RING; ++k) all &= !isnan(P_[c][k]);
      if (!all) continue;
      const float r = (P_[c][newest] - P_[c][oldest]) / span;
      if (isnan(rate[c]) || r > rate[c]) rate[c] = r;
    }
  }

  float peak[RS_N], peakT[RS_N];  // highest PV and its run time
  float dev[NUM_CHANNELS];        // largest |PV - SP| in the holds
  float rate[NUM_CHANNELS];       // steepest rise, degC/s

 private:
  static constexpr int RING = RATE_WIN + 1;
  void peakAt(int i, float v, float t) {
    if (isnan(peak[i]) || v > peak[i]) {
      peak[i] = v;
      peakT[i] = t;
    }
  }
  bool inHold_[NUM_CHANNELS];
  float holdT0_[NUM_CHANNELS], holdSp_[NUM_CHANNELS];
  float T_[RING];
  float P_[NUM_CHANNELS][RING];
  int n_, head_;
  long lastSec_;
};
