// Serial console (spec 7a). Line based, echo + Backspace, case-insensitive commands and keys,
// key=value parameters (a value with spaces in quotes). Passwords are never echoed or printed.
// Everything goes through the same code paths as the touch UI and the web page (sendCmd,
// setManualSp, net*, the program store).
#include "console.h"
#include <WiFi.h>
#include <math.h>
#include <stdarg.h>
#include "app_state.h"
#include "config.h"
#include "net.h"
#include "program_store.h"
#include "version.h"

#if defined(BOARD_S3) && ARDUINO_USB_MODE == 0
#include "USB.h"
#define CON_USB 1
static USBCDC conUsb(0);
static volatile bool conDtr = false;  // a terminal has the port open
static volatile bool conOpened = false;  // DTR went active: print the banner
static Stream& port() { return conUsb; }
static bool portReady() { return conDtr; }
static constexpr bool SHARED = false;  // the log has its own port (UART)
#else
static Stream& port() { return Serial; }
static bool portReady() { return true; }
static constexpr bool SHARED = true;  // CYD: log and console on the one port
#endif

LogPort LogSerial;

// ------------------------------------------------------------ line state
static char line[200];
static int len = 0;
static volatile bool promptShown = false;  // "> " + the typed text is on the screen
static volatile bool reprompt = false;     // a log line removed it: show it again
static bool lastCr = false;                // CR LF counts as one line end
static uint8_t esc = 0;                    // skipping an escape sequence (arrow keys ...)
// password entry: masked, then `pwDone` runs with the text
static bool pwMode = false;
static void (*pwDone)(const char* pw) = nullptr;

static void raw(const char* s) {
  if (portReady()) port().print(s);
}
static void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* fmt, ...) {  // one line (CR LF added)
  char b[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  raw(b);
  raw("\r\n");
}
static void err(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void err(const char* fmt, ...) {
  char b[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  raw("error: ");
  raw(b);
  raw("\r\n");
}

// inside the value of a "pw=" parameter (typed characters are shown as '*')
static bool inPwValue() {
  for (int i = len - 1; i >= 0; --i) {
    if (line[i] == ' ') return false;
    if (i >= 2 && (line[i] == '=') && tolower(line[i - 1]) == 'w' && tolower(line[i - 2]) == 'p' &&
        (i == 2 || line[i - 3] == ' '))
      return true;
  }
  return false;
}

static void showPrompt() {
  if (!portReady()) return;
  raw(pwMode ? "Password: " : "> ");
  for (int i = 0; i < len; ++i) {
    char c[2] = {line[i], 0};
    if (pwMode) c[0] = '*';
    raw(c);
  }
  promptShown = true;
  reprompt = false;
}

// before output that is not the answer to a typed command (events, async results): clear the line
static void breakLine() {
  if (promptShown && portReady()) raw("\r\x1b[K");
  promptShown = false;
  reprompt = true;
}

size_t LogPort::write(const uint8_t* b, size_t n) {
  if (SHARED && promptShown) {  // the typed line goes away, comes back after the log line
    Serial.write((const uint8_t*)"\r\x1b[K", 4);
    promptShown = false;
    reprompt = true;
  }
  return Serial.write(b, n);
}

// ------------------------------------------------------------ helpers
// Snapshot for a command (heap, only while it runs: the CYD's RAM is short)
struct Snap {
  LiveData live;
  Settings set;
};
static Snap* snap = nullptr;
#define L (snap->live)
#define S (snap->set)

static bool runActive() { return L.state == RunState::Running || L.state == RunState::Paused; }

static void fmtT(float sec, char* b, size_t n) {
  if (isnan(sec) || sec < 0) sec = 0;
  const uint32_t t = (uint32_t)(sec + 0.5f);
  if (t >= 3600) snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60), (unsigned long)(t % 60));
  else snprintf(b, n, "%lu:%02lu", (unsigned long)(t / 60), (unsigned long)(t % 60));
}
static void fmtC(float v, char* b, size_t n) {
  if (isnan(v)) strlcpy(b, "-", n);
  else snprintf(b, n, "%.1f \xC2\xB0" "C", v);
}

// "b" / "t" (or bottom / top / 1 / 2) -> channel, -1 = not a channel
static int chanOf(const char* w) {
  if (!w) return -1;
  if (!strcasecmp(w, "b") || !strcasecmp(w, "bottom") || !strcmp(w, "1")) return CH_BOTTOM;
  if (!strcasecmp(w, "t") || !strcasecmp(w, "top") || !strcmp(w, "2")) return CH_TOP;
  return -1;
}
static const char* chWord(int ch) { return ch == CH_BOTTOM ? "bottom" : "top"; }

// ------------------------------------------------------------ parsing
struct Args {
  int n;                 // words
  char* w[8];            // words (lower case)
  int nk;                // key=value pairs
  char* k[6];            // keys (lower case)
  char* v[6];            // values (as typed)
  const char* get(const char* key) const {
    for (int i = 0; i < nk; ++i)
      if (!strcmp(k[i], key)) return v[i];
    return nullptr;
  }
  const char* word(int i) const { return i < n ? w[i] : ""; }
};

static void parse(char* s, Args& a) {
  a.n = a.nk = 0;
  while (*s) {
    while (*s == ' ' || *s == '\t') ++s;
    if (!*s) break;
    char* start = s;
    char* eq = nullptr;
    char* val = nullptr;
    char* o = s;  // write position (quotes removed)
    bool q = false;
    while (*s && (q || (*s != ' ' && *s != '\t'))) {
      if (*s == '"') {
        q = !q;
        ++s;
        continue;
      }
      if (*s == '=' && !eq && !q) {
        eq = o;
        *o++ = 0;
        val = o;
        ++s;
        continue;
      }
      *o++ = *s++;
    }
    const bool end = !*s;
    *o = 0;
    if (!end) ++s;
    if (eq) {
      for (char* p = start; *p; ++p) *p = tolower(*p);
      if (a.nk < 6) {
        a.k[a.nk] = start;
        a.v[a.nk++] = val;
      }
    } else {
      for (char* p = start; *p; ++p) *p = tolower(*p);
      if (a.n < 8) a.w[a.n++] = start;
    }
    if (end) break;
  }
}

static bool number(const char* s, float& v) {
  if (!s || !*s) return false;
  char* e;
  v = strtof(s, &e);
  return *e == 0 && !isnan(v);
}

// ------------------------------------------------------------ help / banner
static void banner() {
  say("%s %s (%s%s%s) - serial commands:", PRODUCT_NAME, FW_VERSION, BOARD_NAME, BOARD_VARIANT[0] ? " " : "", BOARD_VARIANT);
}

static void help() {
  banner();
  static const char* const H[] = {
      "  help                         this list",
      "  status                       temperatures, setpoints, outputs, run and Wi-Fi state",
      "  manual b|t on [sp=<C>]       manual setpoint on (b = bottom, t = top)",
      "  manual b|t sp=<C>            change the manual setpoint (switches it on)",
      "  manual b|t off               outputs off",
      "  wifi scan                    list networks (numbered)",
      "  wifi connect s=<SSID> [pw=<PASSWORD>]",
      "  wifi connect n=<number> [pw=<PASSWORD>]",
      "  wifi ap [pw=<PASSWORD>]      start the access point (TempCtl-Duo-XXXX)",
      "  wifi status                  current Wi-Fi state",
      "  programs | program n=<n> | show   list, select, show programs",
      "  start | pause | resume | stop | next | continue   run control",
      "  extend b|t +<s> | target b|t <C>   extend the running hold, change a target",
      "  resume run | dismiss run     after \"Run interrupted by a reset\"",
      "  save | discard               keep / drop the changes made during the last run",
      "  reboot                       restart the device (not while a program runs)",
      "Without pw= the password is asked for (hidden). A value with spaces in quotes: s=\"My Net\"",
  };
  for (const char* h : H) say("%s", h);
}

// ------------------------------------------------------------ status
static const char* chStateText(int ch, char* b, size_t n) {
  const ChannelLive& C = L.ch[ch];
  const ChOut& o = L.run[ch];
  char tv[20];
  fmtC(o.target, tv, sizeof(tv));
  if (C.type == CH_OFF) return "off (settings)";
  if (C.fault) {
    snprintf(b, n, "FAULT: %s", C.faultText[0] ? C.faultText : "?");
    return b;
  }
  if (!C.commOk) {
    snprintf(b, n, "no connection%s%s", C.lastErr[0] ? ": " : "", C.lastErr);
    return b;
  }
  switch ((ChState)L.chSt[ch]) {
    case ChState::Off: return "program: off";
    case ChState::Ramp: snprintf(b, n, "program: ramp to %s", tv); return b;
    case ChState::Hold: snprintf(b, n, "program: hold %s", tv); return b;
    case ChState::Wait: return "program: waiting for the bottom";
    case ChState::Jump: snprintf(b, n, "program: jump to %s", tv); return b;
    case ChState::Keep: snprintf(b, n, "program: keeps %s", tv); return b;
    case ChState::Halt: return "program: paused";
    case ChState::Pause: return "program pause";
    case ChState::Done: snprintf(b, n, "program done, %s", tv); return b;
    default: break;
  }
  if (C.manualOn) {
    fmtC(C.manualSp, tv, sizeof(tv));
    snprintf(b, n, "manual %s", tv);
    return b;
  }
  return "idle";
}

static void status() {
  char pv[20], sp[20], st[96];
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    const ChannelLive& C = L.ch[ch];
    fmtC(C.pvValid ? C.pv : NAN, pv, sizeof(pv));
    float s = C.spEffValid ? C.spEff : NAN;
    if (isnan(s) && runActive() && L.run[ch].inProgram && !L.run[ch].idle) s = L.run[ch].sp;
    if (isnan(s) && C.manualOn) s = C.manualSp;
    fmtC(s, sp, sizeof(sp));
    char out[16] = "-";
    if (C.outValid) snprintf(out, sizeof(out), "%.0f %%", C.out);
    say("%-6s %-12s PV %-10s SP %-10s out %-5s %s", chWord(ch), C.name, pv, sp, out, chStateText(ch, st, sizeof(st)));
  }
  char t1[16], t2[16];
  fmtT(L.elapsed, t1, sizeof(t1));
  fmtT(L.remaining, t2, sizeof(t2));
  if (runActive()) {
    char where[64];
    if (L.progType == PROG_SEQ) snprintf(where, sizeof(where), "phase %d/%d %s", L.seq.idx + 1, L.phases, stageName(L.seq.st));
    else snprintf(where, sizeof(where), "steps %d/%d, %d/%d", L.lane[0].idx + 1, L.steps[0], L.lane[1].idx + 1, L.steps[1]);
    say("run    %s \"%s\", %s, %s, %s left%s", L.state == RunState::Paused ? "paused" : L.progPause ? "program pause" : L.waiting ? "waiting" : "running",
        L.runName, where, t1, t2, L.progPause && L.pauseMsg ? " - ACTION REQUIRED (continue / stop)" : "");
  } else if (L.state == RunState::Finished || L.state == RunState::Aborted) {
    say("run    \"%s\" %s after %s", L.runName, L.state == RunState::Finished ? "finished" : "stopped", t1);
  } else {
    ProgSummary p;
    if (programSummary(S.selProgram, p)) say("run    ready: %d \"%s\"", S.selProgram + 1, p.name);
    else say("run    ready (no program selected)");
  }
  if (L.intrValid) say("       run \"%s\" interrupted by a reset (%s) - \"resume run\" or \"dismiss run\"", L.runName, L.intrReason);
  String ni = netInfo();
  ni.replace("\n", ", ");
  say("wifi   %s", ni.length() ? ni.c_str() : "off");
}

// ------------------------------------------------------------ manual
static void cmdManual(const Args& a) {
  const int ch = chanOf(a.word(1));
  if (ch < 0) return err("manual b|t on [sp=<C>] | manual b|t sp=<C> | manual b|t off");
  const ChannelLive& C = L.ch[ch];
  if (C.type == CH_OFF) return err("%s is off (channel type in the settings)", chWord(ch));
  if (runActive() && L.run[ch].inProgram) return err("%s is running a program", chWord(ch));
  const char* spS = a.get("sp");
  const char* what = a.word(2);
  if (!strcmp(what, "off")) {
    if (spS) return err("off takes no sp=");
    sendCmd(CMD_MANUAL_OFF, ch);
    return say("%s: manual off", chWord(ch));
  }
  if (*what && strcmp(what, "on")) return err("unknown \"%s\" (on / off / sp=)", what);
  if (!*what && !spS) return err("manual %s on [sp=<C>] | manual %s sp=<C> | manual %s off", a.word(1), a.word(1), a.word(1));
  float v = C.manualSp;
  if (spS) {
    if (!number(spS, v)) return err("sp=%s is not a number", spS);
    const float lo = S.ch[ch].spMin, hi = S.ch[ch].spMax;
    if (v < lo || v > hi) return err("sp=%s is outside the setpoint limits %.1f ... %.1f", spS, lo, hi);
    setManualSp(v, ch);
  }
  if (!C.manualOn) sendCmd(CMD_MANUAL_ON, ch);
  char b[20];
  fmtC(v, b, sizeof(b));
  say("%s: manual %s on", chWord(ch), b);
}

// ------------------------------------------------------------ wifi
static constexpr size_t SCAN_LIST = 800;
static char* scanNames = nullptr;  // the last scan's names (heap, from the first "wifi scan" on)
static int scanCount = 0;            // entries of the last listed scan
static bool scanPending = false;     // started from here, result not printed yet
static char connSsid[33];
static char oldSsid[33], oldPass[65];
static uint32_t connAt = 0;          // 0 = no connect pending
static uint32_t connDisc = 0, connNoAp = 0;

static const char* authName(uint8_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "?";
  }
}

static const char* scanName(int i, char* b, size_t n) {  // 0-based entry of the last list
  const char* p = scanNames;
  if (!p) return nullptr;
  for (int k = 0; k < i && p; ++k) {
    p = strchr(p, '\n');
    if (p) ++p;
  }
  if (!p || !*p) return nullptr;
  const char* e = strchr(p, '\n');
  snprintf(b, n, "%.*s", (int)(e ? e - p : (long)strlen(p)), p);
  return b;
}

static void doConnect(const char* pw) {
  {
    Guard g;
    strlcpy(oldSsid, gSettings.ssid, sizeof(oldSsid));
    strlcpy(oldPass, gSettings.pass, sizeof(oldPass));
    strlcpy(gSettings.ssid, connSsid, sizeof(gSettings.ssid));
    strlcpy(gSettings.pass, pw, sizeof(gSettings.pass));
  }
  netApplyWifi();
  connAt = millis();
  if (!connAt) connAt = 1;  // (0 = none; not "| 1": that can be ahead of millis())
  connDisc = netDisconnects(nullptr);
  connNoAp = 0;
  say("Connecting to \"%s\"...", connSsid);
}

static void connectPoll() {
  if (!connAt) return;
  const char* fail = nullptr;
  if (netConnectedSinceApply()) {
    breakLine();
    saveSettingsQuiet();  // the new network is kept (like the touch / web Wi-Fi page)
    say("connected, IP %s, RSSI %d dBm (saved)", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    connAt = 0;
    return;
  }
  uint8_t reason;
  const uint32_t d = netDisconnects(&reason);
  if (d != connDisc) {
    connDisc = d;
    if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
        reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_MIC_FAILURE)
      fail = "wrong password";
    else if (reason == WIFI_REASON_NO_AP_FOUND && ++connNoAp >= 2)
      fail = "network not found";
  }
  if (!fail && millis() - connAt > 25000) fail = "timeout";
  if (!fail) return;
  breakLine();
  say("failed: %s - back to the previous settings%s%s%s", fail, oldSsid[0] ? " (\"" : "", oldSsid, oldSsid[0] ? "\")" : "");
  {
    Guard g;
    strlcpy(gSettings.ssid, oldSsid, sizeof(gSettings.ssid));
    strlcpy(gSettings.pass, oldPass, sizeof(gSettings.pass));
  }
  memset(oldPass, 0, sizeof(oldPass));
  netApplyWifi();
  connAt = 0;
}

static void scanPoll() {
  if (!scanPending) return;
  const int n = netScanResult(scanNames, SCAN_LIST);
  if (n == -1) return;
  scanPending = false;
  breakLine();
  if (n < 0) return err("the scan failed");
  scanCount = n;
  if (!n) return say("no networks found");
  char nm[40];
  for (int i = 0; i < n; ++i) {
    int rssi = 0, chn = 0;
    uint8_t auth = 0;
    netScanDetail(i, rssi, auth, chn);
    say("%2d  %-32s %4d dBm  %-9s ch %d", i + 1, scanName(i, nm, sizeof(nm)) ? nm : "?", rssi, authName(auth), chn);
  }
  say("connect: wifi connect n=<number> [pw=<PASSWORD>]");
}

static void doAp(const char* pw) {
  const size_t n = strlen(pw);
  if (n < 8 || n > 32) return err("the access point password needs 8 ... 32 characters");
  {
    Guard g;
    strlcpy(gSettings.apPass, pw, sizeof(gSettings.apPass));
  }
  saveSettingsQuiet();
  netStartAp();
  say("AP \"%s\" started, IP %s (password saved)", netApName().c_str(), WiFi.softAPIP().toString().c_str());
}

static void askPassword(void (*done)(const char*)) {
  pwMode = true;
  pwDone = done;
  len = 0;
}

static void cmdWifi(const Args& a) {
  const char* sub = a.word(1);
  if (!strcmp(sub, "scan")) {
    if (netScanBusy() || scanPending) return err("a scan is already running");
    if (!scanNames) scanNames = (char*)malloc(SCAN_LIST);
    if (!scanNames) return err("out of memory");
    scanNames[0] = 0;
    scanCount = 0;
    netScanStart();
    scanPending = true;
    return say("Scanning...");
  }
  if (!strcmp(sub, "status") || (!*sub && !a.nk)) {
    const wifi_mode_t m = WiFi.getMode();
    say("mode   %s", m == WIFI_AP_STA ? "station + access point" : m == WIFI_STA ? "station" : m == WIFI_AP ? "access point" : "off");
    if (WiFi.status() == WL_CONNECTED)
      say("station \"%s\", IP %s, RSSI %d dBm", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    else if (S.ssid[0])
      say("station \"%s\": not connected (status %d)", S.ssid, (int)WiFi.status());
    else
      say("station: no network set");
    if (netApName().length()) say("AP \"%s\", IP %s", netApName().c_str(), WiFi.softAPIP().toString().c_str());
    return;
  }
  if (!strcmp(sub, "connect")) {
    if (connAt) return err("a connection attempt is running");
    const char* s = a.get("s");
    const char* nS = a.get("n");
    char nm[40];
    if (nS) {
      float v;
      if (!number(nS, v) || v < 1 || v > scanCount || !scanName((int)v - 1, nm, sizeof(nm)))
        return err(scanCount ? "n= is 1 ... %d (from the last \"wifi scan\")" : "no scan list yet: \"wifi scan\" first", scanCount);
      s = nm;
    }
    if (!s || !*s) return err("wifi connect s=<SSID> [pw=<PASSWORD>] | wifi connect n=<number> [pw=<PASSWORD>]");
    if (strlen(s) > 32) return err("the network name has at most 32 characters");
    strlcpy(connSsid, s, sizeof(connSsid));
    const char* pw = a.get("pw");
    if (pw) {
      if (strlen(pw) > 64) return err("the password has at most 64 characters");
      return doConnect(pw);
    }
    say("Password for \"%s\" (empty = open network):", connSsid);
    return askPassword(doConnect);
  }
  if (!strcmp(sub, "ap") || a.get("ap")) {
    if (a.get("ap") && *a.get("ap")) say("(the access point name is fixed: %s-XXXX)", AP_NAME_PREFIX);
    const char* pw = a.get("pw");
    if (pw) return doAp(pw);
    say("Access point password (8 ... 32 characters):");
    return askPassword(doAp);
  }
  err("wifi scan | wifi connect ... | wifi ap [pw=...] | wifi status");
}

// ------------------------------------------------------------ programs
static void cmdPrograms() {
  ProgSummary* sums = (ProgSummary*)malloc(sizeof(ProgSummary) * PROG_MAX);  // only while listing
  if (!sums) return err("out of memory");
  programSummaries(sums);
  int n = 0;
  for (int i = 0; i < PROG_MAX; ++i) {
    if (!sums[i].used) continue;
    char t[16];
    fmtT(sums[i].total, t, sizeof(t));
    say("%c%2d  %-31s %-8s %3u %s  %s%s", i == S.selProgram ? '*' : ' ', i + 1, sums[i].name,
        sums[i].type == PROG_TL ? "timeline" : "sequence", sums[i].items, sums[i].type == PROG_TL ? "steps " : "phases",
        t, sums[i].waits ? " + waits" : "");
    ++n;
  }
  free(sums);
  if (!n) return say("no programs");
  say("* = selected for the next run (program n=<number>)");
}

static void cmdProgram(const Args& a) {
  float v;
  if (!number(a.get("n"), v) || v < 1 || v > PROG_MAX) return err("program n=<1 ... %d>", PROG_MAX);
  if (runActive()) return err("a program is running");
  ProgSummary p;
  if (!programSummary((int)v - 1, p)) return err("slot %d is empty", (int)v);
  selectProgram((int)v - 1);
  say("selected %d \"%s\" for the next run", (int)v, p.name);
}

static void seg(const Seg& s, char* b, size_t n) {
  char r[16], h[16];
  fmtT(s.r, r, sizeof(r));
  fmtT(s.h, h, sizeof(h));
  snprintf(b, n, "%.0f \xC2\xB0" "C, ramp %s, hold %s", s.t, r, h);
}

static void cmdShow() {
  ScratchProg sp;
  if (!sp) return err("out of memory");
  DuoProgram& p = *sp;
  bool running = runActive() && runProgramCopy(p);
  char e[96];
  if (!running && !programLoad(S.selProgram, p, e, sizeof(e))) return err("%s", e);
  say("%s\"%s\" (%s)", running ? "running copy of " : "", p.name, p.type == PROG_TL ? "timeline" : "sequence");
  char b[96], c[96];
  const char* nb = S.ch[0].name;
  const char* nt = S.ch[1].name;
  if (p.type == PROG_SEQ) {
    for (int i = 0; i < p.nPh; ++i) {
      const SeqPhase& f = p.ph[i];
      switch (f.kind) {
        case PH_B:
          seg(f.b, b, sizeof(b));
          say("P%-3d %s: %s%s", i + 1, nb, b, f.wait ? ", wait" : "");
          break;
        case PH_T:
          seg(f.t, b, sizeof(b));
          say("P%-3d %s: %s", i + 1, nt, b);
          break;
        case PH_BT:
          seg(f.t, c, sizeof(c));
          if (f.bIdle) snprintf(b, sizeof(b), "%s off", nb);
          else snprintf(b, sizeof(b), "%s %.0f \xC2\xB0" "C", nb, f.b.t);
          say("P%-3d both: %s; %s: %s%s", i + 1, b, nt, c, f.wait ? ", wait" : "");
          break;
        default: {
          const char* t = progText(p, f.text);
          say("P%-3d pause%s%s%s", i + 1, f.msg ? " (action required)" : "", *t ? ": " : "", t);
        }
      }
    }
    if (!p.nPh) say("(no phases)");
    return;
  }
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    say("%s:", ch ? nt : nb);
    for (int i = 0; i < p.nSt[ch]; ++i) {
      const TlStep& s = p.st[ch][i];
      if (s.kind == ST_TARGET) {
        seg(s.s, b, sizeof(b));
        say("  S%-3d %s", i + 1, b);
      } else if (s.kind == ST_IDLE) {
        fmtT(s.s.h, b, sizeof(b));
        say("  S%-3d off for %s", i + 1, b);
      } else {
        const char* t = progText(p, s.text);
        say("  S%-3d pause%s%s%s", i + 1, s.msg ? " (action required)" : "", *t ? ": " : "", t);
      }
    }
    if (!p.nSt[ch]) say("  (no steps)");
  }
}

// ------------------------------------------------------------ run
static bool savePending = false;  // "run changed the program": save / discard

static void cmdRunEdit(const Args& a, bool extend) {
  const int ch = chanOf(a.word(1));
  float v;
  const char* vs = a.word(2);
  if (ch < 0 || !number(*vs == '+' ? vs + 1 : vs, v))
    return err(extend ? "extend b|t +<seconds>" : "target b|t <C>");
  if (!runActive()) return err("no program is running");
  if (!L.run[ch].inProgram) return err("%s is not in the running program", chWord(ch));
  if (extend) {
    if (!L.canExt[ch]) return err("no hold of the %s is running", chWord(ch));
    sendCmd(CMD_RUN_EDIT, ch, v, -1, EDIT_EXTEND);
    return say("%s: hold +%.0f s", chWord(ch), v);
  }
  const float lo = S.ch[ch].spMin, hi = S.ch[ch].spMax;
  if (v < lo || v > hi) return err("%s is outside the setpoint limits %.1f ... %.1f", vs, lo, hi);
  sendCmd(CMD_RUN_EDIT, ch, v, -1, EDIT_TARGET);
  say("%s: target %.1f \xC2\xB0" "C", chWord(ch), v);
}

static void runCmd(const Args& a) {
  const char* c = a.word(0);
  const bool active = runActive(), running = L.state == RunState::Running;
  if (!strcmp(c, "start")) {
    if (active) return err("a program is running");
    if (L.intrValid) return err("a run was interrupted by a reset: \"resume run\" or \"dismiss run\" first");
    ProgSummary p;
    if (!programSummary(S.selProgram, p)) return err("no program selected (programs, program n=<n>)");
    sendCmd(CMD_START, -1);
    return say("starting \"%s\"", p.name);
  }
  if (!strcmp(c, "pause")) {
    if (!running) return err("no program is running");
    sendCmd(CMD_PAUSE);
    return say("pause");
  }
  if (!strcmp(c, "resume")) {
    if (!strcmp(a.word(1), "run")) {
      if (!L.intrValid) return err("no interrupted run");
      sendCmd(CMD_RUN_RECOVER);
      return say("resuming the interrupted run");
    }
    if (L.state != RunState::Paused) return err("the run is not paused");
    sendCmd(L.progPause ? CMD_CONTINUE : CMD_RESUME);
    return say("resume");
  }
  if (!strcmp(c, "dismiss")) {
    if (!L.intrValid) return err("no interrupted run");
    sendCmd(CMD_RUN_DISMISS);
    return say("interrupted run dismissed");
  }
  if (!strcmp(c, "stop")) {
    if (!active) return err("no program is running");
    sendCmd(CMD_STOP);
    return say("stop (each channel applies its end action)");
  }
  if (!strcmp(c, "next")) {
    if (!active) return err("no program is running");
    if (L.progType == PROG_TL) return err("\"next\" is for Sequence programs");
    sendCmd(CMD_NEXT);
    return say("next phase");
  }
  if (!strcmp(c, "continue")) {
    if (!active || !L.progPause) return err("the program is not waiting for continue");
    sendCmd(CMD_CONTINUE);
    return say("continue");
  }
  if (!strcmp(c, "save") || !strcmp(c, "discard")) {
    if (!savePending) return err("nothing to save");
    savePending = false;
    if (!strcmp(c, "save")) {
      sendCmd(CMD_SAVE_RUN);
      return say("saving the run's changes into \"%s\"", L.runName);
    }
    return say("the run's changes are dropped");
  }
}

// ------------------------------------------------------------ dispatch
static void dispatch(const Args& a);
static void execute(char* text) {
  Args a;
  parse(text, a);
  if (!a.n && !a.nk) return;
  snap = (Snap*)malloc(sizeof(Snap));
  if (!snap) return err("out of memory");
  {
    Guard g;
    snap->live = gLive;
    snap->set = gSettings;
  }
  dispatch(a);
  free(snap);
  snap = nullptr;
}

static void dispatch(const Args& a) {
  const char* c = a.word(0);
  if (!strcmp(c, "help") || !strcmp(c, "?")) return help();
  if (!strcmp(c, "status")) return status();
  if (!strcmp(c, "manual")) return cmdManual(a);
  if (!strcmp(c, "wifi")) return cmdWifi(a);
  if (!strcmp(c, "programs")) return cmdPrograms();
  if (!strcmp(c, "program")) return cmdProgram(a);
  if (!strcmp(c, "show")) return cmdShow();
  if (!strcmp(c, "reboot") || !strcmp(c, "restart")) {  // same rules as the touch / web restart
    if (runActive()) return err("a program is running - stop it first");
    say("restarting...");
    return appRequestRestart("from the serial console");
  }
  if (!strcmp(c, "extend")) return cmdRunEdit(a, true);
  if (!strcmp(c, "target")) return cmdRunEdit(a, false);
  static const char* const RUN[] = {"start", "pause", "resume", "stop", "next", "continue", "dismiss", "save", "discard"};
  for (const char* r : RUN)
    if (!strcmp(c, r)) return runCmd(a);
  err("unknown command \"%s\" (help lists them)", c);
}

static void enterLine() {
  line[len] = 0;
  raw("\r\n");
  promptShown = false;
  if (pwMode) {
    pwMode = false;
    char pw[72];
    strlcpy(pw, line, sizeof(pw));
    memset(line, 0, sizeof(line));
    len = 0;
    if (pwDone) pwDone(pw);
    memset(pw, 0, sizeof(pw));
  } else {
    char cmd[sizeof(line)];
    strlcpy(cmd, line, sizeof(cmd));
    len = 0;
    execute(cmd);
    memset(cmd, 0, sizeof(cmd));  // may hold a pw=
  }
  memset(line, 0, sizeof(line));
  showPrompt();
}

static void input(char c) {
  if (esc) {  // ESC [ ... letter / ~
    if (esc == 1 && c == '[') esc = 2;
    else if (esc == 1 || (c >= '@' && c <= '~')) esc = 0;
    return;
  }
  if (c == 0x1B) {
    esc = 1;
    return;
  }
  if (c == '\n' && lastCr) {
    lastCr = false;
    return;
  }
  lastCr = c == '\r';
  if (!promptShown) showPrompt();
  if (c == '\r' || c == '\n') return enterLine();
  if (c == 0x08 || c == 0x7F) {
    if (len) {
      --len;
      raw("\b \b");
    }
    return;
  }
  if (c == 0x03) {  // Ctrl-C: drop the line (and a password entry)
    len = 0;
    pwMode = false;
    raw("^C\r\n");
    promptShown = false;
    showPrompt();
    return;
  }
  if ((uint8_t)c < 0x20 || len >= (int)sizeof(line) - 1) return;
  const bool mask = pwMode || inPwValue();
  line[len++] = c;
  char e[2] = {mask ? '*' : c, 0};
  raw(e);
}

// ------------------------------------------------------------ run events
static uint32_t logShown = 0;
static uint32_t actionAt = 0;
static bool actionWas = false, intrShown = false;
static RunState prevState = RunState::Idle;

static void events() {
  struct {
    RunState state;
    uint8_t progType;
    int16_t phase;
    bool progPause, pauseMsg, runEdited, intrValid;
    char pauseText[PROG_TEXT_LEN], runName[PROG_NAME_LEN], intrReason[sizeof(gLive.intrReason)];
  } E;
  {
    Guard g;
    E.state = gLive.state;
    E.progType = gLive.progType;
    E.phase = gLive.seq.idx;
    E.progPause = gLive.progPause;
    E.pauseMsg = gLive.pauseMsg;
    E.runEdited = gLive.runEdited;
    E.intrValid = gLive.intrValid;
    strlcpy(E.pauseText, gLive.pauseText, sizeof(E.pauseText));
    strlcpy(E.runName, gLive.runName, sizeof(E.runName));
    strlcpy(E.intrReason, gLive.intrReason, sizeof(E.intrReason));
  }
  const bool active = E.state == RunState::Running || E.state == RunState::Paused;
  // the event log (CYD: logEvent prints it on this port already)
  if (!SHARED) {
    LogEntry e[4];
    int n;
    while ((n = logSince(logShown, e, 4)) > 0) {
      breakLine();
      for (int i = 0; i < n; ++i) {
        logShown = e[i].seq;
        char t[16] = "";
        if (!isnan(e[i].runT)) fmtT(e[i].runT, t, sizeof(t));
        say("[event]%s%s %s", t[0] ? " " : "", t, e[i].msg);
      }
    }
  }
  // "Action required": a block, repeated every 30 s
  const bool action = active && E.progPause && E.pauseMsg;
  if (action && (!actionWas || millis() - actionAt > 30000)) {
    actionAt = millis();
    breakLine();
    char where[24] = "";
    if (E.progType == PROG_SEQ) snprintf(where, sizeof(where), " - phase %d", E.phase + 1);
    say("*** ACTION REQUIRED%s%s%s - type \"continue\" or \"stop\" ***", where, E.pauseText[0] ? ": " : "", E.pauseText);
  }
  actionWas = action;
  // end of a run with changes: save / discard
  const bool wasActive = prevState == RunState::Running || prevState == RunState::Paused;
  if (wasActive && (E.state == RunState::Finished || E.state == RunState::Aborted) && E.runEdited) {
    savePending = true;
    breakLine();
    say("run changed the program \"%s\" - type \"save\" or \"discard\"", E.runName);
  }
  if (active) savePending = false;
  prevState = E.state;
  // run interrupted by a reset: once
  if (E.intrValid && !intrShown) {
    intrShown = true;
    breakLine();
    say("Run \"%s\" was interrupted by a reset (%s) - type \"resume run\" or \"dismiss run\"", E.runName, E.intrReason);
  }
  if (!E.intrValid) intrShown = false;
}

// ------------------------------------------------------------ begin / loop
static void greet() {
  raw("\r\n");
  help();
  intrShown = false;  // tell a new terminal about an interrupted run again
  showPrompt();
}

void consoleBegin() {
#ifdef CON_USB
  conUsb.onEvent(ARDUINO_USB_CDC_LINE_STATE_EVENT, [](void*, esp_event_base_t, int32_t, void* data) {
    const arduino_usb_cdc_event_data_t* d = (const arduino_usb_cdc_event_data_t*)data;
    const bool dtr = d->line_state.dtr;
    if (dtr && !conDtr) conOpened = true;
    conDtr = dtr;
  });
  conUsb.setTxTimeoutMs(20);  // never stall the loop when nobody reads
  conUsb.begin(115200);
  USB.productName(PRODUCT_NAME);
  USB.manufacturerName("TempCtl");
  USB.begin();
  Serial.printf("%s %s - serial console on the USB port (115200), log here\n", PRODUCT_NAME, FW_VERSION);
#endif
  logShown = logSeq();
  greet();
}

void consoleLoop() {
#ifdef CON_USB
  if (conOpened) {
    conOpened = false;
    promptShown = false;
    len = 0;
    pwMode = false;
    greet();
  }
#endif
  int budget = 64;  // characters per loop pass
  while (budget-- > 0 && port().available() > 0) input((char)port().read());
  scanPoll();
  connectPoll();
  static uint32_t evAt = 0;
  if (millis() - evAt >= 250) {
    evAt = millis();
    events();
  }
  if (reprompt && !promptShown) showPrompt();
}
