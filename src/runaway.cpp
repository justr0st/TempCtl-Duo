#include "runaway.h"
#include <math.h>

const char* faultName(uint8_t f) {
  switch (f) {
    case FAULT_NONE: return "";
    case FAULT_SENSOR: return "sensor fault";
    case FAULT_OVERTEMP: return "over temperature";
    case FAULT_HEATING: return "heating failed";
    case FAULT_HOLD: return "temperature left the setpoint band";
    case FAULT_OFFHEAT: return "heating while the outputs are off";
    case FAULT_FROZEN: return "reading did not change";
    case FAULT_CJHOT: return "converter too hot";
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

void GuardMonitor::reset() {
  settleT_ = offT_ = 0;
  offRef_ = NAN;
  frozenT_ = 0;
  lastRaw_ = NAN;
}

uint8_t GuardMonitor::update(const Runaway& c, float dt, bool on, bool otherOn, float pv, float raw) {
  if (!(dt > 0)) dt = 0;
  uint8_t f = FAULT_NONE;
  // heating while off
  if (on || otherOn || !c.offEnable) {
    settleT_ = 0;
    offRef_ = NAN;
  } else if (settleT_ < OFF_SETTLE_SEC) {
    settleT_ += dt;
  } else if (!isnan(pv)) {
    if (isnan(offRef_)) {
      offRef_ = pv;
      offT_ = 0;
    }
    if (pv < offRef_) offRef_ = pv;  // cooling: the rise counts from the lowest reading
    offT_ += dt;
    if (pv - offRef_ >= c.offRise) f = FAULT_OFFHEAT;
    else if (offT_ >= c.offSec) {  // next window from here
      offRef_ = pv;
      offT_ = 0;
    }
  }
  // reading frozen
  if (!on || !c.frozenEnable || isnan(raw)) {
    frozenT_ = 0;
    lastRaw_ = raw;
  } else if (isnan(lastRaw_) || raw != lastRaw_) {
    lastRaw_ = raw;
    frozenT_ = 0;
  } else {
    frozenT_ += dt;
    if (frozenT_ >= c.frozenSec && !f) f = FAULT_FROZEN;
  }
  return f;
}
