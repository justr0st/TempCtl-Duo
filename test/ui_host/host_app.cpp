// Host stand-ins for the firmware's app layer + screenshot scenarios of the touch UI.
#include "app_state.h"
#include "lcd.h"
#include "net.h"
#include "settings_store.h"
#include "touch.h"
#include "ui.h"
#include "local_ctrl.h"
#include "tui.h"
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

bool sendCmd(CmdType c, int arg, float value, int idx, uint8_t sub) {
  printf("[host] cmd %d arg %d value %.1f idx %d sub %d\n", (int)c, arg, value, idx, sub);
  return true;
}
void selectProgram(int idx) { gSettings.selProgram = idx; }
void setManualSp(float v, int ch) { gSettings.ch[ch].manualSp = v; printf("[host] manual sp %d = %.1f\n", ch, v); }
void saveSettingsQuiet() {}
void saveSettingsAndNotify() {}
BoardLimits boardLimits() { return BoardLimits{true, 2}; }
uint32_t liveHistSec() { return 86400; }
const uint16_t LIVE_PERIODS[LIVE_PERIODS_N] = {1, 5, 10, 15, 30, 60, 120, 240, 480, 720, 1440};
uint32_t liveMinEff() { return gSettings.liveMin ? gSettings.liveMin : 5; }
void appRequestRestart(const char* from) { printf("[host] restart %s\n", from); }
void logEvent(const char* fmt, ...) {}

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
void lcdSetBrightness(uint8_t) {}
static int tx = -1, ty = -1;
bool touchInit(const TouchCal&) { return true; }
void touchSetRotation(bool) {}
bool touchRead(int32_t* x, int32_t* y) {
  if (tx < 0) return false;
  *x = tx;
  *y = ty;
  return true;
}
void touchLastRaw(int32_t* x, int32_t* y) { *x = tx, *y = ty; }

// ------------------------------------------------------------ scenarios
static void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 10) {
    now_ += 10;
    lv_timer_handler();
  }
}
#define SMALL (SCREEN_W < 400)
static void tap(int x, int y) {
  tx = x, ty = y;
  run(80);
  tx = ty = -1;
  run(300);
}
static void shot(const char* name) {
  run(300);
  lv_refr_now(hostDisp);
  char path[128];
  snprintf(path, sizeof(path), "%s/%s.raw", SCREEN_W < 400 ? "shots_cyd" : "shots", name);
  FILE* f = fopen(path, "wb");
  fwrite(fb, 2, SCREEN_W * SCREEN_H, f);
  fclose(f);
  printf("[host] %s\n", path);
}

static void baseState() {
  settingsDefaults(gSettings, boardLimits());
  strcpy(gSettings.ch[0].name, "Preheater");
  strcpy(gSettings.ch[1].name, "IR head");
  gSettings.ch[0].type = CH_LOCAL;
  gSettings.ch[1].type = CH_MODBUS;
  gSettings.themeDark = 1;
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
  if (argc > 1 && !strcmp(argv[1], "monkey")) {
    uiInit();
    monkey(atoi(argc > 2 ? argv[2] : "3000"));
    return 0;
  }
  for (int i = 0; i < 9; ++i) addLog(5 + i, i > 2 ? i * 40.0f : NAN, i == 0 ? "TempCtl Duo 1.0.19 started (power on)" : i == 1 ? "Wi-Fi connected: 192.168.1.50" : i == 3 ? "Program \"Lead-free BGA\" started" : "Phase 2 (P)");
  uiInit();
  run(500);
  shot("01-idle");
  hostTune = true;  // autotune status in the tile, a message over the chart
  tToast("IR head: manual setpoint on");
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
  gSettings.themeDark = 1;
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
  gLive.ch[0].fault = 0;
  gLive.ch[0].pvValid = true;
  gLive.intrValid = false;
  tap(SMALL ? 40 : 22, SMALL ? 222 : 22);  // menu
  shot("11-menu");
  tShow(SCR_DISPLAY);
  shot("12-display");
  tShow(SCR_NETWORK);
  shot("13-network");
  tShow(SCR_SYSTEM);
  shot("14-system");
  tShow(SCR_LOG);
  run(400);
  shot("15-log");
  tKeyboard("Wi-Fi password", "secret", 64, true, false, nullptr, 0);
  shot("16-keyboard");
  tModalClose();
  tShow(SCR_MAIN);
  tChoice("Extend the IR head hold", "+10 s\n+30 s\n+1 min\n+5 min", -1, nullptr, 0);
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
  return 0;
}
