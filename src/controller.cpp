#include "controller.h"
#include "config.h"
#include "settings_store.h"
#include <math.h>

void modbusBusBegin(ModbusMaster& bus, const Settings& s) {
#if HAS_RS485
  bus.begin(RS485_SERIAL, PIN_RS485_RX, PIN_RS485_TX, PIN_RS485_DE, s.baud, serialConfigFor(s), s.timeoutMs);
#else
  (void)bus;
  (void)s;
#endif
}

ValueFlag Controller::classify(int16_t raw) {
  switch (raw) {
    case -31000: return VAL_SENSOR_FAULT;
    case -32000: return VAL_OFF;
    case -32500: return VAL_UNDEFINED;
    case -32768: return VAL_OVERFLOW;
    default: return VAL_OK;
  }
}

const char* Controller::flagName(uint8_t f) {
  switch (f) {
    case VAL_SENSOR_FAULT: return "sensor fault";
    case VAL_OFF: return "off";
    case VAL_UNDEFINED: return "undefined";
    case VAL_OVERFLOW: return "out of range";
    default: return "ok";
  }
}

MbResult Controller::readBlock(uint16_t addr, uint16_t n, int16_t* out, uint8_t fn) {
  if (addr == REG_UNSET || !bus_) return MbResult::NotConfigured;
  return bus_->readRegs(m_.slaveId, fn ? fn : m_.reg.readFn, addr, n, out);
}

MbResult Controller::readRaw(uint16_t addr, int16_t& v) { return readBlock(addr, 1, &v); }

MbResult Controller::readScaled(uint16_t addr, float& v, uint8_t& flag) {
  int16_t raw = 0;
  const MbResult r = readRaw(addr, raw);
  if (r != MbResult::Ok) return r;
  flag = classify(raw);
  v = (flag == VAL_OK) ? raw / (m_.reg.scale > 0 ? m_.reg.scale : 1.0f) : NAN;
  return r;
}

MbResult Controller::writeRaw(uint16_t addr, int16_t v) {
  if (addr == REG_UNSET || !bus_) return MbResult::NotConfigured;
  return bus_->writeReg(m_.slaveId, m_.reg.writeFn, addr, v);
}

MbResult Controller::writeScaled(uint16_t addr, float v) {
  const float sc = m_.reg.scale > 0 ? m_.reg.scale : 1.0f;
  float raw = roundf(v * sc);
  if (raw > 32000) raw = 32000;
  if (raw < -30000) raw = -30000;
  return writeRaw(addr, (int16_t)raw);
}

// ---------------- PMA KS 4x-1 tables ----------------
static constexpr uint16_t DP1 = 8192;

static const ParamDef PMA_PARAMS[] = {
    {"SP.LO", 3100 + DP1, 1, "Lower setpoint limit"},
    {"SP.Hi", 3101 + DP1, 1, "Upper setpoint limit"},
    {"SP.2", 3102 + DP1, 1, "Second (safety) setpoint"},
    {"r.SP", 3103 + DP1, 1, "Setpoint gradient per minute (off = none)"},
    {"Pb1", 5000 + DP1, 1, "Proportional band heating (deg)"},
    {"ti1", 5002 + DP1, 1, "Integral time heating (s)"},
    {"td1", 5004 + DP1, 1, "Derivative time heating (s)"},
    {"t1", 5006 + DP1, 1, "Minimum cycle time heating (s)"},
    {"SH", 5014 + DP1, 1, "Switching difference / neutral zone"},
    {"Y.Lo", 5018 + DP1, 1, "Lower output limit (%)"},
    {"Y.Hi", 5019 + DP1, 1, "Upper output limit (%)"},
    {"L.1", 2100 + DP1, 1, "Limit 1 lower value"},
    {"H.1", 2101 + DP1, 1, "Limit 1 upper value"},
    {"HYS.1", 2102 + DP1, 1, "Limit 1 hysteresis"},
    {"t.F1", 1104 + DP1, 1, "Input 1 filter time constant (s)"},
    {"Sw.Op", 201, 0, "Firmware version (read only)"},
};

static const ActionDef PMA_ACTIONS[] = {
    {"Start self-tuning", 5150, 1},
    {"Stop self-tuning", 5150, 0},
    {"Controller outputs off", 1086, 1},
    {"Controller outputs on", 1086, 0},
    {"Acknowledge error list", 1088, 1},
    {"Lock front panel", 1080, 1},
    {"Unlock front panel", 1080, 0},
};

const ParamDef* pmaParams(int& count) {
  count = sizeof(PMA_PARAMS) / sizeof(PMA_PARAMS[0]);
  return PMA_PARAMS;
}

const ActionDef* pmaActions(int& count) {
  count = sizeof(PMA_ACTIONS) / sizeof(PMA_ACTIONS[0]);
  return PMA_ACTIONS;
}

const char* pmaModeText(uint16_t st) {
  switch ((st >> 12) & 0x0F) {
    case 0: return "Automatic";
    case 1: return "Self-tuning";
    case 2: return "Self-tuning failed";
    case 3: return "Sensor error";
    case 5: return "Manual";
    case 8: return "Manual (ext.)";
    case 9: return "Outputs off";
    case 10: return "Tuning aborted";
    default: return "Unknown";
  }
}
