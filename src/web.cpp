// HTTP server: single-page web UI + JSON API.
// No authentication: keep the device on a trusted network.
#include "web.h"
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>
#include "app_state.h"
#include "controller.h"
#include "net.h"
#include "settings_store.h"
#include "ui.h"
#include "web_page.h"

static WebServer server(80);

// ------------------------------------------------------------ helpers
static void sendJson(JsonDocument& d, int code = 200) {
  String out;
  serializeJson(d, out);
  server.send(code, "application/json", out);
}

static void sendError(const char* msg, int code = 400) {
  JsonDocument d;
  d["ok"] = false;
  d["err"] = msg;
  sendJson(d, code);
}

static bool parseBody(JsonDocument& d) {
  if (!server.hasArg("plain")) return false;
  return deserializeJson(d, server.arg("plain")) == DeserializationError::Ok;
}

static void setNum(JsonVariant v, float x) {
  if (isnan(x)) v.set(nullptr);
  else v.set(roundf(x * 10.0f) / 10.0f);
}

static int addrOut(uint16_t a) { return a == REG_UNSET ? -1 : (int)a; }
static uint16_t addrIn(JsonVariantConst v, uint16_t cur) {
  if (v.isNull()) return cur;
  const long a = v.as<long>();
  return (a < 0 || a > 65534) ? REG_UNSET : (uint16_t)a;
}

// ------------------------------------------------------------ status / history
static void handleStatus() {
  LiveData L;
  Settings S;
  {
    Guard g;
    L = gLive;
    S = gSettings;
  }
  JsonDocument d;
  JsonObject c = d["comm"].to<JsonObject>();
  c["ok"] = L.commOk;
  c["okCount"] = L.okCount;
  c["errCount"] = L.errCount;
  c["err"] = L.lastErr;
  setNum(d["pv"], L.pvValid ? L.pv : NAN);
  d["pvFlag"] = Controller::flagName(L.pvFlag);
  setNum(d["spEff"], L.spEffValid ? L.spEff : NAN);
  setNum(d["out"], L.outValid ? L.out : NAN);
  if (L.statusValid) {
    d["status"] = L.status;
    if (S.ch[ENGINE_CH].mb.profile == PROFILE_PMA_KS4X) d["mode"] = pmaModeText(L.status);
  }
  if (L.alarmValid) d["alarm"] = L.alarm;
  JsonObject r = d["run"].to<JsonObject>();
  r["state"] = runStateName(L.state);
  r["phase"] = phaseName(L.phase);
  r["step"] = L.step;
  r["phaseEl"] = L.phaseEl;
  r["phaseDur"] = L.phaseDur;
  r["elapsed"] = L.elapsed;
  r["total"] = L.total;
  r["soakWait"] = L.soakWait;
  setNum(r["hostSp"], L.hostSp);
  setNum(r["target"], L.target);
  r["spSynced"] = L.spSynced;
  r["spWrites"] = L.spWrites;
  r["msg"] = L.runMsg;
  r["prog"] = L.runProg;
  r["name"] = L.runName;
  r["steps"] = L.stepCount;
  JsonObject m = d["manual"].to<JsonObject>();
  m["on"] = L.manualOn;
  setNum(m["sp"], S.ch[ENGINE_CH].manualSp);
  m["synced"] = L.manualSynced;
  d["sel"] = S.selProgram;
  {
    Guard g;
    const Program& p = gPrograms[S.selProgram < MAX_PROGRAMS ? S.selProgram : 0];
    d["selName"] = p.name;
    d["selSteps"] = p.count;
    d["plannedTotal"] = ProgramEngine::plannedTotal(p);
  }
  d["spConfigured"] = S.ch[ENGINE_CH].mb.reg.sp != REG_UNSET;
  d["profile"] = S.ch[ENGINE_CH].mb.profile;
  d["net"] = netInfo();
  d["uptime"] = millis() / 1000;
  d["fw"] = FW_VERSION;
  d["board"] = BOARD_NAME;
  d["variant"] = BOARD_VARIANT;
  // both channels (the fields above are the bottom channel, for the old page)
  JsonArray chs = d["channels"].to<JsonArray>();
  for (int i = 0; i < NUM_CHANNELS; ++i) {
    const ChannelLive& C = L.ch[i];
    JsonObject o = chs.add<JsonObject>();
    o["name"] = C.name;
    o["type"] = channelTypeName(C.type);
    o["ok"] = C.commOk;
    o["err"] = C.lastErr;
    setNum(o["pv"], C.pvValid ? C.pv : NAN);
    o["pvFlag"] = Controller::flagName(C.pvFlag);
    setNum(o["sp"], C.spEffValid ? C.spEff : NAN);
    setNum(o["out"], C.outValid ? C.out : NAN);
    if (C.statusValid && C.type == CH_MODBUS && S.ch[i].mb.profile == PROFILE_PMA_KS4X) o["mode"] = pmaModeText(C.status);
    if (C.type == CH_LOCAL) setNum(o["cold"], C.coldC);
    o["fault"] = C.fault ? C.faultText : "";
    JsonObject m = o["manual"].to<JsonObject>();
    m["on"] = C.manualOn;
    setNum(m["sp"], S.ch[i].manualSp);
    m["synced"] = C.manualSynced;
  }
  sendJson(d);
}

static void appendArray(String& out, const char* name, const float* v) {
  char num[16];
  out += ",\"";
  out += name;
  out += "\":[";
  for (int i = 0; i < CHART_N; ++i) {
    if (i) out += ',';
    if (isnan(v[i])) out += "null";
    else { snprintf(num, sizeof(num), "%.1f", v[i]); out += num; }
  }
  out += ']';
}

static void handleHistory() {
  static ChartData d;
  chartSnapshot(d);
  JsonDocument h;
  h["mode"] = d.run ? "run" : "live";
  h["planActive"] = d.planActive;
  h["spActive"] = d.spActive;
  h["binSec"] = d.binSec;
  h["span"] = d.span;
  h["planSpan"] = d.planSpan;
  h["planTotal"] = d.planTotal;
  h["elapsed"] = d.elapsed;
  h["name"] = d.progName;
  String out;
  out.reserve(6500);
  serializeJson(h, out);
  out.remove(out.length() - 1);  // drop '}' and append the arrays
  appendArray(out, "pv", d.pv);
  appendArray(out, "sp", d.sp);
  appendArray(out, "plan", d.plan);
  out += '}';
  server.send(200, "application/json", out);
}

// Log of the last program run (if there was one since boot), else the live window.
static void handleCsv() {
  String out = "time_s,process_value,setpoint,planned\n";
  out.reserve(CHART_N * 24 + 40);
  char line[64];
  bool run;
  {
    Guard g;  // build the text under the lock, send it afterwards
    run = gHist->runValid;
    for (int i = 0; i < CHART_N; ++i) {
      const float pv = run ? gHist->runPv[i] : gHist->livePv[i];
      const float sp = run ? gHist->runSp[i] : gHist->liveSp[i];
      const float t = run ? i * gHist->runBin : -(float)(CHART_N - 1 - i) * LIVE_SAMPLE_SEC;
      const float pl =
          (run && t <= gHist->runTotal) ? ProgramEngine::plannedAt(gHist->runProg, gHist->runStart, t) : NAN;
      if (isnan(pv) && isnan(sp)) continue;
      snprintf(line, sizeof(line), "%.0f,%s,%s,%s\n", t, isnan(pv) ? "" : String(pv, 1).c_str(),
               isnan(sp) ? "" : String(sp, 1).c_str(), isnan(pl) ? "" : String(pl, 1).c_str());
      out += line;
    }
  }
  server.sendHeader("Content-Disposition", run ? "attachment; filename=tempctl-run.csv"
                                               : "attachment; filename=tempctl-live.csv");
  server.send(200, "text/csv", out);
}

// ------------------------------------------------------------ program / commands
static void handleGetPrograms() {
  JsonDocument d;
  {
    Guard g;
    d["sel"] = gSettings.selProgram;
    d["max"] = MAX_PROGRAMS;
    d["maxSteps"] = MAX_STEPS;
    d["spMin"] = gSettings.ch[ENGINE_CH].spMin;
    d["spMax"] = gSettings.ch[ENGINE_CH].spMax;
    JsonArray a = d["programs"].to<JsonArray>();
    for (int i = 0; i < MAX_PROGRAMS; ++i) {
      JsonObject o = a.add<JsonObject>();
      o["name"] = gPrograms[i].name;
      o["steps"] = gPrograms[i].count;
      o["total"] = ProgramEngine::plannedTotal(gPrograms[i]);
    }
  }
  sendJson(d);
}

static int progIndexArg(JsonVariantConst v) {
  if (!v.isNull()) return v.as<int>();
  if (server.hasArg("i")) return server.arg("i").toInt();
  Guard g;
  return gSettings.selProgram;
}

static void sendProgram(int i) {
  static Program p;
  float lo, hi;
  {
    Guard g;
    p = gPrograms[i];
    lo = gSettings.ch[ENGINE_CH].spMin;
    hi = gSettings.ch[ENGINE_CH].spMax;
  }
  JsonDocument d;
  d["i"] = i;
  d["name"] = p.name;
  d["spMin"] = lo;
  d["spMax"] = hi;
  d["maxSteps"] = MAX_STEPS;
  JsonArray a = d["steps"].to<JsonArray>();
  for (int k = 0; k < p.count; ++k) {
    JsonObject o = a.add<JsonObject>();
    o["t"] = p.s[k].target;
    o["r"] = p.s[k].rampSec;
    o["h"] = p.s[k].holdSec;
  }
  sendJson(d);
}

static void handleGetProgram() {
  const int i = progIndexArg(JsonVariantConst());
  if (i < 0 || i >= MAX_PROGRAMS) return sendError("Program index out of range");
  sendProgram(i);
}

// Body: {"i":0,"name":"Bisque","steps":[{"t":100,"r":1800,"h":600}, ...]}  (r, h in seconds)
static void handlePostProgram() {
  JsonDocument d;
  if (!parseBody(d)) return sendError("Invalid JSON");
  const int i = progIndexArg(d["i"]);
  if (i < 0 || i >= MAX_PROGRAMS) return sendError("Program index out of range");
  JsonArrayConst a = d["steps"].as<JsonArrayConst>();
  if (a.isNull()) return sendError("Missing steps");
  if (a.size() > (size_t)MAX_STEPS) return sendError("Too many steps (max 100)");
  static Program p;
  {
    Guard g;
    p = gPrograms[i];
  }
  if (d["name"].is<const char*>()) {
    const char* n = d["name"];
    while (*n == ' ') ++n;
    if (*n) strlcpy(p.name, n, sizeof(p.name));
  }
  p.count = 0;
  for (JsonObjectConst o : a) {
    Step st;
    st.target = o["t"] | 0.0f;
    const float r = o["r"] | 0.0f, h = o["h"] | 0.0f;
    st.rampSec = r > 0 ? (uint32_t)lroundf(fminf(r, (float)MAX_STEP_SEC)) : 0;
    st.holdSec = h > 0 ? (uint32_t)lroundf(fminf(h, (float)MAX_STEP_SEC)) : 0;
    programInsertStep(p, p.count, st);
  }
  {
    Guard g;
    gPrograms[i] = p;
  }
  if (!saveProgram(i)) return sendError("Writing the program to flash failed", 500);
  sendProgram(i);
}

static void handleSelect() {
  JsonDocument d;
  if (!parseBody(d)) return sendError("Invalid JSON");
  const int i = d["i"] | -1;
  if (i < 0 || i >= MAX_PROGRAMS) return sendError("Program index out of range");
  selectProgram(i);
  JsonDocument r;
  r["ok"] = true;
  r["sel"] = i;
  sendJson(r);
}

// Body: {"sp":150.0} and/or {"on":true|false}
static void handleManual() {
  JsonDocument d;
  if (!parseBody(d)) return sendError("Invalid JSON");
  const int ch = d["ch"] | (int)ENGINE_CH;  // 0 = bottom (default), 1 = top
  if (ch < 0 || ch >= NUM_CHANNELS) return sendError("ch must be 0 (bottom) or 1 (top)");
  if (!d["sp"].isNull()) setManualSp(d["sp"].as<float>(), ch);
  bool ok = true;
  if (!d["on"].isNull()) ok = sendCmd(d["on"].as<bool>() ? CMD_MANUAL_ON : CMD_MANUAL_OFF, ch);
  JsonDocument r;
  r["ok"] = ok;
  sendJson(r);
}

// {"ch":0}: acknowledge a channel's latched fault (runaway, sensor, over temperature)
static void handleAck() {
  JsonDocument d;
  if (!parseBody(d)) return sendError("Invalid JSON");
  const int ch = d["ch"] | -1;
  if (ch < 0 || ch >= NUM_CHANNELS) return sendError("ch must be 0 (bottom) or 1 (top)");
  JsonDocument r;
  r["ok"] = sendCmd(CMD_ACK_FAULT, ch);
  sendJson(r);
}

static void handleCmd() {
  JsonDocument d;
  if (!parseBody(d)) return sendError("Invalid JSON");
  const String c = d["cmd"] | "";
  CmdType t;
  int arg = d["prog"] | -1;
  if (c == "start") t = CMD_START;
  else if (c == "pause") t = CMD_PAUSE;
  else if (c == "resume") t = CMD_RESUME;
  else if (c == "stop") t = CMD_STOP;
  else if (c == "skip") t = CMD_SKIP;
  else return sendError("Unknown command");
  JsonDocument r;
  r["ok"] = sendCmd(t, arg);
  sendJson(r);
}

// ------------------------------------------------------------ settings
static void handleGetSettings() {
  Settings S;
  {
    Guard g;
    S = gSettings;
  }
  JsonDocument d;
  d["slaveId"] = S.ch[ENGINE_CH].mb.slaveId;
  d["baud"] = S.baud;
  d["parity"] = S.parity;
  d["stopBits"] = S.stopBits;
  d["timeoutMs"] = S.timeoutMs;
  d["pollMs"] = S.pollMs;
  d["profile"] = S.ch[ENGINE_CH].mb.profile;
  JsonObject r = d["reg"].to<JsonObject>();
  r["pv"] = addrOut(S.ch[ENGINE_CH].mb.reg.pv);
  r["spEff"] = addrOut(S.ch[ENGINE_CH].mb.reg.spEff);
  r["out"] = addrOut(S.ch[ENGINE_CH].mb.reg.out);
  r["status"] = addrOut(S.ch[ENGINE_CH].mb.reg.status);
  r["alarm"] = addrOut(S.ch[ENGINE_CH].mb.reg.alarm);
  r["sp"] = addrOut(S.ch[ENGINE_CH].mb.reg.sp);
  r["spLo"] = addrOut(S.ch[ENGINE_CH].mb.reg.spLo);
  r["spHi"] = addrOut(S.ch[ENGINE_CH].mb.reg.spHi);
  r["rampRate"] = addrOut(S.ch[ENGINE_CH].mb.reg.rampRate);
  r["ctrlOff"] = addrOut(S.ch[ENGINE_CH].mb.reg.ctrlOff);
  r["remote"] = addrOut(S.ch[ENGINE_CH].mb.reg.remote);
  r["scale"] = S.ch[ENGINE_CH].mb.reg.scale;
  r["readFn"] = S.ch[ENGINE_CH].mb.reg.readFn;
  r["writeFn"] = S.ch[ENGINE_CH].mb.reg.writeFn;
  r["offRaw"] = S.ch[ENGINE_CH].mb.reg.offRaw;
  d["rampMode"] = S.ch[ENGINE_CH].mb.rampMode;
  d["minStep"] = S.ch[ENGINE_CH].mb.minStep;
  d["minWriteSec"] = S.ch[ENGINE_CH].mb.minWriteSec;
  d["guaranteedSoak"] = S.guaranteedSoak != 0;
  d["soakBand"] = S.soakBand;
  d["endAction"] = S.ch[ENGINE_CH].endAction;
  d["endSp"] = S.ch[ENGINE_CH].endSp;
  d["lockFront"] = S.ch[ENGINE_CH].mb.lockFront != 0;
  d["commLossSec"] = S.commLossSec;
  d["spMin"] = S.ch[ENGINE_CH].spMin;
  d["spMax"] = S.ch[ENGINE_CH].spMax;
  d["brightness"] = S.brightness;
  d["rotate180"] = S.rotate180 != 0;
  d["themePalette"] = S.themePalette;
  d["themeDark"] = S.themeDark != 0;
  JsonArray themes = d["themes"].to<JsonArray>();  // palette names, index = themePalette
  for (const char* p = uiPaletteNames(); *p;) {
    const char* e = strchr(p, '\n');
    const size_t n = e ? (size_t)(e - p) : strlen(p);
    themes.add(String(p).substring(0, n));
    if (!e) break;
    p = e + 1;
  }
  d["hostname"] = S.hostname;
  d["ssid"] = S.ssid;
  d["scanFrom"] = S.scanFrom;
  d["scanTo"] = S.scanTo;
  d["tsMinX"] = S.tsMinX;
  d["tsMaxX"] = S.tsMaxX;
  d["tsMinY"] = S.tsMinY;
  d["tsMaxY"] = S.tsMaxY;
  sendJson(d);
}

template <typename T>
static void upd(JsonVariantConst v, T& field, T lo, T hi) {
  if (v.isNull()) return;
  T x = v.as<T>();
  if (x < lo) x = lo;
  if (x > hi) x = hi;
  field = x;
}

static void handlePostSettings() {
  JsonDocument d;
  if (!parseBody(d)) return sendError("Invalid JSON");
  {
    Guard g;
    Settings& S = gSettings;
    upd<uint8_t>(d["slaveId"], S.ch[ENGINE_CH].mb.slaveId, 1, 247);
    if (!d["baud"].isNull()) {
      const uint32_t b = d["baud"];
      if (b == 2400 || b == 4800 || b == 9600 || b == 19200 || b == 38400) S.baud = b;
    }
    upd<uint8_t>(d["parity"], S.parity, 0, 2);
    upd<uint8_t>(d["stopBits"], S.stopBits, 1, 2);
    upd<uint16_t>(d["timeoutMs"], S.timeoutMs, 50, 2000);
    upd<uint16_t>(d["pollMs"], S.pollMs, 250, 10000);
    if (!d["profile"].isNull()) {
      const uint8_t p = d["profile"].as<uint8_t>() ? PROFILE_GENERIC : PROFILE_PMA_KS4X;
      if (p != S.ch[ENGINE_CH].mb.profile || (d["loadDefaults"] | false)) regMapDefaults(S.ch[ENGINE_CH].mb.reg, p);
      S.ch[ENGINE_CH].mb.profile = p;
    }
    JsonVariantConst r = d["reg"];
    if (!r.isNull()) {
      S.ch[ENGINE_CH].mb.reg.pv = addrIn(r["pv"], S.ch[ENGINE_CH].mb.reg.pv);
      S.ch[ENGINE_CH].mb.reg.spEff = addrIn(r["spEff"], S.ch[ENGINE_CH].mb.reg.spEff);
      S.ch[ENGINE_CH].mb.reg.out = addrIn(r["out"], S.ch[ENGINE_CH].mb.reg.out);
      S.ch[ENGINE_CH].mb.reg.status = addrIn(r["status"], S.ch[ENGINE_CH].mb.reg.status);
      S.ch[ENGINE_CH].mb.reg.alarm = addrIn(r["alarm"], S.ch[ENGINE_CH].mb.reg.alarm);
      S.ch[ENGINE_CH].mb.reg.sp = addrIn(r["sp"], S.ch[ENGINE_CH].mb.reg.sp);
      S.ch[ENGINE_CH].mb.reg.spLo = addrIn(r["spLo"], S.ch[ENGINE_CH].mb.reg.spLo);
      S.ch[ENGINE_CH].mb.reg.spHi = addrIn(r["spHi"], S.ch[ENGINE_CH].mb.reg.spHi);
      S.ch[ENGINE_CH].mb.reg.rampRate = addrIn(r["rampRate"], S.ch[ENGINE_CH].mb.reg.rampRate);
      S.ch[ENGINE_CH].mb.reg.ctrlOff = addrIn(r["ctrlOff"], S.ch[ENGINE_CH].mb.reg.ctrlOff);
      S.ch[ENGINE_CH].mb.reg.remote = addrIn(r["remote"], S.ch[ENGINE_CH].mb.reg.remote);
      upd<float>(r["scale"], S.ch[ENGINE_CH].mb.reg.scale, 1.0f, 1000.0f);
      if (!r["readFn"].isNull()) S.ch[ENGINE_CH].mb.reg.readFn = (r["readFn"].as<int>() == 4) ? 4 : 3;
      if (!r["writeFn"].isNull()) S.ch[ENGINE_CH].mb.reg.writeFn = (r["writeFn"].as<int>() == 16) ? 16 : 6;
      upd<int16_t>(r["offRaw"], S.ch[ENGINE_CH].mb.reg.offRaw, -32768, 32767);
    }
    upd<uint8_t>(d["rampMode"], S.ch[ENGINE_CH].mb.rampMode, 0, 1);
    upd<float>(d["minStep"], S.ch[ENGINE_CH].mb.minStep, 0.1f, 100.0f);
    upd<uint16_t>(d["minWriteSec"], S.ch[ENGINE_CH].mb.minWriteSec, 1, 3600);
    if (!d["guaranteedSoak"].isNull()) S.guaranteedSoak = d["guaranteedSoak"].as<bool>();
    upd<float>(d["soakBand"], S.soakBand, 0.1f, 500.0f);
    upd<uint8_t>(d["endAction"], S.ch[ENGINE_CH].endAction, 0, 2);
    upd<float>(d["endSp"], S.ch[ENGINE_CH].endSp, -1999.0f, 9999.0f);
    if (!d["lockFront"].isNull()) S.ch[ENGINE_CH].mb.lockFront = d["lockFront"].as<bool>();
    upd<uint16_t>(d["commLossSec"], S.commLossSec, 0, 3600);
    upd<float>(d["spMin"], S.ch[ENGINE_CH].spMin, -1999.0f, 9999.0f);
    upd<float>(d["spMax"], S.ch[ENGINE_CH].spMax, -1999.0f, 9999.0f);
    if (S.ch[ENGINE_CH].spMax < S.ch[ENGINE_CH].spMin) S.ch[ENGINE_CH].spMax = S.ch[ENGINE_CH].spMin;
    upd<uint8_t>(d["brightness"], S.brightness, 10, 255);
    if (!d["rotate180"].isNull()) S.rotate180 = d["rotate180"].as<bool>();
    upd<uint8_t>(d["themePalette"], S.themePalette, 0, (uint8_t)(uiPaletteCount() - 1));
    if (!d["themeDark"].isNull()) S.themeDark = d["themeDark"].as<bool>();
    if (d["hostname"].is<const char*>()) strlcpy(S.hostname, d["hostname"], sizeof(S.hostname));
    upd<uint16_t>(d["scanFrom"], S.scanFrom, 0, 65534);
    upd<uint16_t>(d["scanTo"], S.scanTo, 0, 65534);
    upd<uint16_t>(d["tsMinX"], S.tsMinX, 0, 4095);
    upd<uint16_t>(d["tsMaxX"], S.tsMaxX, 0, 4095);
    upd<uint16_t>(d["tsMinY"], S.tsMinY, 0, 4095);
    upd<uint16_t>(d["tsMaxY"], S.tsMaxY, 0, 4095);
    char msg[160];
    if (!settingsValidate(S, boardLimits(), msg, sizeof(msg))) Serial.printf("[settings] %s\n", msg);
  }
  saveSettingsAndNotify();
  handleGetSettings();
}

// ------------------------------------------------------------ settings document (Duo)
// GET: the whole settings JSON (/duo/settings.json without passwords).
// POST: a partial document; only the keys given are changed, e.g.
//   {"channels":[null,{"type":"modbus","modbus":{"addr":2}}]}
static void handleGetConfig() {
  static Settings S;  // static: large; web handlers run in the loop task only
  {
    Guard g;
    S = gSettings;
  }
  JsonDocument d;
  settingsToJson(S, d.to<JsonObject>(), true);
  sendJson(d);
}

static void handlePostConfig() {
  JsonDocument d;
  if (!parseBody(d)) return sendError("Invalid JSON");
  static Settings S;
  char msg[200];
  {
    Guard g;
    S = gSettings;
  }
  if (!settingsFromJson(d.as<JsonObjectConst>(), S, msg, sizeof(msg))) return sendError(msg);
  if (!settingsValidate(S, boardLimits(), msg, sizeof(msg))) return sendError(msg);
  {
    Guard g;
    gSettings = S;
  }
  saveSettingsAndNotify();
  handleGetConfig();
}

static void handleWifi() {
  JsonDocument d;
  if (!parseBody(d)) return sendError("Invalid JSON");
  {
    Guard g;
    if (d["ssid"].is<const char*>()) strlcpy(gSettings.ssid, d["ssid"], sizeof(gSettings.ssid));
    if (d["pass"].is<const char*>()) strlcpy(gSettings.pass, d["pass"], sizeof(gSettings.pass));
    if (d["apPass"].is<const char*>()) {
      const char* ap = d["apPass"];
      if (strlen(ap) >= 8) strlcpy(gSettings.apPass, ap, sizeof(gSettings.apPass));
    }
  }
  saveSettingsQuiet();
  JsonDocument r;
  r["ok"] = true;
  r["msg"] = "Saved. Connecting... If the address changes, reopen the page at the new IP or http://<host name>.local";
  sendJson(r);
  delay(300);  // let the response go out before the station reconnects
  netApplyWifi();
}

// ------------------------------------------------------------ bus tools
// Optional "ch" (query or JSON body): channel whose controller is used, 0 = bottom (default), 1 = top
static uint8_t argCh() {
  const long c = server.hasArg("ch") ? server.arg("ch").toInt() : 0;
  return (c >= 0 && c < NUM_CHANNELS) ? (uint8_t)c : 0;
}
static uint8_t bodyCh(const JsonDocument& b) {
  const int c = b["ch"] | 0;
  return (c >= 0 && c < NUM_CHANNELS) ? (uint8_t)c : 0;
}

static void handleMbRead() {
  BusRequest r{};
  r.ch = argCh();
  r.op = BUS_READ;
  r.addr = server.arg("addr").toInt();
  r.count = server.hasArg("n") ? server.arg("n").toInt() : 1;
  r.fn = server.hasArg("fn") ? server.arg("fn").toInt() : 0;
  if (r.fn != 0 && r.fn != 3 && r.fn != 4) r.fn = 0;
  const bool ok = busRequest(r);
  JsonDocument d;
  d["ok"] = ok;
  if (!ok) d["err"] = r.err;
  JsonArray a = d["values"].to<JsonArray>();
  for (int i = 0; i < r.n; ++i) a.add(r.values[i]);
  sendJson(d);
}

static void handleMbWrite() {
  JsonDocument b;
  if (!parseBody(b)) return sendError("Invalid JSON");
  BusRequest r{};
  r.ch = bodyCh(b);
  r.op = BUS_WRITE;
  r.addr = b["addr"] | 0;
  r.value = b["value"] | 0;
  const bool ok = busRequest(r);
  JsonDocument d;
  d["ok"] = ok;
  if (!ok) d["err"] = r.err;
  sendJson(d);
}

static void handleFindSp() {
  JsonDocument b;
  parseBody(b);
  BusRequest r{};
  r.ch = bodyCh(b);
  r.op = BUS_FINDSP;
  {
    Guard g;
    r.from = b["from"] | gSettings.scanFrom;
    r.to = b["to"] | gSettings.scanTo;
  }
  const bool ok = busRequest(r, 60000);
  JsonDocument d;
  d["ok"] = ok;
  if (!ok) d["err"] = r.err;
  d["reference"] = r.refValue;
  JsonArray a = d["candidates"].to<JsonArray>();
  for (int i = 0; i < r.n; ++i) a.add((uint16_t)r.values[i]);
  sendJson(d);
}

// ------------------------------------------------------------ controller parameters
static void handleGetParams() {
  uint8_t profile;
  float scale;
  const uint8_t ch = argCh();
  {
    Guard g;
    profile = gSettings.ch[ch].mb.profile;
    scale = gSettings.ch[ch].mb.reg.scale;
  }
  JsonDocument d;
  d["supported"] = profile == PROFILE_PMA_KS4X;
  JsonArray pa = d["params"].to<JsonArray>();
  JsonArray aa = d["actions"].to<JsonArray>();
  if (profile == PROFILE_PMA_KS4X) {
    int n;
    const ParamDef* p = pmaParams(n);
    for (int i = 0; i < n; ++i) {
      JsonObject o = pa.add<JsonObject>();
      o["name"] = p[i].name;
      o["addr"] = p[i].addr;
      o["scaled"] = p[i].scaled != 0;
      o["desc"] = p[i].desc;
      BusRequest r{};
      r.ch = ch;
      r.op = BUS_READ;
      r.addr = p[i].addr;
      r.count = 1;
      if (busRequest(r, 3000)) {
        const int16_t raw = r.values[0];
        if (Controller::classify(raw) == VAL_OFF) o["value"] = "off";
        else if (p[i].scaled) o["value"] = raw / scale;
        else o["value"] = raw;
      } else {
        o["err"] = r.err;
      }
    }
    const ActionDef* a = pmaActions(n);
    for (int i = 0; i < n; ++i) aa.add(a[i].label);
  }
  sendJson(d);
}

static void handlePostParam() {
  JsonDocument b;
  if (!parseBody(b)) return sendError("Invalid JSON");
  float scale;
  const uint8_t ch = bodyCh(b);
  {
    Guard g;
    scale = gSettings.ch[ch].mb.reg.scale;
  }
  BusRequest r{};
  r.ch = ch;
  r.op = BUS_WRITE;
  r.addr = b["addr"] | 0;
  const bool scaled = b["scaled"] | false;
  const String v = b["value"].is<const char*>() ? String((const char*)b["value"]) : String(b["value"].as<float>());
  if (v == "off") r.value = -32000;
  else r.value = (int16_t)lroundf(v.toFloat() * (scaled ? scale : 1.0f));
  // only allow addresses from the parameter table
  int n;
  const ParamDef* p = pmaParams(n);
  bool allowed = false;
  for (int i = 0; i < n; ++i) allowed |= p[i].addr == r.addr && strcmp(p[i].name, "Sw.Op") != 0;
  if (!allowed) return sendError("Address is not an editable parameter");
  const bool ok = busRequest(r);
  JsonDocument d;
  d["ok"] = ok;
  if (!ok) d["err"] = r.err;
  sendJson(d);
}

static void handleAction() {
  JsonDocument b;
  if (!parseBody(b)) return sendError("Invalid JSON");
  int n;
  const ActionDef* a = pmaActions(n);
  const int i = b["i"] | -1;
  if (i < 0 || i >= n) return sendError("Unknown action");
  BusRequest r{};
  r.ch = bodyCh(b);
  r.op = BUS_WRITE;
  r.addr = a[i].addr;
  r.value = a[i].value;
  const bool ok = busRequest(r);
  JsonDocument d;
  d["ok"] = ok;
  if (!ok) d["err"] = r.err;
  sendJson(d);
}

// ------------------------------------------------------------ setup
void webBegin() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", WEB_PAGE); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/log.csv", HTTP_GET, handleCsv);
  server.on("/api/programs", HTTP_GET, handleGetPrograms);
  server.on("/api/program", HTTP_GET, handleGetProgram);
  server.on("/api/program", HTTP_POST, handlePostProgram);
  server.on("/api/select", HTTP_POST, handleSelect);
  server.on("/api/manual", HTTP_POST, handleManual);
  server.on("/api/cmd", HTTP_POST, handleCmd);
  server.on("/api/ack", HTTP_POST, handleAck);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/mb/read", HTTP_GET, handleMbRead);
  server.on("/api/mb/write", HTTP_POST, handleMbWrite);
  server.on("/api/mb/findsp", HTTP_POST, handleFindSp);
  server.on("/api/params", HTTP_GET, handleGetParams);
  server.on("/api/params", HTTP_POST, handlePostParam);
  server.on("/api/action", HTTP_POST, handleAction);
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void webLoop() { server.handleClient(); }
