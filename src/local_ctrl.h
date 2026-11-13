// Local channels (spec 9.2): thermocouple (MAX31855 / MAX6675, bit-banged on
// the board's thermocouple bus) + SSR (time-proportional) + PID on the ESP32.
//
// A small task ("local", every 250 ms) reads the converters, applies the
// calibration, runs the safety checks and the PID and sets the SSR duty.
// An esp_timer (10 ms) switches the SSR pins inside each window and switches
// them off if the task stops refreshing the duty (1.5 s).
//
// Safety (spec 9.2): SSR off during boot (localCtrlEarlyInit), on bad readings
// for longer than local.badSec (tc_filter.h: the first ones are held back and
// the output stays), above the cut-off temperature, on a runaway fault, when
// the control task stops reporting (localCtrlHeartbeat); faults are latched
// until acknowledged (localCtrlAck), which is refused while the cause is
// still there.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "autotune.h"
#include "settings.h"

// First thing in setup(): SSR pins low, thermocouple CS pins high
void localCtrlEarlyInit();
// Starts the task and the SSR timer (once)
void localCtrlBegin();
void localCtrlTick();  // one pass of the local task (every 50 ms; the browser demo runs it itself)
// enabled = the channel is of type local (else its SSR stays off and it is not read)
void localCtrlConfigure(int ch, bool enabled, const LocalChannelSettings& l);
void localCtrlSetSp(int ch, float sp);
// Feed-forward preview (spec 9.23), from the control task every cycle while the channel's FF is on: the
// setpoint ffDead s ahead and its slope there, and the ramp rate now (degC/s). spAhead NaN = none (manual, no
// run): the local task uses its own setpoint without ramp power; so it does for a stale preview.
// A preview counts for 2.5 control cycles (cycleMs = bus.pollMs), at least 2 s.
void localCtrlSetPreview(int ch, float spAhead, float rateAhead, float rateNow, uint32_t cycleMs);
// Returns false if the outputs can't be switched on (fault latched, not configured)
bool localCtrlSetOutputs(int ch, bool on);

struct LocalStatus {
  bool enabled;
  bool fresh;        // a good reading within the last second
  float pv;          // calibrated temperature
  float rawC;        // converter temperature before calibration
  float coldC;       // cold junction (MAX31855)
  uint8_t tcFault;   // TcFault of the last reading
  float sp;
  bool on;           // outputs on (PID active)
  float out;         // PID output %
  float ff;          // its feed-forward share % (NaN: FF off or the PID not running)
  float integ;       // the PID's integrator % (NaN: the PID not running)
  uint8_t fault;     // ChannelFault (latched)
  char faultText[64];
  bool tuning;       // autotune running (drives the SSR instead of the PID)
  float maxTemp;     // the cut-off (settings)
};
void localCtrlGet(int ch, LocalStatus& st);
bool localCtrlAck(int ch, char* err, size_t n);
// The control task is alive (every pass, and inside its long bus requests). Once it has reported, a heating
// local channel without a report for 10 s gets the fault "control task stopped".
void localCtrlHeartbeat();
// another channel (local or Modbus) has its outputs on: no "heating while off" check on ch (control task)
void localCtrlOtherHeating(int ch, bool on);

// ---- Monitor channel (HAS_MONITOR): a third thermocouple on PIN_TC_CS_MON, read by the local task at its
// own interval; shown and logged only (no output, no fault, no alarm)
void localCtrlMonConfigure(bool on, const MonitorSettings& m);
struct MonStatus {
  bool enabled;
  bool fresh;        // a good reading within the last second (longer intervals: 2.5 intervals)
  float pv;          // calibrated temperature
  float rawC;        // before calibration (after the median filter)
  float coldC;       // cold junction (MAX31855)
  uint8_t tcFault;   // TcFault of the last reading
  uint32_t lastGoodMs;
};
void localCtrlMonGet(MonStatus& st);

// ---- PID autotune (relay method, autotune.h) ----
// Starts at test temperature sp; refused on a fault, without a fresh reading or
// too close to the cut-off. The tune stops on "outputs off", a fault or its own
// limits; afterwards the SSR is off and the result waits for accept / discard.
bool localCtrlTuneStart(int ch, float sp, char* err, size_t n);
void localCtrlTuneStop(int ch);
struct LocalTune {
  TuneState state;
  int cycle;
  float elapsed;
  float sp;
  char msg[64];
  TuneResult res;
};
void localCtrlTuneGet(int ch, LocalTune& t);
void localCtrlTuneClear(int ch);  // after accept / discard: back to Idle

// SSR supply relay (the optional pin with the function "relay", io_pins.h; S3 default GPIO4): on while a
// local channel's outputs are on (or autotune runs) and no local channel has a fault; off otherwise,
// switched by the local task every 50 ms (a fault: in the same step; E-stop too). Active high: the pin
// drives the input of a DC solid-state relay directly; it is an input with pull-down from ioEarlyInit()
// until the settings make it an output (low). Setting safety.relay off (no relay fitted, the SSR inputs on
// +5 V directly): the pin stays low. The function moved to another pin or removed: off, switched again by
// the next step.
bool localCtrlRelayWired();             // an optional pin has the relay function now
bool localCtrlRelayUsed();              // wired and switched on in the settings
bool localCtrlRelayOn();
void localCtrlRelayEnable(bool on);     // from the settings (safety.relay)
