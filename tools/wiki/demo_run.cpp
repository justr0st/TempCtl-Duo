// Demo data for the wiki screenshots (tools/wiki/make_shots.sh): runs one of the example programs with
// the firmware's own run engine (src/run_engine.cpp) against a simple heater model and prints what the
// device would answer at that moment: the run chart (/api/chart?v=run: history, lanes, events,
// forecast, as app_state.cpp chartJson) and the run / channel fields of /api/live (as web.cpp
// handleLive). tools/wiki/mock/server.py serves them.
//   g++ -std=c++17 -O1 -Isrc -I<ArduinoJson>/src tools/wiki/demo_run.cpp src/run_engine.cpp
//       src/duo_program.cpp src/settings.cpp -o demo_run
//   ./demo_run <example 0..2> <run time s> [pause-continue s]   > demo.json
// Pause phases / steps are continued after [pause-continue] s (default 10). Deterministic (fixed noise).
//   ./demo_run config     the default settings (S3: RS485 + two local channels) as GET /api/config
//   ./demo_run examples   the example programs (first start) as /api/program JSON
//   ./demo_run zones      the time zone names (GET /api/timezones)
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "duo_program.h"
#include "run_engine.h"

static constexpr int N = 300;  // chart points (CHART_N)
static const char CODE[] = "-orhwjkxpd";  // ChState -> lane code (app_state.cpp CH_STATE_CODE)

struct Heater {  // first order towards the setpoint, heating rate limited, cools to 25 degC when off
  float pv, tau, maxUp, gain;
  float out = 0;
  void step(float sp, bool on, float dt, float noise) {
    float d;
    if (on && !isnan(sp)) {
      d = (sp - pv) / tau;
      if (d > maxUp) d = maxUp;
      if (d < -(pv - 25) / 90) d = -(pv - 25) / 90;
      out = fminf(100, fmaxf(0, (sp - pv) * gain + (sp - 25) / 3.2f));
    } else {
      d = -(pv - 25) / 240;
      out = 0;
    }
    pv += d * dt + noise;
  }
};

struct Rec {
  std::vector<PlanEv> ev;
};
static void onEv(const PlanEv& e, void* u) { ((Rec*)u)->ev.push_back(e); }

struct Fc {  // forecast pieces merged as app_state.cpp fcSeg
  std::vector<PlanSeg> seg[2];
  std::vector<PlanEv> ev;
};
static void fcSeg(const PlanSeg& g, void* u) {
  Fc& k = *(Fc*)u;
  auto& v = k.seg[g.ch];
  if (!v.empty()) {
    PlanSeg& l = v.back();
    bool same = l.idle == g.idle && l.ramp == g.ramp && l.src == g.src && fabsf(l.t1 - g.t0) < 0.01f;
    if (same && !g.idle) {
      if (!g.ramp) same = l.v1 == g.v0 && g.v0 == g.v1;
      else {
        const float s1 = (l.v1 - l.v0) / fmaxf(l.t1 - l.t0, 1e-3f), s2 = (g.v1 - g.v0) / fmaxf(g.t1 - g.t0, 1e-3f);
        same = fabsf(l.v1 - g.v0) < 0.05f && fabsf(s1 - s2) < 1e-3f;
      }
    }
    if (same) {
      l.t1 = g.t1;
      l.v1 = g.v1;
      return;
    }
  }
  v.push_back(g);
}
static void fcEv(const PlanEv& e, void* u) {
  if (e.type != EV_STEP) ((Fc*)u)->ev.push_back(e);
}

static float r1(float v) { return roundf(v * 10) / 10; }
template <class V>
static void num(V v, float x) {
  if (isnan(x)) v.set(nullptr);
  else v.set(r1(x));
}
static void stage(JsonObject o, const StageInfo& s) {
  o["idx"] = s.idx;
  o["stage"] = stageName(s.st);
  o["el"] = r1(s.el);
  o["dur"] = r1(s.dur);
}

int main(int argc, char** argv) {
  if (argc > 1 && (!strcmp(argv[1], "config") || !strcmp(argv[1], "examples") || !strcmp(argv[1], "zones"))) {
    JsonDocument d;
    if (argv[1][0] == 'c') {
      static Settings s;
      const BoardLimits lim{true, 2};
      settingsDefaults(s, lim);
      settingsToJson(s, d.to<JsonObject>(), true);
    } else if (argv[1][0] == 'e') {
      JsonArray a = d.to<JsonArray>();
      for (int i = 0; i < PROG_EXAMPLES; ++i) {
        static DuoProgram p;
        progExample(i, p);
        progToJson(p, a.add<JsonObject>());
      }
    } else {
      JsonArray a = d.to<JsonArray>();
      for (int i = 0; i < tzCount(); ++i) a.add(tzListName(i));
    }
    std::string out;
    serializeJson(d, out);
    puts(out.c_str());
    return 0;
  }
  const int ex = argc > 1 ? atoi(argv[1]) : 0;
  const float T = argc > 2 ? (float)atof(argv[2]) : 200;
  const float contAfter = argc > 3 ? (float)atof(argv[3]) : 10;
  static DuoProgram p;
  progExample(ex, p);
  Heater h[2] = {{24.6f, 9, 3.0f, 6}, {25.4f, 6, 4.0f, 3}};
  float pv[2] = {h[0].pv, h[1].pv};
  bool ok[2] = {true, true};
  RunEngine e;
  Rec rec;
  e.setEventSink(onEv, &rec);
  e.start(&p, pv, ok, 5);
  // chart span as the firmware (runSpanFor): planned time + 10 % (at least 30 s)
  Fc f0;
  const float total = e.forecast(fcSeg, &f0, pv);
  float span = total + fmaxf(30, total * 0.1f), bin = span / N;
  float cpv[2][N], clo[2][N], chi[2][N], csp[2][N];
  char st[2][N + 1];
  for (int c = 0; c < 2; ++c) {
    for (int i = 0; i < N; ++i) cpv[c][i] = clo[c][i] = chi[c][i] = csp[c][i] = NAN;
    memset(st[c], ' ', N);
    st[c][N] = 0;
  }
  int last = -1;
  uint32_t seed = 12345;
  auto rnd = [&]() {
    seed = seed * 1103515245u + 12345u;
    return ((seed >> 16) & 0x7FFF) / 32767.0f - 0.5f;
  };
  std::vector<float> rows;  // once a second: run time, then per channel PV, setpoint (NaN = off), output
  float pausedFor = 0;
  const float dt = 0.25f;
  for (float t = 0; t < T && e.active(); t += dt) {
    if (e.progPaused()) {
      pausedFor += dt;
      if (pausedFor >= contAfter) {
        e.cont();
        pausedFor = 0;
      }
    }
    e.tick(dt, pv, ok);
    for (int c = 0; c < 2; ++c) {
      const ChOut o = e.out(c);
      h[c].step(o.sp, o.inProgram && !o.idle, dt, rnd() * (c ? 0.5f : 0.25f));
      pv[c] = h[c].pv;
    }
    if (fmodf(t + dt, 1.0f) < dt / 2) {  // once a second, as the control task's history
      Fc tmp;  // the run takes longer than planned (waits): half the resolution, twice the span (histCompress)
      for (int k = 0; k < 20 && e.elapsed() + e.forecast(fcSeg, &tmp, pv) > span; ++k) {
        for (int c = 0; c < 2; ++c)
          for (int i = 0; i < N / 2; ++i) {
            const int a = 2 * i, b = 2 * i + 1;
            cpv[c][i] = isnan(cpv[c][b]) ? cpv[c][a] : cpv[c][b];
            csp[c][i] = isnan(cpv[c][b]) ? csp[c][a] : csp[c][b];
            clo[c][i] = isnan(clo[c][a]) ? clo[c][b] : isnan(clo[c][b]) ? clo[c][a] : fminf(clo[c][a], clo[c][b]);
            chi[c][i] = isnan(chi[c][a]) ? chi[c][b] : isnan(chi[c][b]) ? chi[c][a] : fmaxf(chi[c][a], chi[c][b]);
            st[c][i] = st[c][b] != ' ' ? st[c][b] : st[c][a];
          }
        for (int c = 0; c < 2; ++c)
          for (int i = N / 2; i < N; ++i) {
            cpv[c][i] = csp[c][i] = clo[c][i] = chi[c][i] = NAN;
            st[c][i] = ' ';
          }
        bin *= 2;
        span *= 2;
        last = last / 2;
        tmp = Fc();
      }
      int idx = (int)(e.elapsed() / bin);
      if (idx >= N) idx = N - 1;
      for (int c = 0; c < 2; ++c) {
        const bool fresh = idx > last;
        cpv[c][idx] = pv[c];
        clo[c][idx] = fresh || isnan(clo[c][idx]) ? pv[c] : fminf(clo[c][idx], pv[c]);
        chi[c][idx] = fresh || isnan(chi[c][idx]) ? pv[c] : fmaxf(chi[c][idx], pv[c]);
        const ChOut o = e.out(c);
        csp[c][idx] = o.inProgram && !o.idle ? o.sp : NAN;
        const ChState cs = e.chState(c);
        const char code = cs == ChState::None ? '-' : CODE[(int)cs];
        for (int k = last + 1; k <= idx; ++k) st[c][k] = code;
      }
      last = idx;
      rows.push_back(e.elapsed());
      for (int c = 0; c < 2; ++c) {
        const ChOut o = e.out(c);
        rows.push_back(pv[c]);
        rows.push_back(o.inProgram && !o.idle ? o.sp : NAN);
        rows.push_back(h[c].out);
      }
    }
  }
  JsonDocument d;
  // /api/chart?v=run
  JsonObject ch = d["chart"].to<JsonObject>();
  Fc f;
  const float rem = e.active() ? e.forecast(fcSeg, &f, pv, fcEv, &f) : 0;
  ch["mode"] = e.active() ? "run" : "last";
  ch["gen"] = 1;
  ch["name"] = p.name;
  ch["bin"] = roundf(bin * 1000) / 1000;
  ch["span"] = r1(span);
  ch["el"] = r1(e.elapsed());
  ch["rem"] = r1(rem);
  const char* keys[] = {"pv", "sp", "dlo", "dhi"};
  for (int k = 0; k < 4; ++k) {
    JsonArray a = ch[keys[k]].to<JsonArray>();
    for (int c = 0; c < 2; ++c) {
      JsonArray b = a.add<JsonArray>();
      for (int i = 0; i < N; ++i) {
        if (k == 0) num(b.add<JsonVariant>(), cpv[c][i]);
        else if (k == 1) num(b.add<JsonVariant>(), csp[c][i]);
        else {
          const float x = k == 2 ? cpv[c][i] - clo[c][i] : chi[c][i] - cpv[c][i];
          b.add(isnan(x) ? 0 : (int)lroundf(x * 10));
        }
      }
    }
  }
  JsonArray sa = ch["st"].to<JsonArray>();
  for (int c = 0; c < 2; ++c) sa.add(st[c]);
  auto evArr = [](JsonArray a, const std::vector<PlanEv>& v) {
    for (const PlanEv& x : v) {
      JsonArray r = a.add<JsonArray>();
      r.add(r1(x.t));
      r.add(x.type);
      r.add(x.ch == 0xFF ? -1 : (int)x.ch);
      r.add(x.idx);
    }
  };
  evArr(ch["ev"].to<JsonArray>(), rec.ev);
  JsonObject fc = ch["fc"].to<JsonObject>();
  JsonArray fs = fc["seg"].to<JsonArray>();
  for (int c = 0; c < 2; ++c) {
    JsonArray a = fs.add<JsonArray>();
    for (const PlanSeg& g : f.seg[c]) {
      JsonArray r = a.add<JsonArray>();
      r.add(r1(g.t0));
      r.add(r1(g.t1));
      num(r.add<JsonVariant>(), g.idle ? NAN : g.v0);
      num(r.add<JsonVariant>(), g.idle ? NAN : g.v1);
      r.add(g.src);
      r.add((g.idle ? 1 : 0) | (g.ramp ? 2 : 0));
    }
  }
  evArr(fc["ev"].to<JsonArray>(), f.ev);
  // /api/live: run + per channel
  JsonObject r = d["run"].to<JsonObject>();
  r["state"] = runStateName(e.state());
  r["prog"] = ex;
  r["name"] = p.name;
  r["type"] = progTypeName(p.type);
  r["el"] = r1(e.elapsed());
  r["rem"] = r1(rem);
  r["progPause"] = e.progPaused();
  r["actionRequired"] = e.progPaused() && e.pauseMsg();
  r["pauseText"] = e.pauseText();
  r["edited"] = false;
  r["waiting"] = e.waiting();
  if (p.type == PROG_SEQ) {
    stage(r["seq"].to<JsonObject>(), e.seq());
    r["phases"] = p.nPh;
  }
  r["msg"] = "";
  JsonArray a = d["ch"].to<JsonArray>();
  for (int c = 0; c < 2; ++c) {
    const ChOut o = e.out(c);
    JsonObject x = a.add<JsonObject>();
    num(x["pv"], pv[c]);
    num(x["sp"], o.inProgram && !o.idle ? o.sp : NAN);
    num(x["out"], h[c].out);
    x["outOn"] = o.inProgram && !o.idle;
    x["inProgram"] = o.inProgram;
    x["state"] = chStateName(e.chState(c));
    x["idle"] = o.idle;
    num(x["runSp"], o.sp);
    num(x["target"], o.target);
    x["moving"] = o.moving;
    x["kept"] = e.keptIdx(c);
    x["canExtend"] = e.canExtend(c);
    if (p.type == PROG_TL) {
      stage(x["lane"].to<JsonObject>(), e.lane(c));
      x["steps"] = p.nSt[c];
    }
  }
  JsonArray ra = d["rows"].to<JsonArray>();
  for (float v : rows) num(ra.add<JsonVariant>(), v);
  JsonObject pj = d["program"].to<JsonObject>();
  progToJson(p, pj);
  std::string out;
  serializeJson(d, out);
  puts(out.c_str());
  return 0;
}
