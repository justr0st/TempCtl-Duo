// Temperature channel interface (spec 9.1): the control task, the program
// engine, the UIs and the web API talk to a channel, not to its backend.
//   ModbusChannel: a Modbus RTU controller on the shared RS485 bus
//   LocalChannel:  thermocouple + SSR with PID on the ESP32 (step 3; stub now)
//   OffChannel:    channel not used
// All methods run in the control task.
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "controller.h"
#include "runaway.h"
#include "settings.h"

// Published values of one channel (LiveData::ch, under gLock)
struct ChannelLive {
  uint8_t type;             // ChannelType
  char name[CH_NAME_LEN];
  bool commOk;              // device answered at the last poll (always false for Off)
  uint32_t okCount, errCount;
  uint32_t lastOkMs;
  char lastErr[80];
  bool pvValid; float pv; uint8_t pvFlag;
  bool spEffValid; float spEff;
  bool outValid; float out;
  bool statusValid; uint16_t status;
  bool alarmValid; uint16_t alarm;
  bool manualOn; float manualSp; bool manualSynced;
  bool outOn;               // outputs on (control active, autotune too): main screen tile highlighted
  uint8_t fault;            // ChannelFault, latched until acknowledged
  char faultText[64];
  float coldC;              // local: cold-junction temperature (MAX31855)
};

class Channel {
 public:
  virtual ~Channel() = default;
  virtual uint8_t type() const = 0;
  // Polled round-robin with the other bus channels (one Modbus poll per cycle)
  virtual bool onBus() const { return false; }
  // Reads PV etc. into `live` (value fields only). Returns false + message
  // if the device did not answer.
  virtual bool poll(ChannelLive& live, char* err, size_t n) = 0;
  virtual bool writeSp(float sp, char* err, size_t n) = 0;
  // Outputs on (control active) / off (Modbus: C.Off, local: SSR off)
  virtual bool setOutputs(bool on, char* err, size_t n) = 0;
  virtual bool canSwitchOff() const { return true; }  // an "outputs off" exists
  virtual bool spConfigured() const { return true; }  // a setpoint can be written
  virtual Controller* modbus() { return nullptr; }     // Modbus extras (ramp, lock, tools)
  // Latched fault (runaway, sensor, over temperature): outputs stay off until acknowledged
  virtual uint8_t fault(char* text, size_t n) {
    if (n) text[0] = 0;
    return FAULT_NONE;
  }
  // Clears the fault if its cause is gone; the outputs stay off
  virtual bool ack(char* err, size_t n) {
    (void)err;
    (void)n;
    return true;
  }
  // another channel heats: no "heating while off" check (its heat can reach this sensor)
  virtual void setOtherHeating(bool on) { (void)on; }
};

// Runaway check (spec 9.2) with the controller's output (Ypid) and PV while
// the outputs are on; a fault switches them off (C.Off = 1).
class ModbusChannel : public Channel {
 public:
  void configure(ModbusMaster* bus, const ModbusChannelSettings& m) {
    ctl_.attach(bus, m);
    runaway_.reset();
    guard_.reset();
  }
  uint8_t type() const override { return CH_MODBUS; }
  bool onBus() const override { return true; }
  bool poll(ChannelLive& live, char* err, size_t n) override;
  bool writeSp(float sp, char* err, size_t n) override;
  bool setOutputs(bool on, char* err, size_t n) override;
  bool canSwitchOff() const override { return ctl_.reg().ctrlOff != REG_UNSET; }
  bool spConfigured() const override { return ctl_.reg().sp != REG_UNSET; }
  Controller* modbus() override { return &ctl_; }
  uint8_t fault(char* text, size_t n) override;
  bool ack(char* err, size_t n) override;
  void setOtherHeating(bool on) override { otherHeat_ = on; }

 private:
  void trip(uint8_t f);
  Controller ctl_;
  RunawayMonitor runaway_;
  GuardMonitor guard_;
  bool otherHeat_ = false;
  bool offKnown_ = false;  // "outputs off" was written (the controller may control on its own before)
  bool outputsOn_ = false;
  float lastSp_ = NAN;
  uint32_t lastPollMs_ = 0;
  uint8_t fault_ = FAULT_NONE;
  char faultText_[64] = "";
};

// Thermocouple + SSR + PID on the ESP32; the work is done by the local task
// (local_ctrl.h), this is the control task's view of it.
class LocalChannel : public Channel {
 public:
  void configure(int ch) { ch_ = ch; }
  uint8_t type() const override { return CH_LOCAL; }
  bool poll(ChannelLive& live, char* err, size_t n) override;
  bool writeSp(float sp, char* err, size_t n) override;
  bool setOutputs(bool on, char* err, size_t n) override;
  uint8_t fault(char* text, size_t n) override;
  bool ack(char* err, size_t n) override;
  void setOtherHeating(bool on) override;

 private:
  int ch_ = 0;
};

class OffChannel : public Channel {
 public:
  uint8_t type() const override { return CH_OFF; }
  bool poll(ChannelLive& live, char* err, size_t n) override;
  bool writeSp(float sp, char* err, size_t n) override;
  bool setOutputs(bool on, char* err, size_t n) override;
  bool canSwitchOff() const override { return false; }
  bool spConfigured() const override { return false; }
};

// One channel position (bottom / top) with its three possible backends
struct ChannelSlot {
  ModbusChannel mb;
  LocalChannel loc;
  OffChannel off;
  Channel* active = &off;
  // Selects and configures the backend of the channel's type (ch = 0 bottom, 1 top)
  void configure(int ch, ModbusMaster* bus, const ChannelSettings& c);
};

// "Setpoint write: no response (timeout)" etc.
void mbDescribe(MbResult r, uint8_t exception, const char* what, char* out, size_t n);
