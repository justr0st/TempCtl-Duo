#include "app_state.h"
#include <math.h>
#include <string.h>
#include "controller.h"
#include "program_store.h"

Settings gSettings;
LiveData gLive;
History* gHist = nullptr;
Program* gPrograms = nullptr;
SemaphoreHandle_t gLock;
volatile uint32_t gProgGen = 0;

static QueueHandle_t cmdQ;
static SemaphoreHandle_t reqMutex, reqDone;
static BusRequest reqSlot;
static volatile bool reqPending = false;

static Controller ctl;
static ProgramEngine eng;
static Settings s;  // control task's private copy of the settings
static Program startBuf;  // program being started (kept off the task stack)
static int8_t runProg = -1;

// setpoint output bookkeeping
static uint32_t syncedGen = 0xFFFFFFFF;
static bool hostFallback = false;       // controller-ramp mode, segment too slow -> host stepping
static float lastWritten = NAN;
static uint32_t lastWriteMs = 0;
static uint32_t spWrites = 0;
static bool rampSaved = false;
static int16_t rampOriginal = 0;
static bool endPending = false;
static RunState prevState = RunState::Idle;
static char runMsg[80] = "";
static char lastErr[80] = "";

// manual setpoint
static bool manualOn = false;
static float manualSp = NAN;
static bool manualPending = false;   // SP (+ outputs on) still to be written
static bool outputsOffPending = false;

// ------------------------------------------------------------------ helpers

static void setMsg(const char* m) { strlcpy(runMsg, m, sizeof(runMsg)); }

static void describeError(MbResult r, const char* what, char* out, size_t n) {
  if (r == MbResult::Exception)
    snprintf(out, n, "%s: %s", what, mbExceptionName(ctl.lastException()));
  else
    snprintf(out, n, "%s: %s", what, mbResultName(r));
}

static bool writeSp(float v) {
  if (v < s.spMin) v = s.spMin;
  if (v > s.spMax) v = s.spMax;
  const MbResult r = ctl.writeScaled(s.reg.sp, v);
  if (r == MbResult::Ok) {
    lastWritten = v;
    lastWriteMs = millis();
    ++spWrites;
    return true;
  }
  describeError(r, "Setpoint write", lastErr, sizeof(lastErr));
  return false;
}

static bool writeRampOff() {
  if (s.reg.rampRate == REG_UNSET) return true;
  const MbResult r = ctl.writeRaw(s.reg.rampRate, s.reg.offRaw);
  if (r != MbResult::Ok) describeError(r, "Ramp-rate write", lastErr, sizeof(lastErr));
  return r == MbResult::Ok;
}

static bool writeRampRate(float perMin) {
  const MbResult r = ctl.writeScaled(s.reg.rampRate, perMin);
  if (r != MbResult::Ok) describeError(r, "Ramp-rate write", lastErr, sizeof(lastErr));
  return r == MbResult::Ok;
}

float runSpanFor(float total) {
  if (total <= 0) return 60;
  float spare = total * 0.1f;
  if (spare < 30) spare = 30;
  return total + spare;
}

static void histResetLive() {
  for (int i = 0; i < CHART_N; ++i) gHist->livePv[i] = gHist->liveSp[i] = NAN;
  gHist->runValid = false;
}

static void histStartRun(const Program& p, float startTemp, float total) {
  gHist->runValid = true;
  gHist->runTotal = total;
  gHist->runSpan = runSpanFor(total);
  gHist->runBin = gHist->runSpan / CHART_N;
  gHist->runStart = startTemp;
  gHist->runProg = p;
  for (int i = 0; i < CHART_N; ++i) gHist->runPv[i] = gHist->runSp[i] = NAN;
}

void chartSnapshot(ChartData& d) {
  Guard g;
  const bool active = gLive.state == RunState::Running || gLive.state == RunState::Paused;
  d.run = active && gHist->runValid;
  d.planActive = d.run;
  d.spActive = d.run || gLive.manualOn;
  if (d.run) {
    d.binSec = gHist->runBin;
    d.span = gHist->runSpan;
    d.planSpan = gHist->runSpan;
    d.planTotal = gHist->runTotal;
    d.elapsed = gLive.elapsed;
    strlcpy(d.progName, gHist->runProg.name, sizeof(d.progName));
    for (int i = 0; i < CHART_N; ++i) {
      const float t = i * d.binSec;
      d.pv[i] = gHist->runPv[i];
      d.sp[i] = gHist->runSp[i];
      d.plan[i] = t <= d.planTotal ? ProgramEngine::plannedAt(gHist->runProg, gHist->runStart, t) : NAN;
    }
    return;
  }
  // idle: rolling window + preview of the selected program on its own timeline
  const Program& p = gPrograms[gSettings.selProgram < MAX_PROGRAMS ? gSettings.selProgram : 0];
  d.binSec = LIVE_SAMPLE_SEC;
  d.span = CHART_N * LIVE_SAMPLE_SEC;
  d.planTotal = ProgramEngine::plannedTotal(p);
  d.planSpan = runSpanFor(d.planTotal);
  d.elapsed = 0;
  strlcpy(d.progName, p.name, sizeof(d.progName));
  const float start = gLive.pvValid ? gLive.pv : (gLive.spEffValid ? gLive.spEff : 20.0f);
  const float pb = d.planSpan / CHART_N;
  for (int i = 0; i < CHART_N; ++i) {
    const float t = i * pb;
    d.pv[i] = gHist->livePv[i];
    d.sp[i] = gHist->liveSp[i];
    d.plan[i] = (p.count > 0 && t <= d.planTotal) ? ProgramEngine::plannedAt(p, start, t) : NAN;
  }
}

// ------------------------------------------------------------------ commands

static void startProgram(int idx) {
  if (eng.active()) return;
  if (s.reg.sp == REG_UNSET) {
    setMsg("Set the setpoint register address first (web: Controller)");
    return;
  }
  bool pvOk;
  float pv;
  {
    Guard g;
    pvOk = gLive.pvValid;
    pv = gLive.pv;
  }
  if (!pvOk) {
    setMsg("Cannot start: no valid temperature from controller");
    return;
  }
  {
    Guard g;
    if (idx < 0 || idx >= MAX_PROGRAMS) idx = gSettings.selProgram;
    if (idx >= MAX_PROGRAMS) idx = 0;
    startBuf = gPrograms[idx];
  }
  Program& p = startBuf;
  if (p.count == 0) {
    snprintf(runMsg, sizeof(runMsg), "Cannot start: \"%s\" has no steps", p.name);
    return;
  }
  programSanitize(p, s.spMin, s.spMax);
  // The controller silently clamps setpoints to SP.LO..SP.Hi; with guaranteed
  // soak that would stall the program forever, so check before starting.
  if (s.reg.spLo != REG_UNSET && s.reg.spHi != REG_UNSET) {
    float lo, hi;
    uint8_t f1 = 0, f2 = 0;
    if (ctl.readScaled(s.reg.spLo, lo, f1) == MbResult::Ok && ctl.readScaled(s.reg.spHi, hi, f2) == MbResult::Ok &&
        f1 == VAL_OK && f2 == VAL_OK) {
      for (int i = 0; i < p.count; ++i) {
        if (p.s[i].target < lo || p.s[i].target > hi) {
          snprintf(runMsg, sizeof(runMsg), "Step %d target %.1f is outside controller limits %.1f..%.1f", i + 1,
                   p.s[i].target, lo, hi);
          return;
        }
      }
    }
  }
  if (!eng.start(p, pv)) {
    setMsg("Cannot start: the program has no steps");
    return;
  }
  runProg = (int8_t)idx;
  // a program takes over from manual mode; make sure the outputs are on
  manualOn = false;
  manualPending = false;
  outputsOffPending = false;
  if (s.reg.ctrlOff != REG_UNSET) ctl.writeRaw(s.reg.ctrlOff, 0);
  // remember the controller's own gradient so we can restore it at the end
  rampSaved = false;
  if (s.reg.rampRate != REG_UNSET) {
    int16_t raw;
    if (ctl.readRaw(s.reg.rampRate, raw) == MbResult::Ok) {
      rampOriginal = raw;
      rampSaved = true;
    }
    writeRampOff();
  }
  if (s.lockFront && s.reg.remote != REG_UNSET) ctl.writeRaw(s.reg.remote, 1);
  lastWritten = NAN;
  syncedGen = 0xFFFFFFFF;
  hostFallback = false;
  spWrites = 0;
  endPending = false;
  {
    Guard g;
    histStartRun(p, pv, eng.total());
  }
  snprintf(runMsg, sizeof(runMsg), "Program \"%s\" started", p.name);
}

static void manualSwitch(bool on) {
  if (on) {
    if (eng.active()) {
      setMsg("Stop the program before switching to manual");
      return;
    }
    if (s.reg.sp == REG_UNSET) {
      setMsg("Set the setpoint register address first (web: Controller)");
      return;
    }
    manualOn = true;
    manualPending = true;
    outputsOffPending = false;
    setMsg("Manual setpoint on");
  } else {
    if (!manualOn) return;
    manualOn = false;
    manualPending = false;
    outputsOffPending = s.reg.ctrlOff != REG_UNSET;
    setMsg(outputsOffPending ? "Manual off: controller outputs switched off"
                             : "Manual off (no outputs-off register: controller keeps its setpoint)");
  }
}

static void handleCmd(const Cmd& c) {
  switch (c.type) {
    case CMD_START: startProgram(c.arg); break;
    case CMD_PAUSE: eng.pause(); setMsg("Paused"); break;
    case CMD_RESUME: eng.resume(); setMsg("Resumed"); break;
    case CMD_STOP: eng.stop(); setMsg("Stopped by user"); break;
    case CMD_SKIP: eng.skip(); setMsg("Skipped to next phase"); break;
    case CMD_SETTINGS_CHANGED: {
      Guard g;
      s = gSettings;
    }
      ctl.configure(s);
      break;
    case CMD_MANUAL_ON: manualSwitch(true); break;
    case CMD_MANUAL_OFF: manualSwitch(false); break;
    case CMD_MANUAL_SP:
      manualSp = constrain(c.value, s.spMin, s.spMax);
      if (manualOn) manualPending = true;
      break;
  }
}

// ------------------------------------------------------------------ bus tools

static void processRequest(BusRequest& r) {
  r.ok = false;
  r.err[0] = 0;
  r.n = 0;
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
    const MbResult res = ctl.readScaled(s.reg.spEff, ref, flag);
    if (res != MbResult::Ok || flag != VAL_OK) {
      strlcpy(r.err, "Cannot read the effective setpoint (SP.EF) as reference", sizeof(r.err));
      return;
    }
    r.refValue = ref;
    uint16_t to = r.to;
    if (to < r.from) to = r.from;
    if (to - r.from > 300) to = r.from + 300;
    for (uint32_t a = r.from; a <= to && r.n < 64; ++a) {
      if (a == s.reg.spEff || a == s.reg.pv) continue;
      int16_t raw;
      if (ctl.readRaw((uint16_t)a, raw) != MbResult::Ok) continue;
      if (Controller::classify(raw) != VAL_OK) continue;
      if (fabsf(raw / s.reg.scale - ref) < 0.051f) r.values[r.n++] = (int16_t)a;
    }
    r.ok = true;
  }
}

// ------------------------------------------------------------------ cycle

static void poll() {
  float pv = NAN, spEff = NAN, out = NAN;
  int16_t st = 0, al = 0;
  uint8_t pvFlag = VAL_OK, f = 0;
  bool pvOk = false, spOk = false, outOk = false, stOk = false, alOk = false;
  char err[80] = "";

  if (s.reg.pv == REG_UNSET) {
    strlcpy(err, "PV register not configured", sizeof(err));
  } else {
    MbResult r = ctl.readScaled(s.reg.pv, pv, pvFlag);
    if (r == MbResult::Ok) {
      pvOk = (pvFlag == VAL_OK);
      if (s.reg.spEff != REG_UNSET && ctl.readScaled(s.reg.spEff, spEff, f) == MbResult::Ok) spOk = (f == VAL_OK);
      if (s.reg.out != REG_UNSET && ctl.readScaled(s.reg.out, out, f) == MbResult::Ok) outOk = (f == VAL_OK);
      if (s.reg.status != REG_UNSET) stOk = ctl.readRaw(s.reg.status, st) == MbResult::Ok;
      if (s.reg.alarm != REG_UNSET) alOk = ctl.readRaw(s.reg.alarm, al) == MbResult::Ok;
    } else {
      describeError(r, "PV read", err, sizeof(err));
    }
  }
  const bool linkOk = err[0] == 0;
  Guard g;
  gLive.commOk = linkOk;
  if (linkOk) { gLive.okCount++; gLive.lastOkMs = millis(); }
  else { gLive.errCount++; strlcpy(gLive.lastErr, err, sizeof(gLive.lastErr)); }
  gLive.pvValid = pvOk; gLive.pv = pv; gLive.pvFlag = linkOk ? pvFlag : (uint8_t)VAL_OK;
  gLive.spEffValid = spOk; gLive.spEff = spEff;
  gLive.outValid = outOk; gLive.out = out;
  gLive.statusValid = stOk; gLive.status = (uint16_t)st;
  gLive.alarmValid = alOk; gLive.alarm = (uint16_t)al;
}

static void driveSetpoint() {
  const RunState st = eng.state();
  if (st != RunState::Running && st != RunState::Paused) return;
  const uint32_t gen = eng.generation();
  const bool needSync = gen != syncedGen;
  const bool ctrlRamp = s.rampMode == RAMP_CONTROLLER && s.reg.rampRate != REG_UNSET;

  if (ctrlRamp && needSync) {
    bool ok = true;
    hostFallback = false;
    if (st == RunState::Paused) {
      ok = writeRampOff() && writeSp(eng.setpoint());  // freeze where we are
    } else if (eng.phase() == Phase::Ramp && eng.phaseRemaining() > 1.0f) {
      const float cur = eng.setpoint(), tgt = eng.target();
      const float rate = fabsf(tgt - cur) / (eng.phaseRemaining() / 60.0f);
      // 1dP gradient: 0.1/min resolution, and the raw value must fit in int16
      if (rate >= 0.1f && rate * s.reg.scale <= 30000.0f) {
        // start the controller's own ramp from the current program value
        ok = writeRampOff() && writeSp(cur) && writeRampRate(rate) && writeSp(tgt);
      } else {
        hostFallback = true;  // too slow/fast for the controller gradient: step from here
        ok = writeRampOff() && writeSp(cur);
      }
    } else {
      ok = writeRampOff() && writeSp(eng.target());
    }
    if (ok) syncedGen = gen;
    return;
  }

  if (!ctrlRamp || hostFallback) {
    const float sp = eng.setpoint();
    const bool due = needSync || isnan(lastWritten) ||
                     (fabsf(sp - lastWritten) >= s.minStep &&
                      millis() - lastWriteMs >= (uint32_t)s.minWriteSec * 1000UL);
    if (due && writeSp(sp)) syncedGen = gen;
  }
}

static void applyEndAction() {
  bool ok = true;
  const bool aborted = eng.state() == RunState::Aborted;
  const bool ctrlRamp = s.reg.rampRate != REG_UNSET;
  if (ctrlRamp) ok &= writeRampOff();  // freeze a running controller ramp
  float hold = eng.setpoint();
  {
    Guard g;
    if (aborted && gLive.spEffValid) hold = gLive.spEff;
  }
  switch (s.endAction) {
    case END_KEEP: ok &= writeSp(hold); break;
    case END_SET_SP: ok &= writeSp(s.endSp); break;
    case END_OUTPUTS_OFF:
      ok &= writeSp(hold);
      if (s.reg.ctrlOff != REG_UNSET) ok &= ctl.writeRaw(s.reg.ctrlOff, 1) == MbResult::Ok;
      break;
  }
  if (rampSaved) ok &= ctl.writeRaw(s.reg.rampRate, rampOriginal) == MbResult::Ok;
  if (s.lockFront && s.reg.remote != REG_UNSET) ok &= ctl.writeRaw(s.reg.remote, 0) == MbResult::Ok;
  endPending = !ok;
  if (!aborted && ok) setMsg("Program finished");
}

static void driveManual() {
  if (manualOn && manualPending) {
    bool ok = true;
    if (s.reg.ctrlOff != REG_UNSET) {
      const MbResult r = ctl.writeRaw(s.reg.ctrlOff, 0);
      if (r != MbResult::Ok) { describeError(r, "Outputs-on write", lastErr, sizeof(lastErr)); ok = false; }
    }
    if (ok && writeSp(manualSp)) manualPending = false;
  }
  if (!manualOn && outputsOffPending) {
    const MbResult r = ctl.writeRaw(s.reg.ctrlOff, 1);
    if (r == MbResult::Ok) outputsOffPending = false;
    else describeError(r, "Outputs-off write", lastErr, sizeof(lastErr));
  }
}

static void setLed(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, r ? LOW : HIGH);
  digitalWrite(PIN_LED_G, g ? LOW : HIGH);
  digitalWrite(PIN_LED_B, b ? LOW : HIGH);
}

static void cycle(float dt) {
  poll();

  bool pvValid, commOk;
  float pv, spEff;
  bool spEffValid;
  uint32_t lastOk;
  {
    Guard g;
    pvValid = gLive.pvValid; pv = gLive.pv; commOk = gLive.commOk;
    spEff = gLive.spEff; spEffValid = gLive.spEffValid; lastOk = gLive.lastOkMs;
  }

  // communication watchdog
  if (eng.state() == RunState::Running && s.commLossSec > 0 &&
      millis() - lastOk > (uint32_t)s.commLossSec * 1000UL) {
    eng.pause();
    snprintf(runMsg, sizeof(runMsg), "Paused: no answer from controller for %us", s.commLossSec);
  }

  eng.tick(dt, pv, pvValid, s.guaranteedSoak, s.soakBand);
  lastErr[0] = 0;
  if (commOk) driveSetpoint();
  if (commOk) driveManual();

  // end of program (finished or aborted)
  const RunState st = eng.state();
  const bool wasActive = prevState == RunState::Running || prevState == RunState::Paused;
  if (wasActive && (st == RunState::Finished || st == RunState::Aborted)) endPending = true;
  if (endPending && commOk) applyEndAction();
  prevState = st;

  // history + publish
  static uint32_t lastLive = 0;
  const float spNow = spEffValid ? spEff : (eng.active() ? eng.setpoint() : (manualOn ? manualSp : NAN));
  Guard g;
  if (eng.active() && gHist->runValid) {
    int idx = (int)(eng.elapsed() / gHist->runBin);
    if (idx >= CHART_N) idx = CHART_N - 1;
    if (idx < 0) idx = 0;
    gHist->runPv[idx] = pvValid ? pv : NAN;
    gHist->runSp[idx] = spNow;
  }
  // rolling window: one slot per LIVE_SAMPLE_SEC even if polling is slower
  const uint32_t slotMs = LIVE_SAMPLE_SEC * 1000UL;
  uint32_t n = (millis() - lastLive) / slotMs;
  if (n) {
    lastLive = (n > CHART_N) ? millis() : lastLive + n * slotMs;
    if (n > CHART_N) n = CHART_N;
    memmove(gHist->livePv, gHist->livePv + n, sizeof(float) * (CHART_N - n));
    memmove(gHist->liveSp, gHist->liveSp + n, sizeof(float) * (CHART_N - n));
    for (uint32_t i = CHART_N - n; i < CHART_N; ++i) {
      gHist->livePv[i] = pvValid ? pv : NAN;
      gHist->liveSp[i] = spNow;
    }
  }
  gLive.state = st;
  gLive.phase = eng.phase();
  gLive.step = (int8_t)eng.step();
  gLive.stepCount = (uint8_t)eng.stepCount();
  gLive.runProg = runProg;
  strlcpy(gLive.runName, eng.program().name, sizeof(gLive.runName));
  gLive.manualOn = manualOn;
  gLive.manualSp = manualSp;
  gLive.manualSynced = !manualPending && !outputsOffPending;
  gLive.phaseEl = eng.phaseElapsed();
  gLive.phaseDur = eng.phaseDuration();
  gLive.elapsed = eng.elapsed();
  gLive.total = eng.total();
  gLive.soakWait = eng.soakWaiting();
  gLive.hostSp = eng.setpoint();
  gLive.target = eng.target();
  gLive.spSynced = lastErr[0] == 0;
  gLive.lastWrittenSp = lastWritten;
  gLive.spWrites = spWrites;
  if (lastErr[0]) strlcpy(gLive.lastErr, lastErr, sizeof(gLive.lastErr));
  strlcpy(gLive.runMsg, runMsg, sizeof(gLive.runMsg));

  const bool active = eng.active() || manualOn;
  setLed(!commOk, commOk && !active, commOk && active);
}

static void controlTask(void*) {
  {
    Guard g;
    s = gSettings;
  }
  ctl.configure(s);
  manualSp = s.manualSp;
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
  gPrograms = (Program*)calloc(MAX_PROGRAMS, sizeof(Program));
  if (!gHist || !gPrograms) {
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
  static Program legacy;  // static: large
  bool haveLegacy = false;
  if (!settingsLoad(gSettings, &legacy, &haveLegacy)) settingsSave(gSettings);
  if (!haveLegacy) programExample(legacy);
  programStoreBegin(gPrograms, &legacy);
  gLive.manualSp = gSettings.manualSp;
  histResetLive();
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  setLed(false, false, false);
}

void controlTaskStart() {
  xTaskCreatePinnedToCore(controlTask, "control", 8192, nullptr, 3, nullptr, 0);
}

bool sendCmd(CmdType c, int arg, float value) {
  Cmd m{c, (int16_t)arg, value};
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

bool saveProgram(int idx) {
  if (idx < 0 || idx >= MAX_PROGRAMS) return false;
  static Program copy;
  {
    Guard g;
    programSanitize(gPrograms[idx], gSettings.spMin, gSettings.spMax);
    copy = gPrograms[idx];
    ++gProgGen;
  }
  return programStoreSave(idx, copy);
}

void selectProgram(int idx) {
  if (idx < 0 || idx >= MAX_PROGRAMS) return;
  {
    Guard g;
    if (gSettings.selProgram == idx) return;
    gSettings.selProgram = (uint8_t)idx;
  }
  saveSettingsQuiet();
}

void setManualSp(float v) {
  if (isnan(v)) return;
  {
    Guard g;
    v = constrain(v, gSettings.spMin, gSettings.spMax);
    gSettings.manualSp = v;
    gLive.manualSp = v;
  }
  saveSettingsQuiet();
  sendCmd(CMD_MANUAL_SP, 0, v);
}
