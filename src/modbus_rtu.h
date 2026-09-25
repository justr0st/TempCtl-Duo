#pragma once
#include <Arduino.h>
#include "modbus_frame.h"

// Minimal half-duplex Modbus RTU master (FC 3/4/6/16).
class ModbusMaster {
 public:
  void begin(HardwareSerial& port, int rxPin, int txPin, int dePin, uint32_t baud,
             uint32_t serialConfig, uint16_t timeoutMs);
  MbResult readRegs(uint8_t id, uint8_t fn, uint16_t addr, uint16_t count, int16_t* out);
  MbResult writeReg(uint8_t id, uint8_t fn, uint16_t addr, int16_t value);
  uint8_t lastException() const { return lastEx_; }

 private:
  size_t transact(const uint8_t* req, size_t reqLen, uint8_t* resp, size_t maxLen, size_t expectLen);
  HardwareSerial* port_ = nullptr;
  int de_ = -1;
  uint32_t baud_ = 9600;
  uint16_t timeoutMs_ = 300;
  uint32_t charUs_ = 1146;  // 11 bits @ 9600
  uint32_t lastTxEnd_ = 0;
  uint8_t lastEx_ = 0;
};
