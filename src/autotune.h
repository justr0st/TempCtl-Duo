// PID autotune for the local channels (spec 9.2), pure C++, host-tested.
//
// Relay method (Astrom-Hagglund) around a setpoint: the output switches between
// bias + d and bias - d when the PV crosses sp -/+ hysteresis. Like Marlin's
// autotune the bias is re-centred after every cycle so that heating and
// cooling times match (a heater needs much less than 50 % to hold a
// temperature). From the steady oscillation (amplitude a, period Pu):
//   Ku = 4 d / (pi * sqrt(a^2 - h^2))
// and the Tyreus-Luyben rule (decided: no overshoot, see spec):
//   Kp = Ku / 2.2, Ti = 2.2 Pu, Td = Pu / 6.3  ->  ki = Kp / Ti, kd = Kp * Td
#pragma once
#include <stdint.h>

enum class TuneState : uint8_t { Idle = 0, Running, Done, Failed };

struct TuneConfig {
  float sp;           // test temperature
  float outMax;       // output limit % (relay high <= outMax)
  float hyst = 0.5f;  // relay hysteresis degC
  float maxSec = 1800;  // give up after this time
  float maxOver = 40;   // fail if the PV goes this far above sp
  int cycles = 5;       // cycles to measure (after 2 settling cycles)
};

struct TuneResult {
  float ku, pu;         // ultimate gain (%/degC) and period (s)
  float amplitude;      // PV oscillation amplitude (half peak-to-peak) degC
  float bias, d;        // relay centre and half swing %
  float kp, ki, kd;     // proposed PID (Tyreus-Luyben)
};

class RelayAutotune {
 public:
  void start(const TuneConfig& c);
  void stop();  // user abort -> Failed "stopped"
  // One control step: returns the output % to apply
  float update(float pv, float dtSec);
  TuneState state() const { return state_; }
  int cycle() const { return cycle_; }       // completed full cycles
  float elapsed() const { return t_; }
  const TuneResult& result() const { return res_; }
  const char* message() const { return msg_; }

 private:
  void fail(const char* m);
  void finishCycle();
  TuneConfig c_{};
  TuneState state_ = TuneState::Idle;
  TuneResult res_{};
  char msg_[64] = "";
  float t_ = 0;
  bool high_ = true;         // relay state
  bool crossedOnce_ = false; // first crossing above sp + h seen (initial heat-up done)
  float bias_ = 50, d_ = 50;
  float tHighStart_ = 0, tLowStart_ = 0, tHigh_ = 0, tLow_ = 0;
  float cycStart_ = 0;
  float pvMax_ = -1e9f, pvMin_ = 1e9f;
  int cycle_ = 0;
  // measurements of the counted cycles
  float sumPeriod_ = 0, sumAmp_ = 0, minPeriod_ = 1e9f, maxPeriod_ = 0;
  int counted_ = 0;
};
