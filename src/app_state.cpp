#include "app_state.h"
#include "syslog.h"
#include "sdlog.h"
#include "crumbs.h"
#include "console.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include "controller.h"
#include "local_ctrl.h"
#include "net.h"
#include "program_store.h"
#include "settings_store.h"
#include "status_led.h"
#include "version.h"
#include <esp_system.h>
#include <esp_rom_sys.h>

Settings gSettings;
LiveData gLive;
History* gHist = nullptr;
SemaphoreHandle_t gLock;

static QueueHandle_t cmdQ;
static SemaphoreHandle_t reqMutex, reqDone;
static BusRequest reqSlot;
static volatile bool reqPending = false;

static ModbusMaster bus;                  // the one RS485 bus, shared by the Modbus channels
static ChannelSlot slots[NUM_CHANNELS];   // bottom, top
static uint8_t busTurn = 0;               // next channel to poll on the bus (round robin)
static RunEngine eng;                     // one run engine for both channels (spec 9.3)
static Settings s;  // control task's private copy of the settings
static DuoProgram* runProg = nullptr;     // running copy (run edits change it; read by the web under gLock)
static int8_t runIdx = -1;                // slot of the running / last program
static uint32_t editCount = 0, savedCount = 0;  // run edits / edits stored with "save run"
static bool selValid = false;
static int selLoaded = -1;
static uint32_t selGen = 0;
static RunState prevState = RunState::Idle;
static char runMsg[128] = "";
static char lastErr[NUM_CHANNELS][80];  // write errors of this cycle, per channel

// setpoint output of one channel during a run
struct Drive {
  uint32_t syncedGen = 0xFFFFFFFF;
  bool hostFallback = false;   // controller-ramp mode, ramp too slow/fast -> host stepping
  float lastWritten = NAN;
  uint32_t lastWriteMs = 0;
  uint32_t spWrites = 0;
  bool rampSaved = false;      // controller's own gradient, restored at the end
  int16_t rampOriginal = 0;
  int8_t outOn = -1;           // outputs switched by the run: -1 unknown, 0 off, 1 on
  bool endPending = false;     // end action still to be written
};
static Drive drv[NUM_CHANNELS];

// manual setpoint, per channel
struct Manual {
  bool on = false;
  float sp = NAN;
  bool pending = false;     // SP (+ outputs on) still to be written
  bool offPending = false;  // outputs off still to be written
};
static Manual man[NUM_CHANNELS];
// Modbus: the controller's setpoint counts as active (chart, history) while the run or the manual
// setpoint drives it, and after the end of a run that kept or set it. Local channels: while on.
static bool spHeld[NUM_CHANNELS];

// ---- "Run interrupted by a reset" (spec 9.3.5)
// RTC memory keeps the exact position over a reset (not a power loss); /duo/run.json + runpos.json
// (program_store) keep the running copy and the position as of the last phase / step change or minute.
struct RunRtc {
  uint32_t magic, id;
  RunPos pos;
  uint32_t sum;
};
RTC_NOINIT_ATTR static RunRtc rtcRun;
static constexpr uint32_t RTC_MAGIC = 0x52554E31;  // "RUN1"
static uint32_t rtcSum(const RunRtc& r) {
  uint32_t h = 2166136261u;
  const uint8_t* b = (const uint8_t*)&r;
  for (size_t i = 0; i < offsetof(RunRtc, sum); ++i) h = (h ^ b[i]) * 16777619u;
  return h;
}
static RunFileInfo runInfo;        // the current run
static RunPos posFlash;            // position last written to flash
static uint32_t posFlashMs = 0;
struct Interrupted {
  bool valid;
  bool exact;                      // position from RTC memory (else: flash, up to a minute old)
  RunFileInfo info;
  RunPos pos;
  char reason[20];
};
static Interrupted intr;           // the offer (the program is in runProg)
static bool intrOff[NUM_CHANNELS];      // outputs off still to be written (interrupted run's channels)
static bool intrCleanup[NUM_CHANNELS];  // dismissed: outputs off, gradient / front lock restore
static RunFileInfo cleanInfo;

// ------------------------------------------------------------------ helpers

// status message (web run.msg, touch message line) + event log + serial
static void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(runMsg, sizeof(runMsg), fmt, ap);
  va_end(ap);
  // run records (start, pauses, run edits, end) get the run colour in the log, the rest are events
  static const char* const RUN_WORDS[] = {"Program", "program", "phase", "Phase", "Run ", "run ", "Stopped", "Paused",
                                          "resumed", "Action required", nullptr};
  bool run = false;
  for (const char* const* w = RUN_WORDS; *w && !run; ++w) run = strstr(runMsg, *w) != nullptr;
  if (run) logRun("%s", runMsg);
  else logEvent("%s", runMsg);
}
static void setMsg(const char* m) { say("%s", m); }

static float clampSp(int ch, float v) {
  if (v < s.ch[ch].spMin) v = s.ch[ch].spMin;
  if (v > s.ch[ch].spMax) v = s.ch[ch].spMax;
  return v;
}

static Channel& chan(int ch) { return *slots[ch].active; }
static Controller* mbOf(int ch) { return slots[ch].active->modbus(); }  // null if not Modbus
static bool inRun(int ch) { return eng.active() && eng.out(ch).inProgram; }

static bool writeSp(int ch, float v) {
  v = clampSp(ch, v);
  if (chan(ch).writeSp(v, lastErr[ch], sizeof(lastErr[0]))) {
    Drive& d = drv[ch];
    d.lastWritten = v;
    d.lastWriteMs = millis();
    ++d.spWrites;
    return true;
  }
  return false;
}

// Modbus-only extras (true = nothing to do)
static bool hasRampReg(int ch) { return mbOf(ch) && mbOf(ch)->reg().rampRate != REG_UNSET; }

static bool mbWrite(int ch, uint16_t addr, int16_t raw, const char* what) {
  Controller* c = mbOf(ch);
  if (!c || addr == REG_UNSET) return true;
  const MbResult r = c->writeRaw(addr, raw);
  if (r != MbResult::Ok) mbDescribe(r, c->lastException(), what, lastErr[ch], sizeof(lastErr[0]));
  return r == MbResult::Ok;
}

static bool writeRampOff(int ch) {
  if (!hasRampReg(ch)) return true;
  return mbWrite(ch, mbOf(ch)->reg().rampRate, mbOf(ch)->reg().offRaw, "Ramp-rate write");
}

static bool writeRampRate(int ch, float perMin) {
  Controller* c = mbOf(ch);
  const MbResult r = c->writeScaled(c->reg().rampRate, perMin);
  if (r != MbResult::Ok) mbDescribe(r, c->lastException(), "Ramp-rate write", lastErr[ch], sizeof(lastErr[0]));
  return r == MbResult::Ok;
}

float runSpanFor(float total) {
  if (total <= 0) return 60;
  float spare = total * 0.1f;
  if (spare < 30) spare = 30;
  return total + spare;
}

template <typename T>
static void fillNan(T* a, int n) {
  for (int i = 0; i < n; ++i) a[i] = NAN;
}

static void histResetLive() {
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    fillNan(gHist->plan[ch], CHART_N);
    fillNan(gHist->runPlan[ch], CHART_N);
  }
  gHist->runValid = false;
}

// ---- live history ring
// A slot is 3 words (32-bit access only: the ring may be in IRAM):
//   w0 = PV bottom | PV top << 16, w1 = SP bottom | SP top << 16  (int16 0.1 degC, INT16_MIN = none)
//   w2 = one byte each: how far the lowest / highest reading lay below / above PV, bottom then top
//        (0.1 degC up to 19.9 degC, then 2 degC steps up to 130 degC, rounded outwards)
static constexpr int SLOT_W = 3;
static_assert(NUM_CHANNELS == 2, "the live history packs two channels per word");

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
  int16_t pv[NUM_CHANNELS], sp[NUM_CHANNELS];
  int32_t lo[NUM_CHANNELS], hi[NUM_CHANNELS];  // tenths; INT32_MIN with pv
};

static void livePut(const float* pv, const bool* ok, const float* sp, const float* lo, const float* hi) {
  if (!gHist->liveRing) return;
  int16_t p[NUM_CHANNELS], q[NUM_CHANNELS];
  uint32_t d = 0;
  for (int c = 0; c < NUM_CHANNELS; ++c) {
    p[c] = ok[c] ? t16(pv[c]) : (int16_t)INT16_MIN;
    q[c] = t16(sp[c]);
    if (p[c] != INT16_MIN) {
      const int32_t l = isnan(lo[c]) ? p[c] : (int32_t)floorf(lo[c] * 10.0f);
      const int32_t h = isnan(hi[c]) ? p[c] : (int32_t)ceilf(hi[c] * 10.0f);
      d |= (uint32_t)dEnc(p[c] - l) << (16 * c);
      d |= (uint32_t)dEnc(h - p[c]) << (16 * c + 8);
    }
  }
  uint32_t* w = gHist->liveRing + (gHist->liveTotal % gHist->liveCap) * SLOT_W;
  w[0] = (uint16_t)p[0] | (uint32_t)(uint16_t)p[1] << 16;
  w[1] = (uint16_t)q[0] | (uint32_t)(uint16_t)q[1] << 16;
  w[2] = d;
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
// once it is complete (the newest is partial). fn(j, pv, sp, lo, hi) per bin, 0.1 degC (INT32_MIN =
// none): PV and SP of the newest reading, lowest and highest reading. Under gLock.
template <typename F>
static void liveBins(uint32_t k, F fn) {
  const uint32_t tot = gHist->liveTotal, oldest = liveOldest();
  const int64_t last = tot ? (int64_t)(tot - 1) / k : 0;  // newest bin
  for (int j = 0; j < CHART_N; ++j) {
    int32_t pv[NUM_CHANNELS], sp[NUM_CHANNELS], lo[NUM_CHANNELS], hi[NUM_CHANNELS];
    for (int c = 0; c < NUM_CHANNELS; ++c) pv[c] = sp[c] = lo[c] = hi[c] = INT32_MIN;
    const int64_t b = last - (CHART_N - 1 - j);
    if (tot && b >= 0) {
      int64_t s0 = b * k, s1 = s0 + k;
      if (s1 > tot) s1 = tot;
      if (s0 < oldest) s0 = oldest;
      LiveSlot x;
      for (int64_t sl = s0; sl < s1; ++sl) {
        liveGet((uint32_t)sl, x);
        for (int c = 0; c < NUM_CHANNELS; ++c) {
          if (x.sp[c] != INT16_MIN) sp[c] = x.sp[c];
          if (x.pv[c] == INT16_MIN) continue;
          pv[c] = x.pv[c];
          if (lo[c] == INT32_MIN || x.lo[c] < lo[c]) lo[c] = x.lo[c];
          if (hi[c] == INT32_MIN || x.hi[c] > hi[c]) hi[c] = x.hi[c];
        }
      }
    }
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

enum LiveField { LV_PV, LV_SP, LV_LO, LV_HI };
// One row of the live view (field f of channel c) into out[CHART_N]. With PSRAM the whole view is
// kept (rebuilt once per slot: a 24 h view reads 86400 slots); without, each row is computed.
static void liveRow(uint32_t k, LiveField f, int c, int32_t* out) {
#ifdef BOARD_HAS_PSRAM
  static uint32_t vTotal = UINT32_MAX, vK = 0;
  if (gHist->liveTotal != vTotal || k != vK) {
    vTotal = gHist->liveTotal;
    vK = k;
    liveBins(k, [](int j, const int32_t* pv, const int32_t* sp, const int32_t* lo, const int32_t* hi) {
      for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
        gHist->livePv[ch][j].v = pv[ch];
        gHist->liveSp[ch][j].v = sp[ch];
        gHist->liveLo[ch][j].v = lo[ch];
        gHist->liveHi[ch][j].v = hi[ch];
      }
    });
  }
  const TenthRow* r = f == LV_PV ? gHist->livePv : f == LV_SP ? gHist->liveSp : f == LV_LO ? gHist->liveLo : gHist->liveHi;
  for (int j = 0; j < CHART_N; ++j) out[j] = r[c][j].v;
#else
  liveBins(k, [&](int j, const int32_t* pv, const int32_t* sp, const int32_t* lo, const int32_t* hi) {
    out[j] = (f == LV_PV ? pv : f == LV_SP ? sp : f == LV_LO ? lo : hi)[c];
  });
#endif
}

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
    char f[8][14];
    for (int c = 0; c < NUM_CHANNELS; ++c) {
      num(f[c * 4], sizeof(f[0]), x.pv[c]);
      num(f[c * 4 + 1], sizeof(f[0]), x.lo[c]);
      num(f[c * 4 + 2], sizeof(f[0]), x.hi[c]);
      num(f[c * 4 + 3], sizeof(f[0]), x.sp[c]);
    }
    // time: seconds before the newest slot, and uptime (as in the event log); then the chosen columns
    // (pv, min, max / sp per channel). Rows without any of them are left out.
    const long age = (long)(tot - 1 - from) * LIVE_SAMPLE_SEC;
    char line[112];
    int w = snprintf(line, sizeof(line), "%ld,%ld", -age, (long)nowSec - age);
    bool any = false;
    for (int c = 0; c < NUM_CHANNELS; ++c) {
      if (pvMask & (1 << c)) {
        w += snprintf(line + w, sizeof(line) - w, ",%s,%s,%s", f[c * 4], f[c * 4 + 1], f[c * 4 + 2]);
        any |= f[c * 4][0] != 0;
      }
      if (spMask & (1 << c)) {
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

static void histStartRun(float total) {
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
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    half(gHist->runPv[ch], 1);
    half(gHist->runSp[ch], 1);
    half(gHist->runLo[ch], 2);
    half(gHist->runHi[ch], 3);
    half(gHist->runPlan[ch], 0);
  }
  gHist->runBin *= 2;
  gHist->runSpan *= 2;
}

// ---- run events (chart: phase lines, pauses, step end times) and the event log

static const char CH_STATE_CODE[] = "-orhwjkxpd";  // ChState -> chart lane code ('m' = manual)
static PlanEv pendEv[48];                          // from the engine, flushed into gHist each cycle
static int pendN = 0;
static RunEngine gEngSnap;                         // copy of the engine for the web (under gLock)

static void recordEv(const PlanEv& e, void*) {
  if (pendN < (int)(sizeof(pendEv) / sizeof(pendEv[0]))) pendEv[pendN++] = e;
}

// call with the lock free
static void flushRunEvents() {
  for (int i = 0; i < pendN; ++i) {
    const PlanEv& e = pendEv[i];
    if (e.type == EV_PHASE && e.idx < runProg->nPh)
      logRun("Phase %d (%s)", e.idx + 1, phaseKindName(runProg->ph[e.idx].kind));
    else if (e.type == EV_STEP && e.ch < NUM_CHANNELS)
      logRun("%s step %d", s.ch[e.ch].name, e.idx + 1);
  }
  Guard g;
  for (int i = 0; i < pendN; ++i) {
    const PlanEv& e = pendEv[i];
    if (e.type == EV_RAMP_END || e.type == EV_STEP) continue;  // not drawn for the past
    if (gHist->runEvN < RUN_EV_MAX) gHist->runEv[gHist->runEvN++] = e;
  }
  pendN = 0;
}

// ---- event log (any task)

static LogEntry logBuf[LOG_N];
static uint32_t logNext = 1;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;
static volatile float logRunT = NAN;

static void logAddV(LogKind def, const char* fmt, va_list ap) {
  char m[LOG_LEN];
  if (vsnprintf(m, sizeof(m), fmt, ap) >= (int)sizeof(m)) m[utf8Fit(m, sizeof(m) - 1)] = 0;
  const float rt = logRunT;
  const LogKind k = logClassify(m, def);
  LogSerial.printf("[event] %s\n", m);
  syslogAdd(k, m, strlen(m));
  sdlogEvent(k, m);  // (a row in the SD run log while a run is logged)
  portENTER_CRITICAL(&logMux);
  LogEntry& e = logBuf[logNext % LOG_N];
  e.seq = logNext++;
  e.upSec = millis() / 1000;
  e.epoch = syslogEpoch();
  e.runT = rt;
  e.kind = k;
  memcpy(e.msg, m, sizeof(e.msg));
  portEXIT_CRITICAL(&logMux);
}

void logEvent(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  logAddV(LK_EVENT, fmt, ap);
  va_end(ap);
}

void logRun(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  logAddV(LK_RUN, fmt, ap);
  va_end(ap);
}

uint32_t logSeq() { return logNext - 1; }

int logSince(uint32_t seq, LogEntry* out, int max) {
  int n = 0;
  portENTER_CRITICAL(&logMux);
  uint32_t first = logNext > LOG_N ? logNext - LOG_N : 1;
  if (seq + 1 > first) first = seq + 1;
  for (uint32_t q = first; q < logNext && n < max; ++q) out[n++] = logBuf[q % LOG_N];
  portEXIT_CRITICAL(&logMux);
  return n;
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

// Preview of the selected program (gHist->plan), recomputed when the selection or the stored program
// changed, when a start temperature (PV) moved by 1 degC or more, or every 10 s. The program is loaded
// into a temporary buffer each time (no permanent 3.9 kB copy: DRAM is short on the CYD).
static float selTotal = 0;
static float selPvKey[NUM_CHANNELS] = {NAN, NAN};
static uint32_t selAt = 0;
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
    if (isnan(a) != isnan(b) || (!isnan(a) && fabsf(a - b) >= 1.0f)) moved = true;
  }
  if (!selForce && sel == selLoaded && gen == selGen && !moved && millis() - selAt < 10000) return selTotal;
  DuoProgram* p = (DuoProgram*)malloc(sizeof(DuoProgram));
  if (!p) return selTotal;  // try again next time
  selForce = false;
  selLoaded = sel;
  selGen = gen;
  selAt = millis();
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) selPvKey[ch] = pv ? pv[ch] : NAN;
  char err[96];
  selValid = programLoad(sel, *p, err, sizeof(err));
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
  free(p);
  selTotal = total;
  return total;
}

// run: forecast into gHist->runPlan; else preview of the selected program into gHist->plan.
// Returns the (projected) total; remaining = the run's forecast.
static float updatePlan(const float* pv, float& remaining) {
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
  d.spActive = d.run || gLive.manualOn;
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
  int32_t row[CHART_N];
  auto put = [&](Tenth16* dst) {
    for (int i = 0; i < CHART_N; ++i) dst[i].v = row[i] == INT32_MIN ? (int16_t)INT16_MIN : (int16_t)row[i];
  };
  liveRow(k, LV_PV, B, row);
  put(d.pv);
  liveRow(k, LV_SP, B, row);
  put(d.sp);
  liveRow(k, LV_PV, T, row);
  put(d.pvT);
  liveRow(k, LV_SP, T, row);
  put(d.spT);
#ifdef CHART_LOHI
  liveRow(k, LV_LO, B, row);
  put(d.lo);
  liveRow(k, LV_HI, B, row);
  put(d.hi);
  liveRow(k, LV_LO, T, row);
  put(d.loT);
  liveRow(k, LV_HI, T, row);
  put(d.hiT);
#endif
  for (int i = 0; i < CHART_N; ++i) {
    d.plan[i] = gHist->plan[B][i];
    d.planT[i] = gHist->plan[T][i];
  }
}

// ---- /api/chart (web task; built under gLock, sent afterwards)

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
static void jDeltas(String& o, const char* key, TenthRow* pv, TenthRow* ext, bool below) {
  o += ",\"";
  o += key;
  o += "\":[";
  for (int c = 0; c < NUM_CHANNELS; ++c) {
    if (c) o += ',';
    o += '[';
    for (int i = 0; i < CHART_N; ++i) {
      if (i) o += ',';
      const int32_t p = pv[c][i].v, x = ext[c][i].v;
      int32_t d = (p == INT32_MIN || x == INT32_MIN) ? 0 : below ? p - x : x - p;
      if (d < 0) d = 0;
      o += String((long)d);
    }
    o += ']';
  }
  o += ']';
}

static void jArr(String& o, const Tenth* a, int n) {
  o += '[';
  for (int i = 0; i < n; ++i) {
    if (i) o += ',';
    jNum(o, a[i]);
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
  o += String((int)g.src);
  o += ',';
  o += String((g.idle ? 1 : 0) | (g.ramp ? 2 : 0));
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
  o += String((int)e.type);
  o += ',';
  o += e.ch == 0xFF ? String(-1) : String((int)e.ch);
  o += ',';
  o += String((int)e.idx);
  o += ']';
}

static void fcEv(const PlanEv& e, void* u) {
  FcSink& k = *(FcSink*)u;
  if (e.type == EV_STEP) return;
  jEv(k.ev, e);
}

void chartJson(String& o, bool run) {
  o.reserve(run ? 11000 : 8500);
  if (!run) {
    Guard g;
    const uint32_t k = liveK(), sh = liveShift(k);
    const uint32_t have = (gHist->liveTotal - liveOldest()) * LIVE_SAMPLE_SEC;
    // bin: seconds per point; the newest point is now, the others end `sh` s later than a whole
    // number of bins before it; have / cap: seconds recorded / the history's size
    o = "{\"mode\":\"live\",\"bin\":";
    o += String(k * LIVE_SAMPLE_SEC);
    o += ",\"span\":";
    o += String(liveSpan());
    o += ",\"sh\":";
    o += String(sh);
    o += ",\"have\":";
    o += String(have);
    o += ",\"cap\":";
    o += String(liveHistSec());
    int32_t pv[CHART_N], x[CHART_N];  // 2.4 kB of stack
    auto arr = [&](const int32_t* a, const int32_t* ref, bool below) {
      o += '[';
      for (int i = 0; i < CHART_N; ++i) {
        if (i) o += ',';
        if (!ref) {
          jNum(o, a[i] == INT32_MIN ? NAN : a[i] * 0.1f);
          continue;
        }
        int32_t d = (a[i] == INT32_MIN || ref[i] == INT32_MIN) ? 0 : below ? ref[i] - a[i] : a[i] - ref[i];
        o += String((long)(d < 0 ? 0 : d));
      }
      o += ']';
    };
    o += ",\"pv\":[";
    for (int c = 0; c < NUM_CHANNELS; ++c) {
      if (c) o += ',';
      liveRow(k, LV_PV, c, pv);
      arr(pv, nullptr, false);
    }
    o += "],\"sp\":[";
    for (int c = 0; c < NUM_CHANNELS; ++c) {
      if (c) o += ',';
      liveRow(k, LV_SP, c, x);
      arr(x, nullptr, false);
    }
    // "dlo" / "dhi" as for the run (see jDeltas)
    for (int f = 0; f < 2; ++f) {
      o += f ? "],\"dhi\":[" : "],\"dlo\":[";
      for (int c = 0; c < NUM_CHANNELS; ++c) {
        if (c) o += ',';
        liveRow(k, LV_PV, c, pv);
        liveRow(k, f ? LV_HI : LV_LO, c, x);
        arr(x, pv, f == 0);
      }
    }
    o += "]}";
    return;
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
    return;
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
  o += String(gHist->runGen);
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
  o += ",\"rem\":";
  jNum(o, rem);
  o += ",\"pv\":[";
  for (int c = 0; c < NUM_CHANNELS; ++c) {
    if (c) o += ',';
    jArr(o, gHist->runPv[c], CHART_N);
  }
  o += "],\"sp\":[";
  for (int c = 0; c < NUM_CHANNELS; ++c) {
    if (c) o += ',';
    jArr(o, gHist->runSp[c], CHART_N);
  }
  o += ']';
  jDeltas(o, "dlo", gHist->runPv, gHist->runLo, true);
  jDeltas(o, "dhi", gHist->runPv, gHist->runHi, false);
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
}

// ------------------------------------------------------------------ commands

static bool anyFault() {
  Guard g;
  for (int i = 0; i < NUM_CHANNELS; ++i)
    if (gLive.ch[i].fault) return true;
  return false;
}

static bool tuning(int ch) {
  if (s.ch[ch].type != CH_LOCAL) return false;
  LocalStatus st;
  localCtrlGet(ch, st);
  return st.tuning;
}

// keeps the run's files and RTC record up to date
static void saveRunFile() {
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    runInfo.rampSaved[ch] = drv[ch].rampSaved;
    runInfo.rampOrig[ch] = drv[ch].rampOriginal;
  }
  runFileSave(runInfo, *runProg);
}

static void runTrack(bool force) {
  const RunPos p = eng.position();
  rtcRun.magic = RTC_MAGIC;
  rtcRun.id = runInfo.id;
  rtcRun.pos = p;
  rtcRun.sum = rtcSum(rtcRun);
  bool moved = false;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) moved |= p.i[ch] != posFlash.i[ch] || p.st[ch] != posFlash.st[ch];
  if (force || moved || millis() - posFlashMs >= 60000) {
    runPosSave(runInfo.id, p);
    posFlash = p;
    posFlashMs = millis();
  }
}

static void runForget() {
  rtcRun.magic = 0;
  runFileClear();
}

static DuoProgram* startBuf = nullptr;  // "Start without saving": the web page's draft (under gLock)

bool startDraft(int idx, const DuoProgram& p) {
  if (idx < 0 || idx >= PROG_MAX) return false;
  {
    Guard g;
    if (!startBuf) startBuf = (DuoProgram*)malloc(sizeof(DuoProgram));
    if (!startBuf) return false;
    *startBuf = p;
  }
  return sendCmd(CMD_START, idx, 0, -1, 1);
}

static bool runChecks(const DuoProgram& p, float* pv, bool* ok, const char* verb);
static void takeChannels(const DuoProgram& p, const RunFileInfo* ri);
static void dismissInterrupted();

// ------------------------------------------------------------ SD run log (S3, sdlog.h)
static void sdlogStart(const DuoProgram& p, bool resumed) {
  if (!sdlogSlot()) return;
  sdlogRunStart(p.name, p.type == PROG_TL, resumed);
  char b[96];
  snprintf(b, sizeof(b), "bottom = %s, top = %s", s.ch[CH_BOTTOM].name, s.ch[CH_TOP].name);
  sdlogEvent(LK_INFO, b);
}

// one row a second while a run is active (control task, under gLock)
static void sdRow(const float* pv, const bool* pvOk, const float* spNow, const ChOut* out) {
  static uint32_t at = 0;
  if (!sdlogSlot() || (at && millis() - at < 1000)) return;
  at = millis();
  SdRow r;
  r.runS = eng.elapsed();
  if (runProg && runProg->type == PROG_TL) {
    char a[2][18];
    for (int i = 0; i < NUM_CHANNELS; ++i) {
      const StageInfo si = eng.lane(i);
      if (progUses(*runProg, i)) snprintf(a[i], sizeof(a[i]), "%c%d %s", i ? 'T' : 'B', si.idx + 1, stageName(si.st));
      else a[i][0] = 0;
    }
    snprintf(r.where, sizeof(r.where), "%s%s%s", a[0], a[0][0] && a[1][0] ? " / " : "", a[1]);
  } else {
    const StageInfo si = eng.seq();
    snprintf(r.where, sizeof(r.where), "P%d %s", si.idx + 1, stageName(si.st));
  }
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    const ChannelLive& C = gLive.ch[i];
    r.pv[i] = pvOk[i] ? pv[i] : NAN;
    r.sp[i] = spNow[i];
    r.prog[i] = out[i].inProgram && !out[i].idle ? out[i].sp : NAN;
    r.out[i] = C.outValid ? C.out : NAN;
    const ChState cs = eng.chState(i);
    r.state[i] = cs != ChState::None ? chStateName(cs) : man[i].on ? "manual" : C.type == CH_OFF ? "" : "-";
  }
  sdlogRow(r);
}

static void startProgram(int idx, bool draft) {
  if (eng.active()) return;
  if (anyFault()) {
    setMsg("Cannot start: acknowledge the channel fault first");
    return;
  }
  if (idx < 0 || idx >= PROG_MAX) {
    Guard g;
    idx = gSettings.selProgram < PROG_MAX ? gSettings.selProgram : 0;
  }
  // starting ends the "interrupted" offer (its running copy is replaced); the controller gradients it
  // saved are carried over to the channels the new run takes
  const RunFileInfo* carry = nullptr;
  if (intr.valid) {
    dismissInterrupted();
    carry = &cleanInfo;
  }
  DuoProgram& p = *runProg;
  char err[96];
  bool loaded;
  {
    Guard g;  // the web reads the running copy under the lock
    if (draft) {
      loaded = startBuf != nullptr;
      if (loaded) p = *startBuf;
      else strlcpy(err, "no draft to start", sizeof(err));
      free(startBuf);  // only needed for this start (heap is short on the CYD)
      startBuf = nullptr;
    } else {
      loaded = programLoad(idx, p, err, sizeof(err));
    }
  }
  if (!loaded) {
    say("Cannot start: %s", err);
    return;
  }
  if (progItems(p) == 0) {
    say("Cannot start: \"%s\" has no phases/steps", p.name);
    return;
  }
  float pv[NUM_CHANNELS];
  bool ok[NUM_CHANNELS];
  if (!runChecks(p, pv, ok, "Cannot start")) return;
  if (!eng.start(&p, pv, ok, s.bottomBand)) {
    setMsg("Cannot start: the program has no phases/steps");
    return;
  }
  runIdx = (int8_t)idx;
  editCount = savedCount = 0;
  takeChannels(p, carry);
  for (int ch = 0; ch < NUM_CHANNELS; ++ch)
    if (progUses(p, ch)) intrCleanup[ch] = false;  // the new run restores them at its end
  float rem;
  {
    Guard g;
    histStartRun(eng.forecast(nullptr, nullptr, pv));
  }
  updatePlan(pv, rem);
  memset(&runInfo, 0, sizeof(runInfo));
  runInfo.slot = (int8_t)idx;
  runInfo.id = esp_random() | 1;
  saveRunFile();
  runTrack(true);
  sdlogStart(p, false);
  say("Program \"%s\" started%s", p.name, draft ? " (unsaved changes)" : "");
}

// Start / resume conditions (channels on, setpoint registers, temperatures, limits); pv / ok = the
// measured values. False after a message "<verb>: ..."
static bool runChecks(const DuoProgram& p, float* pv, bool* ok, const char* verb) {
  char err[96];
  const char* names[NUM_CHANNELS] = {s.ch[0].name, s.ch[1].name};
  float lo[NUM_CHANNELS], hi[NUM_CHANNELS];
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    {
      Guard g;
      ok[ch] = gLive.ch[ch].pvValid;
      pv[ch] = gLive.ch[ch].pv;
    }
    lo[ch] = s.ch[ch].spMin;
    hi[ch] = s.ch[ch].spMax;
    if (!progUses(p, ch)) continue;
    const char* n = names[ch];
    if (s.ch[ch].type == CH_OFF) {
      say("%s: \"%s\" uses %s, which is off (Settings)", verb, p.name, n);
      return false;
    }
    if (tuning(ch)) {
      say("%s: autotune is running on %s", verb, n);
      return false;
    }
    if (!chan(ch).spConfigured()) {
      say("%s: %s has no setpoint register (web: Controller)", verb, n);
      return false;
    }
    if (!ok[ch]) {
      say("%s: no valid temperature from %s", verb, n);
      return false;
    }
  }
  if (!progCheckLimits(p, lo, hi, err, sizeof(err), names)) {
    say("%s: %s", verb, err);
    return false;
  }
  // The controller silently clamps setpoints to SP.LO..SP.Hi: check before starting
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    Controller* mc = progUses(p, ch) ? mbOf(ch) : nullptr;
    if (!mc || mc->reg().spLo == REG_UNSET || mc->reg().spHi == REG_UNSET) continue;
    float l, h;
    uint8_t f1 = 0, f2 = 0;
    if (mc->readScaled(mc->reg().spLo, l, f1) != MbResult::Ok || mc->readScaled(mc->reg().spHi, h, f2) != MbResult::Ok ||
        f1 != VAL_OK || f2 != VAL_OK)
      continue;
    const float cl[NUM_CHANNELS] = {ch == 0 ? l : -1e9f, ch == 1 ? l : -1e9f};
    const float chh[NUM_CHANNELS] = {ch == 0 ? h : 1e9f, ch == 1 ? h : 1e9f};
    if (!progCheckLimits(p, cl, chh, err, sizeof(err), names)) {
      say("%s (controller limits): %.90s", verb, err);
      return false;
    }
  }
  return true;
}

// The run takes over its channels (manual off, controller gradient off, front lock). ri: resuming an
// interrupted run - the gradient to restore at the end is the one it saved.
static void takeChannels(const DuoProgram& p, const RunFileInfo* ri) {
  const char* names[NUM_CHANNELS] = {s.ch[0].name, s.ch[1].name};
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    drv[ch] = Drive();
    if (!progUses(p, ch)) continue;  // manual channel: untouched
    // the program takes over from manual mode on its channels
    Manual& m = man[ch];
    m.on = m.pending = m.offPending = false;
    Controller* mc = mbOf(ch);
    if (hasRampReg(ch)) {  // remember the controller's own gradient to restore it at the end
      int16_t raw;
      if (ri && ri->rampSaved[ch]) {
        drv[ch].rampOriginal = ri->rampOrig[ch];
        drv[ch].rampSaved = true;
      } else if (mc->readRaw(mc->reg().rampRate, raw) == MbResult::Ok) {
        drv[ch].rampOriginal = raw;
        drv[ch].rampSaved = true;
      }
      writeRampOff(ch);
    }
    if (mc && s.ch[ch].mb.lockFront) mbWrite(ch, mc->reg().remote, 1, "Front-lock write");
    if (!chan(ch).canSwitchOff() && s.ch[ch].type == CH_MODBUS)
      LogSerial.printf("[run] %s has no outputs-off register: Idle keeps the controller's setpoint\n", names[ch]);
  }
}

// "Resume from the interrupted phase" (spec 9.3.5)
static void resumeInterrupted() {
  if (eng.active()) return;
  if (!intr.valid) {
    setMsg("No interrupted run to resume");
    return;
  }
  if (anyFault()) {
    setMsg("Cannot resume: acknowledge the channel fault first");
    return;
  }
  DuoProgram& p = *runProg;  // the running copy, loaded from /duo/run.json at boot
  float pv[NUM_CHANNELS];
  bool ok[NUM_CHANNELS];
  if (!runChecks(p, pv, ok, "Cannot resume")) return;
  if (!eng.resumeAt(&p, intr.pos, pv, ok, s.bottomBand, intr.info.edited)) {
    setMsg("Cannot resume: the interrupted phase is not in the program");
    return;
  }
  runIdx = intr.info.slot;
  editCount = intr.info.edited ? 1 : 0;
  savedCount = 0;
  takeChannels(p, &intr.info);
  float rem;
  {
    Guard g;
    histStartRun(eng.elapsed() + eng.forecast(nullptr, nullptr, pv));
    gHist->runStLast = (int16_t)((int)(eng.elapsed() / gHist->runBin) - 1);  // lanes empty before the resume
  }
  updatePlan(pv, rem);
  runInfo = intr.info;
  intr.valid = false;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) intrOff[ch] = false;
  saveRunFile();
  runTrack(true);
  sdlogStart(p, true);
  const int t = (int)eng.elapsed();
  if (p.type == PROG_SEQ) say("Program \"%s\" resumed: phase %d again (run time %d:%02d)", p.name, eng.seq().idx + 1, t / 60, t % 60);
  else say("Program \"%s\" resumed at run time %d:%02d", p.name, t / 60, t % 60);
}

static void dismissInterrupted() {
  if (!intr.valid) return;
  cleanInfo = intr.info;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    intrCleanup[ch] = progUses(*runProg, ch) && s.ch[ch].type != CH_OFF;
    intrOff[ch] = false;
  }
  intr.valid = false;
  runForget();
  setMsg("Interrupted run dismissed");
}

// outputs off (+ the controller's gradient and front panel back) for a dismissed run
static bool cleanupChannel(int ch) {
  Channel& c = chan(ch);
  bool ok = true;
  if (c.canSwitchOff()) ok &= c.setOutputs(false, lastErr[ch], sizeof(lastErr[0]));
  Controller* mc = mbOf(ch);
  if (mc && cleanInfo.rampSaved[ch] && hasRampReg(ch)) ok &= mbWrite(ch, mc->reg().rampRate, cleanInfo.rampOrig[ch], "Ramp-rate restore");
  if (mc && s.ch[ch].mb.lockFront) ok &= mbWrite(ch, mc->reg().remote, 0, "Front-lock write");
  return ok;
}

static void manualSwitch(int ch, bool on) {
  if (ch < 0 || ch >= NUM_CHANNELS) return;
  Manual& m = man[ch];
  Channel& c = chan(ch);
  const char* name = s.ch[ch].name;
  if (on) {
    if (inRun(ch)) {
      say("%s is in the running program (stop it first)", name);
      return;
    }
    if (c.type() == CH_OFF) {
      say("%s is off (Settings: channel type)", name);
      return;
    }
    char ft[64];
    if (c.fault(ft, sizeof(ft))) {
      say("%s: acknowledge the fault first (%s)", name, ft);
      return;
    }
    if (tuning(ch)) {
      say("%s: autotune is running (stop it first)", name);
      return;
    }
    if (!c.spConfigured()) {
      setMsg("Set the setpoint register address first (web: Controller)");
      return;
    }
    m.on = true;
    m.pending = true;
    m.offPending = false;
    say("%s: manual setpoint on", name);
  } else {
    if (!m.on) return;
    m.on = false;
    spHeld[ch] = false;
    m.pending = false;
    m.offPending = c.canSwitchOff();
    say(m.offPending ? "%s: manual off, outputs switched off"
                                                  : "%s: manual off (no outputs-off register: controller keeps its setpoint)",
             name);
  }
}

// (Re)opens the bus and selects the channel backends after a settings change
static void applySettings(const Settings& prev, bool first) {
  localCtrlRelayLevel(s.relayActiveLow != 0);  // (first: the relay pin becomes an output, off)
  modbusBusBegin(bus, s);
  eng.setBand(s.bottomBand);
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    const bool typeChanged = first || prev.ch[i].type != s.ch[i].type;
    if (typeChanged && !first && inRun(i)) {
      eng.stop();
      say("Stopped: type of channel %s changed", s.ch[i].name);
    }
    slots[i].configure(i, &bus, s.ch[i]);
    if (!first && !man[i].on) man[i].sp = s.ch[i].manualSp;  // may have been changed through the API
    if (typeChanged && !first) man[i].on = man[i].pending = man[i].offPending = false;
    Guard g;
    gLive.ch[i].type = s.ch[i].type;
    strlcpy(gLive.ch[i].name, s.ch[i].name, sizeof(gLive.ch[i].name));
    if (typeChanged) {
      gLive.ch[i].commOk = false;
      gLive.ch[i].lastErr[0] = 0;
    }
  }
}

static void runEdit(const Cmd& c) {
  const int ch = c.arg;
  if (!eng.active()) {
    setMsg("No program is running");
    return;
  }
  if (ch < 0 || ch >= NUM_CHANNELS) {
    setMsg("Run edit: channel must be 0 (bottom) or 1 (top)");
    return;
  }
  char err[96] = "";
  bool ok;
  const char* name = s.ch[ch].name;
  {
    Guard g;  // the edits change the running copy, which the web reads
    switch (c.sub) {
      case EDIT_HOLD: ok = eng.setHold(ch, c.idx, c.value, err, sizeof(err)); break;
      case EDIT_EXTEND: ok = eng.extendHold(ch, c.value, err, sizeof(err)); break;
      case EDIT_RAMP: ok = eng.setRamp(ch, c.idx, c.value, err, sizeof(err)); break;
      case EDIT_TARGET:
        if (c.value < s.ch[ch].spMin || c.value > s.ch[ch].spMax) {
          snprintf(err, sizeof(err), "%.1f is outside the %s limits %.0f .. %.0f", c.value, name, s.ch[ch].spMin,
                   s.ch[ch].spMax);
          ok = false;
        } else {
          ok = eng.setTarget(ch, c.idx, c.value, err, sizeof(err));
        }
        break;
      default: ok = false; strlcpy(err, "unknown edit", sizeof(err)); break;
    }
  }
  if (ok) {
    ++editCount;
    runInfo.edited = 1;
    saveRunFile();  // the running copy survives a reset
    static const char* const W[] = {"hold time", "hold extended", "ramp time", "target"};
    say("Run edit (%s, %s): %s%.1f", name, c.sub < 4 ? W[c.sub] : "?",
             c.sub == EDIT_EXTEND && c.value > 0 ? "+" : "", c.value);
  } else {
    say("Run edit not possible: %s", err);
  }
}

static void saveRun() {
  if (runIdx < 0 || editCount == 0) {
    setMsg("No run edits to save");
    return;
  }
  char err[96];
  bool ok;
  {
    Guard g;
    ok = programSave(runIdx, *runProg, err, sizeof(err));
  }
  if (ok) {
    savedCount = editCount;
    runInfo.edited = 0;
    saveRunFile();
    say("Run changes saved into \"%s\"", runProg->name);
  } else {
    say("Saving the run changes failed: %s", err);
  }
}

// "Keep the stored program" after a run with edits: the question is answered (touch, web and serial close it)
static void discardRun() {
  if (editCount == savedCount) return;
  savedCount = editCount;
  runInfo.edited = 0;
  logRun("Run changes not saved - \"%s\" stays as stored", runProg ? runProg->name : "");
}

static void handleCmd(const Cmd& c) {
  switch (c.type) {
    case CMD_START: startProgram(c.arg, c.sub == 1); break;
    case CMD_PAUSE:
      if (eng.state() == RunState::Running) { eng.pause(); setMsg("Paused"); }
      break;
    case CMD_RESUME:
      if (eng.state() == RunState::Paused) { eng.resume(); setMsg("Resumed"); }
      break;
    case CMD_STOP:
      if (eng.active()) { eng.stop(); setMsg("Stopped by user"); }
      break;
    case CMD_NEXT:
      if (eng.progPaused()) { eng.cont(); setMsg("Continued"); }
      else if (eng.next()) setMsg("Next phase");
      else if (eng.active() && eng.type() == PROG_TL) setMsg("Next phase is not available for Timeline programs");
      break;
    case CMD_CONTINUE:
      if (eng.cont()) setMsg("Continued");
      break;
    case CMD_RUN_EDIT: runEdit(c); break;
    case CMD_SAVE_RUN: saveRun(); break;
    case CMD_DISCARD_RUN: discardRun(); break;
    case CMD_RUN_RECOVER: resumeInterrupted(); break;
    case CMD_RUN_DISMISS: dismissInterrupted(); break;
    case CMD_SETTINGS_CHANGED: {
      static Settings prev;  // static: large
      prev = s;
      {
        Guard g;
        s = gSettings;
      }
      applySettings(prev, false);
      break;
    }
    case CMD_MANUAL_ON: manualSwitch(c.arg, true); break;
    case CMD_MANUAL_OFF: manualSwitch(c.arg, false); break;
    case CMD_ACK_FAULT: {
      if (c.arg < 0 || c.arg >= NUM_CHANNELS) break;
      char err[80] = "";
      if (chan(c.arg).ack(err, sizeof(err))) {
        say("%s: fault acknowledged", s.ch[c.arg].name);
      } else {
        say("%s: fault not cleared: %s", s.ch[c.arg].name, err);
      }
      break;
    }
    case CMD_TUNE_START: {
      if (c.arg < 0 || c.arg >= NUM_CHANNELS) break;
      const char* name = s.ch[c.arg].name;
      if (s.ch[c.arg].type != CH_LOCAL) {
        say("%s: autotune is for local channels (Modbus controllers tune themselves)", name);
        break;
      }
      if (inRun(c.arg)) {
        say("%s: stop the program before the autotune", name);
        break;
      }
      Manual& m = man[c.arg];  // the tune takes over the channel
      m.on = m.pending = m.offPending = false;
      char err[96];
      if (localCtrlTuneStart(c.arg, c.value, err, sizeof(err))) {
        say("%s: autotune started at %.0f C", name, c.value);
      } else {
        say("%s: autotune not started: %s", name, err);
      }
      break;
    }
    case CMD_TUNE_STOP:
      if (c.arg < 0 || c.arg >= NUM_CHANNELS) break;
      localCtrlTuneStop(c.arg);
      say("%s: autotune stopped", s.ch[c.arg].name);
      break;
    case CMD_MANUAL_SP:
      if (c.arg < 0 || c.arg >= NUM_CHANNELS) break;
      man[c.arg].sp = clampSp(c.arg, c.value);
      if (man[c.arg].on) man[c.arg].pending = true;
      break;
  }
}

// ------------------------------------------------------------------ bus tools

static void processRequest(BusRequest& r) {
  r.ok = false;
  r.err[0] = 0;
  r.n = 0;
  Controller* ctlp = r.ch < NUM_CHANNELS ? slots[r.ch].active->modbus() : nullptr;
  if (!ctlp) {
    snprintf(r.err, sizeof(r.err), "%s is not a Modbus channel", r.ch < NUM_CHANNELS ? s.ch[r.ch].name : "?");
    return;
  }
  Controller& ctl = *ctlp;
  const RegMap& reg = ctl.reg();
  auto describeError = [&](MbResult res, const char* what, char* out, size_t n) {
    mbDescribe(res, ctl.lastException(), what, out, n);
  };
  if (r.op == BUS_READ) {
    uint16_t cnt = r.count ? r.count : 1;
    if (cnt > 60) cnt = 60;
    const MbResult res = ctl.readBlock(r.addr, cnt, r.values, r.fn);
    if (res == MbResult::Ok) { r.ok = true; r.n = cnt; }
    else describeError(res, "Read", r.err, sizeof(r.err));
  } else if (r.op == BUS_WRITE) {
    const MbResult res = ctl.writeRaw(r.addr, r.value);
    if (res == MbResult::Ok) r.ok = true;
    else describeError(res, "Write", r.err, sizeof(r.err));
  } else if (r.op == BUS_FINDSP) {
    float ref;
    uint8_t flag = 0;
    const MbResult res = ctl.readScaled(reg.spEff, ref, flag);
    if (res != MbResult::Ok || flag != VAL_OK) {
      strlcpy(r.err, "Cannot read the effective setpoint (SP.EF) as reference", sizeof(r.err));
      return;
    }
    r.refValue = ref;
    uint16_t to = r.to;
    if (to < r.from) to = r.from;
    if (to - r.from > 300) to = r.from + 300;
    for (uint32_t a = r.from; a <= to && r.n < 64; ++a) {
      if (a == reg.spEff || a == reg.pv) continue;
      int16_t raw;
      if (ctl.readRaw((uint16_t)a, raw) != MbResult::Ok) continue;
      if (Controller::classify(raw) != VAL_OK) continue;
      if (fabsf(raw / reg.scale - ref) < 0.051f) r.values[r.n++] = (int16_t)a;
    }
    r.ok = true;
  }
}

// ------------------------------------------------------------------ cycle

static void pollChannel(int i) {
  static ChannelLive v;  // value fields from the backend (static: keep it off the task stack)
  char err[80];
  memset(&v, 0, sizeof(v));
  Channel& c = *slots[i].active;
  const bool ok = c.poll(v, err, sizeof(err));
  bool changed;
  {
    Guard g;
    ChannelLive& L = gLive.ch[i];
    changed = ok != L.commOk || (!ok && strcmp(err, L.lastErr) != 0);
    L.commOk = ok;
    if (ok) { L.okCount++; L.lastOkMs = millis(); L.lastErr[0] = 0; }
    else if (c.type() != CH_OFF) { L.errCount++; strlcpy(L.lastErr, err, sizeof(L.lastErr)); }
    L.pvValid = v.pvValid; L.pv = v.pv; L.pvFlag = v.pvFlag;
    L.spEffValid = v.spEffValid; L.spEff = v.spEff;
    L.outValid = v.outValid; L.out = v.out;
    L.statusValid = v.statusValid; L.status = v.status;
    L.alarmValid = v.alarmValid; L.alarm = v.alarm;
    L.coldC = v.coldC;
    L.outOn = v.outOn;
  }
  if (changed && c.type() != CH_OFF) {  // log link changes, not every failed poll
    if (ok) logEvent("%s (%s): link ok", s.ch[i].name, channelTypeName(c.type()));
    else logEvent("%s (%s): %s", s.ch[i].name, channelTypeName(c.type()), err);
  }
  // autotune finished / failed: log it once
  static bool wasTuning[NUM_CHANNELS];
  if (c.type() == CH_LOCAL) {
    LocalStatus ls;
    localCtrlGet(i, ls);
    if (wasTuning[i] && !ls.tuning) {
      LocalTune t;
      localCtrlTuneGet(i, t);
      if (t.state == TuneState::Done)
        logEvent("%s: autotune %s -> kp %.3f ki %.4f kd %.2f (accept or discard)", s.ch[i].name, t.msg, t.res.kp,
                      t.res.ki, t.res.kd);
      else
        logEvent("%s: autotune failed: %s", s.ch[i].name, t.msg);
      say("%s: autotune %s", s.ch[i].name, t.state == TuneState::Done ? "done - accept or discard (web)" : t.msg);
    }
    wasTuning[i] = ls.tuning;
  } else {
    wasTuning[i] = false;
  }
  // latched fault (runaway, sensor, over temperature): outputs are already off
  char ft[64];
  const uint8_t f = c.fault(ft, sizeof(ft));
  uint8_t prev;
  {
    Guard g;
    ChannelLive& L = gLive.ch[i];
    prev = L.fault;
    L.fault = f;
    strlcpy(L.faultText, ft, sizeof(L.faultText));
  }
  if (f && !prev) {
    Manual& m = man[i];
    m.on = m.pending = false;
    m.offPending = false;
    if (eng.active()) {
      // a fault stops the run (spec 9.2, 9.3.5); on the faulted channel the outputs stay off
      eng.stop();
      say("Stopped: %s fault: %s - acknowledge it (web)", s.ch[i].name, ft);
    } else {
      say("%s fault: %s - acknowledge it (web)", s.ch[i].name, ft);
    }
  }
}

// Local channels every cycle, one bus channel per cycle (round robin)
static void pollChannels() {
  for (int i = 0; i < NUM_CHANNELS; ++i)
    if (!slots[i].active->onBus()) pollChannel(i);
  for (int k = 0; k < NUM_CHANNELS; ++k) {
    const int i = (busTurn + k) % NUM_CHANNELS;
    if (slots[i].active->onBus()) {
      pollChannel(i);
      busTurn = (uint8_t)((i + 1) % NUM_CHANNELS);
      break;
    }
  }
}

static void driveSetpoint(int ch) {
  const ChOut o = eng.out(ch);
  if (!o.inProgram) return;
  Drive& d = drv[ch];
  Channel& c = chan(ch);
  char* err = lastErr[ch];
  if (o.idle) {  // no target yet / Idle: outputs off
    if (d.outOn != 0) {
      bool ok = writeRampOff(ch);
      if (c.canSwitchOff()) ok = ok && c.setOutputs(false, err, sizeof(lastErr[0]));
      if (ok) d.outOn = 0;
    }
    d.syncedGen = 0xFFFFFFFF;
    return;
  }
  if (d.outOn != 1) {
    if (!c.setOutputs(true, err, sizeof(lastErr[0]))) return;
    d.outOn = 1;
  }
  const bool needSync = o.gen != d.syncedGen;
  const bool ctrlRamp = s.ch[ch].type == CH_MODBUS && s.ch[ch].mb.rampMode == RAMP_CONTROLLER && hasRampReg(ch);

  if (ctrlRamp && needSync) {
    bool ok = true;
    d.hostFallback = false;
    if (!o.moving) {
      ok = writeRampOff(ch) && writeSp(ch, o.sp);  // hold, jump, or frozen (pause)
    } else if (o.rampLeft > 1.0f) {
      const float rate = fabsf(o.target - o.sp) / (o.rampLeft / 60.0f);
      // 1dP gradient: 0.1/min resolution, and the raw value must fit in int16
      if (rate >= 0.1f && rate * mbOf(ch)->reg().scale <= 30000.0f) {
        // start the controller's own ramp from the current program value
        ok = writeRampOff(ch) && writeSp(ch, o.sp) && writeRampRate(ch, rate) && writeSp(ch, o.target);
      } else {
        d.hostFallback = true;  // too slow/fast for the controller gradient: step from here
        ok = writeRampOff(ch) && writeSp(ch, o.sp);
      }
    } else {
      ok = writeRampOff(ch) && writeSp(ch, o.target);
    }
    if (ok) d.syncedGen = o.gen;
    return;
  }

  if (!ctrlRamp || d.hostFallback) {
    // a local channel takes every new value (no controller EEPROM to spare)
    const bool local = s.ch[ch].type == CH_LOCAL;
    const float minStep = local ? 0.0f : s.ch[ch].mb.minStep;
    const uint32_t minMs = local ? 0 : (uint32_t)s.ch[ch].mb.minWriteSec * 1000UL;
    const bool due = needSync || isnan(d.lastWritten) ||
                     (fabsf(o.sp - d.lastWritten) >= minStep && o.sp != d.lastWritten && millis() - d.lastWriteMs >= minMs);
    if (due && writeSp(ch, o.sp)) d.syncedGen = o.gen;
  }
}

// end action of one channel (spec 9.3.5); true when everything was written. keptSp: the setpoint
// the channel goes on with (NaN: outputs off / fault)
static bool applyEndAction(int ch, float& keptSp) {
  bool ok = true;
  keptSp = NAN;
  const bool aborted = eng.state() == RunState::Aborted;
  const ChOut o = eng.out(ch);
  Channel& c = chan(ch);
  char* err = lastErr[ch];
  char ft[64];
  if (c.fault(ft, sizeof(ft))) return true;  // latched fault: the outputs stay off
  if (hasRampReg(ch)) ok &= writeRampOff(ch);  // freeze a running controller ramp
  float hold = o.sp;
  {
    Guard g;
    if (aborted && s.ch[ch].type == CH_MODBUS && gLive.ch[ch].spEffValid) hold = gLive.ch[ch].spEff;
  }
  switch (s.ch[ch].endAction) {
    case END_KEEP:
      if (o.idle) {
        if (c.canSwitchOff()) ok &= c.setOutputs(false, err, sizeof(lastErr[0]));
      } else {
        ok &= writeSp(ch, hold);
        keptSp = hold;
      }
      break;
    case END_SET_SP:
      ok &= c.setOutputs(true, err, sizeof(lastErr[0])) && writeSp(ch, s.ch[ch].endSp);
      keptSp = s.ch[ch].endSp;
      break;
    case END_OUTPUTS_OFF:
      if (!o.idle) ok &= writeSp(ch, hold);
      if (c.canSwitchOff()) ok &= c.setOutputs(false, err, sizeof(lastErr[0]));
      break;
  }
  Drive& d = drv[ch];
  if (d.rampSaved && mbOf(ch)) ok &= mbWrite(ch, mbOf(ch)->reg().rampRate, d.rampOriginal, "Ramp-rate restore");
  if (s.ch[ch].mb.lockFront && mbOf(ch)) ok &= mbWrite(ch, mbOf(ch)->reg().remote, 0, "Front-lock write");
  return ok;
}

static void driveManual(int ch) {
  Manual& m = man[ch];
  Channel& c = chan(ch);
  char* err = lastErr[ch];
  if (m.on && m.pending) {
    if (c.setOutputs(true, err, sizeof(lastErr[0])) && writeSp(ch, m.sp)) m.pending = false;
  }
  if (!m.on && m.offPending) {
    if (c.setOutputs(false, err, sizeof(lastErr[0]))) m.offPending = false;
  }
}

static void cycle(float dt) {
  pollChannels();
  crumb(CR_CONTROL, C_CYCLE);

  float pv[NUM_CHANNELS], spEff[NUM_CHANNELS];
  bool pvOk[NUM_CHANNELS], spEffOk[NUM_CHANNELS], commOk[NUM_CHANNELS];
  uint32_t lastOk[NUM_CHANNELS];
  {
    Guard g;
    for (int i = 0; i < NUM_CHANNELS; ++i) {
      const ChannelLive& L = gLive.ch[i];
      pv[i] = L.pv; pvOk[i] = L.pvValid;
      spEff[i] = L.spEff; spEffOk[i] = L.spEffValid;
      commOk[i] = L.commOk; lastOk[i] = L.lastOkMs;
    }
  }

  // communication watchdog (channels of the running program)
  if (eng.state() == RunState::Running && s.commLossSec > 0) {
    for (int i = 0; i < NUM_CHANNELS; ++i) {
      if (!inRun(i) || millis() - lastOk[i] <= (uint32_t)s.commLossSec * 1000UL) continue;
      eng.pause();
      say("Paused: no answer from %s for %us", s.ch[i].name, s.commLossSec);
      break;
    }
  }

  const bool wasPause = eng.progPaused();
  eng.tick(dt, pv, pvOk);
  if (eng.progPaused() && !wasPause) {
    const char* t = eng.pauseText();
    say("%s%s%s - press Continue", eng.pauseMsg() ? "Action required" : "Program pause",
             *t ? ": " : "", t);
  }
  for (int i = 0; i < NUM_CHANNELS; ++i) lastErr[i][0] = 0;
  if (eng.active())
    for (int i = 0; i < NUM_CHANNELS; ++i)
      if (commOk[i]) driveSetpoint(i);
  for (int i = 0; i < NUM_CHANNELS; ++i)
    if (commOk[i]) driveManual(i);
  if (eng.active()) runTrack(false);
  // interrupted run: its channels' outputs off; dismissed: controller gradient / front panel back
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    if (!commOk[i] || inRun(i)) continue;
    if (intrOff[i] && (!chan(i).canSwitchOff() || chan(i).setOutputs(false, lastErr[i], sizeof(lastErr[0])))) intrOff[i] = false;
    if (intrCleanup[i] && cleanupChannel(i)) intrCleanup[i] = false;
  }

  // end of program (finished or aborted): end action per channel of the program
  const RunState st = eng.state();
  const bool wasActive = prevState == RunState::Running || prevState == RunState::Paused;
  if (wasActive && (st == RunState::Finished || st == RunState::Aborted)) {
    for (int i = 0; i < NUM_CHANNELS; ++i) drv[i].endPending = eng.out(i).inProgram;
    runForget();
    logRun("\"%s\" %s after %d:%02d", runProg->name, st == RunState::Finished ? "finished" : "stopped",
             (int)eng.elapsed() / 60, (int)eng.elapsed() % 60);
    sdlogRunEnd();
  }
  bool endOpen = false, endDone = false;
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    Drive& d = drv[i];
    if (!d.endPending || !commOk[i]) {
      endOpen |= d.endPending;
      continue;
    }
    float kept;
    d.endPending = !applyEndAction(i, kept);
    if (!d.endPending) {
      const uint8_t a = s.ch[i].endAction;
      spHeld[i] = !isnan(kept) || (a == END_OUTPUTS_OFF && !chan(i).canSwitchOff());
      // a kept / end setpoint continues as the manual setpoint: the manual button shows it and
      // switches the outputs off
      if (!isnan(kept)) {
        Manual& m = man[i];
        m.on = true;
        m.sp = kept;
        m.pending = m.offPending = false;
        logRun("%s: manual setpoint %.1f \xC2\xB0" "C after the program", s.ch[i].name, kept);
      }
    }
    endOpen |= d.endPending;
    endDone |= !d.endPending;
  }
  if (endDone && !endOpen && st == RunState::Finished) setMsg("Program finished");
  prevState = st;

  float remaining;
  updatePlan(pv, remaining);

  // history + publish
  static uint32_t lastLive = 0;
  float spNow[NUM_CHANNELS];
  ChOut out[NUM_CHANNELS];
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    out[i] = eng.out(i);
    char ft[4];
    if (chan(i).fault(ft, sizeof(ft))) spHeld[i] = false;
    else if (eng.active() && out[i].inProgram) spHeld[i] = !out[i].idle;
    else if (man[i].on) spHeld[i] = true;
    // only an active setpoint goes into the chart / history
    const bool act = s.ch[i].type == CH_LOCAL ? spEffOk[i] : s.ch[i].type == CH_MODBUS && spHeld[i];
    const float runSp = (eng.active() && out[i].inProgram) ? out[i].sp : NAN;
    spNow[i] = !act ? NAN : spEffOk[i] ? spEff[i] : !isnan(runSp) ? runSp : (man[i].on ? man[i].sp : NAN);
  }
  flushRunEvents();
  Guard g;
  gEngSnap = eng;
  // every reading counts for the lowest / highest value of its chart slot (the chart shows the
  // spread, so short peaks and dips between two slots are not lost)
  static float accLo[NUM_CHANNELS] = {NAN, NAN}, accHi[NUM_CHANNELS] = {NAN, NAN};
  for (int i = 0; i < NUM_CHANNELS; ++i)
    if (pvOk[i]) {
      accLo[i] = isnan(accLo[i]) ? pv[i] : fminf(accLo[i], pv[i]);
      accHi[i] = isnan(accHi[i]) ? pv[i] : fmaxf(accHi[i], pv[i]);
    }
  if (eng.active() && gHist->runValid) {
    int idx = (int)(eng.elapsed() / gHist->runBin);
    if (idx >= CHART_N) idx = CHART_N - 1;
    if (idx < 0) idx = 0;
    for (int i = 0; i < NUM_CHANNELS; ++i) {
      gHist->runPv[i][idx] = pvOk[i] ? pv[i] : NAN;
      if (pvOk[i]) {
        const float lo = gHist->runLo[i][idx], hi = gHist->runHi[i][idx];
        const bool fresh = idx > gHist->runStLast;  // first reading of this bin
        gHist->runLo[i][idx] = fresh || isnan(lo) ? pv[i] : fminf(lo, pv[i]);
        gHist->runHi[i][idx] = fresh || isnan(hi) ? pv[i] : fmaxf(hi, pv[i]);
      }
      gHist->runSp[i][idx] = spNow[i];
      const ChState cs = eng.chState(i);
      const char code = cs == ChState::None ? (man[i].on ? 'm' : '-') : CH_STATE_CODE[(int)cs];
      for (int k = gHist->runStLast + 1; k <= idx; ++k) gHist->runSt[i][k] = code;
      gHist->runSt[i][idx] = code;
    }
    gHist->runStLast = (int16_t)idx;
  }
  if (eng.active()) sdRow(pv, pvOk, spNow, out);
  // live history: one slot per LIVE_SAMPLE_SEC even if polling is slower (the same reading fills
  // the slots in between; after a longer stall they stay empty)
  const uint32_t slotMs = LIVE_SAMPLE_SEC * 1000UL;
  uint32_t n = (millis() - lastLive) / slotMs;
  if (n) {
    lastLive = (n > 60) ? millis() : lastLive + n * slotMs;
    if (n > 60) {
      const bool none[NUM_CHANNELS] = {};
      const float nan2[NUM_CHANNELS] = {NAN, NAN};
      for (uint32_t i = 0; i + 1 < n && i < gHist->liveCap; ++i) livePut(nan2, none, nan2, nan2, nan2);
      n = 1;
    }
    for (uint32_t i = 0; i < n; ++i) livePut(pv, pvOk, spNow, accLo, accHi);
    for (int c = 0; c < NUM_CHANNELS; ++c) accLo[c] = accHi[c] = NAN;
  }
  // run
  gLive.state = st;
  gLive.runProg = runIdx;
  if (runIdx >= 0) {
    strlcpy(gLive.runName, runProg->name, sizeof(gLive.runName));
    gLive.progType = runProg->type;
    gLive.phases = runProg->nPh;
    for (int i = 0; i < NUM_CHANNELS; ++i) gLive.steps[i] = runProg->nSt[i];
  }
  logRunT = eng.active() ? eng.elapsed() : NAN;
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    gLive.chSt[i] = (uint8_t)eng.chState(i);
    gLive.canExt[i] = eng.canExtend(i);
    gLive.kept[i] = (int16_t)eng.keptIdx(i);
  }
  gLive.seq = eng.seq();
  for (int i = 0; i < NUM_CHANNELS; ++i) gLive.lane[i] = eng.lane(i);
  gLive.waiting = eng.waiting();
  gLive.progPause = eng.progPaused();
  gLive.pauseMsg = eng.pauseMsg();
  strlcpy(gLive.pauseText, eng.pauseText(), sizeof(gLive.pauseText));
  gLive.runEdited = editCount != savedCount;
  gLive.elapsed = eng.elapsed();
  gLive.remaining = remaining;
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    gLive.run[i] = out[i];
    gLive.spSyncedCh[i] = lastErr[i][0] == 0;
    gLive.spWritesCh[i] = drv[i].spWrites;
    ChannelLive& L = gLive.ch[i];
    L.manualOn = man[i].on;
    L.manualSp = man[i].sp;
    L.manualSynced = !man[i].pending && !man[i].offPending;
    if (lastErr[i][0]) strlcpy(L.lastErr, lastErr[i], sizeof(L.lastErr));
  }
  // legacy views for the old UIs: bottom channel, Sequence phase / bottom step
  {
    const ChannelLive& E = gLive.ch[ENGINE_CH];
    gLive.commOk = E.commOk;
    gLive.okCount = E.okCount;
    gLive.errCount = E.errCount;
    gLive.lastOkMs = E.lastOkMs;
    strlcpy(gLive.lastErr, E.lastErr, sizeof(gLive.lastErr));
    gLive.pvValid = E.pvValid; gLive.pv = E.pv; gLive.pvFlag = E.pvFlag;
    gLive.spEffValid = E.spEffValid; gLive.spEff = E.spEff;
    gLive.outValid = E.outValid; gLive.out = E.out;
    gLive.statusValid = E.statusValid; gLive.status = E.status;
    gLive.alarmValid = E.alarmValid; gLive.alarm = E.alarm;
    gLive.manualOn = E.manualOn;
    gLive.manualSp = E.manualSp;
    gLive.manualSynced = E.manualSynced;
    const bool tl = gLive.progType == PROG_TL;
    const int lc = (tl && gLive.steps[ENGINE_CH] == 0) ? CH_TOP : ENGINE_CH;  // Timeline lane shown
    const StageInfo& si = tl ? gLive.lane[lc] : gLive.seq;
    gLive.phase = si.st;
    gLive.step = si.idx;
    gLive.stepCount = tl ? gLive.steps[lc] : gLive.phases;
    gLive.phaseEl = si.el;
    gLive.phaseDur = si.dur > si.el ? si.dur : si.el;  // wait / pause: nothing "left"
    gLive.total = gLive.elapsed + remaining;
    gLive.soakWait = gLive.waiting;
    gLive.hostSp = out[ENGINE_CH].sp;
    gLive.target = out[ENGINE_CH].target;  // NaN while the bottom is idle
    gLive.spSynced = gLive.spSyncedCh[ENGINE_CH];
    gLive.lastWrittenSp = drv[ENGINE_CH].lastWritten;
    gLive.spWrites = drv[ENGINE_CH].spWrites;
  }
  strlcpy(gLive.runMsg, runMsg, sizeof(gLive.runMsg));
  gLive.intrValid = intr.valid;
  if (intr.valid) {
    gLive.intrExact = intr.exact;
    gLive.intrPos = intr.pos;
    strlcpy(gLive.intrReason, intr.reason, sizeof(gLive.intrReason));
  }

  bool fault = false, active = eng.active();
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    if (tuning(i)) active = true;
    if (s.ch[i].type != CH_OFF && (!gLive.ch[i].commOk || gLive.ch[i].fault)) fault = true;
    if (man[i].on) active = true;
  }
  statusLedSet(statusLedPick(fault, st == RunState::Paused || eng.progPaused(), netApActive(), active));
}

static void controlTask(void*) {
  {
    Guard g;
    s = gSettings;
  }
  applySettings(s, true);
  eng.setEventSink(recordEv, nullptr);
  for (int i = 0; i < NUM_CHANNELS; ++i) man[i].sp = s.ch[i].manualSp;
  uint32_t lastPoll = 0, lastTick = millis();
  for (;;) {
    Cmd c;
    while (xQueueReceive(cmdQ, &c, 0) == pdTRUE) {
      crumb(CR_CONTROL, C_CMD, c.type);
      handleCmd(c);
    }
    if (reqPending) {
      crumb(CR_CONTROL, C_REQUEST);
      processRequest(reqSlot);
      reqPending = false;
      xSemaphoreGive(reqDone);
    }
    const uint32_t now = millis();
    if (now - lastPoll >= s.pollMs) {
      lastPoll = now;
      const float dt = (now - lastTick) / 1000.0f;
      lastTick = now;
      crumb(CR_CONTROL, C_POLL);
      cycle(dt);
    }
    crumb(CR_CONTROL, C_WAIT);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ------------------------------------------------------------------ public API

// A requested restart leaves a note in RTC memory (kept over a software reset, not a power loss)
struct RestartRtc {
  uint32_t magic;
  char from[48];
};
RTC_NOINIT_ATTR static RestartRtc rtcRestart;
static constexpr uint32_t RESTART_MAGIC = 0x52535432;  // "RST2" (from[] was 20 in "RST1")
static volatile bool restartPending = false;

void appRequestRestart(const char* from) {
  logEvent("Restart requested %s", from);
  strlcpy(rtcRestart.from, from, sizeof(rtcRestart.from));
  rtcRestart.magic = RESTART_MAGIC;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) localCtrlSetOutputs(ch, false);
  restartPending = true;
}
bool appRestartPending() { return restartPending; }

static const char* resetReasonFind() {
  static char b[64];
  const esp_reset_reason_t r = esp_reset_reason();
  const bool asked = rtcRestart.magic == RESTART_MAGIC;
  rtcRestart.magic = 0;
  if (asked) rtcRestart.from[sizeof(rtcRestart.from) - 1] = 0;
  if (asked && r == ESP_RST_SW) {
    snprintf(b, sizeof(b), "restart %s", rtcRestart.from);
    return b;
  }
  switch (r) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC:
      // the ROM's reset code helps telling a real crash from a restart (serial log has the details)
      snprintf(b, sizeof(b), "%s, rst %d", asked ? "crash while restarting" : "crash", (int)esp_rom_get_reset_reason(0));
      return b;
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brown-out";
    case ESP_RST_SDIO: return "SDIO";
    default: return "unknown reset";
  }
}
// the reason, worked out once (the RTC note is used up on the first look)
static const char* resetReasonText() {
  static const char* r = nullptr;
  if (!r) r = resetReasonFind();
  return r;
}

// A run was active when the board restarted: /duo/run.json is still there (spec 9.3.5). The channels
// start with their outputs off; the user resumes or dismisses (never automatic).
static void checkInterrupted() {
  RunFileInfo fi;
  RunPos fp;
  bool havePos;
  if (!runFileLoad(fi, *runProg, fp, havePos)) {
    rtcRun.magic = 0;
    return;
  }
  const bool rtcOk = rtcRun.magic == RTC_MAGIC && rtcRun.sum == rtcSum(rtcRun) && rtcRun.id == fi.id;
  memset(&intr, 0, sizeof(intr));
  intr.valid = true;
  intr.exact = rtcOk;
  intr.info = fi;
  if (rtcOk) intr.pos = rtcRun.pos;
  else if (havePos) intr.pos = fp;
  strlcpy(intr.reason, resetReasonText(), sizeof(intr.reason));
  runIdx = fi.slot;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) intrOff[ch] = progUses(*runProg, ch);
  const DuoProgram& p = *runProg;
  const int t = (int)intr.pos.el;
  char where[48];
  if (p.type == PROG_SEQ) snprintf(where, sizeof(where), "phase %d of %d", intr.pos.i[0] + 1, p.nPh);
  else snprintf(where, sizeof(where), "timeline");
  logEvent("Run interrupted by a reset (%s): \"%s\", %s, run time %s%d:%02d", intr.reason, p.name, where,
           rtcOk ? "" : "about ", t / 60, t % 60);
  snprintf(runMsg, sizeof(runMsg), "Run interrupted by a reset (%s): \"%.40s\" - Resume or Dismiss on the web page", intr.reason, p.name);
}

void appInit() {
  // large buffers on the heap (the static DRAM segment is almost full)
  gHist = (History*)calloc(1, sizeof(History));
  runProg = (DuoProgram*)calloc(1, sizeof(DuoProgram));
  // chart values: 6 x 2 x CHART_N words. No PSRAM (CYD): in the IRAM heap, which the rest of the
  // firmware can't use (32-bit access only); DRAM is what runs short there (Wi-Fi, LVGL, web)
  // (with PSRAM 4 more for the live view, see liveRow())
#ifdef BOARD_HAS_PSRAM
  constexpr int HV_ROWS = 10;
#else
  constexpr int HV_ROWS = 6;
#endif
  const size_t hvBytes = sizeof(TenthRow) * NUM_CHANNELS * HV_ROWS;
#ifdef BOARD_HAS_PSRAM
  TenthRow* hv = (TenthRow*)malloc(hvBytes);
#else
  TenthRow* hv = (TenthRow*)heap_caps_malloc(hvBytes, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL);
  if (!hv) hv = (TenthRow*)malloc(hvBytes);
#endif
  if (gHist && hv) {
    gHist->runPv = hv;
    gHist->runSp = hv + NUM_CHANNELS;
    gHist->runPlan = hv + 2 * NUM_CHANNELS;
    gHist->plan = hv + 3 * NUM_CHANNELS;
    gHist->runLo = hv + 4 * NUM_CHANNELS;
    gHist->runHi = hv + 5 * NUM_CHANNELS;
#ifdef BOARD_HAS_PSRAM
    gHist->livePv = hv + 6 * NUM_CHANNELS;
    gHist->liveSp = hv + 7 * NUM_CHANNELS;
    gHist->liveLo = hv + 8 * NUM_CHANNELS;
    gHist->liveHi = hv + 9 * NUM_CHANNELS;
#endif
    for (int r = 0; r < HV_ROWS * NUM_CHANNELS; ++r)
      for (int i = 0; i < CHART_N; ++i) hv[r][i] = NAN;
  }
  // live history ring (see config.h)
  if (gHist) {
    uint32_t cap = LIVE_HIST_MIN * 60 / LIVE_SAMPLE_SEC;
    const char* where = "PSRAM";
#ifdef BOARD_HAS_PSRAM
    gHist->liveRing = (uint32_t*)heap_caps_malloc(cap * SLOT_W * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
#else
    // only the IRAM-only heap (no DRAM), leaving some of it free; 5 min steps
    where = "IRAM";
    const uint32_t step = 300 / LIVE_SAMPLE_SEC, reserve = 8 * 1024;
    const uint32_t pure = iramPureFree();
    while (cap > step && (size_t)cap * SLOT_W * 4 + reserve > pure) cap -= step;
    gHist->liveRing = (uint32_t*)heap_caps_malloc(cap * SLOT_W * 4, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL);
    if (gHist->liveRing && (uintptr_t)gHist->liveRing >= 0x400A0000) where = "D/IRAM (DRAM)";
#endif
    if (!gHist->liveRing) {
      cap = 300 / LIVE_SAMPLE_SEC;
      where = "DRAM";
      gHist->liveRing = (uint32_t*)malloc(cap * SLOT_W * 4);
    }
    gHist->liveCap = cap;
    gHist->liveTotal = 0;
    LogSerial.printf("[hist] live history %lu min (%lu bytes, %s)\n", (unsigned long)(cap * LIVE_SAMPLE_SEC / 60),
                  (unsigned long)(cap * SLOT_W * 4), where);
  }
  if (!gHist || !hv || !runProg || !gHist->liveRing) {
    LogSerial.println("Out of memory");
    abort();
  }
  gLock = xSemaphoreCreateMutex();
  cmdQ = xQueueCreate(8, sizeof(Cmd));
  reqMutex = xSemaphoreCreateMutex();
  reqDone = xSemaphoreCreateBinary();
  memset(&gLive, 0, sizeof(gLive));
  gLive.hostSp = gLive.target = gLive.lastWrittenSp = NAN;
  gLive.step = -1;
  gLive.runProg = -1;
  for (int i = 0; i < NUM_CHANNELS; ++i) gLive.run[i] = eng.out(i);
  if (!settingsLoad(gSettings)) settingsSave(gSettings);
  localCtrlBegin();  // before the control task configures the channels
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    ChannelLive& L = gLive.ch[i];
    L.type = gSettings.ch[i].type;
    strlcpy(L.name, gSettings.ch[i].name, sizeof(L.name));
    L.pv = L.spEff = L.out = NAN;
    L.manualSp = gSettings.ch[i].manualSp;
    LogSerial.printf("[settings] channel %d: %s, %s\n", i + 1, gSettings.ch[i].name, channelTypeName(gSettings.ch[i].type));
  }
  programStoreBegin();
  const char* why = resetReasonText();
  logEvent("%s %s started (%s)", PRODUCT_NAME, FW_VERSION, why);
  {  // after a watchdog / crash: what the loop, the control task and the SD card were doing
    char b[160];
    const bool bad = strstr(why, "watchdog") || strstr(why, "crash");
    if (crumbReport(b, sizeof(b)) && bad && b[0]) {
      LogSerial.printf("[boot] %s - %s\n", why, b);
      logEvent("%s - %s", why, b);
    }
  }
  checkInterrupted();
  gLive.manualSp = gSettings.ch[ENGINE_CH].manualSp;
  histResetLive();
  statusLedBegin();
}

static TaskHandle_t controlHandle = nullptr;
void controlTaskStart() {
#ifdef BOARD_HAS_PSRAM
  const uint32_t stack = 8192;
#else
  const uint32_t stack = 6144;  // CYD: ~3 kB used (measured), DRAM is short
#endif
  xTaskCreatePinnedToCore(controlTask, "control", stack, nullptr, 3, &controlHandle, 0);
}
unsigned controlTaskStackFree() { return controlHandle ? (unsigned)uxTaskGetStackHighWaterMark(controlHandle) : 0; }

bool sendCmd(CmdType c, int arg, float value, int idx, uint8_t sub) {
  Cmd m{c, sub, (int16_t)arg, (int16_t)idx, value};
  return xQueueSend(cmdQ, &m, pdMS_TO_TICKS(50)) == pdTRUE;
}

bool busRequest(BusRequest& r, uint32_t timeoutMs) {
  if (xSemaphoreTake(reqMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
    strlcpy(r.err, "Bus tool busy", sizeof(r.err));
    return false;
  }
  xSemaphoreTake(reqDone, 0);  // clear stale signal
  reqSlot = r;
  reqPending = true;
  const bool done = xSemaphoreTake(reqDone, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  if (done) r = reqSlot;
  else strlcpy(r.err, "Timed out waiting for the control task", sizeof(r.err));
  xSemaphoreGive(reqMutex);
  return done && r.ok;
}

void saveSettingsAndNotify() {
  Settings copy;
  {
    Guard g;
    copy = gSettings;
  }
  settingsSave(copy);
  sendCmd(CMD_SETTINGS_CHANGED);
}

void saveSettingsQuiet() {
  static Settings copy;  // callers are the UI and web (same loop task)
  {
    Guard g;
    copy = gSettings;
  }
  settingsSave(copy);
}

bool runProgramCopy(DuoProgram& out) {
  Guard g;
  if (runIdx < 0) return false;
  out = *runProg;
  return true;
}

bool runProgramJson(JsonObject o) {
  Guard g;
  if (runIdx < 0) return false;
  progToJson(*runProg, o);
  return true;
}

void selectProgram(int idx) {
  if (idx < 0 || idx >= PROG_MAX) return;
  {
    Guard g;
    if (gSettings.selProgram == idx) return;
    gSettings.selProgram = (uint8_t)idx;
  }
  saveSettingsQuiet();
}

void setManualSp(float v, int ch) {
  if (isnan(v) || ch < 0 || ch >= NUM_CHANNELS) return;
  {
    Guard g;
    ChannelSettings& c = gSettings.ch[ch];
    v = constrain(v, c.spMin, c.spMax);
    c.manualSp = v;
    gLive.ch[ch].manualSp = v;
    if (ch == ENGINE_CH) gLive.manualSp = v;
  }
  saveSettingsQuiet();
  sendCmd(CMD_MANUAL_SP, ch, v);
}
