// PRIVATE to app_state.cpp, app_hist.cpp and app_log.cpp (one module split into three files: the control
// task, the chart history, the event log). Only what more than one of them needs; other modules include
// app_state.h, never this file.
#pragma once
#include "app_state.h"
#include "raw_hist.h"

// ---- app_state.cpp (the control task's state the history reads)
extern ChannelSlot slots[NUM_CHANNELS];  // rawRecord(): which channels are off
extern RunEngine eng;                    // updatePlan(): the run's forecast
extern DuoProgram* runProg;              // updatePlan(): the run's name
extern RunEngine gEngSnap;               // chartJson(): copy of the engine for the web (under gLock)

// ---- app_hist.cpp
template <typename T>
static void fillNan(T* a, int n) {
  for (int i = 0; i < n; ++i) a[i] = NAN;
}

// A slot is 3 words (32-bit access only: the ring may be in IRAM):
//   w0 = PV bottom | PV top << 16, w1 = SP bottom | SP top << 16  (int16 0.1 degC, INT16_MIN = none)
//   w2 = one byte each: how far the lowest / highest reading lay below / above PV, bottom then top
//        (0.1 degC up to 19.9 degC, then 2 degC steps up to 130 degC, rounded outwards)
// With the monitor channel (HAS_MONITOR) a 4th word: its PV | lowest byte << 16 | highest byte << 24
// (it has no setpoint).
static constexpr int SLOT_W = 3 + HAS_MONITOR;
static_assert(NUM_CHANNELS == 2, "the live history packs two channels per word");

extern RawRing rawRing[HIST_CH];  // allocated in appInit()
extern RunAnchors runAnch;        // run time against millis, tracked by the control cycle
void histResetLive();
void livePut(const float* pv, const bool* ok, const float* sp, const float* lo, const float* hi);
void rawRecord(const float* pv, const bool* pvOk, const uint32_t* lastOk);
void histStartRun(float total);
float updatePlan(const float* pv, float& remaining);

// ---- app_log.cpp
extern volatile float logRunT;  // run time of new entries (NAN: no run), set by the control cycle
