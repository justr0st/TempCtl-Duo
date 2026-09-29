// Thermocouple converters MAX31855 / MAX6675: frame decoding (pure C++,
// host-tested). Reading the frames (bit-banged SPI) is in local_ctrl.cpp.
#pragma once
#include <stdint.h>

enum TcFault : uint8_t {
  TC_OK = 0,
  TC_OPEN,        // thermocouple open (not connected / broken)
  TC_SHORT_GND,   // MAX31855: thermocouple shorted to GND
  TC_SHORT_VCC,   // MAX31855: thermocouple shorted to VCC
  TC_NO_DEVICE,   // no converter answering (frame all 0 or all 1, reserved bits set)
};

struct TcReading {
  TcFault fault;
  float tempC;   // thermocouple temperature (valid if fault == TC_OK)
  float coldC;   // cold-junction (chip) temperature; NAN for MAX6675
};

// MAX31855: 32-bit frame, D31..D18 = TC temperature (14 bit signed, 0.25 degC),
// D16 = fault, D15..D4 = internal temperature (12 bit signed, 0.0625 degC),
// D2 = SCV, D1 = SCG, D0 = OC; D17 and D3 are always 0.
TcReading tcDecode31855(uint32_t frame);
// MAX6675: 16-bit frame, D15 = 0, D14..D3 = temperature (12 bit, 0.25 degC),
// D2 = thermocouple open, D1 = device id (0), D0 = three-state.
TcReading tcDecode6675(uint16_t frame);

const char* tcFaultName(uint8_t f);
