#include "app_state.h"
#include <math.h>
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
Program* gPrograms = nullptr;
SemaphoreHandle_t gLock;
volatile uint32_t gProgGen = 0;

static QueueHandle_t cmdQ;
static SemaphoreHandle_t reqMutex, reqDone;
static BusRequest reqSlot;
static volatile bool reqPending = false;

static ModbusMaster bus;                  // the one RS485 bus, shared by the Modbus channels
static ChannelSlot slots[NUM_CHANNELS];   // bottom, top
static uint8_t busTurn = 0;               // next channel to poll on the bus (round robin)
static ProgramEngine eng;                 // single-channel engine on ENGINE_CH (until step 5)
static Settings s;  // control task's private copy of the settings
#define EC (s.ch[ENGINE_CH])              // settings of the engine's channel

static Channel& ech() { return *slots[ENGINE_CH].active; }
static Controller* emb() { return slots[ENGINE_CH].active->modbus(); }  // null if not Modbus
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
static char runMsg[128] = "";
static char lastErr[NUM_CHANNELS][80];  // write errors of this cycle, per channel

// manual setpoint, per channel
struct Manual {
  bool on = false;
  float sp = NAN;
  bool pending = false;     // SP (+ outputs on) still to be written
  bool offPending = false;  // outputs off still to be written
};
static Manual man[NUM_CHANNELS];

// ------------------------------------------------------------------ helpers

static void setMsg(const char* m) { strlcpy(runMsg, m, sizeof(runMsg)); }

static float clampSp(int ch, float v) {
  if (v < s.ch[ch].spMin) v = s.ch[ch].spMin;
  if (v > s.ch[ch].spMax) v = s.ch[ch].spMax;
  return v;
}

// setpoint of the engine's channel
static bool writeSp(float v) {
  v = clampSp(ENGINE_CH, v);
  if (ech().writeSp(v, lastErr[ENGINE_CH], sizeof(lastErr[0]))) {
    lastWritten = v;
    lastWriteMs = millis();
    ++spWrites;
    return true;
  }
  return false;
}

// Modbus-only extras of the engine's channel (true = nothing to do)
static bool hasRampReg() { return emb() && emb()->reg().rampRate != REG_UNSET; }

static bool mbWrite(uint16_t addr, int16_t raw, const char* what) {
  Controller* c = emb();
  if (!c || addr == REG_UNSET) return true;
  const MbResult r = c->writeRaw(addr, raw);
  if (r != MbResult::Ok) mbDescribe(r, c->lastException(), what, lastErr[ENGINE_CH], sizeof(lastErr[0]));
  return r == MbResult::Ok;
}

static bool writeRampOff() {
  if (!hasRampReg()) return true;
  return mbWrite(emb()->reg().rampRate, emb()->reg().offRaw, "Ramp-rate write");
}

static bool writeRampRate(float perMin) {
  Controller* c = emb();
  const MbResult r = c->writeScaled(c->reg().rampRate, perMin);
  if (r != MbResult::Ok) mbDescribe(r, c->lastException(), "Ramp-rate write", lastErr[ENGINE_CH], sizeof(lastErr[0]));
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

static bool anyFault() {
  Guard g;
  for (int i = 0; i < NUM_CHANNELS; ++i)
    if (gLive.ch[i].fault) return true;
  return false;
}

static void startProgram(int idx) {
  if (eng.active()) return;
  if (anyFault()) {
    setMsg("Cannot start: acknowledge the channel fault first");
    return;
  }
  if (EC.type == CH_OFF) {
    snprintf(runMsg, sizeof(runMsg), "Cannot start: channel %s is off", EC.name);
    return;
  }
  if (!ech().spConfigured()) {
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
  programSanitize(p, EC.spMin, EC.spMax);
  // The controller silently clamps setpoints to SP.LO..SP.Hi; with guaranteed
  // soak that would stall the program forever, so check before starting.
  Controller* mc = emb();
  if (mc && mc->reg().spLo != REG_UNSET && mc->reg().spHi != REG_UNSET) {
    float lo, hi;
    uint8_t f1 = 0, f2 = 0;
    if (mc->readScaled(mc->reg().spLo, lo, f1) == MbResult::Ok && mc->readScaled(mc->reg().spHi, hi, f2) == MbResult::Ok &&
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
  // a program takes over from manual mode on its channel; make sure the outputs are on
  Manual& m = man[ENGINE_CH];
  m.on = m.pending = m.offPending = false;
  ech().setOutputs(true, lastErr[ENGINE_CH], sizeof(lastErr[0]));
  // remember the controller's own gradient so we can restore it at the end
  rampSaved = false;
  if (hasRampReg()) {
    int16_t raw;
    if (mc->readRaw(mc->reg().rampRate, raw) == MbResult::Ok) {
      rampOriginal = raw;
      rampSaved = true;
    }
    writeRampOff();
  }
  if (mc && EC.mb.lockFront) mbWrite(mc->reg().remote, 1, "Front-lock write");
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

static void manualSwitch(int ch, bool on) {
  if (ch < 0 || ch >= NUM_CHANNELS) return;
  Manual& m = man[ch];
  Channel& c = *slots[ch].active;
  const char* name = s.ch[ch].name;
  if (on) {
    if (ch == ENGINE_CH && eng.active()) {
      snprintf(runMsg, sizeof(runMsg), "%s: stop the program before switching to manual", name);
      return;
    }
    if (c.type() == CH_OFF) {
      snprintf(runMsg, sizeof(runMsg), "%s is off (Settings: channel type)", name);
      return;
    }
    char ft[64];
    if (c.fault(ft, sizeof(ft))) {
      snprintf(runMsg, sizeof(runMsg), "%s: acknowledge the fault first (%s)", name, ft);
      return;
    }
    if (!c.spConfigured()) {
      setMsg("Set the setpoint register address first (web: Controller)");
      return;
    }
    m.on = true;
    m.pending = true;
    m.offPending = false;
    snprintf(runMsg, sizeof(runMsg), "%s: manual setpoint on", name);
  } else {
    if (!m.on) return;
    m.on = false;
    m.pending = false;
    m.offPending = c.canSwitchOff();
    snprintf(runMsg, sizeof(runMsg), m.offPending ? "%s: manual off, outputs switched off"
                                                  : "%s: manual off (no outputs-off register: controller keeps its setpoint)",
             name);
  }
}

// (Re)opens the bus and selects the channel backends after a settings change
static void applySettings(const Settings& prev, bool first) {
  modbusBusBegin(bus, s);
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    const bool typeChanged = first || prev.ch[i].type != s.ch[i].type;
    slots[i].configure(i, &bus, s.ch[i]);
    if (!first && !man[i].on) man[i].sp = s.ch[i].manualSp;  // may have been changed through the API
    if (typeChanged && !first) {
      man[i].on = man[i].pending = man[i].offPending = false;
      if (i == ENGINE_CH && eng.active()) {
        eng.stop();
        endPending = false;
        snprintf(runMsg, sizeof(runMsg), "Stopped: type of channel %s changed", s.ch[i].name);
      }
    }
    Guard g;
    gLive.ch[i].type = s.ch[i].type;
    strlcpy(gLive.ch[i].name, s.ch[i].name, sizeof(gLive.ch[i].name));
    if (typeChanged) {
      gLive.ch[i].commOk = false;
      gLive.ch[i].lastErr[0] = 0;
    }
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
      if (slots[c.arg].active->ack(err, sizeof(err))) {
        snprintf(runMsg, sizeof(runMsg), "%s: fault acknowledged", s.ch[c.arg].name);
        Serial.printf("[ch] %s: fault acknowledged\n", s.ch[c.arg].name);
      } else {
        snprintf(runMsg, sizeof(runMsg), "%s: fault not cleared: %s", s.ch[c.arg].name, err);
      }
      break;
    }
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
      // a fault stops the run (spec 9.2); on the faulted channel the outputs stay off
      eng.stop();
      snprintf(runMsg, sizeof(runMsg), "Stopped: %s fault: %s - acknowledge it (web)", s.ch[i].name, ft);
    } else {
      snprintf(runMsg, sizeof(runMsg), "%s fault: %s - acknowledge it (web)", s.ch[i].name, ft);
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

static void driveSetpoint() {
  const RunState st = eng.state();
  if (st != RunState::Running && st != RunState::Paused) return;
  const uint32_t gen = eng.generation();
  const bool needSync = gen != syncedGen;
  const bool ctrlRamp = EC.mb.rampMode == RAMP_CONTROLLER && hasRampReg();

  if (ctrlRamp && needSync) {
    bool ok = true;
    hostFallback = false;
    if (st == RunState::Paused) {
      ok = writeRampOff() && writeSp(eng.setpoint());  // freeze where we are
    } else if (eng.phase() == Phase::Ramp && eng.phaseRemaining() > 1.0f) {
      const float cur = eng.setpoint(), tgt = eng.target();
      const float rate = fabsf(tgt - cur) / (eng.phaseRemaining() / 60.0f);
      // 1dP gradient: 0.1/min resolution, and the raw value must fit in int16
      if (rate >= 0.1f && rate * emb()->reg().scale <= 30000.0f) {
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
    // a local channel takes every new value (no controller EEPROM to spare)
    const bool local = EC.type == CH_LOCAL;
    const float minStep = local ? 0.0f : EC.mb.minStep;
    const uint32_t minMs = local ? 0 : (uint32_t)EC.mb.minWriteSec * 1000UL;
    const bool due = needSync || isnan(lastWritten) ||
                     (fabsf(sp - lastWritten) >= minStep && sp != lastWritten && millis() - lastWriteMs >= minMs);
    if (due && writeSp(sp)) syncedGen = gen;
  }
}

static void applyEndAction() {
  bool ok = true;
  const bool aborted = eng.state() == RunState::Aborted;
  if (hasRampReg()) ok &= writeRampOff();  // freeze a running controller ramp
  float hold = eng.setpoint();
  {
    Guard g;
    if (aborted && gLive.spEffValid) hold = gLive.spEff;
  }
  switch (EC.endAction) {
    case END_KEEP: ok &= writeSp(hold); break;
    case END_SET_SP: ok &= writeSp(EC.endSp); break;
    case END_OUTPUTS_OFF:
      ok &= writeSp(hold);
      if (ech().canSwitchOff()) ok &= ech().setOutputs(false, lastErr[ENGINE_CH], sizeof(lastErr[0]));
      break;
  }
  if (rampSaved && emb()) ok &= mbWrite(emb()->reg().rampRate, rampOriginal, "Ramp-rate restore");
  if (EC.mb.lockFront && emb()) ok &= mbWrite(emb()->reg().remote, 0, "Front-lock write");
  endPending = !ok;
  if (!aborted && ok) setMsg("Program finished");
}

static void driveManual(int ch) {
  Manual& m = man[ch];
  Channel& c = *slots[ch].active;
  char* err = lastErr[ch];
  if (m.on && m.pending) {
    if (c.setOutputs(true, err, sizeof(lastErr[0])) && c.writeSp(clampSp(ch, m.sp), err, sizeof(lastErr[0]))) {
      m.pending = false;
      if (ch == ENGINE_CH) {
        lastWritten = clampSp(ch, m.sp);
        lastWriteMs = millis();
        ++spWrites;
      }
    }
  }
  if (!m.on && m.offPending) {
    if (c.setOutputs(false, err, sizeof(lastErr[0]))) m.offPending = false;
  }
}

static void cycle(float dt) {
  pollChannels();

  bool pvValid, commOk;
  float pv, spEff;
  bool spEffValid;
  uint32_t lastOk;
  bool commCh[NUM_CHANNELS];
  {
    Guard g;
    const ChannelLive& E = gLive.ch[ENGINE_CH];
    pvValid = E.pvValid; pv = E.pv; commOk = E.commOk;
    spEff = E.spEff; spEffValid = E.spEffValid; lastOk = E.lastOkMs;
    for (int i = 0; i < NUM_CHANNELS; ++i) commCh[i] = gLive.ch[i].commOk;
  }

  // communication watchdog
  if (eng.state() == RunState::Running && s.commLossSec > 0 &&
      millis() - lastOk > (uint32_t)s.commLossSec * 1000UL) {
    eng.pause();
    snprintf(runMsg, sizeof(runMsg), "Paused: no answer from controller for %us", s.commLossSec);
  }

  eng.tick(dt, pv, pvValid, s.guaranteedSoak, s.soakBand);
  for (int i = 0; i < NUM_CHANNELS; ++i) lastErr[i][0] = 0;
  if (commOk) driveSetpoint();
  for (int i = 0; i < NUM_CHANNELS; ++i)
    if (commCh[i]) driveManual(i);

  // end of program (finished or aborted)
  const RunState st = eng.state();
  const bool wasActive = prevState == RunState::Running || prevState == RunState::Paused;
  if (wasActive && (st == RunState::Finished || st == RunState::Aborted)) endPending = true;
  if (endPending && commOk) applyEndAction();
  prevState = st;

  // history + publish
  static uint32_t lastLive = 0;
  const Manual& em = man[ENGINE_CH];
  const float spNow = spEffValid ? spEff : (eng.active() ? eng.setpoint() : (em.on ? em.sp : NAN));
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
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    ChannelLive& L = gLive.ch[i];
    L.manualOn = man[i].on;
    L.manualSp = man[i].sp;
    L.manualSynced = !man[i].pending && !man[i].offPending;
    if (lastErr[i][0]) strlcpy(L.lastErr, lastErr[i], sizeof(L.lastErr));
  }
  // flat mirror of the engine's channel (old UIs)
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
  }
  gLive.phaseEl = eng.phaseElapsed();
  gLive.phaseDur = eng.phaseDuration();
  gLive.elapsed = eng.elapsed();
  gLive.total = eng.total();
  gLive.soakWait = eng.soakWaiting();
  gLive.hostSp = eng.setpoint();
  gLive.target = eng.target();
  gLive.spSynced = lastErr[ENGINE_CH][0] == 0;
  gLive.lastWrittenSp = lastWritten;
  gLive.spWrites = spWrites;
  strlcpy(gLive.runMsg, runMsg, sizeof(gLive.runMsg));

  bool fault = false, active = eng.active();
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    if (s.ch[i].type != CH_OFF && (!gLive.ch[i].commOk || gLive.ch[i].fault)) fault = true;
    if (man[i].on) active = true;
  }
  statusLedSet(statusLedPick(fault, st == RunState::Paused, netApActive(), active));
}

static void controlTask(void*) {
  {
    Guard g;
    s = gSettings;
  }
  applySettings(s, true);
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
  static Program example;  // static: large
  programExample(example);
  programStoreBegin(gPrograms, &example);
  gLive.manualSp = gSettings.ch[ENGINE_CH].manualSp;
  histResetLive();
  statusLedBegin();
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
    programSanitize(gPrograms[idx], gSettings.ch[ENGINE_CH].spMin, gSettings.ch[ENGINE_CH].spMax);
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
