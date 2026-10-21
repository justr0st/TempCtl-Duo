// Host stand-ins for the firmware's app layer + screenshot scenarios of the touch UI.
#include "app_state.h"
#include "lcd.h"
#include "net.h"
#include "settings_store.h"
#include "touch.h"
#include "ui.h"
#include "local_ctrl.h"
#include "tui.h"
#include "version.h"
#include <vector>
#ifdef MEMTEST
#include <malloc.h>
#include "src/display/lv_display_private.h"
void trackStart();
void trackReport();
#endif

SerialT Serial;
#include "console.h"
LogPort LogSerial;  // (console.cpp is not part of the UI preview)
size_t LogPort::write(const uint8_t* b, size_t n) { return fwrite(b, 1, n, stdout); }
EspT ESP;
static uint32_t now_ = 1000;
uint32_t millis() { return now_; }
void delay(uint32_t ms) { now_ += ms; }

Settings gSettings;
LiveData gLive;
History* gHist = nullptr;
SemaphoreHandle_t gLock = nullptr;

// SSR supply relay (local_ctrl.h): the S3 has one, on while the bottom is in manual
bool localCtrlRelayWired() { return SCREEN_W > 400; }
bool localCtrlRelayOn() { return gLive.ch[1].outOn; }
bool localCtrlRelayUsed() { return localCtrlRelayWired() && gSettings.relay; }
void localCtrlRelayEnable(bool) {}

// breadcrumbs (crumbs.h): nothing to keep on a PC
#include "crumbs.h"
void crumb(CrumbSlot, CrumbCode, int16_t) {}
CrumbCode crumbGet(CrumbSlot) { return L_END; }

bool sendCmd(CmdType c, int arg, float value, int idx, uint8_t sub) {
  printf("[host] cmd %d arg %d value %.1f idx %d sub %d\n", (int)c, arg, value, idx, sub);
  return true;
}
void selectProgram(int idx) { gSettings.selProgram = idx; }
// the control task takes a new manual setpoint over in a later cycle (host: hostSpLag ms after it was set)
static float hostSpNew[2] = {NAN, NAN};
static uint32_t hostSpAt[2];
static uint32_t hostSpLag = 0;
void setManualSp(float v, int ch) {
  gSettings.ch[ch].manualSp = v;
  printf("[host] manual sp %d = %.1f\n", ch, v);
  if (!hostSpLag) gLive.ch[ch].manualSp = v;
  else hostSpNew[ch] = v, hostSpAt[ch] = now_;
}
void saveSettingsQuiet() {}
void saveSettingsAndNotify() {}
static uint32_t hostGen = 1;
uint32_t settingsGen() { return hostGen; }
BoardLimits boardLimits() { return BoardLimits{true, 2}; }
uint32_t liveHistSec() { return 86400; }
const uint16_t LIVE_PERIODS[LIVE_PERIODS_N] = {1, 5, 10, 15, 30, 60, 120, 240, 480, 720, 1440};
uint32_t liveMinEff() { return gSettings.liveMin ? gSettings.liveMin : 5; }
void appRequestRestart(const char* from) { printf("[host] restart %s\n", from); }
void logEvent(const char* fmt, ...) {}
void logWarn(const char* fmt, ...) {}
void logError(const char* fmt, ...) {}
void logKind(LogKind, const char* fmt, ...) {}

static std::vector<LogEntry> logv;
uint32_t logSeq() { return logv.size(); }
int logSince(uint32_t seq, LogEntry* out, int max) {
  int n = 0;
  for (size_t i = seq; i < logv.size() && n < max; ++i) out[n++] = logv[i];
  return n;
}
static void addLog(uint32_t up, float rt, const char* m) {
  LogEntry e{};
  e.seq = logv.size() + 1;
  e.upSec = up;
  e.runT = rt;
  strlcpy(e.msg, m, sizeof(e.msg));
  logv.push_back(e);
}

// programs
static ProgSummary sums[PROG_MAX];
void programSummaries(ProgSummary* out) { memcpy(out, sums, sizeof(sums)); }
bool programSummary(int idx, ProgSummary& out) {
  if (idx < 0 || idx >= PROG_MAX) return false;
  out = sums[idx];
  return out.used;
}

static bool hostTune = false;
void localCtrlTuneGet(int ch, LocalTune& t) {
  memset(&t, 0, sizeof(t));
  t.state = hostTune && ch == 0 ? TuneState::Running : TuneState::Idle;
  t.sp = 200;
  t.cycle = 2;
}
void localCtrlTuneClear(int) {}
bool busRequest(BusRequest& r, uint32_t) { r.values[0] = 1234; return true; }
static uint32_t storeGen = 1;
uint32_t programStoreGen() { return storeGen; }
bool programLoad(int idx, DuoProgram& p, char* err, size_t n) {
  if (idx < 3) { progExample(idx, p); return true; }
  snprintf(err, n, "empty");
  return false;
}
bool programSave(int idx, const DuoProgram& p, char* err, size_t n) { ++storeGen; printf("[host] save %d %s\n", idx, p.name); return true; }
bool programDelete(int idx) { ++storeGen; return true; }
bool startDraft(int idx, const DuoProgram& p) { printf("[host] start draft %d\n", idx); return true; }
bool runProgramCopy(DuoProgram& out) { progExample(0, out); return true; }

// chart: a run of example 1 (Lead-free BGA), 540 s, now at 400 s
static bool runMode = false;
static uint32_t liveShift = 0;
void chartSnapshot(ChartData& d, bool live) {
  memset(&d, 0, sizeof(d));
  for (int i = 0; i < CHART_N; ++i)
    d.pv[i] = d.sp[i] = d.plan[i] = d.pvT[i] = d.spT[i] = d.planT[i] = NAN;
#ifdef CHART_LOHI
  for (int i = 0; i < CHART_N; ++i) d.lo[i] = d.hi[i] = d.loT[i] = d.hiT[i] = NAN;
#endif
  auto spB = [](float t) -> float { return t < 60 ? 25 + 125 * t / 60 : t < 222 ? 150 : t < 482 ? 160 : NAN; };
  auto spT = [](float t) -> float {
    if (t < 222) return NAN;
    if (t < 282) return 25 + 125 * (t - 222) / 60;
    if (t < 302) return 150;
    if (t < 342) return 150 + 40 * (t - 302) / 40;
    if (t < 382) return 190;
    if (t < 412) return 190 + 35 * (t - 382) / 30;
    if (t < 432) return 225;
    if (t < 452) return 225 + 20 * (t - 432) / 20;
    if (t < 482) return 245;
    if (t < 522) return 245 - 95 * (t - 482) / 40;
    return 150;
  };
  const bool run = runMode && !live;
  d.run = run;
  if (live) {
    d.binSec = 1;
    d.span = 300;
    for (int i = 0; i < CHART_N; ++i) {
      // absolute sample number: the window moves by liveShift seconds (scroll test)
      const uint32_t n = i + liveShift;
      const float noise = (float)((n * 2654435761u) >> 24 & 0xFF) / 255.0f - 0.5f;
      float pb = 23 + noise * 1.2f;
      const int a = (int)n - 60;  // a 25 s heat burst with a jagged top
      if (a >= 0 && a < 25) pb = 24 + 20 * sinf(a * 3.14159f / 25) + ((a % 4) == 1 ? 4 : 0) + noise * 3;
      d.pv[i] = pb;
#ifdef CHART_LOHI
      d.lo[i] = pb - 0.3f - fabsf(noise);
      d.hi[i] = pb + 0.3f + (a == 12 ? 6 : 0);
#endif
      d.pvT[i] = NAN;
      d.sp[i] = 20;
      d.spT[i] = NAN;
    }
    return;
  }
  const float span = 600, bin = span / CHART_N;
  d.binSec = run ? bin : 1;
  d.span = run ? span : 300;
  d.planSpan = span;
  d.planTotal = 540;
  d.elapsed = 400;
  float pb = 25, pt = 25;
  for (int i = 0; i < CHART_N; ++i) {
    const float t = (i + 0.5f) * bin;
    const float b = spB(t), tt = spT(t);
    d.plan[i] = b;
    d.planT[i] = tt;
    if (run && t <= 400) {
      d.sp[i] = b;
      d.spT[i] = tt;
      pb += ((isnan(b) ? 25 : b) - pb) * 0.08f;
      pt += ((isnan(tt) ? 25 : tt) - pt) * 0.25f;
      d.pv[i] = pb;
      d.pvT[i] = pt;
    }
  }
}

// net
String netInfo() { return String("Wi-Fi \"Workshop\" \xC2\xB7 192.168.1.50 \xC2\xB7 -58 dBm"); }
void netApplyWifi() {}
void netScanStart() {}
bool netScanBusy() { return false; }
int netScanResult(char* out, size_t n) {
  strlcpy(out, "Workshop\nWorkshop-5G\nNeighbour\nFRITZ!Box 7590 XY", n);
  return 4;
}

// display / touch
static uint16_t fb[SCREEN_W * SCREEN_H];
static lv_display_t* hostDisp;
static void flushCb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const int w = lv_area_get_width(a);
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++) fb[y * SCREEN_W + x] = ((uint16_t*)px)[(y - a->y1) * w + (x - a->x1)];
  lv_display_flush_ready(d);
}
lv_display_t* lcdInit(bool) {
  hostDisp = lv_display_create(SCREEN_W, SCREEN_H);
  static uint16_t buf[SCREEN_W * 40];
  lv_display_set_buffers(hostDisp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(hostDisp, flushCb);
  return hostDisp;
}
void lcdSetRotation(bool) {}
bool lcdReinit() { return true; }
int lcdCheck(char* i, size_t n) { snprintf(i, n, "host"); return 1; }
bool lcdTestBreak() { return true; }
void lcdSetBrightness(uint8_t) {}
static uint8_t hostSpi = 40;
static bool hostPerf = false;
bool lcdSetSpiMhz(uint8_t m) { hostSpi = m; return true; }
uint8_t lcdSpiMhz() { return hostSpi; }
void lcdPerf(bool on) { hostPerf = on; }
bool lcdPerfOn() { return hostPerf; }
void lcdPerfChart(uint32_t) {}
static int tx = -1, ty = -1;
static int backupsMade = 0;  // (backupCreate stub: the slide-off test)
bool touchInit(const TouchCal&) { return true; }
void touchSetRotation(bool) {}
bool touchRead(int32_t* x, int32_t* y) {
  if (tx < 0) return false;
  *x = tx;
  *y = ty;
  return true;
}
// raw values like an XPT2046 (x 200 ... 3850, y reversed 3700 ... 300) for the calibration test
void touchLastRaw(int32_t* x, int32_t* y) {
  *x = tx < 0 ? -1 : 200 + tx * 3650 / (SCREEN_W - 1);
  *y = ty < 0 ? -1 : 3700 - ty * 3400 / (SCREEN_H - 1);
}
void touchSetCal(const TouchCal&) {}
bool touchReset(const TouchCal&) { return true; }
int touchCheck(char* i, size_t n) { snprintf(i, n, "host"); return -1; }
void touchStats(char* i, size_t n) { snprintf(i, n, "host"); }

// ------------------------------------------------------------ scenarios
static void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 10) {
    now_ += 10;
    for (int c = 0; c < 2; ++c)
      if (!isnan(hostSpNew[c]) && now_ - hostSpAt[c] >= hostSpLag) gLive.ch[c].manualSp = hostSpNew[c], hostSpNew[c] = NAN;
    lv_timer_handler();
  }
}
#define SMALL (SCREEN_W < 400)
static bool otaOk = false;  // otaCanStart (stub below)
void chartBench(int n, double& lvglUs, double& rasterUs);
static void tap(int x, int y) {
  tx = x, ty = y;
  run(80);
  tx = ty = -1;
  run(300);
}
// press at (x, y), slide to (x2, y2) in steps, release there
static void slide(int x, int y, int x2, int y2) {
  tx = x, ty = y;
  run(80);
  for (int k = 1; k <= 8; ++k) {
    tx = x + (x2 - x) * k / 8, ty = y + (y2 - y) * k / 8;
    run(30);
  }
  run(50);
  tx = ty = -1;
  run(300);
}
// UIHOST_THEME=light: all shots in the light theme, into shots_light/ (shots_cyd_light/), for the wiki
static const bool hostLight = getenv("UIHOST_THEME") && !strcmp(getenv("UIHOST_THEME"), "light");

static void shot(const char* name) {
  run(300);
  lv_refr_now(hostDisp);
  char path[128];
  snprintf(path, sizeof(path), "%s%s/%s.raw", SCREEN_W < 400 ? "shots_cyd" : "shots", hostLight ? "_light" : "", name);
  FILE* f = fopen(path, "wb");
  fwrite(fb, 2, SCREEN_W * SCREEN_H, f);
  fclose(f);
  printf("[host] %s\n", path);
}

// centre of the object (under `o`) showing the label text `t`; false if none
static bool findText(lv_obj_t* o, const char* t, int& x, int& y) {
  if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), t) && !lv_obj_is_hidden(o)) {
    lv_area_t a;
    lv_obj_get_coords(lv_obj_get_parent(o), &a);
    x = (a.x1 + a.x2) / 2;
    y = (a.y1 + a.y2) / 2;
    return true;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i)
    if (findText(lv_obj_get_child(o, i), t, x, y)) return true;
  return false;
}
// the label showing `t` (nullptr if none)
static lv_obj_t* findLabel(lv_obj_t* o, const char* t) {
  if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), t)) return o;
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i)
    if (lv_obj_t* r = findLabel(lv_obj_get_child(o, i), t)) return r;
  return nullptr;
}
static bool visible(lv_obj_t* o) {
  for (; o; o = lv_obj_get_parent(o))
    if (lv_obj_is_hidden(o)) return false;
  return true;
}
static int nthSlider(lv_obj_t* o, int& n, lv_area_t& a) {  // the n-th slider under o (depth first)
  if (lv_obj_check_type(o, &lv_slider_class) && n-- == 0) {
    lv_obj_get_coords(o, &a);
    return 1;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i)
    if (nthSlider(lv_obj_get_child(o, i), n, a)) return 1;
  return 0;
}
static void tapSlider(int idx, float frac) {
  lv_area_t a;
  int n = idx;
  if (!nthSlider(lv_layer_top(), n, a)) return (void)printf("[host] no slider %d\n", idx);
  tap(a.x1 + (int)((a.x2 - a.x1) * frac), (a.y1 + a.y2) / 2);
}

static void baseState() {
  settingsDefaults(gSettings, boardLimits());
  strcpy(gSettings.ch[0].name, "Bottom");
  strcpy(gSettings.ch[1].name, "Top");
  gSettings.ch[0].type = CH_LOCAL;
  gSettings.ch[1].type = CH_MODBUS;
  gSettings.themeDark = hostLight ? 0 : 1;
  strcpy(gSettings.ssid, "Workshop");
  strcpy(gSettings.pass, "secret");
  memset(&gLive, 0, sizeof(gLive));
  gLive.runProg = -1;
  for (int i = 0; i < 2; ++i) {
    ChannelLive& c = gLive.ch[i];
    strcpy(c.name, gSettings.ch[i].name);
    c.type = gSettings.ch[i].type;
    c.commOk = true;
    c.pvValid = true;
    c.pv = i ? 25.4f : 24.8f;
    c.manualSp = 120;
    c.spEff = NAN;
    c.out = 0;
    c.outValid = true;
    gLive.run[i].target = gLive.run[i].sp = NAN;
    gLive.kept[i] = -1;
  }
  gLive.ch[1].manualOn = true;
  gLive.ch[1].outOn = true;
  gLive.ch[0].outOn = false;
  gLive.ch[1].manualSp = 150;
  gLive.ch[1].spEffValid = true;
  gLive.ch[1].spEff = 150;
  gLive.ch[1].pv = 148.6f;
  gLive.ch[1].out = 35;
  gSettings.ch[1].manualSp = 150;
  sums[0] = {"Lead-free BGA", 1, PROG_SEQ, 3, 1, 7, {0, 0}, 420};
  sums[1] = {"Leaded BGA", 1, PROG_SEQ, 3, 1, 6, {0, 0}, 375};
  sums[2] = {"Lead-free BGA (Timeline)", 1, PROG_TL, 3, 0, 9, {3, 6}, 450};
  runMode = false;
}

static void runningState() {
  runMode = true;
  gLive.state = RunState::Running;
  gLive.runProg = 0;
  strcpy(gLive.runName, "Lead-free BGA");
  gLive.progType = PROG_SEQ;
  gLive.phases = 7;
  gLive.seq = {4, Stage::Ramp, 10, 20};
  gLive.elapsed = 400;
  gLive.remaining = 140;
  gLive.ch[1].manualOn = false;
  for (int i = 0; i < 2; ++i) {
    gLive.ch[i].outOn = true;
    gLive.run[i].inProgram = true;
    gLive.run[i].idle = false;
  }
  gLive.ch[0].pv = 159.4f;
  gLive.ch[0].spEffValid = true;
  gLive.ch[0].spEff = 160;
  gLive.ch[0].out = 31;
  gLive.run[0] = {true, false, false, 160, 160, 0, 1};
  gLive.chSt[0] = (uint8_t)ChState::Keep;
  gLive.kept[0] = 2;
  gLive.ch[1].pv = 206.8f;
  gLive.ch[1].spEff = 211;
  gLive.ch[1].out = 82;
  gLive.run[1] = {true, false, true, 211, 225, 14, 1};
  gLive.chSt[1] = (uint8_t)ChState::Ramp;
}

static void monkey(int n) {
  srand(7);
  for (int i = 0; i < n; ++i) {
    const int r = rand() % 100;
    if (r < 3) tShow((TScreen)(rand() % SCR_COUNT));
    else if (r < 5) runningState();
    else if (r < 7) baseState();
    else if (r < 8) { gLive.progPause = !gLive.progPause; gLive.pauseMsg = gLive.progPause; }
    else if (r < 9) { gLive.intrValid = !gLive.intrValid; strcpy(gLive.intrReason, "watchdog"); }
    else if (r < 10) { gLive.ch[rand() % 2].fault = rand() % 2; }
    else if (r < 11) { gLive.state = RunState::Finished; gLive.runEdited = true; }
    else if (r < 12) { lv_obj_t* b = lv_obj_get_child(lv_screen_active(), 1); if (b) lv_obj_scroll_by(b, 0, (rand() % 400) - 200, LV_ANIM_OFF); }
    else {
      tx = rand() % SCREEN_W;
      ty = rand() % SCREEN_H;
      run(30 + rand() % 120);
      tx = ty = -1;
    }
    run(20 + rand() % 100);
  }
  printf("[host] monkey done\n");
}

int main(int argc, char** argv) {
  baseState();
#ifdef MEMTEST
  if (argc > 1 && !strcmp(argv[1], "mem")) {  // heap in use per screen (no sanitizer build)
    auto used = [] { return (long)mallinfo2().uordblks; };
    const long base = used();
    uiInit();
    run(500);
    printf("main screen: %ld\n", used() - base);
    const TScreen S[] = {SCR_MENU, SCR_PROGRAMS, SCR_CHANNELS, SCR_NETWORK, SCR_DISPLAY, SCR_SYSTEM, SCR_LOG};
    const char* N[] = {"menu", "programs", "channels", "network", "display", "system", "log"};
    for (int i = 0; i < 7; ++i) {
      tShow(S[i]);
      run(500);
      printf("%-9s %ld\n", N[i], used() - base);
    }
    progEditOpen(0);
    run(500);
    printf("editor    %ld\n", used() - base);
    tShow(SCR_MAIN);
    run(500);
    printf("back main %ld\n", used() - base);
    {  // what stays allocated while time passes on the main screen
      run(2000);
      const long u0 = used();
      trackStart();
      run(20000);
      printf("20 s on main: %ld more\n", used() - u0);
      trackReport();
    }
    for (int i = 0; i < 7; ++i) {
      long a0 = 0;
      for (int k = 0; k < 12; ++k) {
        tShow(S[i]);
        run(300);
        tShow(SCR_MAIN);
        run(300);
        if (k == 1) a0 = used();
      }
      printf("leak %-9s %ld per visit\n", N[i], (used() - a0) / 10);
      printf("   screens on the display: %u\n", (unsigned)((lv_display_t*)lv_display_get_default())->screen_cnt);
    }
    for (int k = 0; k < 3; ++k) {
      for (int i = 0; i < 7; ++i) {
        tShow(S[i]);
        run(300);
      }
      progEditOpen(1);
      run(300);
      tShow(SCR_MAIN);
      run(500);
      printf("round %d   %ld\n", k, used() - base);
    }
    return 0;
  }
#endif
  // Channels screen: a save elsewhere (web) shows without leaving the screen; the RS485 tab only with a Modbus
  // channel; a change further down keeps the list where it was
  if (argc > 1 && !strcmp(argv[1], "chan")) {
    baseState();
    uiInit();
    tShow(SCR_CHANNELS);
    run(500);
    int fails = 0;
    auto label = [](const char* t) { return findLabel(lv_screen_active(), t); };
    const bool rs1 = visible(label(SMALL ? "RS485" : "RS485 (beta)"));
    gSettings.ch[0].loc.kp = 7.5f;
    gSettings.ch[1].type = CH_LOCAL;  // (as saved on the web)
    ++hostGen;
    run(1300);
    const bool rs2 = visible(label(SMALL ? "RS485" : "RS485 (beta)"));
    const bool kp = label("7.50") != nullptr;
    printf("[host] chan: RS485 tab %d -> %d (want 1 -> 0), Kp 7.50 shown %d\n", rs1, rs2, kp);
    fails += !rs1 || rs2 || !kp;
    // a switch far down: the list stays where it was
    lv_obj_t* l = label("Converter too hot: fault");
    if (!l) return printf("[host] chan: no switch row\n"), 1;
    lv_obj_t* body = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(l)));
    lv_obj_t* row = lv_obj_get_parent(lv_obj_get_parent(l));
    lv_obj_t* sw = lv_obj_get_child(row, -1);
    lv_area_t a;
    lv_obj_get_coords(sw, &a);
    lv_obj_scroll_to_y(body, lv_obj_get_scroll_y(body) + (a.y1 + a.y2) / 2 - SCREEN_H / 2, LV_ANIM_OFF);  // to the middle
    run(100);
    const int32_t y0 = lv_obj_get_scroll_y(body);
    lv_obj_get_coords(sw, &a);
    const bool on0 = lv_obj_has_state(sw, LV_STATE_CHECKED);
    tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
    const int32_t y1 = lv_obj_get_scroll_y(body);
    lv_obj_t* sw2 = lv_obj_get_child(lv_obj_get_parent(lv_obj_get_parent(label("Converter too hot: fault"))), -1);  // (rebuilt)
    const bool on1 = lv_obj_has_state(sw2, LV_STATE_CHECKED);
    printf("[host] chan: switch %d -> %d; scroll %d before, %d after (want equal, > 0)\n", on0, on1, (int)y0, (int)y1);
    fails += on0 == on1 || y0 <= 0 || y0 != y1;
    // the thermocouple and PID / autotune rows (screenshots)
    for (const char* t : {"Read every", "Autotune time limit"}) {
      lv_obj_t* r = label(t);
      if (!r) {
        printf("[host] chan: no row \"%s\"\n", t);
        ++fails;
        continue;
      }
      lv_obj_t* bd = lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(r)));
      lv_area_t ra;
      lv_obj_get_coords(r, &ra);
      lv_obj_scroll_to_y(bd, lv_obj_get_scroll_y(bd) + ra.y1 - 60, LV_ANIM_OFF);
      shot(t[0] == 'R' ? "24c-chan-sensor" : "24d-chan-tune");
    }
    return fails;
  }
#if !TUI_SMALL
  // + on a tile while the device takes the new manual setpoint over only 600 ms later: the old value must not
  // show again, and a + in that time must go on from the new one
  if (argc > 1 && !strcmp(argv[1], "chartbench")) {  // the chart picture: LVGL draw tasks vs. the own raster
    uiInit();
    tShow(SCR_MAIN);
    run(1500);
    double l = 0, r = 0;
    chartBench(20, l, r);
    printf("[host] chart (live): LVGL %.0f us, raster %.0f us per drawing\n", l, r);
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "stepper")) {
    baseState();
    uiInit();
    run(500);
    hostSpLag = 600;
    int x, y, bad = 0;
    if (!findText(lv_screen_active(), "120 \xC2\xB0" "C", x, y)) return printf("[host] stepper: no 120 button\n"), 1;
    // the + of the first tile is right of its value button
    lv_obj_t* scr = lv_screen_active();
    (void)scr;
    const int px = x + 37 + 4 + 19;
    tap(px, y);  // 121 pending
    for (int k = 0; k < 30; ++k) {  // 1.5 s: sent after 0.7 s, taken over 0.6 s later
      run(50);
      int a, b;
      if (findText(lv_screen_active(), "120 \xC2\xB0" "C", a, b)) ++bad;
    }
    printf("[host] stepper: old value shown %d times; device %.0f\n", bad, gLive.ch[0].manualSp);
    // + right after the send, before the device has it: must go on from the new value (121 -> 122 -> 123)
    tap(px, y);  // 122 pending
    run(450);    // sent; the device takes it 600 ms later
    tap(px, y);  // 123
    run(1500);
    int a, b;
    const bool ok123 = findText(lv_screen_active(), "123 \xC2\xB0" "C", a, b);
    printf("[host] stepper: press during the takeover -> %s; device %.0f\n", ok123 ? "123" : "NOT 123", gLive.ch[0].manualSp);
    return bad || !ok123 || gLive.ch[0].manualSp != 123;
  }
#endif
  if (argc > 1 && !strcmp(argv[1], "monkey")) {
    uiInit();
    monkey(atoi(argc > 2 ? argv[2] : "3000"));
    return 0;
  }
  for (int i = 0; i < 9; ++i) addLog(5 + i, i > 2 ? i * 40.0f : NAN, i == 0 ? "TempCtl Duo " FW_VERSION " started (power on)" : i == 1 ? "Wi-Fi connected: 192.168.1.50" : i == 3 ? "Program \"Lead-free BGA\" started" : "Phase 2 (P)");
  uiInit();
  run(500);
  shot("01-idle");
  hostTune = true;  // autotune status in the tile, a message over the chart
  tToast("Top: manual setpoint on");
  shot("01b-tune-toast");
  hostTune = false;
  run(5000);
  tap(SMALL ? 60 : 120, SMALL ? 10 : 18);  // program picker
  shot("02-picker");
  tModalClose();
  runningState();
  run(1200);
  shot("03-running");
  if (!SMALL) {  // + on the top tile, twice (no stepper on 320x240)
    tap(390, 290);
    tap(390, 290);
  }
  shot("04-stepper");
  run(1000);
  tap(SMALL ? 120 : 90, SMALL ? 140 : 290);  // value of the bottom tile -> numpad
  shot("05-numpad");
  tModalClose();
  gLive.progPause = true;
  gLive.pauseMsg = true;
  strcpy(gLive.pauseText, "Place the board");
  gLive.seq = {1, Stage::Pause, 0, 0};
  run(600);
  shot("06-action");
  gLive.progPause = gLive.pauseMsg = false;
  gLive.seq = {4, Stage::Ramp, 10, 20};
  run(400);
  tap(SMALL ? 160 : 240, SMALL ? 70 : 110);  // chart -> live
  run(1200);
  shot("07-live");
  for (int k = 0; k < 4; ++k) {  // window moves by 1 s per frame: the curve must keep its shape
    char nm[24];
    ++liveShift;
    run(1100);
    snprintf(nm, sizeof(nm), "07-scroll-%d", k);
    shot(nm);
  }
  gSettings.themeDark = 0;
  gSettings.accentColor = 0xD65D0E;
  run(800);
  shot("08-light");
  tap(SMALL ? 160 : 240, SMALL ? 70 : 110);  // chart -> program
  gSettings.themeDark = hostLight ? 0 : 1;
  gSettings.accentColor = 0x2196F3;
  // idle with an interrupted run + a fault
  baseState();
  gLive.intrValid = true;
  strcpy(gLive.intrReason, "brown-out");
  gLive.runProg = 0;
  strcpy(gLive.runName, "Lead-free BGA");
  gLive.progType = PROG_SEQ;
  gLive.phases = 7;
  gLive.intrPos = {262, {3, 0}, {1, 0}, {0, 0}};
  gLive.intrExact = true;
  gLive.ch[0].fault = 1;
  strcpy(gLive.ch[0].faultText, "Thermocouple open");
  gLive.ch[0].pvValid = false;
  run(1200);
  shot("09-interrupted");
  tModalClose();
  run(400);
  shot("10-fault");
  run(500);
  shot("10b-fault-blink");
  run(250);
  shot("10c-fault-blink");
  run(250);
  shot("10d-fault-blink");
  gLive.ch[0].fault = 0;
  gLive.ch[0].pvValid = true;
  gLive.intrValid = false;
  tap(SMALL ? 40 : 22, SMALL ? 222 : 22);  // menu
  shot("11-menu");
  tShow(SCR_DISPLAY);
  shot("12-display");
  {  // custom accent: the pen circle -> picker, hue / saturation / brightness, OK
    int x, y;
    if (findText(lv_screen_active(), LV_SYMBOL_EDIT, x, y)) {
      tap(x, y);
      shot("12b-accent-picker");
      tapSlider(0, 0.47f);  // hue ~170 (teal)
      tapSlider(1, 0.85f);
      tapSlider(2, 0.70f);
      shot("12c-accent-picked");
      if (findText(lv_layer_top(), "OK", x, y)) tap(x, y);
      run(600);
      printf("[host] accent after OK: #%06lX\n", (unsigned long)(gSettings.accentColor & 0xFFFFFF));
      shot("12d-display-custom");
      gSettings.accentColor = 0x2196F3;
      run(600);
    } else printf("[host] custom accent button not found\n");
  }
  {  // display SPI clock + frame rate meter rows (bottom of the Display screen), the clock choice
    tShow(SCR_DISPLAY);
    run(200);
    lv_obj_scroll_to_y(lv_obj_get_child(lv_screen_active(), 1), 1000, LV_ANIM_OFF);
    run(200);
    shot("12e-display-spi");
    int x, y;
    if (findText(lv_screen_active(), "40 MHz", x, y)) {
      tap(x, y);
      run(300);
      shot("12f-spi-choice");
      if (findText(lv_layer_top(), "80 MHz (short wires)", x, y)) tap(x, y);
      run(400);
      printf("[host] spi: setting %u, display %u MHz\n", (unsigned)gSettings.spiMhz, (unsigned)lcdSpiMhz());
      gSettings.spiMhz = 40;
      run(400);
    } else printf("[host] spi button not found\n");
  }
  tShow(SCR_NETWORK);
  shot("13-network");
  strcpy(gSettings.tzName, "Europe/Berlin");
  tShow(SCR_SYSTEM);
  shot("14-system");
  tap(SMALL ? 270 : 400, SMALL ? 150 : 175);  // time zone -> regions
  run(300);
  shot("14b-tz-regions");
  tap(SMALL ? 120 : 200, SMALL ? 100 : 128);  // the second row: Europe
  run(300);
  shot("14c-tz-europe");
  tModalClose();
  gLive.estopRaw = true;
  gLive.chipC = 47.3f;
  tShow(SCR_SYSTEM);
  run(2100);
  lv_obj_scroll_to_y(lv_obj_get_child(lv_screen_active(), 1), 400, LV_ANIM_OFF);
  shot("14d-system-safety");
  gLive.estopRaw = false;
  if (!SMALL) {  // SD card backup (S3)
    int x, y;
    lv_obj_scroll_to_y(lv_obj_get_child(lv_screen_active(), 1), 110, LV_ANIM_OFF);
    run(100);
    shot("14e0-system-backup");
    if (findText(lv_screen_active(), "Back up", x, y)) {
      slide(x, y, x - 120, y + 60);  // pressed, slid away, released: cancelled
      printf("[host] slide-off cancel: %s\n", backupsMade == 0 ? "ok (no backup)" : "FAILED (backup made)");
      slide(x, y, x + 5, y + 4);  // a little jitter: still a click
      printf("[host] small slide: %s\n", backupsMade == 1 ? "ok (clicked)" : "FAILED");
      tModalClose();
      run(5000);
      tap(x, y);
      run(300);
      shot("14e-backup-saved");
      run(5000);
    } else printf("[host] backup button not found\n");
    if (findText(lv_screen_active(), "Choose\xE2\x80\xA6", x, y)) {
      tap(x, y);
      run(300);
      shot("14f-restore-list");
      if (findText(lv_layer_top(), "02.10.2026", x, y) || findText(lv_layer_top(), "2026-10-02 09:05:12", x, y)) tap(x, y);
      run(300);
      shot("14g-restore-what");
      if (findText(lv_layer_top(), "Settings and programs", x, y)) tap(x, y);
      run(300);
      shot("14h-restore-confirm");
      if (findText(lv_layer_top(), "Restore", x, y)) tap(x, y);
      run(300);
      shot("14i-restored");
      tModalClose();
    } else printf("[host] restore button not found\n");
    if (findText(lv_screen_active(), "Check", x, y)) {  // online update (release page)
      otaOk = true;
      tap(x, y);
      run(1500);
      shot("14j-online-update");
      tModalClose();
      otaOk = false;
    } else printf("[host] online update button not found\n");
  } else {
    lv_obj_scroll_to_y(lv_obj_get_child(lv_screen_active(), 1), 160, LV_ANIM_OFF);
    run(100);
    shot("14e0-system-online");
  }
  run(6000);  // (the toasts of the online update check go away)
  tShow(SCR_LOG);
  run(400);
  shot("15-log");
  tKeyboard("Wi-Fi password", "secret", 64, true, false, nullptr, 0);
  shot("16-keyboard");
  tModalClose();
  tShow(SCR_MAIN);
  tChoice("Extend the Top hold", "+10 s\n+30 s\n+1 min\n+5 min", -1, nullptr, 0);
  shot("17-choice");
  tModalClose();
  tDialog("Stop the program?", "Each channel applies its end action.", "Cancel", "Stop", "", nullptr, 0, true);
  shot("18-dialog");
  tModalClose();
  baseState();
  tShow(SCR_PROGRAMS);
  shot("19-programs");
  progEditOpen(0);
  shot("20-edit-seq");
  tap(SMALL ? 120 : 60, SMALL ? 80 : 140);  // row 2 -> row menu
  shot("21-rowmenu");
  tModalClose();
  progEditOpen(2);
  shot("22-edit-tl");
  if (SMALL) {  // the other channel, a step menu
    tap(240, 52);
    shot("22b-edit-tl-top");
    tap(150, 100);
    shot("22c-step-menu");
    tModalClose();
    progEditOpen(0);
    tap(150, 63);  // phase 1 -> its menu with the values
    shot("21b-phase-menu");
    tModalClose();
  }
  progEditOpen(5);
  shot("23-edit-empty");
  tShow(SCR_CHANNELS);
  shot("24-chan-local");
  tap(SMALL ? 155 : 220, SMALL ? 18 : 22);  // second tab
  shot("25-chan-modbus");
  run(600);
  tap(SMALL ? 225 : 330, SMALL ? 18 : 22);  // bus tab
  shot("26-chan-bus");
  tap(SMALL ? 85 : 110, SMALL ? 18 : 22);  // first channel (local)
  lv_obj_scroll_to_y(lv_obj_get_child(lv_screen_active(), 1), 560, LV_ANIM_OFF);
  shot("27-chan-pid");
  lv_obj_scroll_to_y(lv_obj_get_child(lv_screen_active(), 1), 1100, LV_ANIM_OFF);
  shot("28-chan-runaway");
  lv_obj_scroll_to_y(lv_obj_get_child(lv_screen_active(), 1), 3000, LV_ANIM_OFF);
  shot("28b-chan-guard");
  {  // firmware update overlay (ota.h)
    extern void otaStubSet(int phase);
    otaStubSet(1);
    now_ += 300;
    tuiOtaPoll();
    run(100);
    shot("30-ota");
    otaStubSet(0);
    now_ += 300;
    tuiOtaPoll();
    run(100);
  }
#if defined(TOUCH_XPT2046)
  tShow(SCR_MAIN);
  if (uiTouchCalStart()) {  // touch calibration: the 4 crosses (24 px from the edges)
    shot("29-touch-cal");
    const int X[] = {24, SCREEN_W - 25, SCREEN_W - 25, 24}, Y[] = {24, 24, SCREEN_H - 25, SCREEN_H - 25};
    for (int i = 0; i < 4; ++i) {
      tap(X[i], Y[i]);
      if (i == 1) shot("29b-touch-cal-3");
    }
    int step;
    uint16_t a, b, c, d;
    const int st = uiTouchCalPoll(step, a, b, c, d);
    printf("[host] touch cal: %d -> x %u ... %u, y %u ... %u\n", st, a, b, c, d);
  }
#endif
  return 0;
}

// safety (spec 9.2a)
#include "safety.h"
void safetyActivity() {}
bool safetyEstopWired() { return SCREEN_W > 400; }
bool safetySoundWired() { return true; }
bool safetyChipTempWired() { return SCREEN_W > 400; }
void safetyBeepTest() {}

// firmware update (ota.h, sdlog.h)
#include "ota.h"
#include "sdlog.h"
static OtaStatus otaStub{};
void otaStatus(OtaStatus& s) { s = otaStub; }
bool otaCanStart(char* e, size_t n) { if (otaOk) return true; snprintf(e, n, "a program is running - stop it first"); return false; }
// the release page (ota_net.h): a check finds 1.0.99 (S3)
#include "ota_net.h"
static int netPolls = 0;
bool netOtaAvailable() { return HAS_NET_OTA; }
bool netOtaCheckStart(char*, size_t) { netPolls = 3; return true; }
bool netOtaInstallStart(char* e, size_t n) { snprintf(e, n, "-"); return false; }
void netOtaInfo(NetOtaInfo& i) {
  memset(&i, 0, sizeof(i));
  if (netPolls > 0) { --netPolls; i.phase = NET_CHECKING; return; }
  i.phase = NET_CHECKED; i.newer = true; strcpy(i.latest, "1.0.99"); strcpy(i.date, "2026-10-20T09:15:00Z"); i.size = 1923456;
}
bool sdlogFirmwareCheckStart(char*, size_t) { return true; }
int sdlogFirmwareCheckResult(SdFirmware& f, char* e, size_t n) { memset(&f, 0, sizeof(f)); if (n) e[0] = 0; return 1; }
bool sdlogFirmwareInstall(char* e, size_t n) { snprintf(e, n, "-"); return false; }
bool sdlogSlot() { return SCREEN_W > 400; }
// backups (backup.h): three on the card
#include "backup.h"
static const char* const BK_NAMES[] = {"2026-10-14_153012.json", "2026-10-02_090512.json", "2026-09-28_181455_2.json"};
int sdlogList(SdFile* out, int max, uint32_t& totalKb, uint32_t& freeKb, char*, size_t, SdDir dir) {
  totalKb = 7775936, freeKb = 6400000;
  int n = 0;
  if (dir == SD_BACKUPS)
    for (const char* b : BK_NAMES)
      if (n < max) {
        memset(&out[n], 0, sizeof(SdFile));
        strcpy(out[n].name, b);
        out[n++].size = 6352;
      }
  return n;
}
bool backupCreate(char* name, size_t nn, char*, size_t) {
  ++backupsMade;
  snprintf(name, nn, "%s", BK_NAMES[0]);
  return true;
}
bool backupInfo(const char*, BackupInfo& i, char*, size_t) {
  memset(&i, 0, sizeof(i));
  strcpy(i.time, "2026-10-02 09:05:12");
  strcpy(i.fw, "1.0.41");
  i.settings = true;
  i.programs = 3;
  strcpy(i.slots, "1, 2, 5");
  i.mask = 0x13;
  return true;
}
bool backupRestore(const char*, uint8_t what, char* msg, size_t n) {
  snprintf(msg, n, "%s restored.", what == BACKUP_ALL ? "Settings and 3 programs" : what == BACKUP_SETTINGS ? "Settings" : "3 programs");
  return true;
}

void otaStubSet(int phase) {
  otaStub = OtaStatus{};
  otaStub.phase = (uint8_t)phase;
  otaStub.source = OTA_WEB;
  otaStub.total = 1900000;
  otaStub.done = 1140000;
  strcpy(otaStub.version, "1.0.39");
}
