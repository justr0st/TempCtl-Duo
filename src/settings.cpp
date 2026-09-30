// Settings: defaults, validation and JSON conversion (pure C++, host-tested).
// File storage: settings_store.cpp.
#include "settings.h"
#include "duo_program.h"  // PROG_MAX
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "version.h"

static void copyStr(char* dst, size_t n, const char* src) {
  if (!n) return;
  snprintf(dst, n, "%s", src ? src : "");
}

// PMA KS 40-1/41-1/42-1 addresses from interface description 9499 040 63511.
// Address layout: base + 8192 = integer with 1 decimal ("1dP"), which we use
// for all temperatures (value = raw / 10).
static constexpr uint16_t DP1 = 8192;

void regMapDefaults(RegMap& r, uint8_t profile) {
  if (profile == PROFILE_PMA_KS4X) {
    r.pv = 5102 + DP1;       // Cntr/Signal In.1   (13294) measured value after correction
    r.spEff = 5101 + DP1;    // Cntr/Signal SP.EF  (13293) effective setpoint
    r.out = 5103 + DP1;      // Cntr/Signal Ypid   (13295) controller output %
    r.status = 5100;         // Cntr/Signal St.Cntr (bit 0 heating, bits 12-15 mode)
    r.alarm = 250;           // ohnE/Signal St.Ala
    r.sp = 3180 + DP1;       // SEtP/Signal SP     (11372) interface setpoint. RAM is updated
                             // immediately, EEPROM storage is timed (max ~1 per 30 min)
    r.spLo = 3100 + DP1;     // SEtP/PArA SP.LO    (11292) lower setpoint limit
    r.spHi = 3101 + DP1;     // SEtP/PArA SP.Hi    (11293) upper setpoint limit
    r.rampRate = 3103 + DP1; // SEtP/PArA r.SP     (11295) setpoint gradient per minute
    r.ctrlOff = 1086;        // LOGI/Signal C.Off  (1 = all controller outputs off)
    r.remote = 1080;         // LOGI/Signal L-R    (1 = front panel blocked)
  } else {
    // Generic Modbus RTU controller (e.g. LCTC TE-M226): fill in from its manual.
    r.pv = r.spEff = r.out = r.status = r.alarm = r.sp = REG_UNSET;
    r.spLo = r.spHi = r.rampRate = r.ctrlOff = r.remote = REG_UNSET;
  }
  r.scale = 10.0f;
  r.readFn = 3;
  r.writeFn = 6;
  r.offRaw = -32000;
}

static void runawayDefaults(Runaway& r, int ch) {
  r.enable = 1;
  r.minOut = 80;
  r.heatSec = ch == CH_BOTTOM ? 300 : 120;  // the preheater heats a board slowly
  r.minRise = 5;
  r.holdBand = 15;
  r.holdSec = 60;
}

static void channelDefaults(ChannelSettings& c, int ch, uint8_t type) {
  memset(&c, 0, sizeof(c));
  copyStr(c.name, sizeof(c.name), ch == CH_BOTTOM ? "Bottom" : "Top");
  c.type = type;
  c.endAction = END_KEEP;
  c.endSp = 0;
  c.spMin = 0;
  c.spMax = 390;  // 10 degC below the local cut-off default (400)
  c.manualSp = 20;
  // KS 4x-1 factory default address is 1; the second controller gets 2
  c.mb.slaveId = (uint8_t)(ch + 1);
  c.mb.profile = PROFILE_PMA_KS4X;
  regMapDefaults(c.mb.reg, c.mb.profile);
  c.mb.rampMode = RAMP_CONTROLLER;  // falls back to host mode if rampRate is unset
  c.mb.minStep = 0.5f;
  c.mb.minWriteSec = 5;
  c.mb.lockFront = 0;
  runawayDefaults(c.mb.runaway, ch);
  c.loc.sensor = SENSOR_MAX31855;
  c.loc.calOffset = 0;
  c.loc.calGain = 1;
  c.loc.kp = 4;      // placeholders until autotune (step 4)
  c.loc.ki = 0.02f;
  c.loc.kd = 0;
  c.loc.windowSec = 1;
  c.loc.maxOut = 100;
  c.loc.maxTemp = 400;
  runawayDefaults(c.loc.runaway, ch);
}

void settingsDefaults(Settings& s, const BoardLimits& lim) {
  memset(&s, 0, sizeof(s));
  // KS 4x-1 factory defaults: 9600 baud, even parity (PrtY = 1)
  s.baud = 9600;
  s.parity = 1;
  s.stopBits = 1;
  s.timeoutMs = 300;
  s.pollMs = 1000;
  s.commLossSec = 30;
  s.bottomBand = 5;
  copyStr(s.apPass, sizeof(s.apPass), "tempctl123");
  copyStr(s.hostname, sizeof(s.hostname), DEFAULT_HOSTNAME);
  s.brightness = 200;
  s.rotate180 = 0;
  s.tsMinX = 200; s.tsMaxX = 3700;
  s.tsMinY = 240; s.tsMaxY = 3800;
  s.themeDark = THEME_DARK_DEFAULT;
  s.themePalette = 0;
  s.accentColor = 0x2196F3;  // "Blue", the first accent preset
  s.scanFrom = 3100 + DP1;
  s.scanTo = 3199 + DP1;
  s.selProgram = 0;
  // bottom = Modbus, top = Off; without RS485 both local (as many as the board wires)
  const uint8_t t0 = lim.rs485 ? CH_MODBUS : (lim.localMax >= 1 ? CH_LOCAL : CH_OFF);
  const uint8_t t1 = lim.rs485 ? CH_OFF : (lim.localMax >= 2 ? CH_LOCAL : CH_OFF);
  channelDefaults(s.ch[CH_BOTTOM], CH_BOTTOM, t0);
  channelDefaults(s.ch[CH_TOP], CH_TOP, t1);
}

const char* channelTypeName(uint8_t t) {
  switch (t) {
    case CH_MODBUS: return "modbus";
    case CH_LOCAL: return "local";
    default: return "off";
  }
}

// ------------------------------------------------------------------ validation

template <typename T>
static void clampv(T& v, T lo, T hi) {
  if (!(v >= lo)) v = lo;  // also catches NaN for floats
  if (v > hi) v = hi;
}

static void addMsg(char* msg, size_t n, const char* text) {
  if (!msg || !n) return;
  const size_t len = strlen(msg);
  if (len + 3 >= n) return;
  snprintf(msg + len, n - len, "%s%s", len ? "; " : "", text);
}

static void validRunaway(Runaway& r) {
  r.enable = r.enable ? 1 : 0;
  clampv(r.minOut, 0.0f, 100.0f);
  clampv<uint16_t>(r.heatSec, 10, 3600);
  clampv(r.minRise, 0.5f, 100.0f);
  clampv(r.holdBand, 1.0f, 200.0f);
  clampv<uint16_t>(r.holdSec, 5, 3600);
}

static bool validHostname(const char* h) {
  if (!h[0]) return false;
  for (const char* p = h; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-')) return false;
  return true;
}

bool settingsValidate(Settings& s, const BoardLimits& lim, char* msg, size_t n) {
  if (msg && n) msg[0] = 0;
  bool ok = true;
  static const uint32_t BAUDS[] = {2400, 4800, 9600, 19200, 38400};
  bool baudOk = false;
  for (uint32_t b : BAUDS) baudOk |= s.baud == b;
  if (!baudOk) s.baud = 9600;
  clampv<uint8_t>(s.parity, 0, 2);
  clampv<uint8_t>(s.stopBits, 1, 2);
  clampv<uint16_t>(s.timeoutMs, 50, 2000);
  clampv<uint16_t>(s.pollMs, 250, 10000);
  clampv<uint16_t>(s.commLossSec, 0, 3600);
  clampv(s.bottomBand, 0.5f, 50.0f);
  s.ssid[sizeof(s.ssid) - 1] = 0;
  s.pass[sizeof(s.pass) - 1] = 0;
  s.apPass[sizeof(s.apPass) - 1] = 0;
  if (strlen(s.apPass) < 8) copyStr(s.apPass, sizeof(s.apPass), "tempctl123");
  s.hostname[sizeof(s.hostname) - 1] = 0;
  if (!validHostname(s.hostname)) copyStr(s.hostname, sizeof(s.hostname), DEFAULT_HOSTNAME);
  clampv<uint8_t>(s.brightness, 10, 255);
  s.rotate180 = s.rotate180 ? 1 : 0;
  if (s.themeDark != THEME_DARK_DEFAULT) s.themeDark = s.themeDark ? 1 : 0;
  s.accentColor &= 0xFFFFFF;
  clampv<uint16_t>(s.tsMinX, 0, 4095); clampv<uint16_t>(s.tsMaxX, 0, 4095);
  clampv<uint16_t>(s.tsMinY, 0, 4095); clampv<uint16_t>(s.tsMaxY, 0, 4095);

  int locals = 0;
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    ChannelSettings& c = s.ch[i];
    c.name[sizeof(c.name) - 1] = 0;
    if (!c.name[0]) copyStr(c.name, sizeof(c.name), i == CH_BOTTOM ? "Bottom" : "Top");
    if (c.type > CH_LOCAL) c.type = CH_OFF;
    char t[96];
    if (c.type == CH_MODBUS && !lim.rs485) {
      snprintf(t, sizeof(t), "%s: this board variant has no RS485, channel set to Off", c.name);
      addMsg(msg, n, t);
      c.type = CH_OFF;
      ok = false;
    }
    if (c.type == CH_LOCAL && ++locals > lim.localMax) {
      snprintf(t, sizeof(t), "%s: this board variant wires only %u local channel(s), channel set to Off", c.name,
               (unsigned)lim.localMax);
      addMsg(msg, n, t);
      c.type = CH_OFF;
      ok = false;
    }
    clampv<uint8_t>(c.endAction, END_KEEP, END_OUTPUTS_OFF);
    clampv(c.endSp, -200.0f, 1500.0f);
    clampv(c.spMin, -200.0f, 1500.0f);
    clampv(c.spMax, -200.0f, 1500.0f);
    // Modbus
    clampv<uint8_t>(c.mb.slaveId, 1, 247);
    clampv<uint8_t>(c.mb.profile, PROFILE_PMA_KS4X, PROFILE_GENERIC);
    if (!(c.mb.reg.scale >= 1.0f && c.mb.reg.scale <= 1000.0f)) c.mb.reg.scale = 10.0f;
    c.mb.reg.readFn = c.mb.reg.readFn == 4 ? 4 : 3;
    c.mb.reg.writeFn = c.mb.reg.writeFn == 16 ? 16 : 6;
    clampv<uint8_t>(c.mb.rampMode, RAMP_HOST, RAMP_CONTROLLER);
    clampv(c.mb.minStep, 0.1f, 100.0f);
    clampv<uint16_t>(c.mb.minWriteSec, 1, 3600);
    c.mb.lockFront = c.mb.lockFront ? 1 : 0;
    validRunaway(c.mb.runaway);
    // local
    clampv<uint8_t>(c.loc.sensor, SENSOR_MAX31855, SENSOR_MAX6675);
    clampv(c.loc.calOffset, -50.0f, 50.0f);
    clampv(c.loc.calGain, 0.5f, 2.0f);
    clampv(c.loc.kp, 0.0f, 1000.0f);
    clampv(c.loc.ki, 0.0f, 100.0f);
    clampv(c.loc.kd, 0.0f, 10000.0f);
    clampv(c.loc.windowSec, 0.2f, 10.0f);
    clampv(c.loc.maxOut, 0.0f, 100.0f);
    clampv(c.loc.maxTemp, 50.0f, 600.0f);
    validRunaway(c.loc.runaway);
    // the setpoint limit stays at least 10 degC below the local cut-off (spec 9.2)
    if (c.type == CH_LOCAL && c.spMax > c.loc.maxTemp - 10) c.spMax = c.loc.maxTemp - 10;
    if (c.spMax < c.spMin) c.spMax = c.spMin;
    clampv(c.manualSp, c.spMin, c.spMax);
  }
  if (s.ch[0].type == CH_MODBUS && s.ch[1].type == CH_MODBUS && s.ch[0].mb.slaveId == s.ch[1].mb.slaveId) {
    char t[96];
    snprintf(t, sizeof(t), "%s and %s use the same Modbus address %u", s.ch[0].name, s.ch[1].name,
             (unsigned)s.ch[0].mb.slaveId);
    addMsg(msg, n, t);
    ok = false;
  }
  if (s.selProgram >= PROG_MAX) s.selProgram = 0;
  return ok;
}

// ------------------------------------------------------------------ JSON

static const char* const END_NAMES[] = {"keep", "sp", "off"};
static const char* const PROFILE_NAMES[] = {"pma", "generic"};
static const char* const RAMP_NAMES[] = {"host", "controller"};
static const char* const SENSOR_NAMES[] = {"max31855", "max6675"};
static const char* const TYPE_NAMES[] = {"off", "modbus", "local"};

static int addrOut(uint16_t a) { return a == REG_UNSET ? -1 : (int)a; }

static void regToJson(const RegMap& r, JsonObject o) {
  o["pv"] = addrOut(r.pv);
  o["spEff"] = addrOut(r.spEff);
  o["out"] = addrOut(r.out);
  o["status"] = addrOut(r.status);
  o["alarm"] = addrOut(r.alarm);
  o["sp"] = addrOut(r.sp);
  o["spLo"] = addrOut(r.spLo);
  o["spHi"] = addrOut(r.spHi);
  o["rampRate"] = addrOut(r.rampRate);
  o["ctrlOff"] = addrOut(r.ctrlOff);
  o["remote"] = addrOut(r.remote);
  o["scale"] = r.scale;
  o["readFn"] = r.readFn;
  o["writeFn"] = r.writeFn;
  o["offRaw"] = r.offRaw;
}

static void runawayToJson(const Runaway& r, JsonObject o) {
  o["enable"] = r.enable != 0;
  o["minOut"] = r.minOut;
  o["heatSec"] = r.heatSec;
  o["minRise"] = r.minRise;
  o["holdBand"] = r.holdBand;
  o["holdSec"] = r.holdSec;
}

void settingsToJson(const Settings& s, JsonObject o, bool forApi) {
  o["fmt"] = SETTINGS_FMT;
  JsonObject b = o["bus"].to<JsonObject>();
  b["baud"] = s.baud;
  b["parity"] = s.parity;
  b["stopBits"] = s.stopBits;
  b["timeoutMs"] = s.timeoutMs;
  b["pollMs"] = s.pollMs;
  b["commLossSec"] = s.commLossSec;
  JsonObject r = o["run"].to<JsonObject>();
  r["bottomBand"] = s.bottomBand;
  JsonObject w = o["wifi"].to<JsonObject>();
  w["ssid"] = s.ssid;
  w["hostname"] = s.hostname;
  if (forApi) {
    w["passSet"] = s.pass[0] != 0;
    w["apPassSet"] = s.apPass[0] != 0;
  } else {
    w["pass"] = s.pass;
    w["apPass"] = s.apPass;
  }
  JsonObject d = o["display"].to<JsonObject>();
  d["brightness"] = s.brightness;
  d["rotate180"] = s.rotate180 != 0;
  if (s.themeDark == THEME_DARK_DEFAULT) d["themeDark"] = nullptr;
  else d["themeDark"] = s.themeDark != 0;
  d["themePalette"] = s.themePalette;
  char hex[8];
  snprintf(hex, sizeof(hex), "#%06X", (unsigned)(s.accentColor & 0xFFFFFF));
  d["accent"] = hex;
  JsonArray tc = d["touchCal"].to<JsonArray>();
  tc.add(s.tsMinX); tc.add(s.tsMaxX); tc.add(s.tsMinY); tc.add(s.tsMaxY);
  JsonObject t = o["tools"].to<JsonObject>();
  t["scanFrom"] = s.scanFrom;
  t["scanTo"] = s.scanTo;
  o["selProgram"] = s.selProgram;
  JsonArray chs = o["channels"].to<JsonArray>();
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    const ChannelSettings& c = s.ch[i];
    JsonObject j = chs.add<JsonObject>();
    j["name"] = c.name;
    j["type"] = TYPE_NAMES[c.type <= CH_LOCAL ? c.type : 0];
    j["endAction"] = END_NAMES[c.endAction <= END_OUTPUTS_OFF ? c.endAction : 0];
    j["endSp"] = c.endSp;
    j["spMin"] = c.spMin;
    j["spMax"] = c.spMax;
    j["manualSp"] = c.manualSp;
    JsonObject m = j["modbus"].to<JsonObject>();
    m["addr"] = c.mb.slaveId;
    m["profile"] = PROFILE_NAMES[c.mb.profile ? 1 : 0];
    regToJson(c.mb.reg, m["reg"].to<JsonObject>());
    m["rampMode"] = RAMP_NAMES[c.mb.rampMode ? 1 : 0];
    m["minStep"] = c.mb.minStep;
    m["minWriteSec"] = c.mb.minWriteSec;
    m["lockFront"] = c.mb.lockFront != 0;
    runawayToJson(c.mb.runaway, m["runaway"].to<JsonObject>());
    JsonObject l = j["local"].to<JsonObject>();
    l["sensor"] = SENSOR_NAMES[c.loc.sensor ? 1 : 0];
    l["calOffset"] = c.loc.calOffset;
    l["calGain"] = c.loc.calGain;
    l["kp"] = c.loc.kp;
    l["ki"] = c.loc.ki;
    l["kd"] = c.loc.kd;
    l["windowSec"] = c.loc.windowSec;
    l["maxOut"] = c.loc.maxOut;
    l["maxTemp"] = c.loc.maxTemp;
    runawayToJson(c.loc.runaway, l["runaway"].to<JsonObject>());
  }
}

// --- reading: every key is optional; a present key with a wrong type is an error

struct Reader {
  char* msg;
  size_t n;
  bool ok;
  Reader(char* m, size_t len) : msg(m), n(len), ok(true) {}
  void fail(const char* path, const char* key, const char* what) {
    if (!ok) return;  // keep the first error
    ok = false;
    snprintf(msg, n, "%s%s: %s", path, key, what);
  }
  template <typename T>
  void num(JsonObjectConst o, const char* path, const char* key, T& out) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!v.is<double>()) return fail(path, key, "number expected");
    out = v.as<T>();
  }
  void flag(JsonObjectConst o, const char* path, const char* key, uint8_t& out) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!v.is<bool>()) return fail(path, key, "true / false expected");
    out = v.as<bool>() ? 1 : 0;
  }
  void str(JsonObjectConst o, const char* path, const char* key, char* out, size_t size) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!v.is<const char*>()) return fail(path, key, "text expected");
    const char* t = v.as<const char*>();
    if (strlen(t) >= size) return fail(path, key, "text too long");
    copyStr(out, size, t);
  }
  void choice(JsonObjectConst o, const char* path, const char* key, const char* const* names, int count,
              uint8_t& out) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    const char* t = v.is<const char*>() ? v.as<const char*>() : nullptr;
    for (int i = 0; t && i < count; ++i)
      if (!strcmp(t, names[i])) { out = (uint8_t)i; return; }
    char w[80];
    snprintf(w, sizeof(w), "one of");
    for (int i = 0; i < count; ++i) {
      const size_t len = strlen(w);
      snprintf(w + len, sizeof(w) - len, "%s \"%s\"", i ? "," : "", names[i]);
    }
    fail(path, key, w);
  }
  void addr(JsonObjectConst o, const char* path, const char* key, uint16_t& out) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!v.is<long>()) return fail(path, key, "register address (0..65534, -1 = not used) expected");
    const long a = v.as<long>();
    if (a < -1 || a > 65534) return fail(path, key, "register address (0..65534, -1 = not used) expected");
    out = a < 0 ? REG_UNSET : (uint16_t)a;
  }
  JsonObjectConst obj(JsonObjectConst o, const char* path, const char* key) {
    JsonVariantConst v = o[key];
    if (!v.isNull() && !v.is<JsonObjectConst>()) fail(path, key, "object expected");
    return v.as<JsonObjectConst>();  // null object if missing / wrong: reads nothing
  }
  void runaway(JsonObjectConst o, const char* p, Runaway& r) {
    if (o.isNull()) return;
    flag(o, p, "enable", r.enable);
    num(o, p, "minOut", r.minOut);
    num(o, p, "heatSec", r.heatSec);
    num(o, p, "minRise", r.minRise);
    num(o, p, "holdBand", r.holdBand);
    num(o, p, "holdSec", r.holdSec);
  }
};

bool settingsFromJson(JsonObjectConst o, Settings& s, char* msg, size_t n) {
  if (msg && n) msg[0] = 0;
  Reader R(msg, n);
  if (o.isNull()) {
    R.fail("", "settings", "JSON object expected");
    return false;
  }
  JsonObjectConst b = R.obj(o, "", "bus");
  if (!b.isNull()) {
    R.num(b, "bus.", "baud", s.baud);
    R.num(b, "bus.", "parity", s.parity);
    R.num(b, "bus.", "stopBits", s.stopBits);
    R.num(b, "bus.", "timeoutMs", s.timeoutMs);
    R.num(b, "bus.", "pollMs", s.pollMs);
    R.num(b, "bus.", "commLossSec", s.commLossSec);
  }
  JsonObjectConst r = R.obj(o, "", "run");
  if (!r.isNull()) {
    R.num(r, "run.", "bottomBand", s.bottomBand);
  }
  JsonObjectConst w = R.obj(o, "", "wifi");
  if (!w.isNull()) {
    R.str(w, "wifi.", "ssid", s.ssid, sizeof(s.ssid));
    R.str(w, "wifi.", "pass", s.pass, sizeof(s.pass));
    R.str(w, "wifi.", "apPass", s.apPass, sizeof(s.apPass));
    R.str(w, "wifi.", "hostname", s.hostname, sizeof(s.hostname));
  }
  JsonObjectConst d = R.obj(o, "", "display");
  if (!d.isNull()) {
    R.num(d, "display.", "brightness", s.brightness);
    R.flag(d, "display.", "rotate180", s.rotate180);
    JsonVariantConst td = d["themeDark"];
    if (td.is<bool>()) s.themeDark = td.as<bool>() ? 1 : 0;
    else if (!td.isNull()) R.fail("display.", "themeDark", "true / false expected");
    R.num(d, "display.", "themePalette", s.themePalette);
    JsonVariantConst ac = d["accent"];
    if (!ac.isNull()) {
      const char* t = ac.is<const char*>() ? ac.as<const char*>() : "";
      unsigned rgb = 0;
      char tail = 0;
      if (t[0] == '#' && strlen(t) == 7 && sscanf(t + 1, "%6x%c", &rgb, &tail) == 1) s.accentColor = rgb;
      else R.fail("display.", "accent", "colour \"#RRGGBB\" expected");
    }
    JsonVariantConst tc = d["touchCal"];
    if (!tc.isNull()) {
      JsonArrayConst a = tc.as<JsonArrayConst>();
      if (a.isNull() || a.size() != 4) R.fail("display.", "touchCal", "[minX, maxX, minY, maxY] expected");
      else {
        s.tsMinX = a[0] | s.tsMinX; s.tsMaxX = a[1] | s.tsMaxX;
        s.tsMinY = a[2] | s.tsMinY; s.tsMaxY = a[3] | s.tsMaxY;
      }
    }
  }
  JsonObjectConst t = R.obj(o, "", "tools");
  if (!t.isNull()) {
    R.num(t, "tools.", "scanFrom", s.scanFrom);
    R.num(t, "tools.", "scanTo", s.scanTo);
  }
  R.num(o, "", "selProgram", s.selProgram);
  JsonVariantConst cv = o["channels"];
  if (!cv.isNull()) {
    JsonArrayConst chs = cv.as<JsonArrayConst>();
    if (chs.isNull() || chs.size() > NUM_CHANNELS) R.fail("", "channels", "array of up to 2 channels expected");
    int i = 0;
    for (JsonVariantConst jv : chs) {
      if (i >= NUM_CHANNELS) break;
      ChannelSettings& c = s.ch[i];
      char p[24], pm[32], pl[32];
      snprintf(p, sizeof(p), "channels[%d].", i);
      snprintf(pm, sizeof(pm), "channels[%d].modbus.", i);
      snprintf(pl, sizeof(pl), "channels[%d].local.", i);
      ++i;
      if (jv.isNull()) continue;  // null = leave this channel unchanged
      JsonObjectConst j = jv.as<JsonObjectConst>();
      if (j.isNull()) { R.fail(p, "", "object expected"); continue; }
      R.str(j, p, "name", c.name, sizeof(c.name));
      R.choice(j, p, "type", TYPE_NAMES, 3, c.type);
      R.choice(j, p, "endAction", END_NAMES, 3, c.endAction);
      R.num(j, p, "endSp", c.endSp);
      R.num(j, p, "spMin", c.spMin);
      R.num(j, p, "spMax", c.spMax);
      R.num(j, p, "manualSp", c.manualSp);
      JsonObjectConst m = R.obj(j, p, "modbus");
      if (!m.isNull()) {
        R.num(m, pm, "addr", c.mb.slaveId);
        uint8_t prof = c.mb.profile;
        R.choice(m, pm, "profile", PROFILE_NAMES, 2, prof);
        // a new profile loads its register map; "reg" below can still override it
        if (prof != c.mb.profile) regMapDefaults(c.mb.reg, prof);
        c.mb.profile = prof;
        JsonObjectConst rg = R.obj(m, pm, "reg");
        if (!rg.isNull()) {
          char pr[40];
          snprintf(pr, sizeof(pr), "%sreg.", pm);
          R.addr(rg, pr, "pv", c.mb.reg.pv);
          R.addr(rg, pr, "spEff", c.mb.reg.spEff);
          R.addr(rg, pr, "out", c.mb.reg.out);
          R.addr(rg, pr, "status", c.mb.reg.status);
          R.addr(rg, pr, "alarm", c.mb.reg.alarm);
          R.addr(rg, pr, "sp", c.mb.reg.sp);
          R.addr(rg, pr, "spLo", c.mb.reg.spLo);
          R.addr(rg, pr, "spHi", c.mb.reg.spHi);
          R.addr(rg, pr, "rampRate", c.mb.reg.rampRate);
          R.addr(rg, pr, "ctrlOff", c.mb.reg.ctrlOff);
          R.addr(rg, pr, "remote", c.mb.reg.remote);
          R.num(rg, pr, "scale", c.mb.reg.scale);
          R.num(rg, pr, "readFn", c.mb.reg.readFn);
          R.num(rg, pr, "writeFn", c.mb.reg.writeFn);
          R.num(rg, pr, "offRaw", c.mb.reg.offRaw);
        }
        R.choice(m, pm, "rampMode", RAMP_NAMES, 2, c.mb.rampMode);
        R.num(m, pm, "minStep", c.mb.minStep);
        R.num(m, pm, "minWriteSec", c.mb.minWriteSec);
        R.flag(m, pm, "lockFront", c.mb.lockFront);
        R.runaway(R.obj(m, pm, "runaway"), pm, c.mb.runaway);
      }
      JsonObjectConst l = R.obj(j, p, "local");
      if (!l.isNull()) {
        R.choice(l, pl, "sensor", SENSOR_NAMES, 2, c.loc.sensor);
        R.num(l, pl, "calOffset", c.loc.calOffset);
        R.num(l, pl, "calGain", c.loc.calGain);
        R.num(l, pl, "kp", c.loc.kp);
        R.num(l, pl, "ki", c.loc.ki);
        R.num(l, pl, "kd", c.loc.kd);
        R.num(l, pl, "windowSec", c.loc.windowSec);
        R.num(l, pl, "maxOut", c.loc.maxOut);
        R.num(l, pl, "maxTemp", c.loc.maxTemp);
        R.runaway(R.obj(l, pl, "runaway"), pl, c.loc.runaway);
      }
    }
  }
  return R.ok;
}
