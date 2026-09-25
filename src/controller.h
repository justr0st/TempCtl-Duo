#pragma once
#include "modbus_rtu.h"
#include "settings.h"

// Raw special values defined by PMA (also common on other controllers)
enum ValueFlag : uint8_t { VAL_OK = 0, VAL_SENSOR_FAULT, VAL_OFF, VAL_UNDEFINED, VAL_OVERFLOW };

struct ParamDef {
  const char* name;
  uint16_t addr;
  uint8_t scaled;   // 1 = value / scale
  const char* desc;
};

struct ActionDef {
  const char* label;
  uint16_t addr;
  int16_t value;
};

class Controller {
 public:
  void configure(const Settings& s);  // (re)open the serial port
  void setSettings(const Settings& s) { s_ = s; }

  MbResult readRaw(uint16_t addr, int16_t& v);
  MbResult readScaled(uint16_t addr, float& v, uint8_t& flag);
  MbResult writeRaw(uint16_t addr, int16_t v);
  MbResult writeScaled(uint16_t addr, float v);
  MbResult readBlock(uint16_t addr, uint16_t n, int16_t* out, uint8_t fn = 0);

  uint8_t lastException() const { return bus_.lastException(); }
  const Settings& settings() const { return s_; }

  static ValueFlag classify(int16_t raw);
  static const char* flagName(uint8_t f);

 private:
  ModbusMaster bus_;
  Settings s_{};
};

// Profile-specific extras (PMA KS 4x-1)
const ParamDef* pmaParams(int& count);
const ActionDef* pmaActions(int& count);
const char* pmaModeText(uint16_t stCntr);
