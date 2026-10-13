// Thermal runaway detection (spec 9.2), pure C++, host-tested. Like 3D-printer
// firmware, two checks while the channel's outputs are on:
//  - "heating failed": output >= minOut % for heatSec seconds, but the PV
//    rose less than minRise degC in that time
//  - "runaway while holding": after the PV has reached a steady setpoint, it
//    stays more than holdBand degC away from it for longer than holdSec seconds
#pragma once
#include <math.h>
#include <stdint.h>
#include "settings.h"

enum ChannelFault : uint8_t {
  FAULT_NONE = 0,
  FAULT_SENSOR,     // thermocouple / converter fault or no fresh reading (local)
  FAULT_OVERTEMP,   // PV above the SSR cut-off temperature (local)
  FAULT_HEATING,    // runaway check: heating failed
  FAULT_HOLD,       // runaway check: PV left the setpoint band while holding
  FAULT_OFFHEAT,    // guard: the PV rises while the outputs are off (stuck SSR / controller)
  FAULT_FROZEN,     // guard: the reading did not change while the outputs are on
  FAULT_CJHOT,      // converter (cold junction) too hot (local)
};
const char* faultName(uint8_t f);

class RunawayMonitor {
 public:
  void reset();
  // Call every control step while the outputs are on (active = false resets).
  // out: output % (NAN if unknown -> no "heating failed" check).
  uint8_t update(const Runaway& cfg, float dtSec, bool active, float sp, float pv, float out);

 private:
  float heatT_ = 0, heatStartPv_ = 0;
  bool heating_ = false;
  float spRef_ = NAN;
  bool reached_ = false;
  float outsideT_ = 0;
};

// Guard checks (spec 9.2a), every reading:
//  - "heating while off": outputs off, but the PV rises offRise degC within offSec seconds. Starts
//    OFF_SETTLE_SEC after the outputs went off (heat still flowing from the heater) and is held off while
//    another channel heats (its heat can reach this sensor).
//  - "reading frozen": outputs on, the unfiltered reading did not change at all for frozenSec seconds.
class GuardMonitor {
 public:
  static constexpr float OFF_SETTLE_SEC = 120;
  void reset();
  // on: this channel's outputs are on; otherOn: another channel heats; pv: reading (NAN none);
  // raw: the unfiltered reading for the frozen check (NAN none)
  uint8_t update(const Runaway& cfg, float dtSec, bool on, bool otherOn, float pv, float raw);

 private:
  float settleT_ = 0, offT_ = 0, offRef_ = NAN;
  float frozenT_ = 0, lastRaw_ = NAN;
};
