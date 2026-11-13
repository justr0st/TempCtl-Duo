// Thermocouple input filter of the local channels and the monitor channel (pure C++, host-tested):
// median over the last readings, "is the reading current", and what happens with bad readings (spec 9.24).
//
// A bad reading is one the converter reports as faulty, or a valid one that differs from the last accepted
// reading by more than maxJump (an implausible jump). The first holdReads() bad readings in a row are held
// back: the value stays, update() returns Hold and the caller keeps its output. After that a valid reading
// is accepted as the new level (the median starts again with it); without a valid one the input is Lost.
// badTooLong(): bad readings without a break for badSec seconds - the caller's sensor fault.
#pragma once
#include <math.h>
#include <stdint.h>

struct TcFilterCfg {
  bool median;       // median filter on
  uint8_t medianN;   // ... over 3, 5, 7 or 9 readings
  uint16_t readMs;   // read interval
  float maxJump;     // degC between two readings that counts as implausible (0 = not checked)
  float badSec;      // bad readings for this long: badTooLong()
};

enum class TcIn : uint8_t {
  Good,  // reading accepted: value() is new
  Hold,  // bad reading held back: value() is the last accepted one
  Lost,  // no valid reading for longer than the hold
};

class TcFilter {
 public:
  static constexpr uint32_t FRESH_MS = 1000;   // a reading counts as current this long ...
  static constexpr uint32_t HOLD_MAX_MS = 3000;  // the output is never kept longer than this without a reading

  void reset() {
    medN_ = 0;
    bad_ = 0;
    everGood_ = false;
    newLevel_ = false;
    value_ = last_ = pending_ = NAN;
    lastGoodMs_ = badSinceMs_ = 0;
  }

  // ... at long read intervals two intervals and a half
  static uint32_t freshMs(const TcFilterCfg& c) {
    const uint32_t f = (uint32_t)c.readMs * 5u / 2u;
    return f > FRESH_MS ? f : FRESH_MS;
  }
  // bad readings in a row that are held back: the median's window, 2 without the median; at most
  // HOLD_MAX_MS worth of readings, at least 1
  static uint8_t holdReads(const TcFilterCfg& c) {
    uint32_t n = c.median ? (c.medianN >= 3 && c.medianN <= 9 ? c.medianN : 3) : 2;
    const uint32_t cap = HOLD_MAX_MS / (c.readMs ? c.readMs : 250);
    if (n > cap) n = cap;
    return (uint8_t)(n < 1 ? 1 : n);
  }

  // One reading: ok = the converter reported no fault, tempC = its temperature.
  TcIn update(const TcFilterCfg& c, bool ok, float tempC, uint32_t nowMs) {
    const uint8_t hold = holdReads(c);
    newLevel_ = false;
    if (!ok || isnan(tempC)) {
      pending_ = NAN;
      markBad(nowMs);
      if (bad_ > hold) medN_ = 0;
      return everGood_ && bad_ <= hold ? TcIn::Hold : TcIn::Lost;
    }
    const bool jump = everGood_ && c.maxJump > 0 && !isnan(last_) && fabsf(tempC - last_) > c.maxJump;
    if (jump && bad_ < hold) {  // held back; the reading after the hold is taken as it is
      pending_ = tempC;
      markBad(nowMs);
      return TcIn::Hold;
    }
    if (jump || bad_ > hold) {  // a new level, or back after a gap: the old readings don't count
      medN_ = 0;
      newLevel_ = everGood_;
    }
    bad_ = 0;
    pending_ = NAN;
    last_ = tempC;
    float t = tempC;
    if (c.median) {
      const uint8_t w = c.medianN >= 3 && c.medianN <= 9 ? c.medianN : 3;
      for (int k = 0; k < 8; ++k) med_[k] = med_[k + 1];
      med_[8] = tempC;
      if (medN_ < 9) ++medN_;
      if (medN_ >= w) t = medianOf(med_ + 9 - w, w);
    } else {
      medN_ = 0;
    }
    value_ = t;
    lastGoodMs_ = nowMs;
    everGood_ = true;
    return TcIn::Good;
  }

  float value() const { return value_; }      // filtered, of the accepted readings (NaN: none yet)
  float pending() const { return pending_; }  // the valid reading held back as a jump (NaN: none)
  bool everGood() const { return everGood_; }
  bool newLevel() const { return newLevel_; }  // the last update() accepted a jump / the first reading after a gap
  uint8_t bad() const { return bad_; }
  uint32_t lastGoodMs() const { return lastGoodMs_; }
  // the value is current: accepted, not older than freshMs(), no bad reading since
  bool fresh(const TcFilterCfg& c, uint32_t nowMs) const {
    return everGood_ && bad_ == 0 && nowMs - lastGoodMs_ < freshMs(c);
  }
  bool badTooLong(const TcFilterCfg& c, uint32_t nowMs) const {
    return bad_ > 0 && (float)(nowMs - badSinceMs_) >= c.badSec * 1000.0f;
  }

  // median of n values (n odd, <= 9)
  static float medianOf(const float* v, int n) {
    float a[9];
    for (int i = 0; i < n; ++i) {  // insertion sort (at most 9)
      const float x = v[i];
      int j = i;
      for (; j > 0 && a[j - 1] > x; --j) a[j] = a[j - 1];
      a[j] = x;
    }
    return a[n / 2];
  }

 private:
  void markBad(uint32_t nowMs) {
    if (!bad_) badSinceMs_ = nowMs;
    if (bad_ < 255) ++bad_;
  }

  float med_[9] = {};  // last accepted readings, oldest first
  uint8_t medN_ = 0;
  uint8_t bad_ = 0;    // bad readings in a row
  bool everGood_ = false, newLevel_ = false;
  float value_ = NAN, last_ = NAN, pending_ = NAN;
  uint32_t lastGoodMs_ = 0, badSinceMs_ = 0;
};
