#include "tc_sensor.h"
#include <math.h>

TcReading tcDecode31855(uint32_t f) {
  TcReading r{TC_OK, NAN, NAN};
  // nothing on the bus: floating / pulled-up SO reads all ones, a missing
  // chip with SO pulled down all zeros; D17 and D3 must be 0
  if (f == 0xFFFFFFFFu || f == 0 || (f & 0x00020008u)) {
    r.fault = TC_NO_DEVICE;
    return r;
  }
  int16_t cj = (int16_t)((f >> 4) & 0x0FFF);
  if (cj & 0x0800) cj -= 0x1000;
  r.coldC = cj * 0.0625f;
  if (f & 0x00010000u) {
    r.fault = (f & 1) ? TC_OPEN : (f & 2) ? TC_SHORT_GND : (f & 4) ? TC_SHORT_VCC : TC_OPEN;
    return r;
  }
  int16_t t = (int16_t)(f >> 18);
  if (t & 0x2000) t -= 0x4000;
  r.tempC = t * 0.25f;
  return r;
}

TcReading tcDecode6675(uint16_t f) {
  TcReading r{TC_OK, NAN, NAN};
  if (f == 0xFFFF || f == 0 || (f & 0x8002)) {  // D15 and D1 must be 0
    r.fault = TC_NO_DEVICE;
    return r;
  }
  if (f & 0x0004) {
    r.fault = TC_OPEN;
    return r;
  }
  r.tempC = (f >> 3) * 0.25f;
  return r;
}

const char* tcFaultName(uint8_t f) {
  switch (f) {
    case TC_OK: return "ok";
    case TC_OPEN: return "thermocouple open";
    case TC_SHORT_GND: return "thermocouple shorted to GND";
    case TC_SHORT_VCC: return "thermocouple shorted to VCC";
    case TC_NO_DEVICE: return "no thermocouple converter";
  }
  return "?";
}
