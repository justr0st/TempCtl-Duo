// Chart history: the live ring, the run's chart rows, every reading (raw_hist.h), the plan
// preview / forecast curves, chartSnapshot() for the touch chart, /api/chart (chartJson) and the CSV of the
// live history. Split from app_state.cpp (the control task calls in through app_internal.h).
#include "app_internal.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include "program_store.h"

float runSpanFor(float total) {
  if (total <= 0) return 60;
  float spare = total * 0.1f;
  if (spare < 30) spare = 30;
  return total + spare;
}

void histResetLive() {
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    fillNan(gHist->plan[ch], CHART_N);
    fillNan(gHist->runPlan[ch], CHART_N);
  }
  gHist->runValid = false;
}

// ---- live history ring
// (the slot layout: SLOT_W in app_internal.h)

static int16_t t16(float v) {
  Tenth16 t;
  t = v;
  return t.v;
}
static uint8_t dEnc(int32_t d) {
  if (d <= 0) return 0;
  if (d < 200) return (uint8_t)d;
  const int32_t c = 200 + (d - 200 + 19) / 20;
  return c > 255 ? 255 : (uint8_t)c;
}
static int32_t dDec(uint8_t c) { return c < 200 ? c : 200 + (c - 200) * 20; }

struct LiveSlot {
  int16_t pv[HIST_CH], sp[HIST_CH];
  int32_t lo[HIST_CH], hi[HIST_CH];  // tenths; INT32_MIN with pv
};

// arrays of HIST_CH (the channels, then the monitor; its sp is ignored)
void livePut(const float* pv, const bool* ok, const float* sp, const float* lo, const float* hi) {
  if (!gHist->liveRing) return;
  int16_t p[HIST_CH], q[NUM_CHANNELS];
  uint8_t dl[HIST_CH] = {}, dh[HIST_CH] = {};
  for (int c = 0; c < HIST_CH; ++c) {
    p[c] = ok[c] ? t16(pv[c]) : (int16_t)INT16_MIN;
    if (c < NUM_CHANNELS) q[c] = t16(sp[c]);
    if (p[c] != INT16_MIN) {
      const int32_t l = isnan(lo[c]) ? p[c] : (int32_t)floorf(lo[c] * 10.0f);
      const int32_t h = isnan(hi[c]) ? p[c] : (int32_t)ceilf(hi[c] * 10.0f);
      dl[c] = dEnc(p[c] - l);
      dh[c] = dEnc(h - p[c]);
    }
  }
  uint32_t* w = gHist->liveRing + (gHist->liveTotal % gHist->liveCap) * SLOT_W;
  w[0] = (uint16_t)p[0] | (uint32_t)(uint16_t)p[1] << 16;
  w[1] = (uint16_t)q[0] | (uint32_t)(uint16_t)q[1] << 16;
  w[2] = dl[0] | (uint32_t)dh[0] << 8 | (uint32_t)dl[1] << 16 | (uint32_t)dh[1] << 24;
#if HAS_MONITOR
  w[3] = (uint16_t)p[CH_MON] | (uint32_t)dl[CH_MON] << 16 | (uint32_t)dh[CH_MON] << 24;
#endif
  ++gHist->liveTotal;
}

static void liveGet(uint32_t s, LiveSlot& o) {
  const uint32_t* w = gHist->liveRing + (s % gHist->liveCap) * SLOT_W;
  const uint32_t a = w[0], b = w[1], d = w[2];
  for (int c = 0; c < NUM_CHANNELS; ++c) {
    const int16_t p = (int16_t)(a >> (16 * c)), q = (int16_t)(b >> (16 * c));
    o.pv[c] = p;
    o.sp[c] = q;
    if (p == INT16_MIN) {
      o.lo[c] = o.hi[c] = INT32_MIN;
      continue;
    }
    o.lo[c] = p - dDec((uint8_t)(d >> (16 * c)));
    o.hi[c] = p + dDec((uint8_t)(d >> (16 * c + 8)));
  }
#if HAS_MONITOR
  const uint32_t m = w[3];
  const int16_t p = (int16_t)(uint16_t)m;
  o.pv[CH_MON] = p;
  o.sp[CH_MON] = INT16_MIN;
  if (p == INT16_MIN) {
    o.lo[CH_MON] = o.hi[CH_MON] = INT32_MIN;
  } else {
    o.lo[CH_MON] = p - dDec((uint8_t)(m >> 16));
    o.hi[CH_MON] = p + dDec((uint8_t)(m >> 24));
  }
#endif
}

static uint32_t liveOldest() { return gHist->liveTotal > gHist->liveCap ? gHist->liveTotal - gHist->liveCap : 0; }

uint32_t liveHistSec() { return gHist ? gHist->liveCap * LIVE_SAMPLE_SEC : 0; }

const uint16_t LIVE_PERIODS[LIVE_PERIODS_N] = {1, 5, 10, 15, 30, 60, 120, 240, 480, 720, 1440};

uint32_t liveMinEff() {
  const uint32_t cap = liveHistSec() / 60;
  uint32_t m = LIVE_PERIODS[0];
  for (uint16_t p : LIVE_PERIODS)
    if (p <= cap && p <= gSettings.liveMin) m = p;
  return m;
}

// Slots per bin of the live view: the period in effect over CHART_N points (at least one slot; a
// period shorter than CHART_N slots shows only the newest points)
static uint32_t liveK() {
  const uint32_t k = liveMinEff() * 60 / LIVE_SAMPLE_SEC / CHART_N;
  return k ? k : 1;
}
static uint32_t liveSpan() { return liveMinEff() * 60; }

// The live view: CHART_N bins of k slots, aligned to the slot count so that a bin doesn't change
// once it is complete (the newest is partial). Bin b (counted since boot) covers the slots b*k ..
// b*k+k-1 that are still kept: PV and SP of the newest reading, lowest and highest reading, 0.1 degC
// (INT32_MIN = none). Under gLock.
static void liveBin(uint32_t k, int64_t b, int32_t* pv, int32_t* sp, int32_t* lo, int32_t* hi) {
  const uint32_t tot = gHist->liveTotal, oldest = liveOldest();
  for (int c = 0; c < HIST_CH; ++c) pv[c] = sp[c] = lo[c] = hi[c] = INT32_MIN;
  if (!tot || b < 0) return;
  int64_t s0 = b * k, s1 = s0 + k;
  if (s1 > tot) s1 = tot;
  if (s0 < oldest) s0 = oldest;
  LiveSlot x;
  for (int64_t sl = s0; sl < s1; ++sl) {
    liveGet((uint32_t)sl, x);
    for (int c = 0; c < HIST_CH; ++c) {
      if (x.sp[c] != INT16_MIN) sp[c] = x.sp[c];
      if (x.pv[c] == INT16_MIN) continue;
      pv[c] = x.pv[c];
      if (lo[c] == INT32_MIN || x.lo[c] < lo[c]) lo[c] = x.lo[c];
      if (hi[c] == INT32_MIN || x.hi[c] > hi[c]) hi[c] = x.hi[c];
    }
  }
}

// the newest bin
static int64_t liveLastBin(uint32_t k) { return gHist->liveTotal ? (int64_t)(gHist->liveTotal - 1) / k : 0; }

// fn(j, pv, sp, lo, hi) for the bins j = 0 .. CHART_N-1 of the view (newest at the end)
template <typename F>
static void liveBins(uint32_t k, F fn) {
  const int64_t last = liveLastBin(k);
  for (int j = 0; j < CHART_N; ++j) {
    int32_t pv[HIST_CH], sp[HIST_CH], lo[HIST_CH], hi[HIST_CH];
    liveBin(k, last - (CHART_N - 1 - j), pv, sp, lo, hi);
    fn(j, pv, sp, lo, hi);
  }
}

// Seconds by which the bins before the newest end later than whole bins before now (0 .. bin)
static uint32_t liveShift(uint32_t k) {
  const uint32_t tot = gHist->liveTotal;
  if (!tot) return 0;
  const uint32_t last = (tot - 1) / k;
  return (k - (tot - last * k)) * LIVE_SAMPLE_SEC;
}


#ifdef BOARD_HAS_PSRAM
// The whole view is kept (gHist->livePv ..). A new slot changes only the newest bin; when new bins
// start the rows move left; slots dropped from the ring's old end change the oldest bins. So only those
// bins are computed again (a 24 h view would otherwise read 86 400 slots every second).
static void liveViewSet(int j, const int32_t* pv, const int32_t* sp, const int32_t* lo, const int32_t* hi) {
  for (int ch = 0; ch < HIST_CH; ++ch) {
    gHist->livePv[ch][j].v = pv[ch];
    gHist->liveSp[ch][j].v = sp[ch];
    gHist->liveLo[ch][j].v = lo[ch];
    gHist->liveHi[ch][j].v = hi[ch];
  }
}

static void liveViewBin(uint32_t k, int64_t last, int j) {
  int32_t pv[HIST_CH], sp[HIST_CH], lo[HIST_CH], hi[HIST_CH];
  liveBin(k, last - (CHART_N - 1 - j), pv, sp, lo, hi);
  liveViewSet(j, pv, sp, lo, hi);
}

static void liveViewUpdate(uint32_t k) {
  static bool valid = false;
  static uint32_t vTotal = 0, vK = 0;
  const uint32_t tot = gHist->liveTotal;
  if (valid && k == vK && tot == vTotal) return;
  const int64_t last = liveLastBin(k);
  const int64_t moved = valid && k == vK && tot > vTotal && vTotal ? last - (int64_t)(vTotal - 1) / k : CHART_N;
  if (moved >= CHART_N) {  // all of it
    liveBins(k, liveViewSet);
  } else {
    const int m = (int)moved;
    if (m > 0) {
      TenthRow* rows[4] = {gHist->livePv, gHist->liveSp, gHist->liveLo, gHist->liveHi};
      for (TenthRow* r : rows)
        for (int ch = 0; ch < HIST_CH; ++ch) memmove(&r[ch][0], &r[ch][m], (CHART_N - m) * sizeof(Tenth));
    }
    // the bin that was the newest (it may have got more slots) and the new ones
    for (int j = CHART_N - 1 - m; j < CHART_N; ++j) liveViewBin(k, last, j);
    // the oldest bins, if they reach back to slots the ring no longer keeps
    const uint32_t oldest = liveOldest();
    for (int j = 0; j < CHART_N - 1 - m && (last - (CHART_N - 1 - j)) * (int64_t)k < (int64_t)oldest; ++j)
      liveViewBin(k, last, j);
  }
  valid = true;
  vTotal = tot;
  vK = k;
}
#endif

// fn(j, pv, sp, lo, hi) for every bin of the live view (as liveBins). With PSRAM the whole view is
// kept (liveViewUpdate); without, it is computed (one pass over the history). Under gLock.
template <typename F>
static void liveRows(uint32_t k, F fn) {
#ifdef BOARD_HAS_PSRAM
  liveViewUpdate(k);
  for (int j = 0; j < CHART_N; ++j) {
    int32_t pv[HIST_CH], sp[HIST_CH], lo[HIST_CH], hi[HIST_CH];
    for (int c = 0; c < HIST_CH; ++c) {
      pv[c] = gHist->livePv[c][j].v;
      sp[c] = gHist->liveSp[c][j].v;
      lo[c] = gHist->liveLo[c][j].v;
      hi[c] = gHist->liveHi[c][j].v;
    }
    fn(j, pv, sp, lo, hi);
  }
#else
  liveBins(k, fn);
#endif
}

static int16_t i16(int32_t v) { return v == INT32_MIN ? (int16_t)INT16_MIN : (int16_t)v; }

size_t liveCsv(uint32_t& from, char* buf, size_t n, uint8_t pvMask, uint8_t spMask) {
  size_t len = 0;
  Guard g;
  if (!gHist->liveRing) return 0;
  const uint32_t tot = gHist->liveTotal;
  if (from < liveOldest()) from = liveOldest();
  auto num = [](char* b, size_t m, int32_t t) {
    if (t == INT32_MIN || t == INT16_MIN) b[0] = 0;
    else snprintf(b, m, "%s%ld.%ld", t < 0 ? "-" : "", (long)(labs(t) / 10), (long)(labs(t) % 10));
  };
  const uint32_t nowSec = millis() / 1000;
  while (from < tot) {
    LiveSlot x;
    liveGet(from, x);
    char f[4 * HIST_CH][14];
    for (int c = 0; c < HIST_CH; ++c) {
      num(f[c * 4], sizeof(f[0]), x.pv[c]);
      num(f[c * 4 + 1], sizeof(f[0]), x.lo[c]);
      num(f[c * 4 + 2], sizeof(f[0]), x.hi[c]);
      num(f[c * 4 + 3], sizeof(f[0]), x.sp[c]);
    }
    // time: seconds before the newest slot, and uptime (as in the event log); then the chosen columns
    // (pv, min, max / sp per channel). Rows without any of them are left out.
    const long age = (long)(tot - 1 - from) * LIVE_SAMPLE_SEC;
    char line[160];
    int w = snprintf(line, sizeof(line), "%ld,%ld", -age, (long)nowSec - age);
    bool any = false;
    for (int c = 0; c < HIST_CH; ++c) {
      if (pvMask & (1 << c)) {
        w += snprintf(line + w, sizeof(line) - w, ",%s,%s,%s", f[c * 4], f[c * 4 + 1], f[c * 4 + 2]);
        any |= f[c * 4][0] != 0;
      }
      if (c < NUM_CHANNELS && (spMask & (1 << c))) {
        w += snprintf(line + w, sizeof(line) - w, ",%s", f[c * 4 + 3]);
        any |= f[c * 4 + 3][0] != 0;
      }
    }
    w += snprintf(line + w, sizeof(line) - w, "\n");
    if (!any) {
      ++from;
      continue;
    }
    if (len + (size_t)w > n) break;
    memcpy(buf + len, line, w);
    len += w;
    ++from;
  }
  return len;
}

uint32_t iramPureFree() {
  // MALLOC_CAP_EXEC also counts the D/IRAM regions, which are DRAM as well (they have MALLOC_CAP_8BIT)
  const size_t all = heap_caps_get_free_size(MALLOC_CAP_EXEC), both = heap_caps_get_free_size(MALLOC_CAP_EXEC | MALLOC_CAP_8BIT);
  return all > both ? all - both : 0;
}

// every PV reading for the web chart (raw_hist.h); the run's time anchors
RawRing rawRing[HIST_CH];
static uint32_t rawLastMs[HIST_CH];
static bool rawGap[HIST_CH];
RunAnchors runAnch;

uint32_t rawCap() { return rawRing[0].cap; }
void rawPlan(const uint32_t* after, uint32_t winMs, uint32_t maxN, RawPlan& p) {
  Guard g;
  p.nowMs = millis();
  for (int c = 0; c < HIST_CH; ++c) {
    const RawRing& r = rawRing[c];
    uint32_t a = after[c];
    if (winMs && (int32_t)(p.nowMs - winMs - a) > 0) a = p.nowMs - winMs;  // not older than the window
    p.from[c] = r.find(a);
    p.n[c] = r.total - p.from[c];
    if (p.n[c] > maxN) p.n[c] = maxN;
  }
}
uint32_t rawCopy(int ch, uint32_t k, uint32_t n, uint8_t* out) {
  Guard g;
  return rawRing[ch].copy(k, n, out);
}
// each cycle (under gLock): a channel's new reading (its poll succeeded since the last one: two Modbus
// controllers are read in turns), or one gap entry when it has no valid reading
void rawRecord(const float* pv, const bool* pvOk, const uint32_t* lastOk) {
  for (int c = 0; c < HIST_CH; ++c) {
    if (pvOk[c]) {
      if (lastOk[c] == rawLastMs[c] && !rawGap[c]) continue;
      rawLastMs[c] = lastOk[c];
      rawGap[c] = false;
      rawRing[c].put(lastOk[c], t16(pv[c]));
    } else if (!rawGap[c] && (c < NUM_CHANNELS ? slots[c].active->type() != CH_OFF : gLive.mon.on)) {
      rawGap[c] = true;
      rawRing[c].put(millis(), RAW_NONE);
    }
  }
}

void histStartRun(float total) {
  runAnch.reset();
  gHist->runValid = true;
  gHist->runSpan = runSpanFor(total);
  gHist->runBin = gHist->runSpan / CHART_N;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    fillNan(gHist->runPv[ch], CHART_N);
    fillNan(gHist->runSp[ch], CHART_N);
    fillNan(gHist->runLo[ch], CHART_N);
    fillNan(gHist->runHi[ch], CHART_N);
    fillNan(gHist->runPlan[ch], CHART_N);
    memset(gHist->runSt[ch], ' ', CHART_N);
  }
  for (int ch = NUM_CHANNELS; ch < HIST_CH; ++ch) {  // the monitor: PV only
    fillNan(gHist->runPv[ch], CHART_N);
    fillNan(gHist->runLo[ch], CHART_N);
    fillNan(gHist->runHi[ch], CHART_N);
  }
  gHist->runTotal = total;
  gHist->runEvN = 0;
  gHist->runStLast = -1;
  ++gHist->runGen;
}

// the run got longer than the chart: half the resolution, double the span
static void histCompress() {
  // mode: 0 first, 1 mean, 2 lowest, 3 highest of each pair
  auto half = [](Tenth* a, int mode) {
    for (int i = 0; i < CHART_N / 2; ++i) {
      const float x = a[2 * i], y = a[2 * i + 1];
      a[i] = mode == 0 ? x
             : isnan(x) ? y
             : isnan(y) ? x
             : mode == 1 ? (x + y) / 2
             : mode == 2 ? fminf(x, y)
                         : fmaxf(x, y);
    }
    for (int i = CHART_N / 2; i < CHART_N; ++i) a[i] = NAN;
  };
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    char* st = gHist->runSt[ch];
    for (int i = 0; i < CHART_N / 2; ++i) st[i] = st[2 * i + 1] != ' ' ? st[2 * i + 1] : st[2 * i];
    memset(st + CHART_N / 2, ' ', CHART_N / 2);
  }
  gHist->runStLast = (int16_t)(gHist->runStLast >= 0 ? gHist->runStLast / 2 : -1);
  for (int ch = 0; ch < HIST_CH; ++ch) {
    half(gHist->runPv[ch], 1);
    half(gHist->runSp[ch], 1);
    half(gHist->runLo[ch], 2);
    half(gHist->runHi[ch], 3);
    half(gHist->runPlan[ch], 0);
  }
  gHist->runBin *= 2;
  gHist->runSpan *= 2;
}

// ---- plan curves (control task; written into gHist under gLock)

struct PlanFill {
  Tenth* a[NUM_CHANNELS];
  float off;    // seconds added to the segment times
  float bin;
  int from;     // first bin to write
};

static void planFn(const PlanSeg& g, void* u) {
  PlanFill& f = *(PlanFill*)u;
  const float t0 = f.off + g.t0, t1 = f.off + g.t1;
  int i = (int)ceilf(t0 / f.bin - 1e-4f);
  if (i < f.from) i = f.from;
  for (; i < CHART_N; ++i) {
    const float t = i * f.bin;
    if (t > t1 + 1e-4f) break;
    f.a[g.ch][i] = g.idle ? NAN : g.v0 + (g.v1 - g.v0) * (t1 > t0 ? (t - t0) / (t1 - t0) : 1);
  }
}

// Preview of the selected program (gHist->plan), recomputed when the selection or the stored program changed
// or a start temperature (PV) moved by PREVIEW_PV_STEP or more. S3: the parsed program is kept in PSRAM, so
// only a new selection / a saved program reads the file again. CYD: no permanent 3.9 kB copy (DRAM is short):
// the file is read for each recompute, so the PV step is larger there.
#ifdef BOARD_HAS_PSRAM
static constexpr float PREVIEW_PV_STEP = 1.0f;
static DuoProgram* selProg = nullptr;  // PSRAM: the parsed selected program (selLoaded / selGen)
#else
static constexpr float PREVIEW_PV_STEP = 5.0f;
#endif
static bool selValid = false;
static int selLoaded = -1;
static uint32_t selGen = 0;
static float selTotal = 0;
static float selPvKey[NUM_CHANNELS] = {NAN, NAN};
static bool selForce = true;

static float updatePreview(const float* pv) {
  int sel;
  {
    Guard g;
    sel = gSettings.selProgram;
  }
  const uint32_t gen = programStoreGen();
  bool moved = false;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    const float a = pv ? pv[ch] : NAN, b = selPvKey[ch];
    if (isnan(a) != isnan(b) || (!isnan(a) && fabsf(a - b) >= PREVIEW_PV_STEP)) moved = true;
  }
  const bool reload = selForce || sel != selLoaded || gen != selGen;
  if (!reload && !moved) return selTotal;
#ifdef BOARD_HAS_PSRAM
  if (!selProg) selProg = (DuoProgram*)heap_caps_malloc(sizeof(DuoProgram), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  DuoProgram* p = selProg;
#else
  DuoProgram* p = (DuoProgram*)malloc(sizeof(DuoProgram));
#endif
  if (!p) return selTotal;  // try again next time
  char err[96];
#ifdef BOARD_HAS_PSRAM
  if (reload) selValid = programLoad(sel, *p, err, sizeof(err));  // (else: the kept copy)
#else
  selValid = programLoad(sel, *p, err, sizeof(err));
#endif
  selForce = false;
  selLoaded = sel;
  selGen = gen;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) selPvKey[ch] = pv ? pv[ch] : NAN;
  const float total = selValid ? programPreview(*p, nullptr, nullptr, nullptr) : 0;
  {
    Guard g;
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) fillNan(gHist->plan[ch], CHART_N);
    gHist->planTotal = total;
    gHist->planSpan = runSpanFor(total);
    gHist->planBin = gHist->planSpan / CHART_N;
    gHist->planName[0] = 0;
    if (selValid) {
      PlanFill f{{gHist->plan[0], gHist->plan[1]}, 0, gHist->planBin, 0};
      programPreview(*p, pv, planFn, &f);
      strlcpy(gHist->planName, p->name, sizeof(gHist->planName));
    }
  }
#ifndef BOARD_HAS_PSRAM
  free(p);
#endif
  selTotal = total;
  return total;
}

// run: forecast into gHist->runPlan; else preview of the selected program into gHist->plan.
// Returns the (projected) total; remaining = the run's forecast.
float updatePlan(const float* pv, float& remaining) {
  remaining = 0;
  if (eng.active()) {
    // keep the plan in range: double the span while elapsed + remaining does not fit
    remaining = eng.forecast(nullptr, nullptr, pv);
    Guard g;
    for (int k = 0; k < 20 && eng.elapsed() + remaining > gHist->runSpan; ++k) histCompress();
    const int now = (int)ceilf(eng.elapsed() / gHist->runBin - 1e-4f);
    for (int ch = 0; ch < NUM_CHANNELS; ++ch)
      for (int i = now < 0 ? 0 : now; i < CHART_N; ++i) gHist->runPlan[ch][i] = NAN;
    PlanFill f{{gHist->runPlan[0], gHist->runPlan[1]}, eng.elapsed(), gHist->runBin, now};
    eng.forecast(planFn, &f, pv);
    gHist->runTotal = eng.elapsed() + remaining;
    strlcpy(gHist->runName, runProg->name, sizeof(gHist->runName));
    selForce = true;  // the preview is rebuilt after the run
    return gHist->runTotal;
  }
  return updatePreview(pv);
}

void chartSnapshot(ChartData& d, bool live) {
  Guard g;
  const bool active = gLive.state == RunState::Running || gLive.state == RunState::Paused;
  d.run = !live && active && gHist->runValid;
  d.planActive = d.run;
  d.spActive = d.run || gLive.ch[ENGINE_CH].manualOn;
  const int T = CH_TOP, B = ENGINE_CH;
  if (d.run) {
    d.binSec = gHist->runBin;
    d.span = gHist->runSpan;
    d.planSpan = gHist->runSpan;
    d.planTotal = gHist->runTotal;
    d.elapsed = gLive.elapsed;
    strlcpy(d.progName, gHist->runName, sizeof(d.progName));
    for (int i = 0; i < CHART_N; ++i) {
      d.pv[i] = gHist->runPv[B][i];
      d.sp[i] = gHist->runSp[B][i];
      d.plan[i] = gHist->runPlan[B][i];
      d.pvT[i] = gHist->runPv[T][i];
      d.spT[i] = gHist->runSp[T][i];
      d.planT[i] = gHist->runPlan[T][i];
#ifdef CHART_LOHI
      d.lo[i] = gHist->runLo[B][i];
      d.hi[i] = gHist->runHi[B][i];
      d.loT[i] = gHist->runLo[T][i];
      d.hiT[i] = gHist->runHi[T][i];
#endif
    }
    return;
  }
  // idle: rolling window + preview of the selected program on its own time axis
  d.planSpan = gHist->planSpan;
  d.planTotal = gHist->planTotal;
  strlcpy(d.progName, gHist->planName, sizeof(d.progName));
  const uint32_t k = liveK();
  d.binSec = k * LIVE_SAMPLE_SEC;
  d.span = liveSpan();
  d.elapsed = 0;
  liveRows(k, [&](int j, const int32_t* pv, const int32_t* sp, const int32_t* lo, const int32_t* hi) {
    d.pv[j].v = i16(pv[B]);
    d.sp[j].v = i16(sp[B]);
    d.pvT[j].v = i16(pv[T]);
    d.spT[j].v = i16(sp[T]);
#ifdef CHART_LOHI
    d.lo[j].v = i16(lo[B]);
    d.hi[j].v = i16(hi[B]);
    d.loT[j].v = i16(lo[T]);
    d.hiT[j].v = i16(hi[T]);
#else
    (void)lo;
    (void)hi;
#endif
  });
  for (int i = 0; i < CHART_N; ++i) {
    d.plan[i] = gHist->plan[B][i];
    d.planT[i] = gHist->plan[T][i];
  }
}

// ---- /api/chart (web task). Numbers are appended through a small buffer (no String temporaries);
// 0.1 degC values are formatted from the integer (float printf is slow and runs under gLock).

static void jInt(String& o, long v) {
  char b[16];
  snprintf(b, sizeof(b), "%ld", v);
  o += b;
}
static void jUns(String& o, unsigned long v) {
  char b[16];
  snprintf(b, sizeof(b), "%lu", v);
  o += b;
}
// v in 0.1 degC as "123.4" (INT32_MIN: "null"); the same text as jNum(v * 0.1f)
static void jTenth(String& o, int32_t v) {
  if (v == INT32_MIN) {
    o += "null";
    return;
  }
  char b[16];
  char* p = b + sizeof(b);
  *--p = 0;
  uint32_t a = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
  *--p = (char)('0' + a % 10);
  a /= 10;
  *--p = '.';
  do {
    *--p = (char)('0' + a % 10);
    a /= 10;
  } while (a);
  if (v < 0) *--p = '-';
  o += p;
}

static void jNum(String& o, float v, const char* fmt = "%.1f") {
  if (isnan(v) || isinf(v)) {
    o += "null";
    return;
  }
  char b[20];
  snprintf(b, sizeof(b), fmt, v);
  o += b;
}

// "dlo":[[..],[..]] / "dhi": how far the lowest / highest reading of each slot lies below / above
// "pv" (the last reading), in 0.1 degC; 0 when equal or unknown (keeps the answer small)
static void jDeltas(String& o, const char* key, TenthRow* pv, TenthRow* ext, bool below, int rows) {
  o += ",\"";
  o += key;
  o += "\":[";
  for (int c = 0; c < rows; ++c) {
    if (c) o += ',';
    o += '[';
    for (int i = 0; i < CHART_N; ++i) {
      if (i) o += ',';
      const int32_t p = pv[c][i].v, x = ext[c][i].v;
      int32_t d = (p == INT32_MIN || x == INT32_MIN) ? 0 : below ? p - x : x - p;
      if (d < 0) d = 0;
      jInt(o, (long)d);
    }
    o += ']';
  }
  o += ']';
}

static void jArr(String& o, const Tenth* a, int n) {
  o += '[';
  for (int i = 0; i < n; ++i) {
    if (i) o += ',';
    jTenth(o, a[i].v);
  }
  o += ']';
}

// forecast segments per channel, pieces of the same course merged
struct FcSink {
  String seg[NUM_CHANNELS];
  String ev;
  bool has[NUM_CHANNELS];
  PlanSeg last[NUM_CHANNELS];
};

static void fcFlush(FcSink& k, int ch) {
  if (!k.has[ch]) return;
  const PlanSeg& g = k.last[ch];
  String& o = k.seg[ch];
  if (o.length()) o += ',';
  o += '[';
  jNum(o, g.t0);
  o += ',';
  jNum(o, g.t1);
  o += ',';
  jNum(o, g.idle ? NAN : g.v0);
  o += ',';
  jNum(o, g.idle ? NAN : g.v1);
  o += ',';
  jInt(o, (int)g.src);
  o += ',';
  jInt(o, (g.idle ? 1 : 0) | (g.ramp ? 2 : 0));
  o += ']';
  k.has[ch] = false;
}

static void fcSeg(const PlanSeg& g, void* u) {
  FcSink& k = *(FcSink*)u;
  const int ch = g.ch;
  if (k.has[ch]) {
    PlanSeg& l = k.last[ch];
    bool same = l.idle == g.idle && l.ramp == g.ramp && l.src == g.src && fabsf(l.t1 - g.t0) < 0.01f;
    if (same && !g.idle) {
      if (!g.ramp) same = l.v1 == g.v0 && g.v0 == g.v1;
      else {
        const float s1 = (l.v1 - l.v0) / fmaxf(l.t1 - l.t0, 1e-3f), s2 = (g.v1 - g.v0) / fmaxf(g.t1 - g.t0, 1e-3f);
        same = fabsf(l.v1 - g.v0) < 0.05f && fabsf(s1 - s2) < 1e-3f;
      }
    }
    if (same) {
      l.t1 = g.t1;
      l.v1 = g.v1;
      return;
    }
    fcFlush(k, ch);
  }
  k.last[ch] = g;
  k.has[ch] = true;
}

static void jEv(String& o, const PlanEv& e) {
  if (o.length() && o[o.length() - 1] != '[') o += ',';
  o += '[';
  jNum(o, e.t);
  o += ',';
  jInt(o, (int)e.type);
  o += ',';
  jInt(o, e.ch == 0xFF ? -1 : (int)e.ch);
  o += ',';
  jInt(o, (int)e.idx);
  o += ']';
}

static void fcEv(const PlanEv& e, void* u) {
  FcSink& k = *(FcSink*)u;
  if (e.type == EV_STEP) return;
  jEv(k.ev, e);
}

// rows of PV in the chart answers: the channels, plus the monitor while it is on
static int monRows() { return HAS_MONITOR && gSettings.mon.on ? HIST_CH : NUM_CHANNELS; }
static void jMon(String& o) {  // "mon": the monitor channel's name while it is on
  if (monRows() == NUM_CHANNELS) return;
  o += ",\"mon\":\"";
  for (const char* p = gSettings.mon.name; *p; ++p) {
    if (*p == '"' || *p == '\\') o += '\\';
    o += *p;
  }
  o += '"';
}

bool chartJson(String& o, bool run) {
  o.reserve(run ? 11000 : 8500);
  if (!run) {
    // the rows are taken under gLock (one pass), the text is made after it
    struct Rows {
      int16_t pv[HIST_CH][CHART_N], sp[HIST_CH][CHART_N];
      int16_t dlo[HIST_CH][CHART_N], dhi[HIST_CH][CHART_N];  // see jDeltas
    };
    Rows* r = (Rows*)malloc(sizeof(Rows));  // 4.8 kB (with the monitor 6.6 kB)
    if (!r) {
      o = "";
      return false;
    }
    uint32_t k, sh, have, span, cap;
    unsigned long ms;
    {
      Guard g;
      k = liveK();
      sh = liveShift(k);
      have = (gHist->liveTotal - liveOldest()) * LIVE_SAMPLE_SEC;
      span = liveSpan();
      cap = liveHistSec();
      ms = millis();
      liveRows(k, [&](int j, const int32_t* pv, const int32_t* sp, const int32_t* lo, const int32_t* hi) {
        for (int c = 0; c < HIST_CH; ++c) {
          r->pv[c][j] = i16(pv[c]);
          r->sp[c][j] = i16(sp[c]);
          const bool no = pv[c] == INT32_MIN;
          const int32_t dl = no || lo[c] == INT32_MIN ? 0 : pv[c] - lo[c];
          const int32_t dh = no || hi[c] == INT32_MIN ? 0 : hi[c] - pv[c];
          r->dlo[c][j] = (int16_t)(dl < 0 ? 0 : dl > INT16_MAX ? INT16_MAX : dl);
          r->dhi[c][j] = (int16_t)(dh < 0 ? 0 : dh > INT16_MAX ? INT16_MAX : dh);
        }
      });
    }
    // bin: seconds per point; the newest point is now, the others end `sh` s later than a whole
    // number of bins before it; have / cap: seconds recorded / the history's size
    o = "{\"mode\":\"live\",\"bin\":";
    jUns(o, (unsigned long)(k * LIVE_SAMPLE_SEC));
    o += ",\"span\":";
    jUns(o, (unsigned long)span);
    o += ",\"sh\":";
    jUns(o, (unsigned long)sh);
    o += ",\"have\":";
    jUns(o, (unsigned long)have);
    o += ",\"cap\":";
    jUns(o, (unsigned long)cap);
    // device millis of this answer ("now" of the chart) and the per-channel size of the every-reading
    // history (GET /api/raw; 0: none)
    o += ",\"ms\":";
    jUns(o, ms);
    o += ",\"raw\":";
    jUns(o, (unsigned long)rawCap());
    jMon(o);
    auto rows = [&](const char* key, int16_t (*a)[CHART_N], bool tenths, int nr) {
      o += key;
      for (int c = 0; c < nr; ++c) {
        o += c ? ",[" : "[";
        for (int i = 0; i < CHART_N; ++i) {
          if (i) o += ',';
          if (tenths) jTenth(o, a[c][i] == INT16_MIN ? INT32_MIN : a[c][i]);
          else jInt(o, a[c][i]);
        }
        o += ']';
      }
      o += ']';
    };
    const int pvRows = monRows();
    rows(",\"pv\":[", r->pv, true, pvRows);
    rows(",\"sp\":[", r->sp, true, NUM_CHANNELS);
    // "dlo" / "dhi" as for the run (see jDeltas)
    rows(",\"dlo\":[", r->dlo, false, pvRows);
    rows(",\"dhi\":[", r->dhi, false, pvRows);
    o += '}';
    free(r);
    return true;
  }
  static FcSink sink;  // web task only
  for (int c = 0; c < NUM_CHANNELS; ++c) {
    sink.seg[c] = "";
    sink.has[c] = false;
  }
  sink.ev = "";
  Guard g;
  if (!gHist->runValid) {
    o = "{\"mode\":\"none\"}";
    return true;
  }
  const bool active = gEngSnap.active();
  float rem = 0;
  if (active) {
    float pv[NUM_CHANNELS];
    for (int c = 0; c < NUM_CHANNELS; ++c) pv[c] = gLive.ch[c].pvValid ? gLive.ch[c].pv : NAN;
    rem = gEngSnap.forecast(fcSeg, &sink, pv, fcEv, &sink);
    for (int c = 0; c < NUM_CHANNELS; ++c) fcFlush(sink, c);
  }
  o = "{\"mode\":\"";
  o += active ? "run" : "last";
  o += "\",\"gen\":";
  jUns(o, (unsigned long)gHist->runGen);
  o += ",\"name\":\"";
  for (const char* p = gHist->runName; *p; ++p) {
    if (*p == '"' || *p == '\\') o += '\\';
    o += *p;
  }
  o += "\",\"bin\":";
  jNum(o, gHist->runBin, "%.3f");
  o += ",\"span\":";
  jNum(o, gHist->runSpan);
  o += ",\"el\":";
  jNum(o, gEngSnap.elapsed());
  // run time against device millis (raw_hist.h RunAnchors): "ra" anchors, "rn" the newest point
  o += ",\"ms\":";
  jUns(o, (unsigned long)millis());
  o += ",\"raw\":";
  jUns(o, (unsigned long)rawCap());
  o += ",\"ra\":[";
  for (int i = 0; i < runAnch.n; ++i) {
    if (i) o += ',';
    o += '[';
    jUns(o, (unsigned long)runAnch.ms[i]);
    o += ',';
    jInt(o, (long)runAnch.rt[i]);
    o += ']';
  }
  o += "],\"rn\":[";
  jUns(o, (unsigned long)runAnch.lastMs);
  o += ',';
  jInt(o, (long)runAnch.lastRt);
  o += ']';
  o += ",\"rem\":";
  jNum(o, rem);
  // "pv", "dlo", "dhi": a third row for the monitor channel while it is on ("mon" its name)
  const int pvRows = monRows();
  jMon(o);
  o += ",\"pv\":[";
  for (int c = 0; c < pvRows; ++c) {
    if (c) o += ',';
    jArr(o, gHist->runPv[c], CHART_N);
  }
  o += "],\"sp\":[";
  for (int c = 0; c < NUM_CHANNELS; ++c) {
    if (c) o += ',';
    jArr(o, gHist->runSp[c], CHART_N);
  }
  o += ']';
  jDeltas(o, "dlo", gHist->runPv, gHist->runLo, true, pvRows);
  jDeltas(o, "dhi", gHist->runPv, gHist->runHi, false, pvRows);
  o += ",\"st\":[";
  for (int c = 0; c < NUM_CHANNELS; ++c) {
    if (c) o += ',';
    o += '"';
    for (int i = 0; i < CHART_N; ++i) o += gHist->runSt[c][i];
    o += '"';
  }
  o += "],\"ev\":[";
  String evs;
  for (int i = 0; i < gHist->runEvN; ++i) jEv(evs, gHist->runEv[i]);
  o += evs;
  o += "],\"fc\":{\"seg\":[";
  for (int c = 0; c < NUM_CHANNELS; ++c) {
    if (c) o += ',';
    o += '[';
    o += sink.seg[c];
    o += ']';
  }
  o += "],\"ev\":[";
  o += sink.ev;
  o += "]}}";
  return true;
}
