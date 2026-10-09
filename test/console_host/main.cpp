// Serial console test on the PC (test/console_host/build.sh): scripted typing -> console output on stdout
#include "app_state.h"
#include "console.h"
#include "net.h"
#include <WiFi.h>
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
String netApName() { return String("TempCtl-Duo-1A2B"); }

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
  printf("\n");
}
