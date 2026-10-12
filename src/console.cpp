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
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include "local_ctrl.h"
#include "settings_store.h"
#include "touch.h"
#include "ui.h"
#include "web.h"
#include "syslog.h"

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
static bool apiMode = false;               // machine mode for the Web Serial page (api on, spec 7b)
static uint8_t esc = 0;                    // skipping an escape sequence (arrow keys ...)
static bool escUsed = false;               // ... whose key already stopped "watch" etc.: no Up / Down
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
  bool inPw = false;  // inside the value of a "pw=" (as typed: shown as '*')
  for (int i = 0; i < len; ++i) {
    char c[2] = {line[i], 0};
    if (line[i] == ' ') inPw = false;
    if (pwMode || inPw) c[0] = '*';
    raw(c);
    if (i >= 2 && line[i] == '=' && tolower(line[i - 1]) == 'w' && tolower(line[i - 2]) == 'p' && (i == 2 || line[i - 3] == ' '))
      inPw = true;
  }
  promptShown = true;
  reprompt = false;
}

// ------------------------------------------------------------ command history (Up / Down keys)
// The last commands (up to 20, 1 kB), oldest first, each '\0'-terminated. Lines with a password
// (pw=) and password entries are not kept.
static char hist[1024];
static int histUsed = 0;
static int histPos = -1;  // -1 = the line being typed, 0 = the newest entry, 1 = the one before ...

static int histCount() {
  int n = 0;
  for (int i = 0; i < histUsed; ++i)
    if (!hist[i]) ++n;
  return n;
}

static const char* histGet(int k) {  // k = 0: the newest
  const int want = histCount() - 1 - k;
  if (want < 0 || k < 0) return nullptr;
  const char* p = hist;
  for (int i = 0; i < want; ++i) p += strlen(p) + 1;
  return p;
}

static bool hasPw(const char* t) {
  for (const char* p = t; *p; ++p)
    if ((p == t || p[-1] == ' ') && tolower(p[0]) == 'p' && tolower(p[1]) == 'w' && p[2] == '=') return true;
  return false;
}

static void histAdd(const char* t) {
  while (*t == ' ') ++t;
  const size_t n = strlen(t);
  if (!n || n + 1 > sizeof(hist) || hasPw(t)) return;
  const char* last = histGet(0);
  if (last && !strcmp(last, t)) return;
  while (histUsed && (histUsed + (int)n + 1 > (int)sizeof(hist) || histCount() >= 20)) {  // drop the oldest
    const int f = strlen(hist) + 1;
    memmove(hist, hist + f, histUsed - f);
    histUsed -= f;
  }
  memcpy(hist + histUsed, t, n + 1);
  histUsed += n + 1;
}

static void histShow(int pos) {  // puts entry `pos` (-1 = empty line) on the command line
  const char* t = pos < 0 ? "" : histGet(pos);
  if (!t) return;
  histPos = pos;
  strlcpy(line, t, sizeof(line));
  len = strlen(line);
  if (promptShown && portReady()) raw("\r\x1b[K");
  showPrompt();
}

static void histKey(char k) {  // 'A' = Up, 'B' = Down
  if (pwMode) return;
  if (k == 'A' && histPos + 1 < histCount()) histShow(histPos + 1);
  else if (k == 'B' && histPos >= 0) histShow(histPos - 1);
}

// before output that is not the answer to a typed command (events, async results): clear the line
static void breakLine() {
  if (promptShown && portReady()) raw("\r\x1b[K");
  promptShown = false;
  reprompt = true;
}

static bool apiLogWrite(const uint8_t* b, size_t n);  // api mode: log lines as {"t":"log"} (below)

size_t LogPort::write(const uint8_t* b, size_t n) {
  syslogTee(b, n);  // every log line also into the system log (web System page)
  if (SHARED && apiLogWrite(b, n)) return n;
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
  char* k[8];            // keys (lower case)
  char* v[8];            // values (as typed)
  bool over;             // more than 8 pairs (the rest is dropped)
  const char* get(const char* key) const {
    for (int i = 0; i < nk; ++i)
      if (!strcmp(k[i], key)) return v[i];
    return nullptr;
  }
  const char* word(int i) const { return i < n ? w[i] : ""; }
};

static void parse(char* s, Args& a) {
  a.n = a.nk = 0;
  a.over = false;
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
      if (a.nk < 8) {
        a.k[a.nk] = start;
        a.v[a.nk++] = val;
      } else a.over = true;
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
  say("%s %s (%s%s%s) - serial console", PRODUCT_NAME, FW_VERSION, BOARD_NAME, BOARD_VARIANT[0] ? " " : "", BOARD_VARIANT);
  char nb[CH_NAME_LEN], nt[CH_NAME_LEN];
  {
    Guard g;
    strlcpy(nb, gSettings.ch[0].name, sizeof(nb));
    strlcpy(nt, gSettings.ch[1].name, sizeof(nt));
  }
  say("b = bottom (%s), t = top (%s). A value with spaces in quotes: s=\"My Net\"", nb, nt);
}

// Help: sections (help <name> prints one); "settings" has a long page of its own
static const char* const H_STATUS[] = {
    "STATUS",
    "  status                         temperatures, setpoints, outputs, run and Wi-Fi",
    "  watch                          status as one line per second, any key stops",
    "  info                           firmware, board, uptime, memory, network",
    "  log [n]                        the last n events (default 20)",
};
static const char* const H_MANUAL[] = {
    "MANUAL CONTROL",
    "  manual b|t on [sp=<C>]         manual setpoint on",
    "  manual b|t sp=<C>              change the manual setpoint (switches it on)",
    "  manual b|t off                 outputs off",
    "  ack b|t                        acknowledge a channel fault",
};
static const char* const H_RUN[] = {
    "PROGRAMS AND RUNS",
    "  programs                       list the stored programs (* = selected)",
    "  program n=<n>                  select a program for the next run",
    "  show                           print the selected / running program",
    "  start | pause | resume | stop  run control",
    "  next | continue                next phase / continue after a program pause",
    "  extend b|t +<s>                extend the running hold",
    "  target b|t <C>                 change the current / kept target",
    "  resume run | dismiss run       after \"Run interrupted by a reset\"",
    "  save | discard                 keep / drop the changes made during the last run",
};
static const char* const H_TUNE[] = {
    "AUTOTUNE (local channels)",
    "  tune b|t start temp=<C>        find PID values around a test temperature",
    "  tune b|t stop                  stop it (outputs off)",
    "  tune b|t accept | discard      take over / drop the values found",
};
static const char* const H_WIFI[] = {
    "WI-FI",
    "  wifi status                    mode, network, IP address, signal",
    "  wifi scan                      list networks (numbered)",
    "  wifi connect n=<n> [pw=<PW>]   join a network from the scan list",
    "  wifi connect s=<SSID> [pw=<PW>]  join a network by name",
    "  wifi ap [pw=<PW>]              start the access point (TempCtl-Duo-XXXX)",
    "  wifi forget                    forget the network: access point only",
    "  Without pw= the password is asked for (hidden).",
};
static const char* const H_SETTINGS_SHORT[] = {
    "SETTINGS (help settings: all settings and their values)",
    "  get [b|t|bus|run|wifi|display] show settings (as key=value, ready for set)",
    "  set <key>=<value> ...          change settings, checked like on the web page",
    "                                 e.g. set b.spMax=300 t.endAction=off",
    "  factory reset yes              all settings to defaults, Wi-Fi included",
    "                                 (programs are kept)",
#if defined(TOUCH_XPT2046)
    "  touch cal                      calibrate the touch screen (tap 4 targets)",
#endif
};
static const char* const H_DEVICE[] = {
    "DEVICE",
    "  reboot                         restart (not while a program runs)",
    "  screen restart                 restart only the display and the touch controller",
    "                                 (when the screen hangs or shows rubbish)",
    "  screen check                   what the automatic screen check sees now",
    "  help [section]                 this list, or one section: help wifi",
    "  Up / Down keys                 the last commands (without passwords)",
    "  api on | api off               machine mode (JSON lines) for programs and the",
    "                                 Web Serial page (help api: how it works)",
};
static const char* const H_API[] = {
    "API MODE - JSON lines for programs (the Web Serial page / local app uses it)",
    "  api on                       start; the device answers with a hello line:",
    "                               {\"t\":\"hello\",\"api\":1,\"fw\":\"...\",\"board\":\"...\",...}",
    "  then one JSON object per line, answered with the same \"id\":",
    "  GET   {\"id\":1,\"p\":\"/api/live\"}",
    "  POST  {\"id\":2,\"m\":\"POST\",\"p\":\"/api/select\",\"b\":{\"i\":0}}   (b: the JSON body)",
    "  PING  {\"id\":3,\"m\":\"PING\"}   (keep-alive; any request counts)",
    "  answer: {\"id\":1,\"code\":200,\"body\":{...}}  (code 400 / 404: body {\"ok\":false,\"err\":...})",
    "  text (CSV) in pieces: {\"id\":4,\"code\":200,\"type\":\"text/csv\",\"chunk\":\"...\"}",
    "                        ... then {\"id\":4,\"end\":true}",
    "  paths, parameters and JSON as the HTTP API (README section 5), e.g. /api/status,",
    "  /api/chart?v=live, /api/programs, /api/program?i=0, /api/config, /api/log?since=0,",
    "  /api/history.csv, /api/manual, /api/run, /api/cmd",
    "  CYD: log lines come as {\"t\":\"log\",\"m\":\"...\"} (the S3 logs on its UART port)",
    "  api off                      back to this console ({\"t\":\"bye\"}); also after 15 s",
    "                               without a request",
    "  e.g. api on, then {\"id\":1,\"p\":\"/api/live\"}",
};
static const char* const H_SETTINGS[] = {
    "SETTINGS - all settings, changed with set, shown with get",
    "  get                          all settings below as key=value (passwords never shown)",
    "  get <section>                one section: b, t, bus, run, wifi, display",
    "  set <key>=<value> ...        change one or more settings; checked first, nothing is",
    "                               changed if one value is wrong; saved and used at once",
    "  factory reset yes            all settings to defaults, Wi-Fi included (programs kept)",
#if defined(TOUCH_XPT2046)
    "  touch cal                    calibrate the touch screen (CYD)",
#endif
    "  b. = bottom channel, t. = top channel. On/off values: on|off (also true|false, 1|0)",
    "  e.g. get b                   show the bottom channel's settings",
    "",
    "CHANNEL (b.* / t.*)",
    "  name=<text>                  shown on all screens, 1 ... 31 characters",
    "  type=off|local|modbus        what the channel is (only what this board can wire;",
    "                               modbus is beta)",
    "  spMin=<C>  spMax=<C>         setpoint limits, -200 ... 1500",
    "                               (local channel: spMax at most maxTemp - 10)",
    "  endAction=keep|sp|off        when a program ends or stops: keep the last setpoint /",
    "                               go to endSp / outputs off (keep and sp continue as manual)",
    "  endSp=<C>                    the setpoint for endAction=sp",
    "  e.g. set b.name=\"Bottom heater\" b.spMax=300 t.endAction=sp t.endSp=100",
    "",
    "LOCAL CHANNEL: THERMOCOUPLE, PID, SSR (b.local.* / t.local.*)",
    "  sensor=max31855|max6675      thermocouple converter",
    "  calOffset=<C>                calibration offset, -50 ... 50",
    "  calGain=<x>                  calibration gain, 0.5 ... 2",
    "  kp=<x>  ki=<x>  kd=<x>       PID values: 0 ... 1000 / 0 ... 100 / 0 ... 10000",
    "                               (or let the autotune find them: tune b|t start temp=<C>)",
    "  windowSec=<s>                SSR time window, 0.2 ... 10",
    "  maxOut=<%>                   highest output, 0 ... 100",
    "  maxTemp=<C>                  cut-off: outputs off above it, 50 ... 600",
    "  e.g. set b.local.calOffset=-1.5 b.local.kp=2.5 b.local.ki=0.02 b.local.kd=40",
    "",
    "RUNAWAY PROTECTION (b.local.runaway.* / t.local.runaway.*,",
    "                    Modbus channel: b.modbus.runaway.* / t.modbus.runaway.*)",
    "  enable=on|off                runaway detection",
    "  minOut=<%>  heatSec=<s>      heating failed: output at least minOut (0 ... 100) for",
    "  minRise=<C>                  heatSec (10 ... 3600) with a rise below minRise (0.5 ... 100)",
    "  holdBand=<C>  holdSec=<s>    holding failed: PV more than holdBand (1 ... 200) off the",
    "                               setpoint for longer than holdSec (5 ... 3600)",
    "  e.g. set b.local.runaway.enable=on b.local.runaway.heatSec=300 b.local.runaway.minRise=5",
    "",
    "MODBUS CONTROLLER - beta (b.modbus.* / t.modbus.*)",
    "  addr=<n>                     controller address, 1 ... 247",
    "  profile=pma|generic          controller type; a new type loads its register defaults",
    "  rampMode=host|controller     who ramps the setpoint: this device or the controller",
    "  minStep=<C>                  smallest setpoint change that is written, 0.1 ... 100",
    "  minWriteSec=<s>              at most one setpoint write per minWriteSec, 1 ... 3600",
    "  lockFront=on|off             lock the controller's front keys during a run",
    "  reg.<name>=<addr>            register addresses: pv spEff out status alarm sp spLo spHi",
    "                               rampRate ctrlOff remote (0 ... 65534, empty = not used)",
    "  reg.scale=<x>  reg.offRaw=<n>  value scale (1 ... 1000), raw \"outputs off\" value",
    "  reg.readFn=3|4  reg.writeFn=6|16   Modbus function codes",
    "  e.g. set t.modbus.addr=2 t.modbus.profile=generic t.modbus.reg.pv=1 t.modbus.reg.sp=2",
    "",
    "RS485 BUS - beta (bus.*)",
    "  baud=2400|4800|9600|19200|38400",
    "  parity=none|even|odd",
    "  stopBits=1|2",
    "  timeoutMs=<ms>               answer timeout, 50 ... 2000",
    "  commLossSec=<s>              pause the run after no answer for this long, 0 ... 3600",
    "                               (0 = never)",
    "  e.g. set bus.baud=19200 bus.parity=none bus.commLossSec=30",
    "",
    "POLLING AND RUNS",
    "  bus.pollMs=<ms>              how often PV, SP and output are read, 250 ... 10000",
    "  run.bottomBand=<C>           \"wait for bottom\" goes on within +- this, 0.5 ... 50",
    "  e.g. set bus.pollMs=500 run.bottomBand=3",
    "",
    "NETWORK (wifi.*; network name and passwords: wifi connect / wifi ap)",
    "  hostname=<name>              http://<name>.local; letters, digits and '-', 1 ... 23",
    "  staticIp=on|off              fixed IP address instead of DHCP",
    "  ip=  subnet=  gateway=       fixed address, subnet mask, router (a.b.c.d)",
    "  dns1=  dns2=                 DNS servers (empty dns1 = the gateway)",
    "  Network settings are applied right away (the Wi-Fi reconnects).",
    "  e.g. set wifi.staticIp=on wifi.ip=192.168.1.60 wifi.subnet=255.255.255.0 wifi.gateway=192.168.1.1",
    "",
    "DISPLAY (display.*)",
    "  brightness=<n>               touch screen backlight, 10 ... 255",
    "  themeDark=on|off             dark theme on the touch screen",
    "  accent=<name>|#RRGGBB        blue, pink, violet, orange, mauve, green or any colour",
    "  rotate180=on|off             turn the screen upside down",
    "  screenCheck=on|off           restart a hanging display / touch controller by itself",
    "  liveMin=<min>                live chart period: 1 5 10 15 30 60 120 240 480 720 1440",
    "  e.g. set display.accent=orange display.brightness=180 display.liveMin=30",
    "",
    "TIME (time.*)",
    "  zone=<zone>                  time zone for the SD run logs (local time): a name from",
    "                               \"help time\", UTC+hh:mm (no summer time), or empty (UTC);",
    "                               set from the web browser the first time",
    "  e.g. set time.zone=Europe/Berlin",
};

struct HelpSec {
  const char* names;  // '|'-separated names for "help <name>"
  const char* const* lines;
  int n;
};
#define HS(names, arr) {names, arr, (int)(sizeof(arr) / sizeof(arr[0]))}
static const HelpSec HELP[] = {
    HS("status|watch|info|log", H_STATUS),
    HS("manual|ack", H_MANUAL),
    HS("programs|program|runs|run|start", H_RUN),
    HS("autotune|tune", H_TUNE),
    HS("wifi|network", H_WIFI),
    HS("", H_SETTINGS_SHORT),  // (help settings: the long page)
    HS("device|reboot|help", H_DEVICE),
};

static bool nameIn(const char* names, const char* w) {
  const size_t n = strlen(w);
  for (const char* p = names; *p;) {
    const char* e = strchr(p, '|');
    const size_t l = e ? (size_t)(e - p) : strlen(p);
    if (l == n && !strncasecmp(p, w, n)) return true;
    if (!e) break;
    p = e + 1;
  }
  return false;
}

static void printLines(const char* const* lines, int n) {
  for (int i = 0; i < n; ++i) say("%s", lines[i]);
}

static void help(const char* section = nullptr) {
  if (section && *section) {
    if (!strcasecmp(section, "settings") || !strcasecmp(section, "set") || !strcasecmp(section, "get"))
      return printLines(H_SETTINGS, sizeof(H_SETTINGS) / sizeof(H_SETTINGS[0]));
    if (!strcasecmp(section, "api") || !strcasecmp(section, "json"))
      return printLines(H_API, sizeof(H_API) / sizeof(H_API[0]));
    if (!strcasecmp(section, "time") || !strcasecmp(section, "timezone") || !strcasecmp(section, "zones")) {
      char now[48] = "", line[120] = "";
      {
        Guard g;
        strlcpy(now, gSettings.tzName, sizeof(now));
      }
      say("time zone now: %s   (set time.zone=<zone>; UTC+hh:mm for a fixed offset; empty = UTC)", now[0] ? now : "not set (UTC)");
      for (int i = 0; i < tzCount(); ++i) {  // several names per line
        const char* z = tzListName(i);
        if (strlen(line) + strlen(z) + 2 > 96) {
          say("  %s", line);
          line[0] = 0;
        }
        if (line[0]) strlcat(line, "  ", sizeof(line));
        strlcat(line, z, sizeof(line));
      }
      if (line[0]) say("  %s", line);
      return;
    }
    for (const HelpSec& h : HELP)
      if (nameIn(h.names, section)) return printLines(h.lines, h.n);
    return err("no help section \"%s\" (status, manual, programs, autotune, wifi, settings, time, device, api)", section);
  }
  banner();
  for (const HelpSec& h : HELP) {
    say("%s", "");
    printLines(h.lines, h.n);
  }
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
  if (C.type == CH_LOCAL) {
    LocalTune lt;
    localCtrlTuneGet(ch, lt);
    if (lt.state == TuneState::Running) {
      snprintf(b, n, "autotune at %.0f \xC2\xB0" "C, cycle %d", lt.sp, lt.cycle);
      return b;
    }
    if (lt.state == TuneState::Done) {
      snprintf(b, n, "autotune done: kp %.3g ki %.3g kd %.3g - tune %s accept|discard", lt.res.kp, lt.res.ki,
               lt.res.kd, ch ? "t" : "b");
      return b;
    }
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
  if (!strcmp(sub, "forget")) {
    {
      Guard g;
      gSettings.ssid[0] = 0;
      memset(gSettings.pass, 0, sizeof(gSettings.pass));
    }
    saveSettingsQuiet();
    netApplyWifi();  // no station: the access point runs
    return say("network forgotten - access point \"%s\" (connect with wifi connect ...)", netApName().length() ? netApName().c_str() : AP_NAME_PREFIX "-XXXX");
  }
  err("wifi scan | wifi connect ... | wifi ap [pw=...] | wifi forget | wifi status");
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
    sendCmd(CMD_DISCARD_RUN);
    return say("the run's changes are dropped");
  }
}

// ------------------------------------------------------------ status extras: watch, info, log
static bool watching = false;
static uint32_t watchAt = 0;

static void watchLine() {
  struct {
    float pv[2], sp[2], out[2];
    bool pvOk[2], outOk[2], manual[2], fault[2];
    RunState st;
    uint8_t type;
    StageInfo seq;
    float el;
  } W;
  {
    Guard g;
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      const ChannelLive& C = gLive.ch[ch];
      W.pv[ch] = C.pv;
      W.pvOk[ch] = C.pvValid;
      W.sp[ch] = C.spEffValid ? C.spEff : (C.manualOn ? C.manualSp : NAN);
      if (isnan(W.sp[ch]) && gLive.run[ch].inProgram && !gLive.run[ch].idle) W.sp[ch] = gLive.run[ch].sp;
      W.out[ch] = C.out;
      W.outOk[ch] = C.outValid;
      W.manual[ch] = C.manualOn;
      W.fault[ch] = C.fault != 0;
    }
    W.st = gLive.state;
    W.type = gLive.progType;
    W.seq = gLive.seq;
    W.el = gLive.elapsed;
  }
  char line[160] = "", part[64];
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    char pv[12] = "-", sp[12] = "-", out[8] = "-";
    if (W.pvOk[ch]) snprintf(pv, sizeof(pv), "%.1f", W.pv[ch]);
    if (!isnan(W.sp[ch])) snprintf(sp, sizeof(sp), "%.1f", W.sp[ch]);
    if (W.outOk[ch]) snprintf(out, sizeof(out), "%.0f%%", W.out[ch]);
    snprintf(part, sizeof(part), "%s%s PV %6s SP %6s %4s%s", ch ? " | " : "", ch ? "t" : "b", pv, sp, out,
             W.fault[ch] ? " FAULT" : W.manual[ch] ? " man" : "");
    strlcat(line, part, sizeof(line));
  }
  if (W.st == RunState::Running || W.st == RunState::Paused) {
    char t[16];
    fmtT(W.el, t, sizeof(t));
    if (W.type == PROG_SEQ) snprintf(part, sizeof(part), " | %s %s P%d %s", W.st == RunState::Paused ? "paused" : "run", t, W.seq.idx + 1, stageName(W.seq.st));
    else snprintf(part, sizeof(part), " | %s %s", W.st == RunState::Paused ? "paused" : "run", t);
    strlcat(line, part, sizeof(line));
  }
  say("%s", line);
}

static void cmdInfo() {
  char up[16];
  fmtT(millis() / 1000.0f, up, sizeof(up));
  say("firmware  %s %s, %s%s%s", PRODUCT_NAME, FW_VERSION, BOARD_NAME, BOARD_VARIANT[0] ? " " : "", BOARD_VARIANT);
  say("uptime    %s", up);
  const uint32_t caps = MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL;
#ifdef BOARD_HAS_PSRAM
  say("memory    RAM %u kB free (lowest %u kB, largest block %u kB), PSRAM %u kB free",
      (unsigned)(heap_caps_get_free_size(caps) / 1024), (unsigned)(heap_caps_get_minimum_free_size(caps) / 1024),
      (unsigned)(heap_caps_get_largest_free_block(caps) / 1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
#else
  say("memory    RAM %u kB free (lowest %u kB, largest block %u kB)", (unsigned)(heap_caps_get_free_size(caps) / 1024),
      (unsigned)(heap_caps_get_minimum_free_size(caps) / 1024), (unsigned)(heap_caps_get_largest_free_block(caps) / 1024));
#endif
  char h[16];
  const uint32_t m = liveHistSec() / 60;
  if (m % 60) snprintf(h, sizeof(h), "%lu min", (unsigned long)m);
  else snprintf(h, sizeof(h), "%lu h", (unsigned long)(m / 60));
  say("history   live history in memory: %s (lost on restart)", h);
  String ni = netInfo();
  ni.replace("\n", ", ");
  say("network   %s", ni.length() ? ni.c_str() : "off");
  say("web       http://%s.local/", S.hostname);
  ProgSummary* sums = (ProgSummary*)malloc(sizeof(ProgSummary) * PROG_MAX);
  if (sums) {
    programSummaries(sums);
    int used = 0;
    for (int i = 0; i < PROG_MAX; ++i) used += sums[i].used ? 1 : 0;
    const ProgSummary& sel = sums[S.selProgram < PROG_MAX ? S.selProgram : 0];
    say("programs  %d of %d slots used, selected %d%s%s%s", used, PROG_MAX, S.selProgram + 1, sel.used ? " \"" : " (empty)",
        sel.used ? sel.name : "", sel.used ? "\"" : "");
    free(sums);
  }
}

static void cmdLog(const Args& a) {
  float v = 20;
  if (*a.word(1) && (!number(a.word(1), v) || v < 1)) return err("log [n]: n = 1 ... %d", LOG_N);
  int n = (int)v;
  if (n > LOG_N) n = LOG_N;
  LogEntry* e = (LogEntry*)malloc(sizeof(LogEntry) * n);
  if (!e) return err("out of memory");
  const uint32_t seq = logSeq();
  const int got = logSince(seq > (uint32_t)n ? seq - n : 0, e, n);
  if (!got) say("no events yet");
  for (int i = 0; i < got; ++i) {
    char t[20], u[16];
    if (!isnan(e[i].runT)) {
      fmtT(e[i].runT, u, sizeof(u));
      snprintf(t, sizeof(t), "run %s", u);
    } else {
      fmtT(e[i].upSec, u, sizeof(u));
      snprintf(t, sizeof(t), "up %s", u);
    }
    const char* tag = e[i].kind == LK_ERROR ? "ERROR: " : e[i].kind == LK_WARN ? "warning: " : "";
    say("[%10s] %s%s", t, tag, e[i].msg);
  }
  free(e);
}

// ------------------------------------------------------------ faults, autotune
static void cmdAck(const Args& a) {
  const int ch = chanOf(a.word(1));
  if (ch < 0) return err("ack b|t");
  if (!L.ch[ch].fault) return err("%s has no fault", chWord(ch));
  sendCmd(CMD_ACK_FAULT, ch);
  say("%s: acknowledging \"%s\"", chWord(ch), L.ch[ch].faultText);
}

static void cmdTune(const Args& a) {
  const int ch = chanOf(a.word(1));
  const char* what = a.word(2);
  if (ch < 0) return err("tune b|t start temp=<C> | tune b|t stop | tune b|t accept|discard");
  if (S.ch[ch].type != CH_LOCAL) return err("%s is not a local channel (Modbus controllers tune themselves)", chWord(ch));
  LocalTune t;
  localCtrlTuneGet(ch, t);
  if (!strcmp(what, "start")) {
    float v;
    const float hi = S.ch[ch].loc.maxTemp - 20;
    if (!number(a.get("temp"), v)) return err("tune %s start temp=<C> (50 ... %.0f)", a.word(1), hi);
    if (v < 50 || v > hi) return err("temp=%s: allowed 50 ... %.0f (cut-off temperature - 20)", a.get("temp"), hi);
    if (runActive() && L.run[ch].inProgram) return err("%s is running a program", chWord(ch));
    if (L.ch[ch].fault) return err("%s has a fault: ack %s first", chWord(ch), a.word(1));
    sendCmd(CMD_TUNE_START, ch, v);
    return say("%s: autotune at %.0f \xC2\xB0" "C - the heater is switched on and off around it (tune %s stop ends it)",
               chWord(ch), v, a.word(1));
  }
  if (!strcmp(what, "stop")) {
    if (t.state != TuneState::Running) return err("no autotune is running on %s", chWord(ch));
    sendCmd(CMD_TUNE_STOP, ch);
    return say("%s: autotune stopped, outputs off", chWord(ch));
  }
  if (!strcmp(what, "accept")) {
    if (t.state != TuneState::Done) return err("no autotune result for %s", chWord(ch));
    {
      Guard g;
      gSettings.ch[ch].loc.kp = t.res.kp;
      gSettings.ch[ch].loc.ki = t.res.ki;
      gSettings.ch[ch].loc.kd = t.res.kd;
    }
    saveSettingsAndNotify();
    localCtrlTuneClear(ch);
    return say("%s: PID values taken over: kp %.3g ki %.3g kd %.3g", chWord(ch), t.res.kp, t.res.ki, t.res.kd);
  }
  if (!strcmp(what, "discard")) {
    if (t.state != TuneState::Done && t.state != TuneState::Failed) return err("no autotune result for %s", chWord(ch));
    localCtrlTuneClear(ch);
    return say("%s: autotune result dropped", chWord(ch));
  }
  // no sub-command: the state
  if (t.state == TuneState::Running) say("%s: autotune at %.0f \xC2\xB0" "C, cycle %d", chWord(ch), t.sp, t.cycle);
  else if (t.state == TuneState::Done) say("%s: autotune done: kp %.3g ki %.3g kd %.3g (accept / discard)", chWord(ch), t.res.kp, t.res.ki, t.res.kd);
  else if (t.state == TuneState::Failed) say("%s: autotune failed: %s", chWord(ch), t.msg);
  else say("%s: no autotune (tune %s start temp=<C>)", chWord(ch), a.word(1));
}

// ------------------------------------------------------------ settings: get / set
// One table: the keys of the settings API (channels[i]. -> b. / t.), what values they take (checked
// before anything is changed) and how get shows them.
enum KType : uint8_t { K_NUM, K_INT, K_BOOL, K_CHOICE, K_SET, K_STR, K_IP, K_ADDR, K_ACCENT, K_PARITY, K_HOST, K_TZ };
enum KSec : uint8_t { KS_CH, KS_BUS, KS_RUN, KS_WIFI, KS_DISP, KS_TIME };
static const char* const KSEC_NAME[] = {"", "bus", "run", "wifi", "display", "time"};
struct SKey {
  uint8_t sec, t;
  const char* key;  // path below the section (channels: below channels[i])
  float lo, hi;
  const char* opts;  // K_CHOICE / K_SET / K_PARITY: '|'-separated
};
#define RUNAWAY(p)                                                                                      \
  {KS_CH, K_BOOL, p "runaway.enable", 0, 0, nullptr}, {KS_CH, K_NUM, p "runaway.minOut", 0, 100, nullptr}, \
      {KS_CH, K_INT, p "runaway.heatSec", 10, 3600, nullptr}, {KS_CH, K_NUM, p "runaway.minRise", 0.5f, 100, nullptr}, \
      {KS_CH, K_NUM, p "runaway.holdBand", 1, 200, nullptr}, {KS_CH, K_INT, p "runaway.holdSec", 5, 3600, nullptr}
static const SKey KEYS[] = {
    {KS_CH, K_STR, "name", 1, 31, nullptr},
    {KS_CH, K_CHOICE, "type", 0, 0, "off|local|modbus"},
    {KS_CH, K_NUM, "spMin", -200, 1500, nullptr},
    {KS_CH, K_NUM, "spMax", -200, 1500, nullptr},
    {KS_CH, K_CHOICE, "endAction", 0, 0, "keep|sp|off"},
    {KS_CH, K_NUM, "endSp", -200, 1500, nullptr},
    {KS_CH, K_CHOICE, "local.sensor", 0, 0, "max31855|max6675"},
    {KS_CH, K_NUM, "local.calOffset", -50, 50, nullptr},
    {KS_CH, K_NUM, "local.calGain", 0.5f, 2, nullptr},
    {KS_CH, K_NUM, "local.kp", 0, 1000, nullptr},
    {KS_CH, K_NUM, "local.ki", 0, 100, nullptr},
    {KS_CH, K_NUM, "local.kd", 0, 10000, nullptr},
    {KS_CH, K_NUM, "local.windowSec", 0.2f, 10, nullptr},
    {KS_CH, K_NUM, "local.maxOut", 0, 100, nullptr},
    {KS_CH, K_NUM, "local.maxTemp", 50, 600, nullptr},
    RUNAWAY("local."),
    {KS_CH, K_INT, "modbus.addr", 1, 247, nullptr},
    {KS_CH, K_CHOICE, "modbus.profile", 0, 0, "pma|generic"},
    {KS_CH, K_CHOICE, "modbus.rampMode", 0, 0, "host|controller"},
    {KS_CH, K_NUM, "modbus.minStep", 0.1f, 100, nullptr},
    {KS_CH, K_INT, "modbus.minWriteSec", 1, 3600, nullptr},
    {KS_CH, K_BOOL, "modbus.lockFront", 0, 0, nullptr},
    {KS_CH, K_ADDR, "modbus.reg.pv", 0, 0, nullptr},
    {KS_CH, K_ADDR, "modbus.reg.spEff", 0, 0, nullptr},
    {KS_CH, K_ADDR, "modbus.reg.out", 0, 0, nullptr},
    {KS_CH, K_ADDR, "modbus.reg.status", 0, 0, nullptr},
    {KS_CH, K_ADDR, "modbus.reg.alarm", 0, 0, nullptr},
    {KS_CH, K_ADDR, "modbus.reg.sp", 0, 0, nullptr},
    {KS_CH, K_ADDR, "modbus.reg.spLo", 0, 0, nullptr},
    {KS_CH, K_ADDR, "modbus.reg.spHi", 0, 0, nullptr},
    {KS_CH, K_ADDR, "modbus.reg.rampRate", 0, 0, nullptr},
    {KS_CH, K_ADDR, "modbus.reg.ctrlOff", 0, 0, nullptr},
    {KS_CH, K_ADDR, "modbus.reg.remote", 0, 0, nullptr},
    {KS_CH, K_NUM, "modbus.reg.scale", 1, 1000, nullptr},
    {KS_CH, K_INT, "modbus.reg.offRaw", -32768, 32767, nullptr},
    {KS_CH, K_SET, "modbus.reg.readFn", 0, 0, "3|4"},
    {KS_CH, K_SET, "modbus.reg.writeFn", 0, 0, "6|16"},
    RUNAWAY("modbus."),
    {KS_BUS, K_SET, "baud", 0, 0, "2400|4800|9600|19200|38400"},
    {KS_BUS, K_PARITY, "parity", 0, 0, "none|even|odd"},
    {KS_BUS, K_SET, "stopBits", 0, 0, "1|2"},
    {KS_BUS, K_INT, "timeoutMs", 50, 2000, nullptr},
    {KS_BUS, K_INT, "commLossSec", 0, 3600, nullptr},
    {KS_BUS, K_INT, "pollMs", 250, 10000, nullptr},
    {KS_RUN, K_NUM, "bottomBand", 0.5f, 50, nullptr},
    {KS_WIFI, K_HOST, "hostname", 1, 23, nullptr},
    {KS_WIFI, K_BOOL, "staticIp", 0, 0, nullptr},
    {KS_WIFI, K_IP, "ip", 0, 0, nullptr},
    {KS_WIFI, K_IP, "subnet", 0, 0, nullptr},
    {KS_WIFI, K_IP, "gateway", 0, 0, nullptr},
    {KS_WIFI, K_IP, "dns1", 0, 0, nullptr},
    {KS_WIFI, K_IP, "dns2", 0, 0, nullptr},
    {KS_DISP, K_INT, "brightness", 10, 255, nullptr},
    {KS_DISP, K_BOOL, "themeDark", 0, 0, nullptr},
    {KS_DISP, K_ACCENT, "accent", 0, 0, nullptr},
    {KS_DISP, K_BOOL, "rotate180", 0, 0, nullptr},
    {KS_DISP, K_BOOL, "screenCheck", 0, 0, nullptr},
    {KS_DISP, K_SET, "liveMin", 0, 0, "1|5|10|15|30|60|120|240|480|720|1440"},
    {KS_TIME, K_TZ, "zone", 0, 0, nullptr},
};
static const char* const ACCENT_NAMES[] = {"blue", "pink", "violet", "orange", "mauve", "green"};
static const uint32_t ACCENT_RGB[] = {0x2196F3, 0xD1449B, 0x6C71C4, 0xD65D0E, 0x8839EF, 0x50A14F};

// i-th option of "a|b|c" into out; false past the end
static bool opt(const char* opts, int i, char* out, size_t n) {
  const char* p = opts;
  for (int k = 0; k < i; ++k) {
    p = strchr(p, '|');
    if (!p) return false;
    ++p;
  }
  const char* e = strchr(p, '|');
  snprintf(out, n, "%.*s", (int)(e ? e - p : (long)strlen(p)), p);
  return true;
}
static int optIndex(const char* opts, const char* v) {
  char o[24];
  for (int i = 0; opt(opts, i, o, sizeof(o)); ++i)
    if (!strcasecmp(o, v)) return i;
  return -1;
}

// the JSON value of key k (channel ch) in a settings document
static JsonVariantConst kValue(JsonObjectConst root, const SKey& k, int ch) {
  JsonVariantConst v = k.sec == KS_CH ? root["channels"][ch] : root[KSEC_NAME[k.sec]];
  char seg[24];
  for (const char* p = k.key; *p;) {
    const char* e = strchr(p, '.');
    snprintf(seg, sizeof(seg), "%.*s", (int)(e ? e - p : (long)strlen(p)), p);
    v = v[seg];
    if (!e) break;
    p = e + 1;
  }
  return v;
}

// a value as get shows it (and set accepts it back)
static void kFormat(JsonVariantConst v, const SKey& k, char* b, size_t n) {
  switch (k.t) {
    case K_NUM: snprintf(b, n, "%.6g", v.as<float>()); break;
    case K_INT:
    case K_SET: snprintf(b, n, "%ld", v.as<long>()); break;
    case K_BOOL: snprintf(b, n, "%s", v.isNull() || v.as<bool>() ? "on" : "off"); break;  // themeDark: null = dark
    case K_PARITY: opt(k.opts, (int)v.as<long>(), b, n); break;
    case K_ADDR: {
      const long a = v.as<long>();
      if (a < 0) b[0] = 0;
      else snprintf(b, n, "%ld", a);
      break;
    }
    case K_ACCENT: {
      const char* h = v.as<const char*>();
      const uint32_t rgb = h && h[0] == '#' ? strtoul(h + 1, nullptr, 16) : 0;
      snprintf(b, n, "%s", h ? h : "");
      for (int i = 0; i < 6; ++i)
        if (rgb == ACCENT_RGB[i]) snprintf(b, n, "%s", ACCENT_NAMES[i]);
      break;
    }
    default: {
      const char* t = v.as<const char*>();
      if (t && strchr(t, ' ')) snprintf(b, n, "\"%s\"", t);
      else snprintf(b, n, "%s", t ? t : "");
    }
  }
}

static void keyName(const SKey& k, int ch, char* b, size_t n) {
  if (k.sec == KS_CH) snprintf(b, n, "%s.%s", ch ? "t" : "b", k.key);
  else snprintf(b, n, "%s.%s", KSEC_NAME[k.sec], k.key);
}

static bool keyShown(const SKey& k) {  // Modbus keys only where the board has RS485
  if (boardLimits().rs485) return true;
  return !(k.sec == KS_BUS && strcmp(k.key, "pollMs")) && !(k.sec == KS_CH && !strncmp(k.key, "modbus.", 7));
}

static void cmdGet(const Args& a) {
  const char* f = a.word(1);  // a section or any key prefix ("b", "b.local", "display")
  JsonDocument doc;
  settingsToJson(S, doc.to<JsonObject>(), true);
  JsonObjectConst root = doc.as<JsonObjectConst>();
  int shown = 0, lastGroup = -1;
  char name[48], val[48];
  for (int ch = 0; ch < NUM_CHANNELS + 1; ++ch) {  // the two channels, then the other sections
    for (const SKey& k : KEYS) {
      if ((ch < NUM_CHANNELS) != (k.sec == KS_CH) || !keyShown(k)) continue;
      keyName(k, ch < NUM_CHANNELS ? ch : 0, name, sizeof(name));
      const size_t fl = strlen(f);
      if (fl && !(strncasecmp(name, f, fl) == 0 && (name[fl] == 0 || name[fl] == '.'))) continue;
      const int group = ch < NUM_CHANNELS ? ch : 2 + k.sec;
      if (lastGroup >= 0 && group != lastGroup) say("%s", "");
      lastGroup = group;
      kFormat(kValue(root, k, ch < NUM_CHANNELS ? ch : 0), k, val, sizeof(val));
      say("%s=%s", name, val);
      ++shown;
    }
  }
  if (!shown) err("no settings \"%s\" (b, t, bus, run, wifi, display; help settings)", f);
}

static bool validHost(const char* h) {
  const size_t n = strlen(h);
  if (n < 1 || n > 23 || h[0] == '-' || h[n - 1] == '-') return false;
  for (const char* p = h; *p; ++p)
    if (!isalnum((unsigned char)*p) && *p != '-') return false;
  return true;
}

static bool validIp(const char* t) {
  if (!*t) return true;
  unsigned b[4];
  char end;
  return sscanf(t, "%u.%u.%u.%u%c", &b[0], &b[1], &b[2], &b[3], &end) == 4 && b[0] < 256 && b[1] < 256 && b[2] < 256 && b[3] < 256;
}

static void cmdSet(const Args& a) {
  if (!a.nk) return err("set <key>=<value> ... (help settings lists them)");
  if (a.over) return err("at most 8 settings at once - nothing changed");
  if (a.n > 1) return err("\"%s\" is not key=value - nothing changed", a.w[1]);
  struct Item {
    const SKey* k;
    int ch;
    char name[48];
    char val[48];  // canonical value (JSON string / number text)
  } it[8];
  JsonDocument patch;
  JsonObject root = patch.to<JsonObject>();
  JsonArray chs;
  bool wifi = false, host = false;
  for (int i = 0; i < a.nk; ++i) {
    // a.k[i] is lower case: match the table without case
    const char* key = a.k[i];
    const char* v = a.v[i];
    int ch = 0;
    const SKey* k = nullptr;
    for (const SKey& t : KEYS) {
      char n0[48], n1[48];
      keyName(t, 0, n0, sizeof(n0));
      keyName(t, 1, n1, sizeof(n1));
      if (!strcasecmp(key, n0)) { k = &t; ch = 0; }
      else if (t.sec == KS_CH && !strcasecmp(key, n1)) { k = &t; ch = 1; }
      if (k) break;
    }
    if (!k || !keyShown(*k)) return err("unknown setting \"%s\" (help settings lists them)", key);
    Item& m = it[i];
    m.k = k;
    m.ch = ch;
    keyName(*k, ch, m.name, sizeof(m.name));
    // the value: check and convert
    float f;
    char o[24];
    // the object the key lives in (created as needed) and the key in it; assigned through the object
    // (a JsonVariant taken from obj[key] does not create the member). Keys are char arrays: copied.
    JsonObject dobj;
    char dkey[24];
    {
      JsonObject obj;
      if (k->sec == KS_CH) {
        if (chs.isNull()) {
          chs = root["channels"].to<JsonArray>();
          chs.add(nullptr);
          chs.add(nullptr);
        }
        obj = chs[ch].is<JsonObject>() ? chs[ch].as<JsonObject>() : chs[ch].to<JsonObject>();
      } else {
        obj = root[KSEC_NAME[k->sec]].is<JsonObject>() ? root[KSEC_NAME[k->sec]].as<JsonObject>() : root[KSEC_NAME[k->sec]].to<JsonObject>();
      }
      char seg[24];
      for (const char* p = k->key;;) {
        const char* e = strchr(p, '.');
        snprintf(seg, sizeof(seg), "%.*s", (int)(e ? e - p : (long)strlen(p)), p);
        if (!e) {
          dobj = obj;
          strlcpy(dkey, seg, sizeof(dkey));
          break;
        }
        obj = obj[seg].is<JsonObject>() ? obj[seg].as<JsonObject>() : obj[seg].to<JsonObject>();
        p = e + 1;
      }
    }
    switch (k->t) {
      case K_NUM:
      case K_INT:
        if (!number(v, f)) return err("%s=%s: a number is expected", m.name, v);
        if (k->t == K_INT && f != floorf(f)) return err("%s=%s: a whole number is expected", m.name, v);
        if (f < k->lo || f > k->hi) return err("%s=%s: allowed %g ... %g", m.name, v, k->lo, k->hi);
        if (k->t == K_INT) dobj[dkey] = (long)f;
        else dobj[dkey] = f;
        break;
      case K_SET:
        if (!number(v, f) || optIndex(k->opts, v) < 0) return err("%s=%s: one of %s", m.name, v, k->opts);
        dobj[dkey] = (long)f;
        break;
      case K_BOOL: {
        const bool on = !strcasecmp(v, "on") || !strcasecmp(v, "true") || !strcmp(v, "1");
        const bool off = !strcasecmp(v, "off") || !strcasecmp(v, "false") || !strcmp(v, "0");
        if (!on && !off) return err("%s=%s: on or off", m.name, v);
        dobj[dkey] = on;
        break;
      }
      case K_CHOICE:
      case K_PARITY: {
        const int x = optIndex(k->opts, v);
        if (x < 0) return err("%s=%s: one of %s", m.name, v, k->opts);
        if (k->t == K_PARITY) dobj[dkey] = x;
        else {
          opt(k->opts, x, o, sizeof(o));
          strlcpy(m.val, o, sizeof(m.val));
          dobj[dkey] = m.val;
        }
        break;
      }
      case K_STR:
        if (strlen(v) < k->lo || strlen(v) > k->hi) return err("%s: %g ... %g characters", m.name, k->lo, k->hi);
        strlcpy(m.val, v, sizeof(m.val));
        dobj[dkey] = m.val;
        break;
      case K_TZ: {
        char rule[48];
        if (*v && !tzLookup(v, rule, sizeof(rule)))
          return err("%s=%s: a zone from \"help time\" (e.g. Europe/Berlin), UTC+hh:mm, or empty (UTC)", m.name, v);
        strlcpy(m.val, v, sizeof(m.val));
        dobj[dkey] = m.val;
        break;
      }
      case K_HOST:
        if (!validHost(v)) return err("%s=%s: letters, digits and '-', 1 ... 23 characters, not starting or ending with '-'", m.name, v);
        strlcpy(m.val, v, sizeof(m.val));
        dobj[dkey] = m.val;
        host = true;
        break;
      case K_IP:
        if (!validIp(v)) return err("%s=%s: an address a.b.c.d (or empty)", m.name, v);
        strlcpy(m.val, v, sizeof(m.val));
        dobj[dkey] = m.val;
        break;
      case K_ADDR:
        if (!*v) dobj[dkey] = -1;
        else if (!number(v, f) || f != floorf(f) || f < 0 || f > 65534) return err("%s=%s: 0 ... 65534 (empty = not used)", m.name, v);
        else dobj[dkey] = (long)f;
        break;
      case K_ACCENT: {
        int x = -1;
        for (int c = 0; c < 6; ++c)
          if (!strcasecmp(v, ACCENT_NAMES[c])) x = c;
        uint32_t rgb;
        if (x >= 0) rgb = ACCENT_RGB[x];
        else if (v[0] == '#' && strlen(v) == 7 && strspn(v + 1, "0123456789abcdefABCDEF") == 6) rgb = strtoul(v + 1, nullptr, 16);
        else return err("%s=%s: blue, pink, violet, orange, mauve, green or #RRGGBB", m.name, v);
        snprintf(m.val, sizeof(m.val), "#%06lX", (unsigned long)rgb);
        dobj[dkey] = m.val;
        break;
      }
    }
    if (k->sec == KS_WIFI) wifi = true;
  }
  // apply to a copy, check the rules between settings, then take it over (nothing changes on an error)
  Settings* nx = (Settings*)malloc(sizeof(Settings));
  if (!nx) return err("out of memory");
  char msg[200] = "";
  bool ok;
  {
    Guard g;
    *nx = gSettings;
    ok = settingsFromJson(patch.as<JsonObjectConst>(), *nx, msg, sizeof(msg)) && settingsValidate(*nx, boardLimits(), msg, sizeof(msg));
    if (ok) gSettings = *nx;
  }
  if (!ok) {
    free(nx);
    return err("%s - nothing changed", msg);
  }
  JsonDocument after;  // (may point into *nx: freed after the comparison below)
  settingsToJson(*nx, after.to<JsonObject>(), true);
  saveSettingsAndNotify();
  char want[48], got[48];
  for (int i = 0; i < a.nk; ++i) {
    const Item& m = it[i];
    kFormat(kValue(patch.as<JsonObjectConst>(), *m.k, m.ch), *m.k, want, sizeof(want));
    kFormat(kValue(after.as<JsonObjectConst>(), *m.k, m.ch), *m.k, got, sizeof(got));
    if (strcmp(want, got)) say("%s=%s (adjusted: it depends on other settings)", m.name, got);
    else say("%s=%s", m.name, got);
  }
  free(nx);
  say("saved%s%s", wifi ? ", Wi-Fi reconnects" : "", host ? " (the new host name is used after a reboot)" : "");
  if (wifi) netApplyWifi();  // after the report: its log lines come after it
}

static void cmdFactory(const Args& a) {
  if (strcmp(a.word(1), "reset") || strcmp(a.word(2), "yes"))
    return err("factory reset yes - all settings to defaults, Wi-Fi included (programs kept), then a restart");
  if (runActive()) return err("a program is running - stop it first");
  Settings* d = (Settings*)malloc(sizeof(Settings));
  if (!d) return err("out of memory");
  settingsDefaults(*d, boardLimits());
  {
    Guard g;
    gSettings = *d;
  }
  free(d);
  saveSettingsAndNotify();
  say("settings reset to defaults - restarting (then the access point %s is open)", netApName().c_str());
  appRequestRestart("after a factory reset from the serial console");
}

// ------------------------------------------------------------ touch calibration (CYD)
static bool calRunning = false;
static int calShownStep = 0;

static void cmdTouchCal(const Args& a) {
  if (strcmp(a.word(1), "cal")) return err("touch cal");
  if (!uiTouchCalStart()) return err("this touch screen needs no calibration (capacitive)");
  calRunning = true;
  calShownStep = 0;
  say("Tap the centre of each of the 4 crosses on the touch screen, one after the other");
  say("(with a pen or fingernail; any key here cancels)");
}

static void calPoll() {
  if (!calRunning) return;
  int step;
  uint16_t r[4];
  const int st = uiTouchCalPoll(step, r[0], r[1], r[2], r[3]);
  if (st == 0) {
    if (step != calShownStep && step > 1) {
      breakLine();
      say("cross %d / 4", step);
    }
    calShownStep = step;
    return;
  }
  calRunning = false;
  breakLine();
  if (st < 0) return say("touch calibration cancelled (or the crosses were not hit) - unchanged");
  {
    Guard g;
    gSettings.tsMinX = r[0];
    gSettings.tsMaxX = r[1];
    gSettings.tsMinY = r[2];
    gSettings.tsMaxY = r[3];
  }
  touchSetCal(TouchCal{r[0], r[1], r[2], r[3]});
  saveSettingsQuiet();
  say("touch calibrated and saved: x %u ... %u, y %u ... %u", r[0], r[1], r[2], r[3]);
}

// ------------------------------------------------------------ screen restart
static uint32_t scrWaitN = 0, scrWaitAt = 0;
static bool scrWaiting = false;

static void sayLine(const char* t) { say("%s", t); }

static void cmdScreen(const Args& a) {
  if (!strcmp(a.word(1), "check") && !strcmp(a.word(2), "test")) {  // (not in the help: a test of the check)
    switch (uiScreenCheckTest()) {
      case 1:
        return say("test: display switched off and put to sleep - the screen check should restart it within "
                   "a few seconds (event \"Screen restarted automatically\")");
      case -1: return err("the screen check is off (set display.screenCheck=on)");
      case -2: return err("the display status cannot be read back here - the check would not notice it");
      default: return err("no display");
    }
  }
  if (!strcmp(a.word(1), "check")) return uiScreenCheckInfo(sayLine);
  if (strcmp(a.word(1), "restart") && strcmp(a.word(1), "reset")) return err("screen restart | screen check");
  bool l, t;
  scrWaitN = uiScreenRestartPoll(l, t);
  uiScreenRestart("from the serial console");
  scrWaiting = true;
  scrWaitAt = millis();
  say("restarting the display and the touch controller...");
}

static void screenPoll() {
  if (!scrWaiting) return;
  bool lcdOk, touchOk;
  if (uiScreenRestartPoll(lcdOk, touchOk) != scrWaitN) {
    scrWaiting = false;
    breakLine();
    say("screen restarted: display %s, touch controller %s", lcdOk ? "set up" : "NOT set up (no display?)",
        touchOk ? "answers" : "does NOT answer (cable?)");
  } else if (millis() - scrWaitAt > 5000) {
    scrWaiting = false;
    breakLine();
    say("the screen task does not respond - restart the device (reboot)");
  }
}

// ------------------------------------------------------------ dispatch
static void dispatch(const Args& a);
static void cmdApi(const Args& a);  // (api mode, below)
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
  if (!strcmp(c, "help") || !strcmp(c, "?")) return help(a.word(1));
  if (!strcmp(c, "status")) return status();
  if (!strcmp(c, "manual")) return cmdManual(a);
  if (!strcmp(c, "wifi")) return cmdWifi(a);
  if (!strcmp(c, "programs")) return cmdPrograms();
  if (!strcmp(c, "program")) return cmdProgram(a);
  if (!strcmp(c, "show")) return cmdShow();
  if (!strcmp(c, "watch")) {
    watching = true;
    watchAt = millis();
    say("(one line per second, any key stops)");
    return watchLine();
  }
  if (!strcmp(c, "info")) return cmdInfo();
  if (!strcmp(c, "log")) return cmdLog(a);
  if (!strcmp(c, "ack")) return cmdAck(a);
  if (!strcmp(c, "tune")) return cmdTune(a);
  if (!strcmp(c, "get")) return cmdGet(a);
  if (!strcmp(c, "set")) return cmdSet(a);
  if (!strcmp(c, "factory")) return cmdFactory(a);
  if (!strcmp(c, "touch")) return cmdTouchCal(a);
  if (!strcmp(c, "screen")) return cmdScreen(a);
  if (!strcmp(c, "api")) return cmdApi(a);
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
    histAdd(cmd);
    histPos = -1;
    execute(cmd);
    memset(cmd, 0, sizeof(cmd));  // may hold a pw=
  }
  memset(line, 0, sizeof(line));
  if (!watching && !apiMode) showPrompt();
}

static void input(char c) {
  if (c == '\n' && lastCr) {  // the LF of a CR LF line end (also the one after "watch" / "touch cal")
    lastCr = false;
    return;
  }
  if (watching) {  // any key ends "watch" (and is used up)
    watching = false;
    lastCr = c == '\r';
    esc = c == 0x1B;  // the rest of an arrow key's sequence is skipped
    escUsed = true;
    say("(watch stopped)");
    return showPrompt();
  }
  if (calRunning) {  // any key cancels the touch calibration
    uiTouchCalCancel();
    lastCr = c == '\r';
    esc = c == 0x1B;
    escUsed = true;
    return;
  }
  if (esc) {  // ESC [ ... letter / ~ (also ESC O A: arrow keys in "application" mode)
    if (esc == 1 && (c == '[' || c == 'O')) esc = 2;
    else if (esc == 1 || (c >= '@' && c <= '~')) {
      const bool arrow = esc == 2 && (c == 'A' || c == 'B') && !escUsed;
      esc = 0;
      escUsed = false;
      if (arrow) {
        if (!promptShown) showPrompt();
        return histKey(c);
      }
    }
    return;
  }
  if (c == 0x1B) {
    esc = 1;
    escUsed = false;
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
    histPos = -1;
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
  if (active || !E.runEdited) savePending = false;  // (answered on the touch screen or the web page)
  prevState = E.state;
  // run interrupted by a reset: once
  if (E.intrValid && !intrShown) {
    intrShown = true;
    breakLine();
    say("Run \"%s\" was interrupted by a reset (%s) - type \"resume run\" or \"dismiss run\"", E.runName, E.intrReason);
  }
  if (!E.intrValid) intrShown = false;
}

// ------------------------------------------------------------ api mode (spec 7b)
// JSON lines for the Web Serial page: a request per line {"id":7,"m":"GET","p":"/api/live","b":{...}},
// answered by the web API's own handlers (webApiRequest) with {"id":7,"code":200,"body":{...}} or, for
// text (CSV), {"id":7,"code":200,"type":"text/csv","chunk":"..."} lines and {"id":7,"end":true}.
// {"t":"hello"} after "api on", {"t":"bye"} at the end; on a shared port (CYD) log lines come as
// {"t":"log","m":"..."}. "api off", or no request for 15 s, ends it (back to this console). Output is
// queued and written as the port takes it, so the loop never waits for the port.
static uint32_t apiLastReq = 0;
static constexpr uint32_t API_IDLE_MS = 15000;
static constexpr size_t API_LINE_MAX = 16384;  // longest request (a program of 100 phases)
static constexpr size_t API_LOG_KEEP = 8192;   // log lines are dropped while more than this is queued
static char* apiIn = nullptr;
static size_t apiInLen = 0, apiInCap = 0;
static bool apiInOver = false;
static char* apiQ = nullptr;  // output queue
static size_t apiQLen = 0, apiQPos = 0, apiQCap = 0;
static SemaphoreHandle_t apiMux = nullptr;
static char apiLog[192];  // log line being put together
static size_t apiLogLen = 0;
static uint32_t apiLogDropped = 0;

struct ApiLock {
  ApiLock() {
    if (apiMux) xSemaphoreTake(apiMux, portMAX_DELAY);
  }
  ~ApiLock() {
    if (apiMux) xSemaphoreGive(apiMux);
  }
};

static bool qAppend(const char* s, size_t n) {  // (lock held)
  if (apiQPos && apiQPos == apiQLen) apiQPos = apiQLen = 0;
  if (apiQLen + n > apiQCap) {
    if (apiQPos) {
      memmove(apiQ, apiQ + apiQPos, apiQLen - apiQPos);
      apiQLen -= apiQPos;
      apiQPos = 0;
    }
    if (apiQLen + n > apiQCap) {
      size_t c = apiQCap ? apiQCap * 2 : 1024;
      if (c < apiQLen + n + 64) c = apiQLen + n + 64;
      char* p = (char*)realloc(apiQ, c);
      if (!p) return false;
      apiQ = p;
      apiQCap = c;
    }
  }
  memcpy(apiQ + apiQLen, s, n);
  apiQLen += n;
  return true;
}

size_t apiQueued() {
  ApiLock l;
  return apiQLen - apiQPos;
}

void apiOut(const char* s, size_t n) {
  if (!apiMode) return;
  ApiLock l;
  qAppend(s, n);
}

// JSON string content (without quotes)
static void jsonEsc(const char* s, size_t n, char* out, size_t cap) {
  size_t o = 0;
  for (size_t i = 0; i < n && o + 7 < cap; ++i) {
    const uint8_t c = (uint8_t)s[i];
    if (c == '"' || c == '\\') {
      out[o++] = '\\';
      out[o++] = c;
    } else if (c == '\n') {
      out[o++] = '\\';
      out[o++] = 'n';
    } else if (c == '\r') {
      out[o++] = '\\';
      out[o++] = 'r';
    } else if (c == '\t') {
      out[o++] = '\\';
      out[o++] = 't';
    } else if (c < 0x20) {
      o += snprintf(out + o, cap - o, "\\u%04x", c);
    } else {
      out[o++] = c;
    }
  }
  out[o] = 0;
}

static void apiLogFlushLine() {  // (lock held)
  if (!apiLogLen) return;
  if (apiQLen - apiQPos > API_LOG_KEEP) {
    ++apiLogDropped;
  } else {
    char e[sizeof(apiLog) * 2 + 48];
    char m[sizeof(apiLog) * 2 + 8];
    if (apiLogDropped) {
      snprintf(e, sizeof(e), "{\"t\":\"log\",\"m\":\"(%lu log lines dropped)\"}\n", (unsigned long)apiLogDropped);
      qAppend(e, strlen(e));
      apiLogDropped = 0;
    }
    jsonEsc(apiLog, apiLogLen, m, sizeof(m));
    const int k = snprintf(e, sizeof(e), "{\"t\":\"log\",\"m\":\"%s\"}\n", m);
    qAppend(e, k);
  }
  apiLogLen = 0;
}

static bool apiLogWrite(const uint8_t* b, size_t n) {
  if (!apiMode) return false;
  ApiLock l;
  for (size_t i = 0; i < n; ++i) {
    if (b[i] == '\r') continue;
    if (b[i] == '\n' || apiLogLen >= sizeof(apiLog) - 1) apiLogFlushLine();
    if (b[i] != '\n') apiLog[apiLogLen++] = b[i];
  }
  return true;
}

// writes what the port takes now; nobody listening (S3: port closed): the output is dropped
static bool apiPump() {
  ApiLock l;
  const size_t q = apiQLen - apiQPos;
  if (!q) return false;
  if (!portReady()) {
    apiQPos = apiQLen = 0;
    return false;
  }
  const int room = port().availableForWrite();
  if (room <= 0) return false;
  const size_t n = q < (size_t)room ? q : (size_t)room;
  port().write((const uint8_t*)apiQ + apiQPos, n);
  apiQPos += n;
  if (apiQPos == apiQLen) {
    apiQPos = apiQLen = 0;
    if (apiQCap > 8192) {  // a big answer was sent: give the RAM back
      free(apiQ);
      apiQ = nullptr;
      apiQCap = 0;
    }
  }
  return true;
}

void apiFlush(size_t keep) {
  uint32_t last = millis();
  while (apiMode && apiQueued() > keep) {
    if (apiPump()) {
      last = millis();
    } else if (millis() - last > 5000) {  // the other side stopped reading: drop it
      ApiLock l;
      apiQPos = apiQLen = 0;
      break;
    } else {
      delay(1);
    }
    feedLoopWDT();
  }
}

static void apiHello() {
  char h[256], host[24];
  {
    Guard g;
    strlcpy(host, gSettings.hostname, sizeof(host));
  }
  const int k = snprintf(h, sizeof(h),
                         "{\"t\":\"hello\",\"api\":%d,\"fw\":\"%s\",\"name\":\"%s\",\"board\":\"%s\",\"variant\":\"%s\","
                         "\"host\":\"%s\"}\n",
                         API_VERSION, FW_VERSION, PRODUCT_NAME, BOARD_NAME, BOARD_VARIANT, host);
  ApiLock l;
  qAppend(h, k);
}

static void apiEnter() {
  if (!apiMux) apiMux = xSemaphoreCreateMutex();
  watching = false;
  if (calRunning) uiTouchCalCancel();
  promptShown = false;
  len = 0;
  apiInLen = 0;
  apiInOver = false;
  apiLastReq = millis();
  apiMode = true;
  apiHello();
}

static void apiExit(const char* why) {
  {
    char b[64];
    const int k = snprintf(b, sizeof(b), "{\"t\":\"bye\",\"why\":\"%s\"}\n", why);
    ApiLock l;
    apiLogFlushLine();
    qAppend(b, k);
  }
  apiFlush(0);
  apiMode = false;
  {
    ApiLock l;
    free(apiQ);
    apiQ = nullptr;
    apiQLen = apiQPos = apiQCap = 0;
    apiLogLen = 0;
  }
  free(apiIn);
  apiIn = nullptr;
  apiInLen = apiInCap = 0;
  if (portReady()) {
    raw("\r\n");
    say("(api mode ended: %s - serial console; help lists the commands)", why);
    showPrompt();
  }
}

static void apiError(const char* what) {
  char b[128];
  const int k = snprintf(b, sizeof(b), "{\"t\":\"error\",\"err\":\"%s\"}\n", what);
  apiOut(b, k);
}

static void apiLine(char* t) {
  apiLastReq = millis();
  while (*t == ' ' || *t == '\t') ++t;
  if (*t == '{') return webApiRequest(t);
  for (char* p = t; *p; ++p) *p = tolower(*p);
  while (*t && t[strlen(t) - 1] == ' ') t[strlen(t) - 1] = 0;
  if (!strcmp(t, "api off")) return apiExit("api off");
  if (!strcmp(t, "api on")) return apiHello();  // a new session (page reloaded)
  if (*t) apiError("not a JSON request (api off: back to the console)");
}

static void apiInput(char c) {
  if (c == '\r') return;
  if (c == '\n') {
    if (apiInOver) apiError("request too long");
    else if (apiInLen) {
      apiIn[apiInLen] = 0;
      apiLine(apiIn);
    }
    apiInLen = 0;
    apiInOver = false;
    if (apiMode && apiInCap > 2048) {  // a big request (program) was handled: give the RAM back
      free(apiIn);
      apiIn = nullptr;
      apiInCap = 0;
    }
    return;
  }
  if (apiInOver) return;
  if (apiInLen + 2 > apiInCap) {
    size_t c2 = apiInCap ? apiInCap * 2 : 512;
    if (c2 > API_LINE_MAX + 2) c2 = API_LINE_MAX + 2;
    char* p = apiInLen + 2 > c2 ? nullptr : (char*)realloc(apiIn, c2);
    if (!p) {
      apiInOver = true;
      return;
    }
    apiIn = p;
    apiInCap = c2;
  }
  apiIn[apiInLen++] = c;
}

static void apiLoop() {
  int budget = 4096;
  while (apiMode && budget-- > 0 && port().available() > 0) apiInput((char)port().read());
  if (!apiMode) return;
  {
    ApiLock l;
    if (apiLogLen && !SHARED) apiLogLen = 0;
  }
  apiPump();
  if (millis() - apiLastReq > API_IDLE_MS) apiExit("no request for 15 s");
}

static void cmdApi(const Args& a) {
  if (!strcmp(a.word(1), "on")) return apiEnter();
  if (!strcmp(a.word(1), "off")) return say("api mode is not on");
  err("api on | api off");
}

// ------------------------------------------------------------ begin / loop
static void greet() {
  raw("\r\n");
  watching = false;
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
  conUsb.setRxBufferSize(4096);  // api mode: a request line (a whole program) must not overflow it
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
  if (apiMode) {
#ifdef CON_USB
    conOpened = false;  // (a page that opens the port again just sends "api on" again)
#endif
    return apiLoop();
  }
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
  calPoll();
  screenPoll();
  if (watching && millis() - watchAt >= 1000) {
    watchAt += 1000;
    if (millis() - watchAt >= 1000) watchAt = millis();  // (fell behind: no burst)
    watchLine();
  }
  static uint32_t evAt = 0;
  if (millis() - evAt >= 250) {
    evAt = millis();
    events();
  }
  if (reprompt && !promptShown && !watching) showPrompt();
}
