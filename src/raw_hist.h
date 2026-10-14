// Every PV reading for the web chart (spec 7, "Chart: every reading"): per channel a ring of
// (millis, PV) pairs, filled with each poll that brought a new value; plus the run's time anchors, so
// the page can put readings on the program timeline. Pure C++ (host-tested).
#pragma once
#include <stddef.h>
#include <stdint.h>

static constexpr int16_t RAW_NONE = INT16_MIN;  // PV value of a "no valid reading" entry (a gap)

struct RawRing {
  // 2 words per reading: millis, PV (int16, 0.1 degC) in the low half. Accessed word by word only
  // (the CYD keeps the ring in its IRAM heap, which takes 32-bit accesses only).
  uint32_t* w = nullptr;
  uint32_t cap = 0, total = 0;  // reading k (counted since boot) is at k % cap

  void put(uint32_t ms, int16_t v) {
    if (!cap) return;
    uint32_t* e = w + (total % cap) * 2;
    e[0] = ms;
    e[1] = (uint16_t)v;
    ++total;
  }
  uint32_t oldest() const { return total > cap ? total - cap : 0; }
  uint32_t msAt(uint32_t k) const { return w[(k % cap) * 2]; }
  int16_t pvAt(uint32_t k) const { return (int16_t)(uint16_t)w[(k % cap) * 2 + 1]; }
  // first index whose reading is later than `after` (millis, compared wrap-safe); total if none
  uint32_t find(uint32_t after) const {
    uint32_t lo = oldest(), hi = total;
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      if ((int32_t)(msAt(mid) - after) > 0) hi = mid;
      else lo = mid + 1;
    }
    return lo;
  }
  // readings k .. k+n-1 as 8-byte little-endian records (uint32 millis, int16 PV, 2 bytes 0); a reading
  // that was overwritten meanwhile comes out as a gap at time 0 (the page drops it). Returns n.
  uint32_t copy(uint32_t k, uint32_t n, uint8_t* out) const {
    const uint32_t old = oldest();
    for (uint32_t i = 0; i < n; ++i, ++k, out += 8) {
      uint32_t ms = 0, pv = (uint16_t)RAW_NONE;
      if (k >= old && k < total) {
        ms = msAt(k);
        pv = (uint16_t)pvAt(k);
      }
      for (int b = 0; b < 4; ++b) out[b] = (uint8_t)(ms >> (8 * b));
      out[4] = (uint8_t)pv;
      out[5] = (uint8_t)(pv >> 8);
      out[6] = out[7] = 0;
    }
    return n;
  }
};

// Program time against millis: anchors where the pace changes (start, pause, continue, a jump), so
// that run time = linear interpolation between them (and after the last point: + the wall time while
// running, constant while paused). Run time in ms.
struct RunAnchors {
  static constexpr int MAX = 48;
  uint32_t ms[MAX];
  int32_t rt[MAX];
  int n = 0;
  uint32_t lastMs = 0;  // the newest tracked point (the page interpolates up to it)
  int32_t lastRt = 0;
  int8_t pace = -1;     // pace of the interval before lastMs: 1 running, 0 standing, -1 unknown

  void reset() { n = 0, pace = -1; }
  void add(uint32_t m, int32_t r) {
    if (n && ms[n - 1] == m) {
      rt[n - 1] = r;
      return;
    }
    if (n == MAX) n = MAX - 1;  // full: the newest anchor replaces the last one (mapping gets coarser)
    ms[n] = m;
    rt[n] = r;
    ++n;
  }
  // each control cycle while a run is active
  void track(uint32_t now, int32_t r) {
    if (!n) {
      add(now, r);
      lastMs = now, lastRt = r, pace = -1;
      return;
    }
    const int32_t dt = (int32_t)(now - lastMs), dr = r - lastRt;
    if (dt <= 0) return;
    const int8_t p = 2 * dr > dt ? 1 : 0;
    if (dr < 0 || dr > dt + 1500) {  // run time went back or jumped ahead (resumed run, position set)
      add(lastMs, lastRt);
      add(now, r);
      pace = -1;
    } else if (pace == -1) {
      pace = p;
    } else if (p != pace) {
      add(lastMs, lastRt);
      pace = p;
    }
    lastMs = now, lastRt = r;
  }
};
