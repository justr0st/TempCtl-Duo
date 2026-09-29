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
