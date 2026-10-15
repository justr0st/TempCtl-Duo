#include "duo_program.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char* const DEF_NAMES[NUM_CHANNELS] = {"Bottom", "Top"};

void progClear(DuoProgram& p, uint8_t type, const char* name) {
  memset(&p, 0, sizeof(p));
  p.type = type == PROG_TL ? PROG_TL : PROG_SEQ;
  snprintf(p.name, sizeof(p.name), "%s", name ? name : "");
}

const char* progTypeName(uint8_t type) { return type == PROG_TL ? "tl" : "seq"; }

const char* phaseKindName(uint8_t kind) {
  static const char* const K[] = {"B", "T", "BT", "P"};
  return kind < 4 ? K[kind] : "?";
}

const char* progText(const DuoProgram& p, uint8_t idx) { return idx < p.nText ? p.text[idx] : ""; }

bool progAddText(DuoProgram& p, const char* s, uint8_t& idx) {
  idx = NO_TEXT;
  if (!s || !*s) return true;
  for (int i = 0; i < p.nText; ++i)
    if (!strcmp(p.text[i], s)) { idx = (uint8_t)i; return true; }
  if (p.nText >= PROG_MAX_TEXTS || strlen(s) >= PROG_TEXT_LEN) return false;
  snprintf(p.text[p.nText], PROG_TEXT_LEN, "%s", s);
  idx = p.nText++;
  return true;
}

int progItems(const DuoProgram& p) {
  if (p.type == PROG_TL) return p.nSt[0] + p.nSt[1];
  return p.nPh;
}

bool progUses(const DuoProgram& p, int ch) {
  if (ch < 0 || ch >= NUM_CHANNELS) return false;
  if (p.type == PROG_TL) return p.nSt[ch] > 0;
  for (int i = 0; i < p.nPh; ++i) {
    const uint8_t k = p.ph[i].kind;
    if (k == PH_BT) return true;
    if (ch == CH_BOTTOM && k == PH_B) return true;
    if (ch == CH_TOP && k == PH_T) return true;
  }
  return false;
}

const Seg* progSeg(const DuoProgram& p, int ch, int idx) {
  if (ch < 0 || ch >= NUM_CHANNELS || idx < 0) return nullptr;
  if (p.type == PROG_TL) {
    if (idx >= p.nSt[ch] || p.st[ch][idx].kind != ST_TARGET) return nullptr;
    return &p.st[ch][idx].s;
  }
  if (idx >= p.nPh) return nullptr;
  const SeqPhase& f = p.ph[idx];
  if (ch == CH_BOTTOM) {
    if (f.kind == PH_B) return &f.b;
    if (f.kind == PH_BT && !f.bIdle) return &f.b;
    return nullptr;
  }
  return (f.kind == PH_T || f.kind == PH_BT) ? &f.t : nullptr;
}

Seg* progSeg(DuoProgram& p, int ch, int idx) {
  return const_cast<Seg*>(progSeg(static_cast<const DuoProgram&>(p), ch, idx));
}

bool progHasTarget(const DuoProgram& p, int ch, int idx) { return progSeg(p, ch, idx) != nullptr; }

// ------------------------------------------------------------------ JSON in

namespace {

struct Parser {
  char* err;
  size_t n;
  const char* const* names;
  bool fail(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
    return false;
  }
};

// seconds: number >= 0, rounded, <= PROG_MAX_SEC
bool readSec(JsonVariantConst v, uint32_t& out, bool& ok) {
  out = 0;
  if (v.isNull()) return true;
  if (!v.is<float>()) { ok = false; return false; }
  const float x = v.as<float>();
  if (!(x >= 0) || x > (float)PROG_MAX_SEC) { ok = false; return false; }
  out = (uint32_t)lroundf(x);
  return true;
}

// {"t":..,"r":..,"h":..} ("t" required; r/h only if rampHold)
const char* readSeg(JsonVariantConst v, Seg& s, bool rampHold) {
  s.t = 0;
  s.r = s.h = 0;
  if (!v.is<JsonObjectConst>()) return "missing";
  JsonVariantConst t = v["t"];
  if (!t.is<float>()) return "target missing";
  s.t = t.as<float>();
  if (!isfinite(s.t)) return "target is not a number";
  if (!rampHold) return nullptr;
  bool ok = true;
  readSec(v["r"], s.r, ok);
  if (!ok) return "ramp time must be 0 .. 599940 s";
  readSec(v["h"], s.h, ok);
  if (!ok) return "hold time must be 0 .. 599940 s";
  return nullptr;
}

bool readPause(Parser& P, JsonVariantConst v, DuoProgram& p, uint8_t& msg, uint8_t& text, const char* where, int no) {
  msg = (v["msg"] | false) ? 1 : 0;
  text = NO_TEXT;
  const char* s = v["text"] | "";
  if (strlen(s) >= PROG_TEXT_LEN) return P.fail("%s %d: pause text longer than 40 characters", where, no);
  if (!progAddText(p, s, text)) return P.fail("%s %d: too many different pause texts (max 16)", where, no);
  return true;
}

}  // namespace

bool progFromJson(JsonObjectConst o, DuoProgram& p, char* err, size_t n, const char* const* names) {
  Parser P{err, n, names ? names : DEF_NAMES};
  if (n) err[0] = 0;
  if (o.isNull()) return P.fail("Program JSON object missing");
  const int fmt = o["fmt"] | (int)PROG_FMT;
  if (fmt > PROG_FMT) return P.fail("Program format %d is newer than this firmware supports", fmt);
  const char* type = o["type"] | "";
  uint8_t t;
  if (!strcmp(type, "seq")) t = PROG_SEQ;
  else if (!strcmp(type, "tl")) t = PROG_TL;
  else return P.fail("type must be \"seq\" (Sequence) or \"tl\" (Timeline)");
  const char* name = o["name"] | "";
  while (*name == ' ') ++name;
  size_t len = strlen(name);
  while (len && name[len - 1] == ' ') --len;
  if (len >= PROG_NAME_LEN) return P.fail("Name longer than 31 characters");
  progClear(p, t, "");
  memcpy(p.name, name, len);
  p.name[len] = 0;
  if (!p.name[0]) snprintf(p.name, sizeof(p.name), "Program");

  if (t == PROG_SEQ) {
    JsonArrayConst a = o["phases"].as<JsonArrayConst>();
    if (!o["phases"].isNull() && a.isNull()) return P.fail("phases must be an array");
    if (a.size() > (size_t)PROG_MAX_PHASES) return P.fail("Too many phases (max 100)");
    int i = 0;
    for (JsonVariantConst v : a) {
      ++i;
      SeqPhase& f = p.ph[p.nPh];
      memset(&f, 0, sizeof(f));
      f.text = NO_TEXT;
      const char* k = v["kind"] | "";
      const char* e;
      if (!strcmp(k, "B")) {
        f.kind = PH_B;
        if ((e = readSeg(v["b"], f.b, true))) return P.fail("Phase %d: %s %s", i, P.names[CH_BOTTOM], e);
        f.wait = (v["wait"] | false) ? 1 : 0;
      } else if (!strcmp(k, "T")) {
        f.kind = PH_T;
        if ((e = readSeg(v["t"], f.t, true))) return P.fail("Phase %d: %s %s", i, P.names[CH_TOP], e);
      } else if (!strcmp(k, "BT")) {
        f.kind = PH_BT;
        if (v["b"]["idle"] | false) {
          f.bIdle = 1;
        } else if ((e = readSeg(v["b"], f.b, false))) {
          return P.fail("Phase %d: %s %s", i, P.names[CH_BOTTOM], e);
        }
        if ((e = readSeg(v["t"], f.t, true))) return P.fail("Phase %d: %s %s", i, P.names[CH_TOP], e);
        f.wait = (!f.bIdle && (v["wait"] | false)) ? 1 : 0;
      } else if (!strcmp(k, "P")) {
        f.kind = PH_P;
        if (!readPause(P, v, p, f.msg, f.text, "Phase", i)) return false;
      } else {
        return P.fail("Phase %d: kind must be B, T, BT or P", i);
      }
      ++p.nPh;
    }
    return true;
  }

  static const char* const KEYS[NUM_CHANNELS] = {"B", "T"};
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    JsonVariantConst av = o[KEYS[ch]];
    JsonArrayConst a = av.as<JsonArrayConst>();
    if (!av.isNull() && a.isNull()) return P.fail("%s steps must be an array", P.names[ch]);
    if (a.size() > (size_t)PROG_MAX_STEPS) return P.fail("%s: too many steps (max %d)", P.names[ch], PROG_MAX_STEPS);
    int i = 0;
    for (JsonVariantConst v : a) {
      ++i;
      TlStep& s = p.st[ch][p.nSt[ch]];
      memset(&s, 0, sizeof(s));
      s.text = NO_TEXT;
      char where[40];
      snprintf(where, sizeof(where), "%s step", P.names[ch]);
      if (v["pause"] | false) {
        s.kind = ST_PAUSE;
        if (!readPause(P, v, p, s.msg, s.text, where, i)) return false;
      } else if (v["idle"] | false) {
        s.kind = ST_IDLE;
        bool ok = true;
        readSec(v["h"], s.s.h, ok);
        if (!ok) return P.fail("%s %d: time must be 0 .. 599940 s", where, i);
      } else {
        s.kind = ST_TARGET;
        const char* e = readSeg(v, s.s, true);
        if (e) return P.fail("%s %d: %s", where, i, e);
      }
      ++p.nSt[ch];
    }
  }
  return true;
}

// ------------------------------------------------------------------ JSON out

static void segOut(JsonObject o, const Seg& s, bool rampHold) {
  o["t"] = roundf(s.t * 10.0f) / 10.0f;
  if (!rampHold) return;
  o["r"] = s.r;
  o["h"] = s.h;
}

void progToJson(const DuoProgram& p, JsonObject o) {
  o["fmt"] = PROG_FMT;
  o["name"] = JsonString(p.name);  // copied: a const char[] would be kept as a pointer (ArduinoJson 7.3+)
  o["type"] = progTypeName(p.type);
  if (p.type == PROG_SEQ) {
    JsonArray a = o["phases"].to<JsonArray>();
    for (int i = 0; i < p.nPh; ++i) {
      const SeqPhase& f = p.ph[i];
      JsonObject v = a.add<JsonObject>();
      v["kind"] = phaseKindName(f.kind);
      switch (f.kind) {
        case PH_B:
          if (f.wait) v["wait"] = true;
          segOut(v["b"].to<JsonObject>(), f.b, true);
          break;
        case PH_T:
          segOut(v["t"].to<JsonObject>(), f.t, true);
          break;
        case PH_BT:
          if (f.wait) v["wait"] = true;
          if (f.bIdle) v["b"]["idle"] = true;
          else segOut(v["b"].to<JsonObject>(), f.b, false);
          segOut(v["t"].to<JsonObject>(), f.t, true);
          break;
        default:
          if (f.msg) v["msg"] = true;
          if (f.text != NO_TEXT) v["text"] = progText(p, f.text);
          break;
      }
    }
    return;
  }
  static const char* const KEYS[NUM_CHANNELS] = {"B", "T"};
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    JsonArray a = o[KEYS[ch]].to<JsonArray>();
    for (int i = 0; i < p.nSt[ch]; ++i) {
      const TlStep& s = p.st[ch][i];
      JsonObject v = a.add<JsonObject>();
      if (s.kind == ST_PAUSE) {
        v["pause"] = true;
        if (s.msg) v["msg"] = true;
        if (s.text != NO_TEXT) v["text"] = progText(p, s.text);
      } else if (s.kind == ST_IDLE) {
        v["idle"] = true;
        v["h"] = s.s.h;
      } else {
        segOut(v, s.s, true);
      }
    }
  }
}

bool progCheckLimits(const DuoProgram& p, const float* spMin, const float* spMax, char* err, size_t n,
                     const char* const* names) {
  if (!names) names = DEF_NAMES;
  const int items = p.type == PROG_TL ? PROG_MAX_STEPS : p.nPh;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    for (int i = 0; i < items; ++i) {
      const Seg* s = progSeg(p, ch, i);
      if (!s) continue;
      if (s->t < spMin[ch] || s->t > spMax[ch]) {
        snprintf(err, n, "%s %d: %s target %.1f is outside the %s setpoint limits %.0f .. %.0f",
                 p.type == PROG_TL ? "Step" : "Phase", i + 1, names[ch], s->t, names[ch], spMin[ch], spMax[ch]);
        return false;
      }
    }
  }
  return true;
}

// ------------------------------------------------------------------ examples

static void addPh(DuoProgram& p, uint8_t kind, bool wait, float bt, uint32_t br, uint32_t bh, float tt, uint32_t tr,
                  uint32_t th, bool bIdle = false) {
  SeqPhase& f = p.ph[p.nPh++];
  memset(&f, 0, sizeof(f));
  f.kind = kind;
  f.wait = wait;
  f.bIdle = bIdle;
  f.text = NO_TEXT;
  f.b = Seg{bt, br, bh};
  f.t = Seg{tt, tr, th};
}

static void addPause(DuoProgram& p, bool msg, const char* text) {
  SeqPhase& f = p.ph[p.nPh++];
  memset(&f, 0, sizeof(f));
  f.kind = PH_P;
  f.msg = msg;
  progAddText(p, text, f.text);
}

static void addSt(DuoProgram& p, int ch, uint8_t kind, float t, uint32_t r, uint32_t h) {
  TlStep& s = p.st[ch][p.nSt[ch]++];
  memset(&s, 0, sizeof(s));
  s.kind = kind;
  s.text = NO_TEXT;
  s.s = Seg{t, r, h};
}

void progExample(int i, DuoProgram& p) {
  switch (i) {
    case 0:  // spec 9.9 (1)
      progClear(p, PROG_SEQ, "Lead-free BGA");
      addPh(p, PH_B, true, 150, 60, 60, 0, 0, 0);
      addPause(p, true, "");
      addPh(p, PH_BT, true, 160, 0, 0, 150, 60, 20);
      addPh(p, PH_T, false, 0, 0, 0, 190, 40, 40);
      addPh(p, PH_T, false, 0, 0, 0, 225, 30, 20);
      addPh(p, PH_T, false, 0, 0, 0, 245, 20, 30);
      addPh(p, PH_BT, false, 0, 0, 0, 150, 40, 0, true);
      break;
    case 1:  // spec 9.9 (2)
      progClear(p, PROG_SEQ, "Leaded BGA");
      addPh(p, PH_B, true, 130, 40, 60, 0, 0, 0);
      addPh(p, PH_BT, true, 140, 0, 0, 130, 50, 20);
      addPh(p, PH_T, false, 0, 0, 0, 170, 40, 30);
      addPh(p, PH_T, false, 0, 0, 0, 205, 30, 20);
      addPh(p, PH_T, false, 0, 0, 0, 220, 15, 30);
      addPh(p, PH_BT, false, 0, 0, 0, 120, 40, 0, true);
      break;
    default:  // spec 9.9 (3)
      progClear(p, PROG_TL, "Lead-free BGA (Timeline)");
      addSt(p, CH_BOTTOM, ST_TARGET, 150, 60, 150);
      addSt(p, CH_BOTTOM, ST_TARGET, 165, 30, 150);
      addSt(p, CH_BOTTOM, ST_IDLE, 0, 0, 60);
      addSt(p, CH_TOP, ST_IDLE, 0, 0, 120);
      addSt(p, CH_TOP, ST_TARGET, 150, 40, 20);
      addSt(p, CH_TOP, ST_TARGET, 190, 40, 30);
      addSt(p, CH_TOP, ST_TARGET, 225, 30, 20);
      addSt(p, CH_TOP, ST_TARGET, 245, 20, 30);
      addSt(p, CH_TOP, ST_TARGET, 150, 40, 0);
      break;
  }
}
