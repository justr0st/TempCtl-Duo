#include "modbus_rtu.h"

void ModbusMaster::begin(HardwareSerial& port, int rxPin, int txPin, int dePin, uint32_t baud,
                         uint32_t serialConfig, uint16_t timeoutMs) {
  if (port_) port_->end();
  port_ = &port;
  de_ = dePin;
  baud_ = baud ? baud : 9600;
  timeoutMs_ = timeoutMs ? timeoutMs : 300;
  charUs_ = (11UL * 1000000UL) / baud_;  // start + 8 data + parity/stop + stop
  if (de_ >= 0) {
    pinMode(de_, OUTPUT);
    digitalWrite(de_, LOW);  // receive
  }
  port_->begin(baud_, serialConfig, rxPin, txPin);
  port_->setTimeout(timeoutMs_);
  lastTxEnd_ = micros();
}

size_t ModbusMaster::transact(const uint8_t* req, size_t reqLen, uint8_t* resp, size_t maxLen,
                              size_t expectLen) {
  if (!port_) return 0;
  // Discard stale bytes
  while (port_->available()) port_->read();

  // Respect the 3.5 character inter-frame silence
  const uint32_t silence = charUs_ * 4;
  while ((uint32_t)(micros() - lastTxEnd_) < silence) delayMicroseconds(100);

  if (de_ >= 0) digitalWrite(de_, HIGH);
  port_->write(req, reqLen);
  port_->flush();  // wait until the last stop bit has left the UART
  if (de_ >= 0) digitalWrite(de_, LOW);

  // Read the answer
  size_t n = 0;
  const uint32_t t0 = millis();
  uint32_t lastByte = 0;
  const uint32_t gapUs = charUs_ * 4 + 2000;  // generous end-of-frame gap
  while (n < maxLen) {
    if (port_->available()) {
      resp[n++] = (uint8_t)port_->read();
      lastByte = micros();
      if (n == expectLen) break;
      if (n == 5 && (resp[1] & 0x80)) break;  // exception frame
      continue;
    }
    if (n > 0 && (uint32_t)(micros() - lastByte) > gapUs) break;
    if (n == 0 && (millis() - t0) > timeoutMs_) break;
    if ((millis() - t0) > (uint32_t)timeoutMs_ + 200) break;
    vTaskDelay(1);  // wait 1 ms and let other tasks run (the UART driver buffers the answer meanwhile)
  }
  lastTxEnd_ = micros();
  return n;
}

MbResult ModbusMaster::readRegs(uint8_t id, uint8_t fn, uint16_t addr, uint16_t count, int16_t* out) {
  if (count == 0 || count > 60) return MbResult::BadResponse;
  uint8_t req[8], resp[128];
  mbBuildRead(req, id, fn, addr, count);
  const size_t n = transact(req, sizeof(req), resp, sizeof(resp), mbExpectedReadLen(count));
  lastEx_ = 0;
  return mbParse(resp, n, id, fn, count, out, &lastEx_);
}

MbResult ModbusMaster::writeReg(uint8_t id, uint8_t fn, uint16_t addr, int16_t value) {
  uint8_t req[11], resp[16];
  size_t len = (fn == 16) ? mbBuildWrite16(req, id, addr, value) : mbBuildWrite6(req, id, addr, value);
  if (fn != 16) fn = 6;
  const size_t n = transact(req, len, resp, sizeof(resp), 8);
  lastEx_ = 0;
  return mbParse(resp, n, id, fn, 1, nullptr, &lastEx_);
}
