#include "channel.h"
#include <Arduino.h>
#include <math.h>
#include "local_ctrl.h"
#include "tc_sensor.h"
#include <stdio.h>
#include <string.h>

void mbDescribe(MbResult r, uint8_t exception, const char* what, char* out, size_t n) {
  if (r == MbResult::Exception) snprintf(out, n, "%s: %s", what, mbExceptionName(exception));
  else snprintf(out, n, "%s: %s", what, mbResultName(r));
}

void ChannelSlot::configure(int ch, ModbusMaster* bus, const ChannelSettings& c) {
  localCtrlConfigure(ch, c.type == CH_LOCAL, c.loc);
  switch (c.type) {
    case CH_MODBUS:
      mb.configure(bus, c.mb);
      active = &mb;
      break;
    case CH_LOCAL:
      loc.configure(ch);
      active = &loc;
      break;
    default:
      active = &off;
      break;
  }
}

// ------------------------------------------------------------------ Modbus

bool ModbusChannel::poll(ChannelLive& L, char* err, size_t n) {
  const RegMap& reg = ctl_.reg();
  float pv = NAN, spEff = NAN, out = NAN;
  int16_t st = 0, al = 0;
  uint8_t pvFlag = VAL_OK, f = 0;
  bool pvOk = false, spOk = false, outOk = false, stOk = false, alOk = false;
  err[0] = 0;
  if (reg.pv == REG_UNSET) {
    snprintf(err, n, "PV register not configured");
  } else {
    const MbResult r = ctl_.readScaled(reg.pv, pv, pvFlag);
    if (r == MbResult::Ok) {
      pvOk = (pvFlag == VAL_OK);
      if (reg.spEff != REG_UNSET && ctl_.readScaled(reg.spEff, spEff, f) == MbResult::Ok) spOk = (f == VAL_OK);
      if (reg.out != REG_UNSET && ctl_.readScaled(reg.out, out, f) == MbResult::Ok) outOk = (f == VAL_OK);
      if (reg.status != REG_UNSET) stOk = ctl_.readRaw(reg.status, st) == MbResult::Ok;
      if (reg.alarm != REG_UNSET) alOk = ctl_.readRaw(reg.alarm, al) == MbResult::Ok;
    } else {
      mbDescribe(r, ctl_.lastException(), "PV read", err, n);
    }
  }
  const bool ok = err[0] == 0;
  L.pvValid = pvOk; L.pv = pv; L.pvFlag = ok ? pvFlag : (uint8_t)VAL_OK;
  L.spEffValid = spOk; L.spEff = spEff;
  L.outValid = outOk; L.out = out;
  L.statusValid = stOk; L.status = (uint16_t)st;
  L.alarmValid = alOk; L.alarm = (uint16_t)al;
  // runaway check while the outputs are on
  const uint32_t now = millis();
  const float dt = lastPollMs_ ? (now - lastPollMs_) / 1000.0f : 0;
  lastPollMs_ = now;
  if (ok && !fault_) {
    const float sp = spOk ? spEff : lastSp_;
    const uint8_t f = runaway_.update(ctl_.settings().runaway, dt, outputsOn_, sp, pvOk ? pv : NAN, outOk ? out : NAN);
    if (f) {
      fault_ = f;
      char e[64];
      const bool off = ctl_.reg().ctrlOff != REG_UNSET && setOutputs(false, e, sizeof(e));
      snprintf(faultText_, sizeof(faultText_), "%s%s", faultName(f), off ? "" : " (outputs could NOT be switched off)");
      outputsOn_ = false;
    }
  }
  return ok;
}

uint8_t ModbusChannel::fault(char* text, size_t n) {
  if (n) snprintf(text, n, "%s", faultText_);
  return fault_;
}

bool ModbusChannel::ack(char*, size_t) {
  fault_ = FAULT_NONE;
  faultText_[0] = 0;
  runaway_.reset();
  return true;
}

bool ModbusChannel::writeSp(float sp, char* err, size_t n) {
  const MbResult r = ctl_.writeScaled(ctl_.reg().sp, sp);
  if (r == MbResult::Ok) {
    lastSp_ = sp;
    return true;
  }
  mbDescribe(r, ctl_.lastException(), "Setpoint write", err, n);
  return false;
}

bool ModbusChannel::setOutputs(bool on, char* err, size_t n) {
  if (on && fault_) {
    snprintf(err, n, "acknowledge the fault first (%s)", faultText_);
    return false;
  }
  if (ctl_.reg().ctrlOff == REG_UNSET) {  // nothing to switch; "off" is not possible
    if (on) outputsOn_ = true;  // (the controller keeps controlling after "off" too)
    return on;
  }
  const MbResult r = ctl_.writeRaw(ctl_.reg().ctrlOff, on ? 0 : 1);
  if (r == MbResult::Ok) {
    if (outputsOn_ != on) runaway_.reset();
    outputsOn_ = on;
    return true;
  }
  mbDescribe(r, ctl_.lastException(), on ? "Outputs-on write" : "Outputs-off write", err, n);
  return false;
}

// ------------------------------------------------------------------ local

bool LocalChannel::poll(ChannelLive& L, char* err, size_t n) {
  LocalStatus st;
  localCtrlGet(ch_, st);
  L.pvValid = st.fresh;
  L.pv = st.fresh ? st.pv : NAN;
  L.pvFlag = 0;
  L.spEffValid = st.on && !isnan(st.sp);
  L.spEff = L.spEffValid ? st.sp : NAN;
  L.outValid = true;
  L.out = st.out;
  L.statusValid = L.alarmValid = false;
  L.coldC = st.coldC;
  if (!st.enabled) {
    snprintf(err, n, "no thermocouple pins for this channel on this board");
    return false;
  }
  if (!st.fresh) {
    snprintf(err, n, "no reading: %s", tcFaultName(st.tcFault));
    return false;
  }
  return true;
}

bool LocalChannel::writeSp(float sp, char*, size_t) {
  localCtrlSetSp(ch_, sp);
  return true;
}

bool LocalChannel::setOutputs(bool on, char* err, size_t n) {
  if (localCtrlSetOutputs(ch_, on)) return true;
  LocalStatus st;
  localCtrlGet(ch_, st);
  if (st.fault) snprintf(err, n, "acknowledge the fault first (%s)", st.faultText);
  else if (!st.enabled) snprintf(err, n, "no thermocouple pins for this channel on this board");
  else snprintf(err, n, "%.1f C is above the cut-off", st.pv);
  return false;
}

uint8_t LocalChannel::fault(char* text, size_t n) {
  LocalStatus st;
  localCtrlGet(ch_, st);
  if (n) snprintf(text, n, "%s", st.faultText);
  return st.fault;
}

bool LocalChannel::ack(char* err, size_t n) { return localCtrlAck(ch_, err, n); }

// ------------------------------------------------------------------ off

bool OffChannel::poll(ChannelLive& L, char* err, size_t n) {
  L.pvValid = L.spEffValid = L.outValid = L.statusValid = L.alarmValid = false;
  L.pv = L.spEff = L.out = NAN;
  if (n) err[0] = 0;
  return false;
}

bool OffChannel::writeSp(float, char* err, size_t n) {
  snprintf(err, n, "channel is off");
  return false;
}

bool OffChannel::setOutputs(bool on, char* err, size_t n) {
  if (!on) return true;
  snprintf(err, n, "channel is off");
  return false;
}
