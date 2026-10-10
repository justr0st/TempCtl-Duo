// Serial console test on the PC (test/console_host/build.sh): scripted typing -> console output on stdout
#include "app_state.h"
#include "console.h"
#include "net.h"
#include <WiFi.h>
#include "local_ctrl.h"
#include "settings_store.h"
#include "touch.h"
#include "ui.h"
SerialT Serial;
EspT ESP;
WiFiT WiFi;
static uint32_t now_ = 1000;
uint32_t millis() { return now_; }
void delay(uint32_t ms) { now_ += ms; }
Settings gSettings;
LiveData gLive;
SemaphoreHandle_t gLock = nullptr;
bool sendCmd(CmdType c, int arg, float value, int idx, uint8_t sub) {
  printf("<cmd %d arg %d value %.1f idx %d sub %d>", (int)c, arg, value, idx, sub);
  return true;
}
void selectProgram(int idx) { gSettings.selProgram = idx; }
void setManualSp(float v, int ch) { gSettings.ch[ch].manualSp = v; gLive.ch[ch].manualSp = v; printf("<manual sp %d = %.1f>", ch, v); }
void saveSettingsQuiet() { printf("<saved: ssid %s>", gSettings.ssid); }
static ProgSummary sums[PROG_MAX];
void programSummaries(ProgSummary* out) { memcpy(out, sums, sizeof(sums)); }
bool programSummary(int i, ProgSummary& o) { if (i < 0 || i >= PROG_MAX) return false; o = sums[i]; return o.used; }
bool programLoad(int i, DuoProgram& p, char* e, size_t n) { if (i > 2) { snprintf(e, n, "Program %d is empty", i + 1); return false; } progExample(i, p); return true; }
bool runProgramCopy(DuoProgram& p) { progExample(0, p); return true; }
static uint32_t lseq = 0;
static LogEntry lg[8];
uint32_t logSeq() { return lseq; }
int logSince(uint32_t s, LogEntry* o, int m) { int n = 0; for (uint32_t q = s + 1; q <= lseq && n < m; ++q) o[n++] = lg[q % 8]; return n; }
void logEvent(const char* f, ...) {
  LogEntry& e = lg[++lseq % 8]; e.seq = lseq; e.runT = NAN; va_list ap; va_start(ap, f); vsnprintf(e.msg, sizeof(e.msg), f, ap); va_end(ap);
  LogSerial.printf("[event] %s\n", e.msg);
}
// net
static int scanState = -2;
static uint32_t disc = 0; static uint8_t reason = 0; static bool gotIp = false;
String netInfo() { return String("Wi-Fi Workshop  192.168.1.50"); }
void netApplyWifi() { printf("<apply wifi: %s>", gSettings.ssid); gotIp = false; }
bool netConnectedSinceApply() { return gotIp; }
void netScanStart() { scanState = -1; }
bool netScanBusy() { return scanState == -1; }
int netScanResult(char* out, size_t n) { if (scanState != 0) return scanState == -1 ? -1 : -2; strlcpy(out, "Workshop\nMy Net 5G\nNeighbour", n); scanState = -2; return 3; }
bool netScanDetail(int i, int& rssi, uint8_t& auth, int& ch) { rssi = -50 - i * 9; auth = i == 2 ? 0 : 3; ch = 1 + i * 5; return i < 3; }
uint32_t netDisconnects(uint8_t* r) { if (r) *r = reason; return disc; }
void netStartAp() {}
void appRequestRestart(const char* from) { printf("<restart %s>", from); }
// stubs for the settings / autotune / touch commands
void saveSettingsAndNotify() { printf("<saved + notify>"); }
BoardLimits boardLimits() { return BoardLimits{true, 2}; }
uint32_t liveHistSec() { return 30 * 60; }
static LocalTune tune[2];
void localCtrlTuneGet(int ch, LocalTune& t) { t = tune[ch]; }
void localCtrlTuneClear(int ch) { tune[ch].state = TuneState::Idle; printf("<tune clear %d>", ch); }
void touchSetCal(const TouchCal& c) { printf("<touch cal %u %u %u %u>", c.minX, c.maxX, c.minY, c.maxY); }
static int calState = 0, calStep = 1;
bool uiTouchCalStart() { calState = 0; calStep = 1; return true; }
int uiTouchCalPoll(int& step, uint16_t& a, uint16_t& b, uint16_t& c, uint16_t& d) {
  step = calStep;
  if (calState == 1) { a = 220; b = 3850; c = 3700; d = 300; }
  return calState;
}
void uiTouchCalCancel() { calState = -1; }
String netApName() { return String("TempCtl-Duo-1A2B"); }
// web API stand-in for the api mode: echoes the request, "/big" answers 20 kB in pieces
void webApiRequest(const char* line) {
  if (strstr(line, "/big")) {
    for (int i = 0; i < 20; ++i) {
      char b[1100];
      memset(b, 'x', 1000);
      int k = snprintf(b + 1000, 100, "%d\n", i);
      apiOut(b, 1000 + k);
      apiFlush(4096);
    }
    return;
  }
  char b[300];
  int k = snprintf(b, sizeof(b), "{\"echo\":%s}\n", line);
  apiOut(b, k);
}
static bool scrReq = false;
static uint32_t scrN = 0;
void uiScreenRestart() { scrReq = true; }
int uiScreenCheckTest() { return gSettings.screenCheck ? 1 : -1; }
void uiScreenCheckInfo(void (*line)(const char*)) {
  line("display  OK: power 0x9C, MADCTL 0x28 (0x28 expected), pixel format 0x55");
  line("touch    XPT2046: cannot be asked (only a press stuck in one place is noticed)");
  line(gSettings.screenCheck ? "check    on (every second), 0 automatic restart(s) since the start" : "check    off, 0 automatic restart(s) since the start");
}
uint32_t uiScreenRestartPoll(bool& l, bool& t) {
  if (scrReq) { scrReq = false; ++scrN; }  // (the UI loop's part)
  l = true; t = scrN < 2;
  return scrN;
}

static void run(int ms) { for (int t = 0; t < ms; t += 50) { consoleLoop(); delay(50); } }
static void type(const char* s) { Serial.feed(s); run(300); }

int main() {
  settingsDefaults(gSettings, BoardLimits{true, 2});
  strcpy(gSettings.ch[0].name, "Preheater"); strcpy(gSettings.ch[1].name, "IR head");
  gSettings.ch[0].type = CH_LOCAL; gSettings.ch[1].type = CH_LOCAL; gSettings.selProgram = 0;
  strcpy(gSettings.ssid, "Workshop"); strcpy(gSettings.pass, "oldsecret");
  for (int i = 0; i < 2; ++i) {
    ChannelLive& c = gLive.ch[i]; strcpy(c.name, gSettings.ch[i].name); c.type = CH_LOCAL; c.commOk = true; c.pvValid = true; c.pv = 24.5f + i;
    c.manualSp = 150; c.spEff = NAN; c.outValid = true; c.out = 0; gLive.run[i].target = gLive.run[i].sp = NAN;
  }
  for (int i = 0; i < 3; ++i) {
    DuoProgram p; progExample(i, p);
    strcpy(sums[i].name, p.name); sums[i].used = 1; sums[i].type = p.type; sums[i].items = progItems(p); sums[i].total = 420 + i * 30; sums[i].waits = i < 2;
  }
  consoleBegin();
  type("help\r");
  type("status\r");
  type("STATUS\r\n");
  type("manual b on sp=180\r");
  type("manual t sp=9999\r");
  type("manual x on\r");
  type("manual b off\r");
  type("programs\r");
  type("program n=3\r");
  type("show\r");
  type("program n=1\r");
  type("statxs\b\bus\r");          // backspace
  type("wifi scan\r");
  scanState = 0; run(300);
  type("wifi connect n=2 pw=secret123\r");   // masked pw= value
  disc = 1; reason = WIFI_REASON_AUTH_FAIL; run(300);
  type("wifi connect s=\"My Net 5G\"\r");
  type("hunter22\r");                         // password prompt, masked
  gotIp = true; run(300);
  type("wifi ap pw=short\r");
  type("wifi ap\r");
  type("longenough\r");
  type("wifi status\r");
  type("bogus cmd\r");
  // a log line while typing (shared port): the typed text comes back after it
  Serial.feed("stat"); run(100);
  logEvent("Wi-Fi connected: 192.168.1.50");
  run(200);
  type("us\r");
  // run: start, action required, extend, target, stop + save
  type("start\r");
  gLive.state = RunState::Running; gLive.progType = PROG_SEQ; strcpy(gLive.runName, "Lead-free BGA"); gLive.phases = 7; gLive.seq = {1, Stage::Pause, 0, 0};
  gLive.progPause = gLive.pauseMsg = true; strcpy(gLive.pauseText, "Place the board");
  gLive.run[0].inProgram = true; gLive.canExt[0] = true;
  run(400);
  type("next\r");
  type("continue\r");
  gLive.progPause = gLive.pauseMsg = false; gLive.seq = {2, Stage::Hold, 0, 0};
  type("manual b on\r");
  type("extend b +30\r");
  type("extend t +30\r");
  type("target b 165\r");
  type("stop\r");
  gLive.state = RunState::Aborted; gLive.runEdited = true; run(400);
  type("save\r");
  type("save\r");
  gLive.intrValid = true; strcpy(gLive.intrReason, "power on"); run(400);
  type("start\r");
  type("resume run\r");
  gLive.state = RunState::Running; type("reboot\r");  // refused: running
  gLive.state = RunState::Idle; type("REBOOT\r");
  // 1.0.23: help sections, status extras, autotune, settings, factory reset, touch calibration, wifi forget
  type("help wifi\r");
  type("help bogus\r");
  type("help settings\r");
  type("info\r");
  type("log 3\r");
  type("watch\r\n");                       // CR LF (PlatformIO monitor): the LF must not stop it
  run(2100);
  type("x");
  type("ack b\r");
  gLive.ch[0].fault = 1; strcpy(gLive.ch[0].faultText, "Runaway: no rise");
  type("ack b\r");
  gLive.ch[0].fault = 0;
  type("tune b start temp=200\r");
  type("tune b start temp=500\r");
  tune[0].state = TuneState::Done; tune[0].res.kp = 3.2f; tune[0].res.ki = 0.021f; tune[0].res.kd = 45;
  type("status\r");
  type("tune b accept\r");
  type("get b.local\r");
  type("get display\r");
  type("set b.spMax=300 b.local.kp=2.5 display.accent=orange t.name=\"IR head 2\"\r");
  type("set b.spMax=2000\r");
  type("set b.local.maxTemp=200 b.spMax=300\r");   // spMax adjusted to maxTemp - 10
  type("set wifi.staticIp=on wifi.ip=192.168.1.60 wifi.subnet=255.255.255.0 wifi.gateway=192.168.1.1\r");
  type("set wifi.ip=300.1.1.1\r");
  type("set bus.parity=odd bus.baud=19200 t.modbus.reg.alarm=\r");
  type("get bus\r");
  type("set nope=1\r");
  type("set set b.spMax=300\r");
  type("set b.spMin=0 b.spMin=0 b.spMin=0 b.spMin=0 b.spMin=0 b.spMin=0 b.spMin=0 b.spMin=0 b.spMin=0\r");
  type("touch cal\r");
  calStep = 2; run(300); calStep = 3; run(300); calState = 1; run(300);
  type("touch cal\r");
  type("q");
  run(300);
  // 1.0.24: screen restart, history (Up / Down), pw= masked when the line is shown again
  type("screen restart\r");
  type("screen reset\r");
  type("screen check\r");
  type("screen check test\r");
  type("set display.screenCheck=off\r");
  type("screen check test\r");
  type("screen check\r");
  type("get display\r");
  type("\x1b[A");          // Up: "screen reset"
  type("\x1b[A");          // Up: "screen restart"
  type("\x1b[B");          // Down: "screen reset"
  type("\x1b[B");          // Down: empty line
  type("\x1b[B");          // nothing more
  type("\x1bOA\r");       // Up (application mode) + Enter: "screen reset" again
  type("wifi connect n=1 pw=abc");
  logEvent("a log line while a password is typed");
  run(200);
  type("\x03");
  type("\x1b[A\x1b[A\x1b[A");  // the pw= line is not in the history
  type("\x03");
  type("watch\r\n");
  run(1100);
  type("\x1b[A");          // an arrow key stops watch, its "[A" is not typed
  type("status\r");
  // 1.0.26: api mode (JSON lines)
  type("api on\r\n");
  Serial.feed("{\"id\":1,\"m\":\"GET\",\"p\":\"/api/live\"}\n"); run(300);
  logEvent("a log line in api mode, with \"quotes\"");
  run(300);
  Serial.feed("hello\n"); run(300);
  Serial.feed("{\"id\":2,\"p\":\"/big\"}\n"); run(300);
  printf("\n[test] (20 kB answer above)\n");
  Serial.feed("api off\n"); run(300);
  type("status\r");
  type("api on\r");
  run(16000);  // no request for 15 s: back to the console
  type("help api\r");
  type("wifi forget\r");
  type("factory reset\r");
  type("factory reset yes\r");
  printf("\n");
}
