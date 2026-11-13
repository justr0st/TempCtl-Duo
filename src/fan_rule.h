// When the cooling fan runs (spec 9.25; pure C++, host-tested; fan.h drives the pin):
//  - during a program run while its current phase / step asks for the fan;
//  - after a run (afterRun on): from the end of the run until the hottest valid reading of all channels
//    (the monitor channel included) is below offBelow, at most maxMin minutes (no valid reading: maxMin).
//    The cooling ends at once when a new run starts or a channel heats outside a run (manual mode, autotune:
//    nothing to cool while it heats).
#pragma once
#include <math.h>
#include <stdint.h>

struct FanCfg {
  bool afterRun;
  float offBelow;   // degC
  uint16_t maxMin;  // minutes
};

// why the cooling after a run ended (the last end; for the log)
enum class FanEnd : uint8_t { None, Cool, Time, Heating, Run };

class FanRule {
 public:
  void reset() { *this = FanRule(); }

  // One control cycle: returns fan on / off. hottest = the highest valid reading (NaN: none), dt in s.
  bool update(const FanCfg& c, bool runActive, bool phaseFan, bool heating, float hottest, float dt) {
    if (runActive) {
      if (cooling_) end(FanEnd::Run);
    } else if (wasRun_ && c.afterRun) {  // the run just ended: cooling starts
      cooling_ = true;
      coolSec_ = 0;
    }
    wasRun_ = runActive;
    if (cooling_) {
      coolSec_ += dt > 0 ? dt : 0;
      if (heating) end(FanEnd::Heating);
      else if (!isnan(hottest) && hottest < c.offBelow) end(FanEnd::Cool);
      else if (coolSec_ >= c.maxMin * 60.0f) end(FanEnd::Time);
    }
    on_ = (runActive && phaseFan) || cooling_;
    return on_;
  }

  bool on() const { return on_; }
  bool cooling() const { return cooling_; }   // the after-run cooling runs
  float coolSec() const { return coolSec_; }  // ... for this long
  FanEnd lastEnd() const { return end_; }

 private:
  void end(FanEnd why) {
    cooling_ = false;
    end_ = why;
  }
  bool wasRun_ = false, cooling_ = false, on_ = false;
  float coolSec_ = 0;
  FanEnd end_ = FanEnd::None;
};
