#include "app_state.h"
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include "controller.h"
#include "local_ctrl.h"
#include "net.h"
#include "program_store.h"
#include "settings_store.h"
#include "status_led.h"

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
static DuoProgram* selProg = nullptr;     // selected program, for the preview
static DuoProgram* scratch = nullptr;     // loop task (UI, web)
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

// ------------------------------------------------------------------ helpers

// status message (web run.msg, touch message line) + event log + serial
static void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(runMsg, sizeof(runMsg), fmt, ap);
  va_end(ap);
  logEvent("%s", runMsg);
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

static void fillNan(float* a, int n) {
  for (int i = 0; i < n; ++i) a[i] = NAN;
}

static void histResetLive() {
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    fillNan(gHist->livePv[ch], CHART_N);
    fillNan(gHist->liveSp[ch], CHART_N);
    fillNan(gHist->plan[ch], CHART_N);
    fillNan(gHist->runPlan[ch], CHART_N);
  }
  gHist->runValid = false;
}

static void histStartRun(float total) {
  gHist->runValid = true;
  gHist->runSpan = runSpanFor(total);
  gHist->runBin = gHist->runSpan / CHART_N;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    fillNan(gHist->runPv[ch], CHART_N);
    fillNan(gHist->runSp[ch], CHART_N);
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
  auto half = [](float* a, bool mean) {
    for (int i = 0; i < CHART_N / 2; ++i) {
      const float x = a[2 * i], y = a[2 * i + 1];
      a[i] = !mean ? x : isnan(x) ? y : isnan(y) ? x : (x + y) / 2;
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
    half(gHist->runPv[ch], true);
    half(gHist->runSp[ch], true);
    half(gHist->runPlan[ch], false);
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
      logEvent("Phase %d (%s)", e.idx + 1, phaseKindName(runProg->ph[e.idx].kind));
    else if (e.type == EV_STEP && e.ch < NUM_CHANNELS)
      logEvent("%s step %d", s.ch[e.ch].name, e.idx + 1);
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

void logEvent(const char* fmt, ...) {
  char m[LOG_LEN];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(m, sizeof(m), fmt, ap);
  va_end(ap);
  const float rt = logRunT;
  Serial.printf("[event] %s\n", m);
  portENTER_CRITICAL(&logMux);
  LogEntry& e = logBuf[logNext % LOG_N];
  e.seq = logNext++;
  e.upSec = millis() / 1000;
  e.runT = rt;
  memcpy(e.msg, m, sizeof(e.msg));
  portEXIT_CRITICAL(&logMux);
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
  float* a[NUM_CHANNELS];
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

// selected program: (re)load it when the selection or the stored program changed
static void refreshSelected() {
  int sel;
  {
    Guard g;
    sel = gSettings.selProgram;
  }
  const uint32_t gen = programStoreGen();
  if (sel == selLoaded && gen == selGen) return;
  selLoaded = sel;
  selGen = gen;
  char err[96];
  selValid = programLoad(sel, *selProg, err, sizeof(err));
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
    return gHist->runTotal;
  }
  refreshSelected();
  const float total = selValid ? programPreview(*selProg, nullptr, nullptr, nullptr) : 0;
  Guard g;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) fillNan(gHist->plan[ch], CHART_N);
  gHist->planTotal = total;
  gHist->planSpan = runSpanFor(total);
  gHist->planBin = gHist->planSpan / CHART_N;
  gHist->planName[0] = 0;
  if (selValid) {
    PlanFill f{{gHist->plan[0], gHist->plan[1]}, 0, gHist->planBin, 0};
    programPreview(*selProg, pv, planFn, &f);
    strlcpy(gHist->planName, selProg->name, sizeof(gHist->planName));
  }
  return total;
}

void chartSnapshot(ChartData& d) {
  Guard g;
  const bool active = gLive.state == RunState::Running || gLive.state == RunState::Paused;
  d.run = active && gHist->runValid;
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
    }
    return;
  }
  // idle: rolling window + preview of the selected program on its own time axis
  d.planSpan = gHist->planSpan;
  d.planTotal = gHist->planTotal;
  strlcpy(d.progName, gHist->planName, sizeof(d.progName));
  d.binSec = LIVE_SAMPLE_SEC;
  d.span = CHART_N * LIVE_SAMPLE_SEC;
  d.elapsed = 0;
  for (int i = 0; i < CHART_N; ++i) {
    d.pv[i] = gHist->livePv[B][i];
    d.sp[i] = gHist->liveSp[B][i];
    d.plan[i] = gHist->plan[B][i];
    d.pvT[i] = gHist->livePv[T][i];
    d.spT[i] = gHist->liveSp[T][i];
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

static void jArr(String& o, const float* a, int n) {
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
  o.reserve(run ? 9000 : 6000);
  if (!run) {
    Guard g;
    o = "{\"mode\":\"live\",\"bin\":";
    o += String(LIVE_SAMPLE_SEC);
    o += ",\"span\":";
    o += String(CHART_N * LIVE_SAMPLE_SEC);
    o += ",\"pv\":[";
    for (int c = 0; c < NUM_CHANNELS; ++c) {
      if (c) o += ',';
      jArr(o, gHist->livePv[c], CHART_N);
    }
    o += "],\"sp\":[";
    for (int c = 0; c < NUM_CHANNELS; ++c) {
      if (c) o += ',';
      jArr(o, gHist->liveSp[c], CHART_N);
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
  o += "],\"st\":[";
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

static void startProgram(int idx) {
  if (eng.active()) return;
  if (anyFault()) {
    setMsg("Cannot start: acknowledge the channel fault first");
    return;
  }
  if (idx < 0 || idx >= PROG_MAX) {
    Guard g;
    idx = gSettings.selProgram < PROG_MAX ? gSettings.selProgram : 0;
  }
  DuoProgram& p = *runProg;
  char err[96];
  bool loaded;
  {
    Guard g;  // the web reads the running copy under the lock
    loaded = programLoad(idx, p, err, sizeof(err));
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
      say("Cannot start: \"%s\" uses %s, which is off (Settings)", p.name, n);
      return;
    }
    if (tuning(ch)) {
      say("Cannot start: autotune is running on %s", n);
      return;
    }
    if (!chan(ch).spConfigured()) {
      say("Cannot start: %s has no setpoint register (web: Controller)", n);
      return;
    }
    if (!ok[ch]) {
      say("Cannot start: no valid temperature from %s", n);
      return;
    }
  }
  if (!progCheckLimits(p, lo, hi, err, sizeof(err), names)) {
    say("Cannot start: %s", err);
    return;
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
      say("Cannot start (controller limits): %.90s", err);
      return;
    }
  }
  if (!eng.start(&p, pv, ok, s.bottomBand)) {
    setMsg("Cannot start: the program has no phases/steps");
    return;
  }
  runIdx = (int8_t)idx;
  editCount = savedCount = 0;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    drv[ch] = Drive();
    if (!progUses(p, ch)) continue;  // manual channel: untouched
    // the program takes over from manual mode on its channels
    Manual& m = man[ch];
    m.on = m.pending = m.offPending = false;
    Controller* mc = mbOf(ch);
    if (hasRampReg(ch)) {  // remember the controller's own gradient to restore it at the end
      int16_t raw;
      if (mc->readRaw(mc->reg().rampRate, raw) == MbResult::Ok) {
        drv[ch].rampOriginal = raw;
        drv[ch].rampSaved = true;
      }
      writeRampOff(ch);
    }
    if (mc && s.ch[ch].mb.lockFront) mbWrite(ch, mc->reg().remote, 1, "Front-lock write");
    if (!chan(ch).canSwitchOff() && s.ch[ch].type == CH_MODBUS)
      Serial.printf("[run] %s has no outputs-off register: Idle keeps the controller's setpoint\n", names[ch]);
  }
  float rem;
  {
    Guard g;
    histStartRun(eng.forecast(nullptr, nullptr, pv));
  }
  updatePlan(pv, rem);
  say("Program \"%s\" started", p.name);
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
    m.pending = false;
    m.offPending = c.canSwitchOff();
    say(m.offPending ? "%s: manual off, outputs switched off"
                                                  : "%s: manual off (no outputs-off register: controller keeps its setpoint)",
             name);
  }
}

// (Re)opens the bus and selects the channel backends after a settings change
static void applySettings(const Settings& prev, bool first) {
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
    say("Run changes saved into \"%s\"", runProg->name);
  } else {
    say("Saving the run changes failed: %s", err);
  }
}

static void handleCmd(const Cmd& c) {
  switch (c.type) {
    case CMD_START: startProgram(c.arg); break;
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
  }
  if (changed && c.type() != CH_OFF) {  // log link changes, not every failed poll
    if (ok) Serial.printf("[ch] %s (%s): link ok\n", s.ch[i].name, channelTypeName(c.type()));
    else Serial.printf("[ch] %s (%s): %s\n", s.ch[i].name, channelTypeName(c.type()), err);
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
        Serial.printf("[tune] %s: %s -> kp %.3f ki %.4f kd %.2f (accept or discard)\n", s.ch[i].name, t.msg, t.res.kp,
                      t.res.ki, t.res.kd);
      else
        Serial.printf("[tune] %s: failed: %s\n", s.ch[i].name, t.msg);
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
    Serial.printf("[ch] %s: FAULT %s - outputs off\n", s.ch[i].name, ft);
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

// end action of one channel (spec 9.3.5); true when everything was written
static bool applyEndAction(int ch) {
  bool ok = true;
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
      }
      break;
    case END_SET_SP:
      ok &= c.setOutputs(true, err, sizeof(lastErr[0])) && writeSp(ch, s.ch[ch].endSp);
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

  // end of program (finished or aborted): end action per channel of the program
  const RunState st = eng.state();
  const bool wasActive = prevState == RunState::Running || prevState == RunState::Paused;
  if (wasActive && (st == RunState::Finished || st == RunState::Aborted)) {
    for (int i = 0; i < NUM_CHANNELS; ++i) drv[i].endPending = eng.out(i).inProgram;
    logEvent("\"%s\" %s after %d:%02d", runProg->name, st == RunState::Finished ? "finished" : "stopped",
             (int)eng.elapsed() / 60, (int)eng.elapsed() % 60);
  }
  bool endOpen = false, endDone = false;
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    Drive& d = drv[i];
    if (!d.endPending || !commOk[i]) {
      endOpen |= d.endPending;
      continue;
    }
    d.endPending = !applyEndAction(i);
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
    const float runSp = (eng.active() && out[i].inProgram) ? out[i].sp : NAN;
    spNow[i] = spEffOk[i] ? spEff[i] : !isnan(runSp) ? runSp : (man[i].on ? man[i].sp : NAN);
  }
  flushRunEvents();
  Guard g;
  gEngSnap = eng;
  if (eng.active() && gHist->runValid) {
    int idx = (int)(eng.elapsed() / gHist->runBin);
    if (idx >= CHART_N) idx = CHART_N - 1;
    if (idx < 0) idx = 0;
    for (int i = 0; i < NUM_CHANNELS; ++i) {
      gHist->runPv[i][idx] = pvOk[i] ? pv[i] : NAN;
      gHist->runSp[i][idx] = spNow[i];
      const ChState cs = eng.chState(i);
      const char code = cs == ChState::None ? (man[i].on ? 'm' : '-') : CH_STATE_CODE[(int)cs];
      for (int k = gHist->runStLast + 1; k <= idx; ++k) gHist->runSt[i][k] = code;
      gHist->runSt[i][idx] = code;
    }
    gHist->runStLast = (int16_t)idx;
  }
  // rolling window: one slot per LIVE_SAMPLE_SEC even if polling is slower
  const uint32_t slotMs = LIVE_SAMPLE_SEC * 1000UL;
  uint32_t n = (millis() - lastLive) / slotMs;
  if (n) {
    lastLive = (n > CHART_N) ? millis() : lastLive + n * slotMs;
    if (n > CHART_N) n = CHART_N;
    for (int c = 0; c < NUM_CHANNELS; ++c) {
      memmove(gHist->livePv[c], gHist->livePv[c] + n, sizeof(float) * (CHART_N - n));
      memmove(gHist->liveSp[c], gHist->liveSp[c] + n, sizeof(float) * (CHART_N - n));
      for (uint32_t i = CHART_N - n; i < CHART_N; ++i) {
        gHist->livePv[c][i] = pvOk[c] ? pv[c] : NAN;
        gHist->liveSp[c][i] = spNow[c];
      }
    }
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
    while (xQueueReceive(cmdQ, &c, 0) == pdTRUE) handleCmd(c);
    if (reqPending) {
      processRequest(reqSlot);
      reqPending = false;
      xSemaphoreGive(reqDone);
    }
    const uint32_t now = millis();
    if (now - lastPoll >= s.pollMs) {
      lastPoll = now;
      const float dt = (now - lastTick) / 1000.0f;
      lastTick = now;
      cycle(dt);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ------------------------------------------------------------------ public API

void appInit() {
  // large buffers on the heap (the static DRAM segment is almost full)
  gHist = (History*)calloc(1, sizeof(History));
  runProg = (DuoProgram*)calloc(1, sizeof(DuoProgram));
  selProg = (DuoProgram*)calloc(1, sizeof(DuoProgram));
  scratch = (DuoProgram*)calloc(1, sizeof(DuoProgram));
  if (!gHist || !runProg || !selProg || !scratch) {
    Serial.println("Out of memory");
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
    Serial.printf("[settings] channel %d: %s, %s\n", i + 1, gSettings.ch[i].name, channelTypeName(gSettings.ch[i].type));
  }
  programStoreBegin();
  gLive.manualSp = gSettings.ch[ENGINE_CH].manualSp;
  histResetLive();
  statusLedBegin();
}

void controlTaskStart() {
  xTaskCreatePinnedToCore(controlTask, "control", 8192, nullptr, 3, nullptr, 0);
}

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

DuoProgram& scratchProgram() { return *scratch; }

bool loadLegacyView(int idx, Program& out) {
  char err[96];
  if (!programLoad(idx, *scratch, err, sizeof(err))) {
    char name[PROG_NAME_LEN];
    snprintf(name, sizeof(name), "Program %d", idx + 1);
    programClear(out, name);
    return false;
  }
  programLegacyView(*scratch, out);
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
