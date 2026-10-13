#include "local_ctrl.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <math.h>
#include "app_state.h"
#include "config.h"
#include "pid.h"
#include "runaway.h"
#include "safety.h"
#include "tc_sensor.h"

#if defined(PIN_TC_SCK) && (defined(PIN_TC_CS_LOCAL) || defined(PIN_TC_CS_BOTTOM))
#define HAVE_LOCAL 1
#else
#define HAVE_LOCAL 0
#endif

static constexpr uint32_t PERIOD_MS = 250;
static constexpr uint32_t FRESH_MS = 1000;
static constexpr uint8_t BAD_READS_FAULT = 3;
static constexpr uint32_t SSR_TIMEOUT_MS = 1500;

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
  uint8_t badReads = 0;
  float med[3] = {0, 0, 0};  // last good raw readings (spike filter)
  uint8_t medN = 0;
  uint32_t lastGoodMs = 0;
  bool everGood = false;
  uint8_t fault = FAULT_NONE;
  char faultText[64] = "";
  Pid pid;
  RunawayMonitor runaway;
  GuardMonitor guard;
  volatile bool otherHeat = false;  // another channel (also a Modbus one) has its outputs on
  RelayAutotune tune;
  bool tuning = false;
  float tuneSp = NAN;
};

static Local loc[NUM_CHANNELS];
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
#if defined(PIN_SSR_RELAY)
  digitalWrite(PIN_SSR_RELAY, LOW);  // SSR supply relay off (active high, driven directly by the pin)
  pinMode(PIN_SSR_RELAY, OUTPUT);
#endif
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
      on = (now % w) < (uint32_t)(d * w);
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

static TcReading readSensor(const Local& l) {
  if (l.cfg.sensor == SENSOR_MAX6675) return tcDecode6675((uint16_t)readFrame(l.pins.cs, 16));
  return tcDecode31855(readFrame(l.pins.cs, 32));
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
  Local snap;
  {
    LockL g;
    enabled = l.enabled && l.pins.cs >= 0;
    if (enabled) snap.cfg = l.cfg, snap.pins = l.pins;
  }
  if (!enabled) {
    ssrSet(ch, 0, 1);
    return;
  }
  const TcReading r = readSensor(snap);
  float duty = 0, window = 1;
  {
    LockL g;
    const LocalChannelSettings& c = l.cfg;
    window = c.windowSec;
    l.tcFault = r.fault;
    l.coldC = r.coldC;
    if (r.fault == TC_OK) {
      l.badReads = 0;
      // median of the last three readings: single-reading spikes (thermocouple wires picking up
      // noise) don't reach the PID or the display; costs one reading (250 ms) of delay at most
      l.med[0] = l.med[1];
      l.med[1] = l.med[2];
      l.med[2] = r.tempC;
      if (l.medN < 3) ++l.medN;
      float t = r.tempC;
      if (l.medN == 3) {
        const float a = l.med[0], b = l.med[1], d = l.med[2];
        t = fmaxf(fminf(a, b), fminf(fmaxf(a, b), d));
      }
      l.rawC = t;
      l.pv = t * c.calGain + c.calOffset;
      l.lastGoodMs = millis();
      l.everGood = true;
    } else {
      if (l.badReads < 255) ++l.badReads;
      l.medN = 0;
    }
    const bool fresh = l.everGood && millis() - l.lastGoodMs < FRESH_MS && l.badReads == 0;
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
    if (l.badReads >= BAD_READS_FAULT && heating) {
      snprintf(t, sizeof(t), "%s", tcFaultName(r.fault));
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
      duty = (l.fault || !l.tuning) ? 0 : l.out / 100.0f;
    } else if (!l.fault && l.on && fresh && !isnan(l.sp)) {
      const PidParams p{c.kp, c.ki, c.kd, c.maxOut};
      l.out = l.pid.update(p, l.sp, l.pv, dt);
      const uint8_t rf = l.runaway.update(c.runaway, dt, true, l.sp, l.pv, l.out);
      if (rf) {
        snprintf(t, sizeof(t), "%s", faultName(rf));
        latch(l, rf, t);
      }
      duty = l.fault ? 0 : l.out / 100.0f;
    } else {
      if (!l.on || l.fault) {
        l.pid.reset();
        l.runaway.reset();
      }
      l.out = 0;
    }
    if (l.fault) l.out = 0;
  }
  ssrSet(ch, duty, window);
}

// ------------------------------------------------------------------ SSR supply relay
#if defined(PIN_SSR_RELAY) && HAVE_LOCAL
static volatile bool relayOn = false;
static void relayWrite(bool on) { digitalWrite(PIN_SSR_RELAY, on ? HIGH : LOW); }  // active high
bool localCtrlRelayWired() { return true; }
bool localCtrlRelayOn() { return relayOn; }

// after each control step: on while outputs are wanted, off at once on any local fault
static void relayUpdate() {
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
  const bool on = want && !fault && !estop;
  if (on == relayOn) return;
  relayOn = on;
  relayWrite(on);
  if (!on && (fault || estop)) logEvent("SSR supply relay off: %s", estop ? "E-stop" : why && *why ? why : "fault");
}
#else
bool localCtrlRelayWired() { return false; }
bool localCtrlRelayOn() { return false; }
static void relayUpdate() {}
#endif

static void localTask(void*) {
  uint32_t last = millis();
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(PERIOD_MS));
    const uint32_t now = millis();
    const float dt = (now - last) / 1000.0f;
    last = now;
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) step(ch, dt);
    relayUpdate();
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
    l.badReads = 0;
    l.everGood = false;
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

bool localCtrlSetOutputs(int ch, bool on) {
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return !on;
  LockL g;
  Local& l = loc[ch];
  if (on && (!l.enabled || l.fault || safetyEstop())) return false;
  if (on && l.cfg.cjEnable && l.everGood && !isnan(l.coldC) && l.coldC > l.cfg.cjMax) return false;
  if (on && l.everGood && l.pv > l.cfg.maxTemp) return false;  // already above the cut-off
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
  st.fresh = l.enabled && l.everGood && millis() - l.lastGoodMs < FRESH_MS && l.badReads == 0;
  st.pv = l.pv;
  st.rawC = l.rawC;
  st.coldC = l.coldC;
  st.tcFault = l.tcFault;
  st.sp = l.sp;
  st.on = l.on;
  st.out = l.out;
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
  const bool fresh = l.everGood && millis() - l.lastGoodMs < FRESH_MS && l.badReads == 0;
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

void localCtrlOtherHeating(int ch, bool on) {
  if (ch < 0 || ch >= NUM_CHANNELS) return;
  loc[ch].otherHeat = on;
}

// ------------------------------------------------------------------ autotune

bool localCtrlTuneStart(int ch, float sp, char* err, size_t n) {
  if (ch < 0 || ch >= NUM_CHANNELS || !mtx) return false;
  LockL g;
  Local& l = loc[ch];
  const bool fresh = l.everGood && millis() - l.lastGoodMs < FRESH_MS && l.badReads == 0;
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
  c.maxOver = margin - 5 < 40 ? margin - 5 : 40;  // stays below the cut-off
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
