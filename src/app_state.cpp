#include "app_state.h"
#include "app_internal.h"
#include "psram.h"
#include "syslog.h"
#include "sdlog.h"
#include "crumbs.h"
#include "console.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include "controller.h"
#include "fan.h"
#include "fan_rule.h"
#include "io_pins.h"
#include "local_ctrl.h"
#include "safety.h"
#include "net.h"
#include "program_store.h"
#include "settings_store.h"
#include "status_led.h"
#include "version.h"
#include "raw_hist.h"
#include "run_stats.h"
#include <esp_system.h>
#include <esp_task_wdt.h>
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
ChannelSlot slots[NUM_CHANNELS];   // bottom, top
static uint8_t busTurn = 0;               // next channel to poll on the bus (round robin)
RunEngine eng;                     // one run engine for both channels (spec 9.3)
static Settings s;  // control task's private copy of the settings
DuoProgram* runProg = nullptr;     // running copy (run edits change it; read by the web under gLock)
static int8_t runIdx = -1;                // slot of the running / last program
static uint32_t editCount = 0, savedCount = 0;  // run edits / edits stored with "save run"
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
  uint32_t onMs = 0;        // switched on (manual idle timeout: activity counts from here at the earliest)
  bool idleWarned = false;  // "manual mode ends in 1 min" said
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

// status message (web run.msg, touch message line) + event log (kind k: LK_RUN run records, LK_EVENT, LK_WARN,
// LK_ERROR) + serial
static void say(LogKind k, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(LogKind k, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(runMsg, sizeof(runMsg), fmt, ap);
  va_end(ap);
  logKind(k, "%s", runMsg);
}
static void setMsg(LogKind k, const char* m) { say(k, "%s", m); }

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

// (chart history, plan preview, /api/chart: app_hist.cpp; event log: app_log.cpp; shared between these:
// app_internal.h)

// ---- run events (chart: phase lines, pauses, step end times) and the event log

static const char CH_STATE_CODE[] = "-orhwjkxpd";  // ChState -> chart lane code ('m' = manual)
static PlanEv pendEv[48];                          // from the engine, flushed into gHist each cycle
static int pendN = 0;
RunEngine gEngSnap;                                // copy of the engine for the web (under gLock)

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
static void startTempWarning(const DuoProgram& p, const float* pv, const bool* ok);
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
    r.ff[i] = C.ff;
    r.integ[i] = C.integ;
    const ChState cs = eng.chState(i);
    r.state[i] = cs != ChState::None ? chStateName(cs) : man[i].on ? "manual" : C.type == CH_OFF ? "" : "-";
  }
  r.mon = HAS_MONITOR && pvOk[HIST_CH - 1] && HIST_CH > NUM_CHANNELS ? pv[HIST_CH - 1] : NAN;
  sdlogRow(r);
}

static void startProgram(int idx, bool draft) {
  if (eng.active()) return;
  if (safetyEstop()) {
    setMsg(LK_WARN, "Cannot start: E-stop active");
    return;
  }
  if (anyFault()) {
    setMsg(LK_WARN, "Cannot start: acknowledge the channel fault first");
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
    say(LK_WARN, "Cannot start: %s", err);
    return;
  }
  if (progItems(p) == 0) {
    say(LK_WARN, "Cannot start: \"%s\" has no phases/steps", p.name);
    return;
  }
  float pv[NUM_CHANNELS];
  bool ok[NUM_CHANNELS];
  if (!runChecks(p, pv, ok, "Cannot start")) return;
  if (!eng.start(&p, pv, ok, s.bottomBand)) {
    setMsg(LK_WARN, "Cannot start: the program has no phases/steps");
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
  say(LK_RUN, "Program \"%s\" started%s", p.name, draft ? " (unsaved changes)" : "");
  startTempWarning(p, pv, ok);
}

// The first target of each channel: a channel that already is near or above it starts "hot" (a profile
// expects a cooler start) - only a warning (spec 9.2a)
static void startTempWarning(const DuoProgram& p, const float* pv, const bool* ok) {
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    if (!progUses(p, ch) || !ok[ch]) continue;
    const int n = p.type == PROG_TL ? p.nSt[ch] : p.nPh;
    for (int i = 0; i < n; ++i) {
      const Seg* g = progSeg(p, ch, i);
      if (!g) continue;
      if (pv[ch] >= g->t - START_WARN_BAND)
        say(LK_WARN, "Warning: %s starts at %.0f \xC2\xB0" "C, near or above its first target %.0f \xC2\xB0" "C", s.ch[ch].name,
            pv[ch], g->t);
      break;
    }
  }
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
      say(LK_WARN, "%s: \"%s\" uses %s, which is off (Settings)", verb, p.name, n);
      return false;
    }
    if (tuning(ch)) {
      say(LK_WARN, "%s: autotune is running on %s", verb, n);
      return false;
    }
    if (!chan(ch).spConfigured()) {
      say(LK_WARN, "%s: %s has no setpoint register (web: Controller)", verb, n);
      return false;
    }
    if (!ok[ch]) {
      say(LK_WARN, "%s: no valid temperature from %s", verb, n);
      return false;
    }
  }
  if (!progCheckLimits(p, lo, hi, err, sizeof(err), names)) {
    say(LK_WARN, "%s: %s", verb, err);
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
      say(LK_WARN, "%s (controller limits): %.90s", verb, err);
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
  if (safetyEstop()) {
    setMsg(LK_WARN, "Cannot resume: E-stop active");
    return;
  }
  if (!intr.valid) {
    setMsg(LK_WARN, "No interrupted run to resume");
    return;
  }
  if (anyFault()) {
    setMsg(LK_WARN, "Cannot resume: acknowledge the channel fault first");
    return;
  }
  DuoProgram& p = *runProg;  // the running copy, loaded from /duo/run.json at boot
  float pv[NUM_CHANNELS];
  bool ok[NUM_CHANNELS];
  if (!runChecks(p, pv, ok, "Cannot resume")) return;
  if (!eng.resumeAt(&p, intr.pos, pv, ok, s.bottomBand, intr.info.edited)) {
    setMsg(LK_WARN, "Cannot resume: the interrupted phase is not in the program");
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
  if (p.type == PROG_SEQ) say(LK_RUN, "Program \"%s\" resumed: phase %d again (run time %d:%02d)", p.name, eng.seq().idx + 1, t / 60, t % 60);
  else say(LK_RUN, "Program \"%s\" resumed at run time %d:%02d", p.name, t / 60, t % 60);
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
  setMsg(LK_RUN, "Interrupted run dismissed");
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
      say(LK_WARN, "%s is in the running program (stop it first)", name);
      return;
    }
    if (c.type() == CH_OFF) {
      say(LK_WARN, "%s is off (Settings: channel type)", name);
      return;
    }
    if (safetyEstop()) {
      say(LK_WARN, "%s: E-stop active", name);
      return;
    }
    char ft[64];
    if (c.fault(ft, sizeof(ft))) {
      say(LK_WARN, "%s: acknowledge the fault first (%s)", name, ft);
      return;
    }
    if (tuning(ch)) {
      say(LK_WARN, "%s: autotune is running (stop it first)", name);
      return;
    }
    if (!c.spConfigured()) {
      setMsg(LK_WARN, "Set the setpoint register address first (web: Controller)");
      return;
    }
    m.on = true;
    m.pending = true;
    m.offPending = false;
    m.onMs = millis();
    m.idleWarned = false;
    say(LK_EVENT, "%s: manual setpoint on", name);
  } else {
    if (!m.on) return;
    m.on = false;
    spHeld[ch] = false;
    m.pending = false;
    m.offPending = c.canSwitchOff();
    say(LK_EVENT, m.offPending ? "%s: manual off, outputs switched off"
                                                  : "%s: manual off (no outputs-off register: controller keeps its setpoint)",
             name);
  }
}

// (Re)opens the bus and selects the channel backends after a settings change
static void applySettings(const Settings& prev, bool first) {
  // first the optional pins (relay, E-stop, buzzer, fan: io_pins.h), then what uses them
  ioConfigure(s.ioFn);
  safetyConfigure(s.estop != 0, s.sound != 0, s.volume, s.keyBeep != 0);
  localCtrlRelayEnable(s.relay != 0);
  modbusBusBegin(bus, s);
  eng.setBand(s.bottomBand);
  localCtrlMonConfigure(HAS_MONITOR && s.mon.on, s.mon);
  {
    Guard g;
    gLive.mon.on = HAS_MONITOR && s.mon.on;
    strlcpy(gLive.mon.name, s.mon.name, sizeof(gLive.mon.name));
    if (!gLive.mon.on) gLive.mon.valid = false;
  }
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    const bool typeChanged = first || prev.ch[i].type != s.ch[i].type;
    if (typeChanged && !first && inRun(i)) {
      eng.stop();
      say(LK_RUN, "Stopped: type of channel %s changed", s.ch[i].name);
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
    setMsg(LK_WARN, "No program is running");
    return;
  }
  if (ch < 0 || ch >= NUM_CHANNELS) {
    setMsg(LK_WARN, "Run edit: channel must be 0 (bottom) or 1 (top)");
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
    say(LK_RUN, "Run edit (%s, %s): %s%.1f", name, c.sub < 4 ? W[c.sub] : "?",
             c.sub == EDIT_EXTEND && c.value > 0 ? "+" : "", c.value);
  } else {
    say(LK_WARN, "Run edit not possible: %s", err);
  }
}

static void saveRun() {
  if (runIdx < 0 || editCount == 0) {
    setMsg(LK_WARN, "No run edits to save");
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
    say(LK_RUN, "Run changes saved into \"%s\"", runProg->name);
  } else {
    say(LK_ERROR, "Saving the run changes failed: %s", err);
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
      if (eng.state() == RunState::Running) { eng.pause(); setMsg(LK_RUN, "Paused"); }
      break;
    case CMD_RESUME:
      if (eng.state() == RunState::Paused) { eng.resume(); setMsg(LK_RUN, "Resumed"); }
      break;
    case CMD_STOP:
      if (eng.active()) { eng.stop(); setMsg(LK_RUN, "Stopped by user"); }
      break;
    case CMD_NEXT:
      if (eng.progPaused()) { eng.cont(); setMsg(LK_RUN, "Continued"); }
      else if (eng.next()) setMsg(LK_RUN, "Next phase");
      else if (eng.active() && eng.type() == PROG_TL) setMsg(LK_WARN, "Next phase is not available for Timeline programs");
      break;
    case CMD_CONTINUE:
      if (eng.cont()) setMsg(LK_RUN, "Continued");
      break;
    case CMD_RUN_EDIT: runEdit(c); break;
    case CMD_SAVE_RUN: saveRun(); break;
    case CMD_DISCARD_RUN: discardRun(); break;
    case CMD_RUN_RECOVER: resumeInterrupted(); break;
    case CMD_RUN_DISMISS: dismissInterrupted(); break;
    case CMD_SETTINGS_CHANGED: {
      static PSRAM_VAR(Settings, prev);  // static: large
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
        say(LK_EVENT, "%s: fault acknowledged", s.ch[c.arg].name);
      } else {
        say(LK_WARN, "%s: fault not cleared: %s", s.ch[c.arg].name, err);
      }
      break;
    }
    case CMD_TUNE_START: {
      if (c.arg < 0 || c.arg >= NUM_CHANNELS) break;
      const char* name = s.ch[c.arg].name;
      if (s.ch[c.arg].type != CH_LOCAL) {
        say(LK_WARN, "%s: autotune is for local channels (Modbus controllers tune themselves)", name);
        break;
      }
      if (inRun(c.arg)) {
        say(LK_WARN, "%s: stop the program before the autotune", name);
        break;
      }
      if (safetyEstop()) {
        say(LK_WARN, "%s: autotune not started: E-stop active", name);
        break;
      }
      Manual& m = man[c.arg];  // the tune takes over the channel
      m.on = m.pending = m.offPending = false;
      char err[96];
      if (localCtrlTuneStart(c.arg, c.value, err, sizeof(err))) {
        say(LK_EVENT, "%s: autotune started at %.0f C (%s)", name, c.value,
            s.ch[c.arg].loc.tuneFast ? "faster" : "slow, no overshoot");
      } else {
        say(LK_WARN, "%s: autotune not started: %s", name, err);
      }
      break;
    }
    case CMD_TUNE_STOP:
      if (c.arg < 0 || c.arg >= NUM_CHANNELS) break;
      localCtrlTuneStop(c.arg);
      say(LK_EVENT, "%s: autotune stopped", s.ch[c.arg].name);
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
    // at most FINDSP_MS: the control task polls nothing else meanwhile (r.to = the last address scanned)
    constexpr uint32_t FINDSP_MS = 20000;
    const uint32_t t0 = millis();
    r.to = to;
    for (uint32_t a = r.from; a <= to && r.n < 64; ++a) {
      localCtrlHeartbeat();  // (a long request, not a stopped task)
      if (millis() - t0 > FINDSP_MS) {
        r.to = (uint16_t)(a - 1);
        break;
      }
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
  localCtrlHeartbeat();  // (bus timeouts of several registers add up)
  static ChannelLive v;  // value fields from the backend (static: keep it off the task stack)
  char err[80];
  memset(&v, 0, sizeof(v));
  v.ff = v.integ = NAN;  // (only a local channel has them)
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
    L.ff = v.ff; L.integ = v.integ;
    L.statusValid = v.statusValid; L.status = v.status;
    L.alarmValid = v.alarmValid; L.alarm = v.alarm;
    L.coldC = v.coldC;
    L.outOn = v.outOn;
  }
  if (changed && c.type() != CH_OFF) {  // log link changes, not every failed poll
    if (ok) logEvent("%s (%s): link ok", s.ch[i].name, channelTypeName(c.type()));
    else logError("%s (%s): %s", s.ch[i].name, channelTypeName(c.type()), err);
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
        logError("%s: autotune failed: %s", s.ch[i].name, t.msg);
      say(t.state == TuneState::Done ? LK_EVENT : LK_WARN, "%s: autotune %s", s.ch[i].name, t.state == TuneState::Done ? "done - accept or discard (web)" : t.msg);
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
      say(LK_ERROR, "Stopped: %s fault: %s - acknowledge it (web)", s.ch[i].name, ft);
    } else {
      say(LK_ERROR, "%s fault: %s - acknowledge it (web)", s.ch[i].name, ft);
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

// feed-forward of a local channel (spec 9.23): in a run the setpoint ffDead s ahead (and the ramp rate now),
// else none (the local task then uses its own setpoint, without ramp power)
static void feedPreview(int ch) {
  if (s.ch[ch].type != CH_LOCAL || !s.ch[ch].loc.ff) return;
  float spA, rateA, spN, rateN;
  if (inRun(ch) && eng.preview(ch, s.ch[ch].loc.ffDead, spA, rateA) && eng.preview(ch, 0, spN, rateN))
    localCtrlSetPreview(ch, spA, rateA, rateN, s.pollMs);
  else
    localCtrlSetPreview(ch, NAN, 0, 0, s.pollMs);
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

// E-stop, manual idle timeout, other-channel heating, alarm sound, chip temperature (spec 9.2a)
static void safetyCycle(const bool* commOk) {
  const uint32_t now = millis();
  // the "heating while off" check of a channel pauses while the other one heats
  bool on[NUM_CHANNELS];
  {
    Guard g;
    for (int i = 0; i < NUM_CHANNELS; ++i) on[i] = gLive.ch[i].type != CH_OFF && gLive.ch[i].outOn;
  }
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    bool other = false;
    for (int j = 0; j < NUM_CHANNELS; ++j) other |= j != i && on[j];
    chan(i).setOtherHeating(other);
  }
  // E-stop: the SSRs and the relay are already off (local task); here the run, manual mode, autotune
  // and the Modbus outputs
  static bool wasEstop = false;
  const bool es = safetyEstop();
  if (es) {
    if (!wasEstop && eng.active()) eng.stop();
    for (int i = 0; i < NUM_CHANNELS; ++i) {
      Manual& m = man[i];
      m.on = m.pending = m.offPending = false;
      spHeld[i] = false;
      if (tuning(i)) localCtrlTuneStop(i);
      Channel& c = chan(i);
      if (c.type() == CH_MODBUS && commOk[i] && c.canSwitchOff() && on[i]) c.setOutputs(false, lastErr[i], sizeof(lastErr[0]));
    }
  }
  if (es != wasEstop) {
    if (es) say(LK_ERROR, "E-stop: all outputs off%s", eng.active() || prevState == RunState::Running || prevState == RunState::Paused ? ", program stopped" : "");
    else say(LK_EVENT, "Emergency stop released: switch on again when ready");
    wasEstop = es;
  }
  // manual mode without user activity
  bool idleWarn = false;
  if (s.manualIdle) {
    const uint32_t limit = (uint32_t)s.manualIdleMin * 60000UL;
    const uint32_t act = safetyLastActivityMs();
    for (int i = 0; i < NUM_CHANNELS; ++i) {
      Manual& m = man[i];
      if (!m.on || inRun(i)) {
        m.idleWarned = false;
        continue;
      }
      const uint32_t from = (int32_t)(act - m.onMs) > 0 ? act : m.onMs;
      const uint32_t idle = now - from;
      if (idle >= limit) {
        manualSwitch(i, false);
        say(LK_EVENT, "%s: manual mode ended after %u min without activity", s.ch[i].name, (unsigned)s.manualIdleMin);
      } else if (idle + 60000UL >= limit) {
        if (!m.idleWarned) say(LK_WARN, "%s: manual mode ends in 1 min (no activity) - touch the screen to keep it", s.ch[i].name);
        m.idleWarned = idleWarn = true;
      } else {
        m.idleWarned = false;
      }
    }
  }
  // alarm sound
  bool fault = es;
  {
    Guard g;
    for (int i = 0; i < NUM_CHANNELS; ++i) fault |= gLive.ch[i].fault != 0;
  }
  const bool action = (eng.progPaused() && eng.pauseMsg()) || idleWarn;
  safetySound(fault ? SOUND_ALARM : action ? SOUND_ACTION : SOUND_NONE);
  // chip temperature (S3), every 30 s
  static uint32_t chipAt = 0;
  static bool chipHot = false;
  if (safetyChipTempWired() && s.chipWarn && (!chipAt || now - chipAt >= 30000)) {
    chipAt = now;
    safetyChipTempPoll();
    const float t = safetyChipTemp();
    if (!isnan(t) && !chipHot && t > s.chipMaxC) {
      chipHot = true;
      logWarn("Warning: chip temperature %.0f \xC2\xB0" "C (limit %.0f \xC2\xB0" "C) - check the cooling", t, s.chipMaxC);
    } else if (!isnan(t) && chipHot && t < s.chipMaxC - 5) {
      chipHot = false;
      logEvent("Chip temperature back to %.0f \xC2\xB0" "C", t);
    }
  }
  Guard g;
  gLive.estop = es;
  gLive.estopRaw = safetyEstopRaw();
  gLive.chipC = safetyChipTemp();
  gLive.chipHot = chipHot;
  gLive.soundMuted = safetySoundMuted();
}

// The running program's current phase / step asks for the cooling fan ("fan" on a phase / step)
static bool runWantsFan() { return eng.fanWanted(); }

// ---- run report (run_stats.h): the figures of the current run, the report of the last one (under gLock)
static RunStats runStats;
static RunReport runReport;  // (zero: valid false)

bool runReportGet(RunReport& out) {
  Guard g;
  out = runReport;
  return out.valid;
}

// end of a run: the report into runReport and the event log (one line, or one per channel if it is too
// long; it also lands in the SD run log as an event row: call before sdlogRunEnd())
static void runReportEnd(bool finished) {
  RunReport r;
  memset(&r, 0, sizeof(r));
  r.valid = true;
  r.finished = finished;
  strlcpy(r.name, runProg->name, sizeof(r.name));
  r.dur = eng.elapsed();
  for (int i = 0; i < 3; ++i) {
    r.peak[i] = runStats.peak[i];
    r.peakT[i] = runStats.peakT[i];
  }
  for (int c = 0; c < NUM_CHANNELS; ++c) {
    r.dev[c] = runStats.dev[c];
    r.rate[c] = runStats.rate[c];
  }
  {
    Guard g;
    runReport = r;
  }
  char part[3][LOG_LEN];
  for (int c = 0; c < NUM_CHANNELS; ++c) reportPart(part[c], sizeof(part[c]), s.ch[c].name, r, c);
  part[2][0] = 0;
  if (HAS_MONITOR && s.mon.on) reportPart(part[2], sizeof(part[2]), s.mon.name, r, RS_MON);
  char line[4 * LOG_LEN];  // (3 parts < LOG_LEN each + separators: fits)
  int k = snprintf(line, sizeof(line), "Report:");
  bool any = false;
  for (int i = 0; i < 3; ++i) {
    if (!part[i][0]) continue;
    k += snprintf(line + k, sizeof(line) - k, "%s %s", any ? " \xC2\xB7" : "", part[i]);
    any = true;
  }
  if (!any) return;
  if (k < (int)LOG_LEN) {
    logRun("%s", line);
    return;
  }
  for (int i = 0; i < 3; ++i)
    if (part[i][0]) logRun("Report: %s", part[i]);
}

// Cooling fan (spec 9.25, fan_rule.h): every cycle, after the end of a run is handled (its end action may
// keep a channel heating: then there is no cooling)
static void fanCycle(float dt, const float* pv, const bool* pvOk) {
  static FanRule rule;
  static uint32_t onSince = 0;  // millis the fan was switched on (0 = off)
  static bool slowWarned = false;
  bool heating = false;  // outputs on outside a run: manual mode or autotune
  for (int i = 0; i < NUM_CHANNELS; ++i) heating |= man[i].on || tuning(i);
  float hot = NAN;
  for (int i = 0; i < HIST_CH; ++i)
    if (pvOk[i] && !isnan(pv[i]) && (isnan(hot) || pv[i] > hot)) hot = pv[i];
  const bool run = eng.active();
  const bool wasCooling = rule.cooling();
  const bool on = rule.update(FanCfg{s.fanAfterRun != 0, s.fanOffBelow, s.fanMaxMin}, run, run && runWantsFan(),
                              heating, hot, dt);
  fanSet(on, s.fanSpeed);
  const bool wired = fanWired();
  if (wired && rule.cooling() && !wasCooling)
    logRun("Fan on after the run (until below %.0f \xC2\xB0" "C, at most %u min)", s.fanOffBelow, (unsigned)s.fanMaxMin);
  if (wired && wasCooling && !rule.cooling()) {
    const unsigned m = (unsigned)(rule.coolSec() / 60), sec = (unsigned)rule.coolSec() % 60;
    switch (rule.lastEnd()) {
      case FanEnd::Cool: logEvent("Fan off: below %.0f \xC2\xB0" "C after %u:%02u", s.fanOffBelow, m, sec); break;
      case FanEnd::Time:
        if (isnan(hot)) logEvent("Fan off after %u min (no reading)", (unsigned)s.fanMaxMin);
        else logEvent("Fan off after %u min (still %.0f \xC2\xB0" "C)", (unsigned)s.fanMaxMin, hot);
        break;
      case FanEnd::Heating: logEvent("Fan off: a channel heats"); break;
      case FanEnd::Run: logEvent("Fan off: a new run started"); break;
      default: break;
    }
  }
  // tach: a fan that is on but does not turn (blocked, wire off), once per start
  const int rpm = fanRpm();
  const bool isOn = fanOn();
  if (!isOn) {
    onSince = 0;
    slowWarned = false;
  } else if (!onSince) {
    onSince = millis();
    if (!onSince) onSince = 1;
  } else if (rpm >= 0 && rpm < 100 && !slowWarned && millis() - onSince > 5000) {
    slowWarned = true;
    logWarn("Fan does not turn (%d rpm) - check the fan and its wires", rpm);
  }
  Guard g;
  gLive.fanWired = wired;
  gLive.fanOn = isOn;
  gLive.fanRpm = (int16_t)(rpm > 32767 ? 32767 : rpm);
}

static volatile uint32_t cycles = 0;
uint32_t controlCycles() { return cycles; }

static void cycle(float dt) {
  ++cycles;
  pollChannels();
  crumb(CR_CONTROL, C_CYCLE);

  // (the first NUM_CHANNELS: the channels; the history arrays also have the monitor channel, CH_MON)
  float pv[HIST_CH], spEff[NUM_CHANNELS];
  bool pvOk[HIST_CH], spEffOk[NUM_CHANNELS], commOk[NUM_CHANNELS];
  uint32_t lastOk[HIST_CH];
#if HAS_MONITOR
  MonStatus mst;
  localCtrlMonGet(mst);
  pv[CH_MON] = mst.pv;
  pvOk[CH_MON] = mst.enabled && mst.fresh && !isnan(mst.pv);
  lastOk[CH_MON] = mst.lastGoodMs;
#endif
  {
    Guard g;
#if HAS_MONITOR
    MonLive& ml = gLive.mon;
    ml.valid = pvOk[CH_MON];
    ml.pv = mst.pv;
    ml.rawC = mst.rawC;
    ml.coldC = mst.coldC;
    ml.tcFault = mst.tcFault;
#endif
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
      say(LK_WARN, "Paused: no answer from %s for %us", s.ch[i].name, s.commLossSec);
      break;
    }
  }

  const bool wasPause = eng.progPaused();
  eng.tick(dt, pv, pvOk);
  if (eng.progPaused() && !wasPause) {
    const char* t = eng.pauseText();
    say(LK_RUN, "%s%s%s - press Continue", eng.pauseMsg() ? "Action required" : "Program pause",
             *t ? ": " : "", t);
  }
  for (int i = 0; i < NUM_CHANNELS; ++i) lastErr[i][0] = 0;
  if (eng.active())
    for (int i = 0; i < NUM_CHANNELS; ++i)
      if (commOk[i]) driveSetpoint(i);
  for (int i = 0; i < NUM_CHANNELS; ++i)
    if (commOk[i]) driveManual(i);
  for (int i = 0; i < NUM_CHANNELS; ++i) feedPreview(i);
  if (eng.active()) runTrack(false);
  // interrupted run: its channels' outputs off; dismissed: controller gradient / front panel back
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    if (!commOk[i] || inRun(i)) continue;
    if (intrOff[i] && (!chan(i).canSwitchOff() || chan(i).setOutputs(false, lastErr[i], sizeof(lastErr[0])))) intrOff[i] = false;
    if (intrCleanup[i] && cleanupChannel(i)) intrCleanup[i] = false;
  }

  safetyCycle(commOk);

  const RunState st = eng.state();
  const bool wasActive = prevState == RunState::Running || prevState == RunState::Paused;
  // run report: a new run starts the figures afresh (a resumed run too: its report covers only the part
  // after the resume, the figures before the reset are lost); fed every cycle while the run runs (not in
  // a user pause)
  if (eng.active() && !wasActive) {
    runStats.reset();
    Guard g;
    runReport.valid = false;
  }
  if (st == RunState::Running) {
    float rsSp[NUM_CHANNELS];
    ChState rsSt[NUM_CHANNELS];
    for (int i = 0; i < NUM_CHANNELS; ++i) {
      rsSp[i] = eng.out(i).sp;
      rsSt[i] = eng.chState(i);
    }
#if HAS_MONITOR
    runStats.update(eng.elapsed(), pv, pvOk, rsSp, rsSt, pv[CH_MON], pvOk[CH_MON]);
#else
    runStats.update(eng.elapsed(), pv, pvOk, rsSp, rsSt, NAN, false);
#endif
  }

  // end of program (finished or aborted): end action per channel of the program
  if (wasActive && (st == RunState::Finished || st == RunState::Aborted)) {
    for (int i = 0; i < NUM_CHANNELS; ++i) drv[i].endPending = eng.out(i).inProgram && !safetyEstop();
    if (st == RunState::Finished) safetySoundFinished();
    runForget();
    logRun("\"%s\" %s after %d:%02d", runProg->name, st == RunState::Finished ? "finished" : "stopped",
             (int)eng.elapsed() / 60, (int)eng.elapsed() % 60);
    runReportEnd(st == RunState::Finished);
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
        m.onMs = millis();
        m.idleWarned = false;
        logRun("%s: manual setpoint %.1f \xC2\xB0" "C after the program", s.ch[i].name, kept);
      }
    }
    endOpen |= d.endPending;
    endDone |= !d.endPending;
  }
  if (endDone && !endOpen && st == RunState::Finished) setMsg(LK_RUN, "Program finished");
  prevState = st;
  fanCycle(dt, pv, pvOk);

  float remaining;
  updatePlan(pv, remaining);

  // history + publish
  static uint32_t lastLive = 0;
  float spNow[HIST_CH];
  for (int i = NUM_CHANNELS; i < HIST_CH; ++i) spNow[i] = NAN;
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
  static float accLo[HIST_CH], accHi[HIST_CH];
  static bool accInit = false;
  if (!accInit) {
    accInit = true;
    fillNan(accLo, HIST_CH);
    fillNan(accHi, HIST_CH);
  }
  rawRecord(pv, pvOk, lastOk);
  for (int i = 0; i < HIST_CH; ++i)
    if (pvOk[i]) {
      accLo[i] = isnan(accLo[i]) ? pv[i] : fminf(accLo[i], pv[i]);
      accHi[i] = isnan(accHi[i]) ? pv[i] : fmaxf(accHi[i], pv[i]);
    }
  if (eng.active() && gHist->runValid) {
    runAnch.track(millis(), (int32_t)lroundf(eng.elapsed() * 1000.0f));
    int idx = (int)(eng.elapsed() / gHist->runBin);
    if (idx >= CHART_N) idx = CHART_N - 1;
    if (idx < 0) idx = 0;
    for (int i = 0; i < HIST_CH; ++i) {
      gHist->runPv[i][idx] = pvOk[i] ? pv[i] : NAN;
      if (pvOk[i]) {
        const float lo = gHist->runLo[i][idx], hi = gHist->runHi[i][idx];
        const bool fresh = idx > gHist->runStLast;  // first reading of this bin
        gHist->runLo[i][idx] = fresh || isnan(lo) ? pv[i] : fminf(lo, pv[i]);
        gHist->runHi[i][idx] = fresh || isnan(hi) ? pv[i] : fmaxf(hi, pv[i]);
      }
      if (i >= NUM_CHANNELS) continue;  // (the monitor: PV only)
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
      const bool none[HIST_CH] = {};
      float nan2[HIST_CH];
      fillNan(nan2, HIST_CH);
      for (uint32_t i = 0; i + 1 < n && i < gHist->liveCap; ++i) livePut(nan2, none, nan2, nan2, nan2);
      n = 1;
    }
    for (uint32_t i = 0; i < n; ++i) livePut(pv, pvOk, spNow, accLo, accHi);
    for (int c = 0; c < HIST_CH; ++c) accLo[c] = accHi[c] = NAN;
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

// The control task: controlTaskBegin(), then controlTaskStep() every 10 ms (the browser demo, demo/sim.cpp,
// calls the two itself)
static uint32_t lastPoll = 0, lastTick = 0;
void controlTaskBegin() {
  {
    Guard g;
    s = gSettings;
  }
  applySettings(s, true);
  eng.setEventSink(recordEv, nullptr);
  for (int i = 0; i < NUM_CHANNELS; ++i) man[i].sp = s.ch[i].manualSp;
  lastPoll = 0;
  lastTick = millis();
}

void controlTaskStep() {
  localCtrlHeartbeat();  // (the local task switches its heaters off if this task stops)
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
}

static void controlTask(void*) {
  controlTaskBegin();
  for (;;) {
    controlTaskStep();
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
  logWarn("Run interrupted by a reset (%s): \"%s\", %s, run time %s%d:%02d", intr.reason, p.name, where,
           rtcOk ? "" : "about ", t / 60, t % 60);
  snprintf(runMsg, sizeof(runMsg), "Run interrupted by a reset (%s): \"%.40s\" - Resume or Dismiss on the web page", intr.reason, p.name);
}

void appInit() {
  // large buffers on the heap (the static DRAM segment is almost full)
  gHist = (History*)calloc(1, sizeof(History));
  runProg = (DuoProgram*)calloc(1, sizeof(DuoProgram));
  // chart values: 6 x HIST_CH x CHART_N words (rows per channel, the monitor's too: HIST_CH). No PSRAM (CYD): in the IRAM heap, which the rest of the
  // firmware can't use (32-bit access only); DRAM is what runs short there (Wi-Fi, LVGL, web)
  // (with PSRAM 4 more for the live view, see liveViewUpdate())
#ifdef BOARD_HAS_PSRAM
  constexpr int HV_ROWS = 10;
#else
  constexpr int HV_ROWS = 6;
#endif
  const size_t hvBytes = sizeof(TenthRow) * HIST_CH * HV_ROWS;
#ifdef BOARD_HAS_PSRAM
  TenthRow* hv = (TenthRow*)malloc(hvBytes);
#else
  TenthRow* hv = (TenthRow*)heap_caps_malloc(hvBytes, MALLOC_CAP_EXEC | MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL);
  if (!hv) hv = (TenthRow*)malloc(hvBytes);
#endif
  if (gHist && hv) {
    gHist->runPv = hv;
    gHist->runSp = hv + HIST_CH;
    gHist->runPlan = hv + 2 * HIST_CH;
    gHist->plan = hv + 3 * HIST_CH;
    gHist->runLo = hv + 4 * HIST_CH;
    gHist->runHi = hv + 5 * HIST_CH;
#ifdef BOARD_HAS_PSRAM
    gHist->livePv = hv + 6 * HIST_CH;
    gHist->liveSp = hv + 7 * HIST_CH;
    gHist->liveLo = hv + 8 * HIST_CH;
    gHist->liveHi = hv + 9 * HIST_CH;
#endif
    for (int r = 0; r < HV_ROWS * HIST_CH; ++r)
      for (int i = 0; i < CHART_N; ++i) hv[r][i] = NAN;
  }
  // every PV reading (raw_hist.h): S3 RAW_HIST_BYTES of PSRAM; CYD RAW_HIST_CYD readings per channel in
  // the IRAM heap (32-bit access only; before the live history, which takes what is left of that heap)
  if (gHist) {
#ifdef BOARD_HAS_PSRAM
    const uint32_t rc = RAW_HIST_BYTES / NUM_CHANNELS / 8;
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT;
#else
    const uint32_t rc = RAW_HIST_CYD;
    const uint32_t caps = MALLOC_CAP_EXEC | MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL;
#endif
    for (int c = 0; c < HIST_CH; ++c) {
      rawRing[c].w = (uint32_t*)heap_caps_malloc(rc * 8, caps);
      rawRing[c].cap = rawRing[c].w ? rc : 0;
    }
    if (!rawRing[0].cap || !rawRing[1].cap) rawRing[0].cap = rawRing[1].cap = 0;  // both or none
    for (int c = NUM_CHANNELS; c < HIST_CH; ++c)  // the monitor's: the same size (or none: its chart line uses the slots)
      if (!rawRing[0].cap) rawRing[c].cap = 0;
    LogSerial.printf("[hist] every reading: %lu per channel (%lu bytes)\n", (unsigned long)rawRing[0].cap,
                     (unsigned long)(rawRing[0].cap * 8 * HIST_CH));
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
  gLive.runProg = -1;
  for (int i = 0; i < NUM_CHANNELS; ++i) gLive.run[i] = eng.out(i);
  if (!settingsLoad(gSettings)) settingsSave(gSettings);
  localCtrlBegin();  // before the control task configures the channels
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    ChannelLive& L = gLive.ch[i];
    L.type = gSettings.ch[i].type;
    strlcpy(L.name, gSettings.ch[i].name, sizeof(L.name));
    L.pv = L.spEff = L.out = L.ff = L.integ = NAN;
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
      logAuto(LK_EVENT, "%s - %s", why, b);
    }
  }
  checkInterrupted();
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
  if (c != CMD_SETTINGS_CHANGED) safetyActivity();  // (the commands come from the user interfaces)
  Cmd m{c, sub, (int16_t)arg, (int16_t)idx, value};
  return xQueueSend(cmdQ, &m, pdMS_TO_TICKS(50)) == pdTRUE;
}

// a background request (busStart) holds the slot; all callers are in the loop task
static bool busAsync = false;
static uint32_t busAsyncAt = 0, busAsyncMs = 0;

bool busStart(const BusRequest& r, uint32_t timeoutMs) {
  if (busAsync || xSemaphoreTake(reqMutex, 0) != pdTRUE) return false;
  xSemaphoreTake(reqDone, 0);  // clear stale signal
  reqSlot = r;
  reqPending = true;
  busAsync = true;
  busAsyncAt = millis();
  busAsyncMs = timeoutMs;
  return true;
}

int busCheck(BusRequest& r) {
  if (!busAsync) return -1;
  if (xSemaphoreTake(reqDone, 0) == pdTRUE) {
    r = reqSlot;
  } else if (millis() - busAsyncAt < busAsyncMs) {
    return 0;
  } else {
    strlcpy(r.err, "Timed out waiting for the control task", sizeof(r.err));
    r.ok = false;
  }
  busAsync = false;
  xSemaphoreGive(reqMutex);
  return 1;
}

bool busRequest(BusRequest& r, uint32_t timeoutMs) {
  if (busAsync) {  // (the same task holds the slot: waiting would only block)
    strlcpy(r.err, "Bus tool busy", sizeof(r.err));
    return false;
  }
  if (xSemaphoreTake(reqMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
    strlcpy(r.err, "Bus tool busy", sizeof(r.err));
    return false;
  }
  xSemaphoreTake(reqDone, 0);  // clear stale signal
  reqSlot = r;
  reqPending = true;
  // wait in 1 s slices and keep the caller's task watchdog fed: the web / console tools wait in the loop task,
  // whose watchdog restarts the board after 15 s (a find-setpoint scan can take longer)
  bool done = false;
  for (uint32_t waited = 0; !done && waited < timeoutMs;) {
    const uint32_t slice = timeoutMs - waited < 1000 ? timeoutMs - waited : 1000;
    done = xSemaphoreTake(reqDone, pdMS_TO_TICKS(slice)) == pdTRUE;
    waited += slice;
    esp_task_wdt_reset();  // (a task the watchdog does not watch: no effect)
  }
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

// (no memory for the heap copy: on the stack, in a function of its own so that the usual way needs no
// stack room for it)
static void __attribute__((noinline)) saveSettingsStack() {
  Settings copy;
  {
    Guard g;
    copy = gSettings;
  }
  settingsSave(copy);
}

void saveSettingsQuiet() {
  ScratchSettings copy;
  if (!copy) return saveSettingsStack();
  {
    Guard g;
    *copy = gSettings;
  }
  settingsSave(*copy);
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
  }
  saveSettingsQuiet();
  sendCmd(CMD_MANUAL_SP, ch, v);
}
