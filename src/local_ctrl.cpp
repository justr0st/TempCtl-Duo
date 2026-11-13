#include "local_ctrl.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <math.h>
#include "app_state.h"
#include "config.h"
#include "io_pins.h"
#include "pid.h"
#include "runaway.h"
#include "safety.h"
#include "tc_filter.h"
#include "tc_sensor.h"

#if defined(PIN_TC_SCK) && (defined(PIN_TC_CS_LOCAL) || defined(PIN_TC_CS_BOTTOM))
#define HAVE_LOCAL 1
#else
#define HAVE_LOCAL 0
#endif

static constexpr uint32_t TICK_MS = 50;  // the task's tick; each channel reads every cfg.readMs (100 ... 1000 ms)
static constexpr uint32_t SSR_TIMEOUT_MS = 1500;
// the control task (setpoints, run engine, end of a run) reports every pass; without a report for this long
// a heating channel is switched off with a fault (its setpoint would never change again)
static constexpr uint32_t CONTROL_TIMEOUT_MS = 10000;
static volatile uint32_t controlBeatMs = 0;  // millis of the last report, 0 = none yet (task not started)

struct LocalPins {
  int cs = -1, ssr = -1;
};

struct Local {
  // configuration / commands (control task -> local task)
  bool enabled = false;
  LocalChannelSettings cfg{};
  LocalPins pins;
  float sp = NAN;
  bool on = false;
  // state (local task)
  float pv = NAN, rawC = NAN, coldC = NAN, out = 0;
  uint8_t tcFault = TC_NO_DEVICE;
  TcFilter flt;              // median, bad readings, "current" (tc_filter.h)
  uint32_t lastStepMs = 0;   // the channel's last reading (millis; 0 = none yet)
  uint8_t fault = FAULT_NONE;
  char faultText[64] = "";
  Pid pid;
  RunawayMonitor runaway;
  GuardMonitor guard;
  volatile bool otherHeat = false;  // another channel (also a Modbus one) has its outputs on
  RelayAutotune tune;
  bool tuning = false;
  float tuneSp = NAN;
  // feed-forward (spec 9.23): setpoint preview from the control task, its age; FF on in the last PID step
  float spAhead = NAN, rateAhead = 0, rateNow = 0;
  uint32_t previewMs = 0;
  uint32_t previewEveryMs = 0;  // the control cycle (bus.pollMs) the preview comes with
  bool ffWas = false;
  float ff = NAN, integ = NAN;  // shown / logged (NaN = not running)
};

static Local loc[NUM_CHANNELS];
static TcFilterCfg fltCfg(const LocalChannelSettings& c) {
  return TcFilterCfg{c.median != 0, c.medianN, c.readMs, c.jumpMax, (float)c.badSec};
}
static bool freshNow(const Local& l) { return l.flt.fresh(fltCfg(l.cfg), millis()); }
static SemaphoreHandle_t mtx = nullptr;

struct LockL {
  LockL() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~LockL() { xSemaphoreGive(mtx); }
};

// ------------------------------------------------------------------ pins

static bool pinsFor(int ch, LocalPins& p) {
#if defined(PIN_TC_CS_LOCAL)
  (void)ch;  // one wired local channel: whichever channel is local uses it
  p.cs = PIN_TC_CS_LOCAL;
  p.ssr = PIN_SSR_LOCAL;
  return true;
#elif defined(PIN_TC_CS_BOTTOM)
  p.cs = ch == CH_BOTTOM ? PIN_TC_CS_BOTTOM : PIN_TC_CS_TOP;
  p.ssr = ch == CH_BOTTOM ? PIN_SSR_BOTTOM : PIN_SSR_TOP;
  return true;
#else
  (void)ch;
  (void)p;
  return false;
#endif
}

void localCtrlEarlyInit() {
#if HAVE_LOCAL
  static const int ssrPins[] = {
#if defined(PIN_SSR_LOCAL)
      PIN_SSR_LOCAL,
#else
      PIN_SSR_BOTTOM, PIN_SSR_TOP,
#endif
  };
  static const int csPins[] = {
#if defined(PIN_TC_CS_LOCAL)
      PIN_TC_CS_LOCAL,
#else
      PIN_TC_CS_BOTTOM, PIN_TC_CS_TOP,
#endif
  };
  for (int p : ssrPins) {
    digitalWrite(p, LOW);
    pinMode(p, OUTPUT);
  }
  for (int p : csPins) {
    digitalWrite(p, HIGH);
    pinMode(p, OUTPUT);
  }
#if defined(PIN_TC_CS_MON)
  digitalWrite(PIN_TC_CS_MON, HIGH);  // monitor converter deselected
  pinMode(PIN_TC_CS_MON, OUTPUT);
#endif
  // (the SSR supply relay is an optional pin: off from ioEarlyInit, io_pins.h)
  pinMode(PIN_TC_SCK, OUTPUT);
  digitalWrite(PIN_TC_SCK, LOW);
  pinMode(PIN_TC_SO, INPUT_PULLUP);  // no converter: all ones = "no device"
#endif
}

// ------------------------------------------------------------------ SSR (time proportioning)

static volatile float ssrDuty[NUM_CHANNELS];
static volatile uint32_t ssrWindowMs[NUM_CHANNELS];
static volatile uint32_t ssrSetMs[NUM_CHANNELS];
static volatile int ssrPin[NUM_CHANNELS] = {-1, -1};
static bool ssrLevel[NUM_CHANNELS];

static void ssrTick(void*) {
  const uint32_t now = millis();
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    const int pin = ssrPin[i];
    if (pin < 0) continue;
    bool on = false;
    const float d = ssrDuty[i];
    if (d > 0 && !safetyEstop() && now - ssrSetMs[i] < SSR_TIMEOUT_MS) {
      const uint32_t w = ssrWindowMs[i] ? ssrWindowMs[i] : 1000;
      // the top channel's window starts half a window later: the two heaters do not switch on at the same
      // instant (lower peak mains current while both run part load)
      on = ((now + (i ? w / 2 : 0)) % w) < (uint32_t)(d * w);
    }
    if (on != ssrLevel[i]) {
      ssrLevel[i] = on;
      digitalWrite(pin, on ? HIGH : LOW);
    }
  }
}

static void ssrSet(int ch, float duty, float windowSec) {
  if (duty < 0) duty = 0;
  if (duty > 1) duty = 1;
  ssrWindowMs[ch] = (uint32_t)(windowSec * 1000.0f);
  ssrDuty[ch] = duty;
  ssrSetMs[ch] = millis();
}

// ------------------------------------------------------------------ thermocouple

static uint32_t readFrame(int cs, int bits) {
  uint32_t v = 0;
  digitalWrite(PIN_TC_SCK, LOW);
  digitalWrite(cs, LOW);
  delayMicroseconds(2);
  for (int i = 0; i < bits; ++i) {  // data bit valid while SCK is low, next bit after the falling edge
    v = (v << 1) | (digitalRead(PIN_TC_SO) ? 1u : 0u);
    digitalWrite(PIN_TC_SCK, HIGH);
    delayMicroseconds(2);
    digitalWrite(PIN_TC_SCK, LOW);
    delayMicroseconds(2);
  }
  digitalWrite(cs, HIGH);
  return v;
}

static TcReading readSensor(uint8_t sensor, int cs) {
  if (sensor == SENSOR_MAX6675) return tcDecode6675((uint16_t)readFrame(cs, 16));
  return tcDecode31855(readFrame(cs, 32));
}

// ------------------------------------------------------------------ task

static void latch(Local& l, uint8_t f, const char* text) {
  if (l.fault) return;
  l.fault = f;
  snprintf(l.faultText, sizeof(l.faultText), "%s", text);
  l.on = false;
  l.pid.reset();
  l.runaway.reset();
  if (l.tuning) {
    l.tune.stop();
    l.tuning = false;
  }
}

static void step(int ch, float dt) {
  Local& l = loc[ch];
  // read outside the lock (a few hundred microseconds)
  bool enabled;
  uint8_t sensor = 0;
  int cs = -1;
  {
    LockL g;
    enabled = l.enabled && l.pins.cs >= 0;
    sensor = l.cfg.sensor;
    cs = l.pins.cs;
  }
  if (!enabled) {
    ssrSet(ch, 0, 1);
    return;
  }
  const TcReading r = readSensor(sensor, cs);
  float duty = 0, window = 1;
  {
    LockL g;
    const LocalChannelSettings& c = l.cfg;
    const TcFilterCfg fc = fltCfg(c);
    const uint32_t now = millis();
    window = c.windowSec;
    l.tcFault = r.fault;
    l.coldC = r.coldC;
    // bad readings (converter fault, implausible jump): the first ones are held back and the output stays
    // (tc_filter.h); a jump that lasts is the new level
    const TcIn in = l.flt.update(fc, r.fault == TC_OK, r.tempC, now);
    if (in == TcIn::Good) {
      l.rawC = l.flt.value();
      l.pv = l.rawC * c.calGain + c.calOffset;
      if (l.flt.newLevel()) l.pid.rebase(l.pv);  // (no derivative kick from the step)
    }
    const bool fresh = l.flt.fresh(fc, now);
    char t[64];
    if (safetyEstop() && (l.on || l.tuning)) {  // E-stop: outputs off (not a fault; the control task tells)
      l.on = false;
      if (l.tuning) {
        l.tune.stop();
        l.tuning = false;
      }
      l.pid.reset();
      l.runaway.reset();
    }
    // faults are latched only while the outputs are on; when off the channel just shows "no reading"
    const bool heating = l.on || l.tuning;
    if (heating && l.flt.badTooLong(fc, now)) {  // bad readings for longer than the setting
      if (r.fault != TC_OK) snprintf(t, sizeof(t), "%s", tcFaultName(r.fault));
      else snprintf(t, sizeof(t), "reading jumps (%.1f C)", r.tempC);
      latch(l, FAULT_SENSOR, t);
    }
    if (fresh && heating && l.pv > c.maxTemp) {
      snprintf(t, sizeof(t), "%.1f C above the cut-off %.0f C", l.pv, c.maxTemp);
      latch(l, FAULT_OVERTEMP, t);
    }
    if (heating && c.cjEnable && r.fault == TC_OK && !isnan(r.coldC) && r.coldC > c.cjMax) {
      snprintf(t, sizeof(t), "converter %.1f C (max %.0f C)", r.coldC, c.cjMax);
      latch(l, FAULT_CJHOT, t);
    }
    // the control task stopped: nothing would change the setpoint or end the run any more
    const uint32_t beat = controlBeatMs;
    if (heating && beat && (int32_t)(now - beat) > (int32_t)CONTROL_TIMEOUT_MS)
      latch(l, FAULT_CONTROL, "control task stopped");
    if (!l.fault) {  // heating while off / reading frozen
      const Local& o = loc[ch ^ 1];
      const bool other = l.otherHeat || (o.enabled && (o.on || o.tuning));
      const uint8_t gf = l.guard.update(c.runaway, dt, heating, other, fresh ? l.pv : NAN,
                                        r.fault == TC_OK ? r.tempC : NAN);
      if (gf) {
        snprintf(t, sizeof(t), "%s (%.1f C)", faultName(gf), l.pv);
        latch(l, gf, t);
      }
    }
    // a held-back reading above the cut-off: no power until it is clear which reading is right
    const float pend = l.flt.pending();
    const bool pendHot = !isnan(pend) && pend * c.calGain + c.calOffset > c.maxTemp;
    if (!l.fault && l.tuning) {
      // autotune drives the output; runaway: "heating failed" only (the PV swings around the test SP)
      l.out = l.tune.update(fresh ? l.pv : NAN, dt);
      if (l.out > c.maxOut) l.out = c.maxOut;
      const uint8_t rf = fresh ? l.runaway.update(c.runaway, dt, true, l.pv, l.pv, l.out) : (uint8_t)FAULT_NONE;
      if (rf) {
        snprintf(t, sizeof(t), "%s", faultName(rf));
        latch(l, rf, t);
      }
      if (l.tune.state() != TuneState::Running) {
        l.tuning = false;
        l.out = 0;
        l.runaway.reset();
      }
      duty = (l.fault || !l.tuning || pendHot) ? 0 : l.out / 100.0f;
      l.ff = l.integ = NAN;
    } else if (!l.fault && l.on && fresh && !isnan(l.sp)) {
      const PidParams p{c.kp, c.ki, c.kd, c.maxOut};
      // feed-forward (spec 9.23): the heater model's output for the setpoint ahead, at most maxOut; the
      // preview counts for 2.5 control cycles, at least 2 s (a stalled control task: the channel's own
      // setpoint, no ramp power). Switching it on / off while the PID runs moves its share into / out of the
      // integrator, so the output doesn't jump.
      float ff = NAN, spRate = 0;
      const uint32_t age = l.previewMs ? now - l.previewMs : UINT32_MAX;
      const uint32_t maxAge = ffMaxAgeMs(l.previewEveryMs);
      if (c.ff) {
        ff = clampFf(ffInput(FfModel{c.ffK, c.ffTau, c.ffDead}, l.sp, l.spAhead, l.rateAhead, age, maxAge), c.maxOut);
        if (age <= maxAge && !isnan(l.spAhead)) spRate = l.rateNow;
        if (!l.ffWas && l.pid.running()) l.pid.shiftIntegral(-ff);
      } else if (l.ffWas && l.pid.running()) {
        l.pid.shiftIntegral(l.pid.lastFf());
      }
      l.ffWas = c.ff != 0;
      l.out = l.pid.update(p, l.sp, l.pv, dt, ff, spRate);
      l.ff = ff;
      l.integ = l.pid.integral();
      const uint8_t rf = l.runaway.update(c.runaway, dt, true, l.sp, l.pv, l.out);
      if (rf) {
        snprintf(t, sizeof(t), "%s", faultName(rf));
        latch(l, rf, t);
      }
      duty = l.fault ? 0 : l.out / 100.0f;
    } else if (!l.fault && l.on && in == TcIn::Hold && l.pid.running() && !isnan(l.sp)) {
      // a bad reading within the hold: the PID waits and the output stays as it was
      duty = pendHot ? 0 : l.out / 100.0f;
    } else {
      if (!l.on || l.fault) {
        l.pid.reset();
        l.runaway.reset();
      }
      l.out = 0;
      l.ff = l.integ = NAN;
    }
    if (l.fault) {
      l.out = 0;
      l.ff = l.integ = NAN;
    }
  }
  ssrSet(ch, duty, window);
}

// ------------------------------------------------------------------ monitor channel
struct Mon {
  bool enabled = false;
  MonitorSettings cfg{};
  float pv = NAN, rawC = NAN, coldC = NAN;
  uint8_t tcFault = TC_NO_DEVICE;
  TcFilter flt;  // (as the channels; no jump check: nothing depends on this reading)
  uint32_t lastStepMs = 0;
};
static Mon mon;
static TcFilterCfg monCfg(const MonitorSettings& c) { return TcFilterCfg{c.median != 0, c.medianN, c.readMs, 0, 1}; }

#if defined(PIN_TC_CS_MON) && HAVE_LOCAL
static void monStep() {
  uint8_t sensor;
  {
    LockL g;
    if (!mon.enabled) return;
    sensor = mon.cfg.sensor;
  }
  const TcReading r = readSensor(sensor, PIN_TC_CS_MON);
  LockL g;
  const MonitorSettings& c = mon.cfg;
  mon.tcFault = r.fault;
  mon.coldC = r.coldC;
  if (mon.flt.update(monCfg(c), r.fault == TC_OK, r.tempC, millis()) != TcIn::Good) return;
  mon.rawC = mon.flt.value();
  mon.pv = mon.rawC * c.calGain + c.calOffset;
}
#else
static void monStep() {}
#endif

void localCtrlMonConfigure(bool on, const MonitorSettings& m) {
  if (!mtx) return;
  LockL g;
#if defined(PIN_TC_CS_MON)
  if (on && !mon.enabled) {
    mon.flt.reset();
    mon.lastStepMs = 0;
  }
  mon.enabled = on;
#else
  (void)on;
  mon.enabled = false;
#endif
  mon.cfg = m;
}

void localCtrlMonGet(MonStatus& st) {
  st = MonStatus{};
  st.pv = st.rawC = st.coldC = NAN;
  st.tcFault = TC_NO_DEVICE;
  if (!mtx) return;
  LockL g;
  st.enabled = mon.enabled;
  st.fresh = mon.enabled && mon.flt.fresh(monCfg(mon.cfg), millis());
  st.pv = mon.pv;
  st.rawC = mon.rawC;
  st.coldC = mon.coldC;
  st.tcFault = mon.tcFault;
  st.lastGoodMs = mon.flt.lastGoodMs();
}

// ------------------------------------------------------------------ SSR supply relay
#if HAVE_LOCAL
// The relay is the optional pin with the function "relay" (io_pins.h; S3 default GPIO4), active high.
static volatile bool relayOn = false, relayEnabled = true;
static int relayPin = -1;  // the pin relayOn refers to (local task)
bool localCtrlRelayWired() { return ioPin(IO_RELAY) >= 0; }
bool localCtrlRelayUsed() { return relayEnabled && localCtrlRelayWired(); }
bool localCtrlRelayOn() { return relayOn; }
void localCtrlRelayEnable(bool on) { relayEnabled = on; }  // off: the next step switches it off for good

// after each control step: on while outputs are wanted, off at once on any local fault
static void relayUpdate() {
  // the function moved to another pin or was removed: ioConfigure left the old pin and the new one off
  // (low); switched again below
  const int pin = ioPin(IO_RELAY);
  if (pin != relayPin) {
    relayPin = pin;
    relayOn = false;
  }
  if (pin < 0) return;
  bool want = false, fault = false;
  const char* why = nullptr;
  {
    LockL g;
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      const Local& l = loc[ch];
      if (!l.enabled) continue;
      if (l.fault) {
        fault = true;
        why = l.faultText;
      }
      want |= l.on || l.tuning;
    }
  }
  const bool estop = safetyEstop();
  const bool used = relayEnabled;
  const bool on = used && want && !fault && !estop;
  if (on == relayOn) return;
  relayOn = on;
  ioWrite(IO_RELAY, on);  // (the pin the function has now: never a pin that just got another job)
  if (!on && used && (fault || estop)) logError("SSR supply relay off: %s", estop ? "E-stop" : why && *why ? why : "fault");
}
#else
bool localCtrlRelayWired() { return false; }
bool localCtrlRelayUsed() { return false; }
bool localCtrlRelayOn() { return false; }
void localCtrlRelayEnable(bool) {}
static void relayUpdate() {}
#endif

// one tick of the local task (every TICK_MS; the browser demo, demo/sim.cpp, calls it itself)
void localCtrlTick() {
  const uint32_t now = millis();
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    Local& l = loc[ch];
    uint32_t every;
    {
      LockL g;
      every = l.cfg.readMs >= 100 && l.cfg.readMs <= 1000 ? l.cfg.readMs : 250;
    }
    // due: a reading every `every` ms (the 50 ms tick can be up to one tick late: 5 ms early is due)
    if (l.lastStepMs && now - l.lastStepMs + 5 < every) continue;
    const float dt = l.lastStepMs ? (now - l.lastStepMs) / 1000.0f : every / 1000.0f;
    l.lastStepMs = now;
    step(ch, dt);
  }
  relayUpdate();
  {  // the monitor channel, at its own interval (after the control channels: never delays them)
    uint32_t every;
    bool en;
    {
      LockL g;
      en = mon.enabled;
      every = mon.cfg.readMs >= 100 && mon.cfg.readMs <= 1000 ? mon.cfg.readMs : 1000;
    }
    if (en && (!mon.lastStepMs || now - mon.lastStepMs + 5 >= every)) {
      mon.lastStepMs = now;
      monStep();
    }
  }
}

static void localTask(void*) {
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(TICK_MS));
    localCtrlTick();
  }
}

// ------------------------------------------------------------------ API

void localCtrlBegin() {
  static bool started = false;
  if (started) return;
  started = true;
  mtx = xSemaphoreCreateMutex();
#if HAVE_LOCAL
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    LocalPins p;
    pinsFor(i, p);
    ssrPin[i] = p.ssr;
  }
  esp_timer_create_args_t a = {};
  a.callback = ssrTick;
  a.name = "ssr";
  esp_timer_handle_t h;
  if (esp_timer_create(&a, &h) == ESP_OK) esp_timer_start_periodic(h, 10000);
  // above the control task (3), so Modbus timeouts don't delay the PID
  xTaskCreatePinnedToCore(localTask, "local", 4096, nullptr, 4, nullptr, 0);
#endif
}

void localCtrlConfigure(int ch, bool enabled, const LocalChannelSettings& c) {
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return;
  LockL g;
  Local& l = loc[ch];
  LocalPins p;
  const bool wired = pinsFor(ch, p);
  if (enabled && !l.enabled) {  // (re)enabled: start clean
    l.flt.reset();
    l.pid.reset();
    l.runaway.reset();
    l.guard.reset();
  }
  if (!enabled) {
    l.on = false;
    l.fault = FAULT_NONE;
    l.faultText[0] = 0;
    l.tune.stop();
    l.tuning = false;
  }
  l.enabled = enabled && wired;
  l.pins = p;
  l.cfg = c;
}

void localCtrlSetSp(int ch, float sp) {
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return;
  LockL g;
  loc[ch].sp = sp;
}

void localCtrlSetPreview(int ch, float spAhead, float rateAhead, float rateNow, uint32_t cycleMs) {
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return;
  LockL g;
  Local& l = loc[ch];
  l.spAhead = spAhead;
  l.rateAhead = isnan(spAhead) ? 0 : rateAhead;
  l.rateNow = isnan(spAhead) ? 0 : rateNow;
  l.previewEveryMs = cycleMs;
  l.previewMs = millis();
  if (!l.previewMs) l.previewMs = 1;
}

bool localCtrlSetOutputs(int ch, bool on) {
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return !on;
  LockL g;
  Local& l = loc[ch];
  if (on && (!l.enabled || l.fault || safetyEstop())) return false;
  if (on && l.cfg.cjEnable && l.flt.everGood() && !isnan(l.coldC) && l.coldC > l.cfg.cjMax) return false;
  if (on && l.flt.everGood() && l.pv > l.cfg.maxTemp) return false;  // already above the cut-off
  if (on && l.tuning) return false;
  if (!on && l.tuning) {  // "outputs off" also ends an autotune
    l.tune.stop();
    l.tuning = false;
  }
  if (on && !l.on) {
    l.pid.reset();
    l.runaway.reset();
  }
  l.on = on;
  return true;
}

void localCtrlGet(int ch, LocalStatus& st) {
  st = LocalStatus{};
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return;
  LockL g;
  const Local& l = loc[ch];
  st.enabled = l.enabled;
  st.fresh = l.enabled && freshNow(l);
  st.pv = l.pv;
  st.rawC = l.rawC;
  st.coldC = l.coldC;
  st.tcFault = l.tcFault;
  st.sp = l.tuning ? l.tuneSp : l.sp;  // (autotune: its test temperature)
  st.on = l.on;
  st.out = l.out;
  st.ff = l.ff;
  st.integ = l.integ;
  st.fault = l.fault;
  snprintf(st.faultText, sizeof(st.faultText), "%s", l.faultText);
  st.tuning = l.tuning;
  st.maxTemp = l.cfg.maxTemp;
}

bool localCtrlAck(int ch, char* err, size_t n) {
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return false;
  LockL g;
  Local& l = loc[ch];
  if (!l.fault) return true;
  const bool fresh = freshNow(l);
  if (!fresh) {
    snprintf(err, n, "still no valid reading (%s)", tcFaultName(l.tcFault));
    return false;
  }
  if (l.pv > l.cfg.maxTemp - 5) {
    snprintf(err, n, "still too hot (%.1f C, cut-off %.0f C)", l.pv, l.cfg.maxTemp);
    return false;
  }
  if (l.fault == FAULT_CJHOT && !isnan(l.coldC) && l.coldC > l.cfg.cjMax - 5) {
    snprintf(err, n, "converter still too hot (%.1f C, max %.0f C)", l.coldC, l.cfg.cjMax);
    return false;
  }
  l.fault = FAULT_NONE;
  l.faultText[0] = 0;
  l.on = false;
  l.pid.reset();
  l.runaway.reset();
  l.guard.reset();
  return true;
}

void localCtrlHeartbeat() {
  uint32_t t = millis();
  if (!t) t = 1;  // (0 = no report yet)
  controlBeatMs = t;
}

void localCtrlOtherHeating(int ch, bool on) {
  if (ch < 0 || ch >= NUM_CHANNELS) return;
  loc[ch].otherHeat = on;
}

// ------------------------------------------------------------------ autotune

bool localCtrlTuneStart(int ch, float sp, char* err, size_t n) {
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return false;
  LockL g;
  Local& l = loc[ch];
  const bool fresh = freshNow(l);
  if (!l.enabled) return snprintf(err, n, "not a local channel"), false;
  if (safetyEstop()) return snprintf(err, n, "E-stop active"), false;
  if (l.fault) return snprintf(err, n, "acknowledge the fault first (%s)", l.faultText), false;
  if (!fresh) return snprintf(err, n, "no temperature reading"), false;
  if (l.tuning) return snprintf(err, n, "autotune already running"), false;
  const float margin = l.cfg.maxTemp - sp;
  if (!(sp > l.pv + 10) || margin < 15) {
    return snprintf(err, n, "test temperature must be > PV + 10 C and <= cut-off - 15 C (%.0f C)",
                    l.cfg.maxTemp - 15), false;
  }
  TuneConfig c;
  c.sp = sp;
  c.outMax = l.cfg.maxOut;
  c.maxSec = (l.cfg.tuneMaxMin >= 10 ? l.cfg.tuneMaxMin : 70) * 60.0f;  // time limit (setting)
  c.maxOver = margin - 5 < 40 ? margin - 5 : 40;  // stays below the cut-off
  c.fast = l.cfg.tuneFast != 0;                    // the rule (setting; fixed for this run)
  l.on = false;
  l.pid.reset();
  l.runaway.reset();
  l.tune.start(c);
  l.tuning = true;
  l.tuneSp = sp;
  return true;
}

void localCtrlTuneStop(int ch) {
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return;
  LockL g;
  Local& l = loc[ch];
  l.tune.stop();
  l.tuning = false;
}

void localCtrlTuneGet(int ch, LocalTune& t) {
  t = LocalTune{};
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return;
  LockL g;
  const Local& l = loc[ch];
  t.state = l.tune.state();
  t.cycle = l.tune.cycle();
  t.elapsed = l.tune.elapsed();
  t.sp = l.tuneSp;
  snprintf(t.msg, sizeof(t.msg), "%s", l.tune.message());
  t.res = l.tune.result();
}

void localCtrlTuneClear(int ch) {
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return;
  LockL g;
  Local& l = loc[ch];
  if (l.tuning) return;
  l.tune = RelayAutotune();
  l.tuneSp = NAN;
}
