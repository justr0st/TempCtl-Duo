#include "runaway.h"
#include <math.h>

const char* faultName(uint8_t f) {
  switch (f) {
    case FAULT_NONE: return "";
    case FAULT_SENSOR: return "sensor fault";
    case FAULT_OVERTEMP: return "over temperature";
    case FAULT_HEATING: return "heating failed";
    case FAULT_HOLD: return "temperature left the setpoint band";
  }
  return "fault";
}

void RunawayMonitor::reset() {
  heatT_ = 0;
  heating_ = false;
  spRef_ = NAN;
  reached_ = false;
  outsideT_ = 0;
}

uint8_t RunawayMonitor::update(const Runaway& c, float dt, bool active, float sp, float pv, float out) {
  if (!active || !c.enable || isnan(pv) || isnan(sp) || !(dt > 0)) {
    reset();
    return FAULT_NONE;
  }
  // heating failed
  if (!isnan(out) && out >= c.minOut) {
    if (!heating_) {
      heating_ = true;
      heatT_ = 0;
      heatStartPv_ = pv;
    }
    heatT_ += dt;
    if (pv - heatStartPv_ >= c.minRise) {  // it heats: start a new window from here
      heatT_ = 0;
      heatStartPv_ = pv;
    } else if (heatT_ >= c.heatSec) {
      return FAULT_HEATING;
    }
  } else {
    heating_ = false;
  }
  // runaway while holding: only once a steady setpoint was reached
  if (isnan(spRef_) || fabsf(sp - spRef_) > 0.5f) {  // setpoint moved (ramp, jump, edit)
    spRef_ = sp;
    reached_ = false;
    outsideT_ = 0;
  }
  const float dev = fabsf(pv - sp);
  const float reachBand = c.holdBand < 2.0f ? c.holdBand : 2.0f;
  if (!reached_) {
    if (dev <= reachBand) reached_ = true;
    return FAULT_NONE;
  }
  if (dev > c.holdBand) {
    outsideT_ += dt;
    if (outsideT_ > c.holdSec) return FAULT_HOLD;
  } else {
    outsideT_ = 0;
  }
  return FAULT_NONE;
}
