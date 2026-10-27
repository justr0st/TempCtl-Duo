// TempCtl Duo programs (spec 9.3): one program drives both channels.
//   Sequence: phases one after another (Bottom / Top / Both / Pause)
//   Timeline: two independent step lists (one per channel), started together
// Data model, limits, JSON codec (the same JSON as /duo/progN.json and the
// web API) and example programs. Pure C++ (no Arduino), host-tested.
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>
#include "settings.h"  // NUM_CHANNELS, CH_BOTTOM, CH_TOP

constexpr int PROG_MAX = 20;             // stored programs (/duo/prog0..19.json)
constexpr int PROG_MAX_PHASES = 100;     // Sequence
constexpr int PROG_MAX_STEPS = 100;      // Timeline, per channel
constexpr size_t PROG_NAME_LEN = 32;     // 31 characters (bytes)
constexpr size_t PROG_TEXT_LEN = 41;     // pause text: 40 characters (bytes)
constexpr int PROG_MAX_TEXTS = 16;       // different pause texts per program
constexpr uint32_t PROG_MAX_SEC = 9999UL * 60UL;  // longest ramp / hold
constexpr uint8_t PROG_FMT = 1;          // "fmt" of the program JSON
constexpr uint8_t NO_TEXT = 0xFF;
constexpr int PROG_EXAMPLES = 3;         // example programs created on first start

enum ProgType : uint8_t { PROG_SEQ = 0, PROG_TL = 1 };
enum PhaseKind : uint8_t { PH_B = 0, PH_T = 1, PH_BT = 2, PH_P = 3 };  // Bottom, Top, Both, Pause
enum StepKind : uint8_t { ST_TARGET = 0, ST_IDLE = 1, ST_PAUSE = 2 };

// target (degC), ramp time and hold time (s). Ramp 0 = jump.
struct Seg {
  float t;
  uint32_t r, h;
};

struct SeqPhase {
  uint8_t kind;   // PhaseKind
  uint8_t wait;   // B, BT: "wait for bottom to reach target"
  uint8_t bIdle;  // BT: bottom Idle (outputs off) instead of a jump target
  uint8_t tIdle;  // BT: top Idle (outputs off); the bottom then has a full target / ramp / hold (as in B)
                  // both Idle (1.0.61): both outputs off for b.h seconds (b.t, b.r unused)
  uint8_t msg;    // P: show "Action required"
  uint8_t text;   // P: index into DuoProgram::text, NO_TEXT = none
  Seg b;          // B, BT with tIdle: target/ramp/hold; BT: b.t = jump target (r, h unused); both Idle: b.h
  Seg t;          // T, BT: target/ramp/hold (unused with tIdle)
};

struct TlStep {
  uint8_t kind;   // StepKind
  uint8_t msg;    // pause: show "Action required"
  uint8_t text;   // pause: text index, NO_TEXT = none
  Seg s;          // target: t/r/h; idle: h only
};

struct DuoProgram {
  char name[PROG_NAME_LEN];
  uint8_t type;                       // ProgType
  uint8_t nText;                      // used entries of text[]
  uint16_t nPh;                       // Sequence: phases
  uint16_t nSt[NUM_CHANNELS];         // Timeline: steps per channel
  char text[PROG_MAX_TEXTS][PROG_TEXT_LEN];
  union {
    SeqPhase ph[PROG_MAX_PHASES];
    TlStep st[NUM_CHANNELS][PROG_MAX_STEPS];
  };
};

void progClear(DuoProgram& p, uint8_t type, const char* name);
const char* progTypeName(uint8_t type);   // "seq" / "tl"
const char* phaseKindName(uint8_t kind);  // "B" / "T" / "BT" / "P"
const char* progText(const DuoProgram& p, uint8_t idx);  // "" if none
// Adds (or finds) a pause text; "" -> NO_TEXT. False if the pool is full.
bool progAddText(DuoProgram& p, const char* s, uint8_t& idx);

int progItems(const DuoProgram& p);           // phases, or steps of both channels
bool progUses(const DuoProgram& p, int ch);   // channel has phases/steps (else: manual during the run)
bool progHasTarget(const DuoProgram& p, int ch, int idx);  // phase/step idx sets a target for ch
Seg* progSeg(DuoProgram& p, int ch, int idx);  // the Seg of ch in phase/step idx with a target, or null
const Seg* progSeg(const DuoProgram& p, int ch, int idx);

// JSON (spec 9.3.5). Unknown keys are ignored, missing r/h/wait/msg take
// defaults. Returns false + message on a structural error (wrong kind, missing
// target, too many phases, name/text too long ...). `names` (optional) are the
// channel names used in messages.
bool progFromJson(JsonObjectConst o, DuoProgram& p, char* err, size_t n, const char* const* names = nullptr);
void progToJson(const DuoProgram& p, JsonObject o);

// Every target within the channel's setpoint limits
bool progCheckLimits(const DuoProgram& p, const float* spMin, const float* spMax, char* err, size_t n,
                     const char* const* names = nullptr);

// Example programs (spec 9.9), i = 0 .. PROG_EXAMPLES-1
void progExample(int i, DuoProgram& p);
