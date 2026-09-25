// Modbus RTU frame helpers. Header-only, no Arduino dependencies.
// Implements exactly what the PMA KS 4x-1 interface description specifies:
// FC 03/04 read, FC 06 single write, FC 16 multiple write, CRC-16 (poly 0xA001,
// init 0xFFFF, low byte first), big-endian ("Motorola") register data.
#pragma once
#include <stdint.h>
#include <stddef.h>

enum class MbResult : uint8_t { Ok = 0, Timeout, CrcError, Exception, BadResponse, NotConfigured, Busy };

inline const char* mbResultName(MbResult r) {
  switch (r) {
    case MbResult::Ok: return "ok";
    case MbResult::Timeout: return "no response (timeout)";
    case MbResult::CrcError: return "CRC error";
    case MbResult::Exception: return "device returned exception";
    case MbResult::BadResponse: return "malformed response";
    case MbResult::NotConfigured: return "register address not configured";
    case MbResult::Busy: return "bus busy";
  }
  return "?";
}

inline const char* mbExceptionName(uint8_t code) {
  switch (code) {
    case 1: return "ILLEGAL FUNCTION";
    case 2: return "ILLEGAL DATA ADDRESS";
    case 3: return "ILLEGAL DATA VALUE (out of range or not writable now)";
    case 4: return "SLAVE DEVICE FAILURE";
  }
  return "unknown exception";
}

inline uint16_t mbCrc16(const uint8_t* d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= d[i];
    for (int b = 0; b < 8; ++b) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
  }
  return crc;
}

inline void mbAppendCrc(uint8_t* d, size_t n) {
  const uint16_t c = mbCrc16(d, n);
  d[n] = c & 0xFF;
  d[n + 1] = c >> 8;
}

inline bool mbCheckCrc(const uint8_t* d, size_t n) {
  return n >= 4 && mbCrc16(d, n) == 0;  // CRC over frame incl. CRC yields 0
}

// Read request (FC 3 or 4): 8 bytes
inline size_t mbBuildRead(uint8_t* out, uint8_t id, uint8_t fn, uint16_t addr, uint16_t count) {
  out[0] = id; out[1] = fn;
  out[2] = addr >> 8; out[3] = addr & 0xFF;
  out[4] = count >> 8; out[5] = count & 0xFF;
  mbAppendCrc(out, 6);
  return 8;
}

// Single register write (FC 6): 8 bytes
inline size_t mbBuildWrite6(uint8_t* out, uint8_t id, uint16_t addr, int16_t value) {
  const uint16_t v = (uint16_t)value;
  out[0] = id; out[1] = 6;
  out[2] = addr >> 8; out[3] = addr & 0xFF;
  out[4] = v >> 8; out[5] = v & 0xFF;
  mbAppendCrc(out, 6);
  return 8;
}

// One register via FC 16: 11 bytes
inline size_t mbBuildWrite16(uint8_t* out, uint8_t id, uint16_t addr, int16_t value) {
  const uint16_t v = (uint16_t)value;
  out[0] = id; out[1] = 16;
  out[2] = addr >> 8; out[3] = addr & 0xFF;
  out[4] = 0; out[5] = 1;
  out[6] = 2;
  out[7] = v >> 8; out[8] = v & 0xFF;
  mbAppendCrc(out, 9);
  return 11;
}

inline size_t mbExpectedReadLen(uint16_t count) { return 5 + 2 * (size_t)count; }

// Parse a response. For reads, values receive `count` registers.
inline MbResult mbParse(const uint8_t* r, size_t n, uint8_t id, uint8_t fn, uint16_t count,
                        int16_t* values, uint8_t* exCode) {
  if (n == 0) return MbResult::Timeout;
  if (n >= 5 && r[0] == id && r[1] == (uint8_t)(fn | 0x80)) {
    if (!mbCheckCrc(r, 5)) return MbResult::CrcError;
    if (exCode) *exCode = r[2];
    return MbResult::Exception;
  }
  if (fn == 3 || fn == 4) {
    const size_t want = mbExpectedReadLen(count);
    if (n < want) return MbResult::BadResponse;
    if (!mbCheckCrc(r, want)) return MbResult::CrcError;
    if (r[0] != id || r[1] != fn || r[2] != 2 * count) return MbResult::BadResponse;
    for (uint16_t i = 0; i < count; ++i) values[i] = (int16_t)((r[3 + 2 * i] << 8) | r[4 + 2 * i]);
    return MbResult::Ok;
  }
  // FC 6 / 16 reply is 8 bytes
  if (n < 8) return MbResult::BadResponse;
  if (!mbCheckCrc(r, 8)) return MbResult::CrcError;
  if (r[0] != id || r[1] != fn) return MbResult::BadResponse;
  return MbResult::Ok;
}
