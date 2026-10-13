// Local channels (spec 9.2): thermocouple (MAX31855 / MAX6675, bit-banged on
// the board's thermocouple bus) + SSR (time-proportional) + PID on the ESP32.
//
// A small task ("local", every 250 ms) reads the converters, applies the
// calibration, runs the safety checks and the PID and sets the SSR duty.
// An esp_timer (10 ms) switches the SSR pins inside each window and switches
// them off if the task stops refreshing the duty (1.5 s).
//
// Safety (spec 9.2): SSR off during boot (localCtrlEarlyInit), on a sensor
// fault or no fresh reading (3 bad reads in a row), above the cut-off
// temperature, on a runaway fault; faults are latched until acknowledged
// (localCtrlAck), which is refused while the cause is still there.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "autotune.h"
#include "settings.h"

// First thing in setup(): SSR pins low, thermocouple CS pins high
void localCtrlEarlyInit();
// Starts the task and the SSR timer (once)
void localCtrlBegin();
// enabled = the channel is of type local (else its SSR stays off and it is not read)
void localCtrlConfigure(int ch, bool enabled, const LocalChannelSettings& l);
void localCtrlSetSp(int ch, float sp);
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
  uint8_t fault;     // ChannelFault (latched)
  char faultText[64];
  bool tuning;       // autotune running (drives the SSR instead of the PID)
  float maxTemp;     // the cut-off (settings)
};
void localCtrlGet(int ch, LocalStatus& st);
bool localCtrlAck(int ch, char* err, size_t n);
// another channel (local or Modbus) has its outputs on: no "heating while off" check on ch (control task)
void localCtrlOtherHeating(int ch, bool on);

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

// SSR supply relay (PIN_SSR_RELAY, S3): on while a local channel's outputs are on (or autotune runs) and
// no local channel has a fault; off otherwise, switched by the local task every 250 ms (a fault: in the
// same step; E-stop too). Active high: the pin drives the input of a DC solid-state relay directly; it is
// low (off) from localCtrlEarlyInit().
bool localCtrlRelayWired();
bool localCtrlRelayOn();
