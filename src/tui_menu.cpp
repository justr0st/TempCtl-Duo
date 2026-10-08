// Touch UI menu and setup pages: Menu, Event log, Display, Network, System.
#include "tui.h"
#ifdef UI_V2
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "net.h"
#include "settings_store.h"

static void backToMenu(lv_event_t*) { tShow(SCR_MENU); }
static void backToMain(lv_event_t*) { tShow(SCR_MAIN); }

// ------------------------------------------------------------ menu
static void onMenuItem(lv_event_t* e) { tShow((TScreen)(intptr_t)lv_event_get_user_data(e)); }

lv_obj_t* menuCreate() {
  TPage p = tPageCreate("Menu", backToMain);
  lv_obj_set_flex_flow(p.body, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(p.body, 8, 0);
  lv_obj_set_style_pad_all(p.body, 8, 0);
  struct Item {
    const char* icon;
    const char* text;
    const char* sub;
    TScreen s;
    bool on;
  };
  static const Item items[] = {
      {LV_SYMBOL_FILE, "Programs", "view, edit, start", SCR_PROGRAMS, true},
      {LV_SYMBOL_SETTINGS, "Channels", "controllers, sensors, PID", SCR_CHANNELS, true},
      {LV_SYMBOL_WIFI, "Network", "Wi-Fi, address", SCR_NETWORK, true},
      {LV_SYMBOL_IMAGE, "Display", "brightness, colours", SCR_DISPLAY, true},
      {LV_SYMBOL_HOME, "System", "polling, info, restart", SCR_SYSTEM, true},
      {LV_SYMBOL_LIST, "Event log", "what happened", SCR_LOG, true},
  };
  const int w = (SCREEN_W - 16 - 16) / 3, h = (SCREEN_H - 44 - 16 - 8) / 2;
  for (const Item& it : items) {
    lv_obj_t* b = tButton(p.body, "", onMenuItem, (void*)(intptr_t)it.s, w, h);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(b, 4, 0);
    lv_obj_t* ic = lv_obj_get_child(b, 0);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_20, 0);
    lv_label_set_text(ic, it.icon);
    lv_obj_add_style(ic, &S_.acc, 0);
    tLabel(b, &tfont_16b, it.text);
    lv_obj_t* s = tLabel(b, &tfont_12, it.sub);
    lv_obj_add_style(s, &S_.muted, 0);
    tSetEnabled(b, it.on);
  }
  return p.scr;
}

// ------------------------------------------------------------ event log
static lv_obj_t* logList;
static uint32_t logShownSeq = 0xFFFFFFFF;

lv_obj_t* logCreate() {
  TPage p = tPageCreate("Event log", backToMenu);
  logList = p.body;
  lv_obj_set_style_pad_gap(logList, 0, 0);
  return p.scr;
}

void logUpdate() {
  const uint32_t seq = logSeq();
  if (seq == logShownSeq) return;
  logShownSeq = seq;
  static LogEntry* e = nullptr;
  if (!e) e = (LogEntry*)malloc(sizeof(LogEntry) * LOG_N);
  if (!e) return;
  const int n = logSince(0, e, LOG_N);
  lv_obj_clean(logList);
  if (!n) tLabel(logList, &tfont_14, "No events yet.");
  for (int i = n - 1; i >= 0; --i) {
    lv_obj_t* row = tRow(logList, 8);
    lv_obj_set_style_pad_ver(row, 5, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, tc(C_LINE), 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    char t[20];
    if (!isnan(e[i].runT)) fmtTime(e[i].runT, t, sizeof(t));
    else {
      char u[16];
      fmtTime(e[i].upSec, u, sizeof(u));
      snprintf(t, sizeof(t), "up %s", u);
    }
    lv_obj_t* tl = tLabel(row, &tfont_12, t);
    lv_obj_add_style(tl, &S_.muted, 0);
    lv_obj_set_width(tl, 70);
    lv_obj_t* m = tLabel(row, &tfont_14, e[i].msg);
    lv_obj_set_flex_grow(m, 1);
    lv_label_set_long_mode(m, LV_LABEL_LONG_MODE_WRAP);
  }
}

// ------------------------------------------------------------ helpers: setting rows
// A row: label (+ hint) on the left, a control on the right
static lv_obj_t* settingRow(lv_obj_t* parent, const char* label, const char* hint, lv_obj_t** hintOut = nullptr) {
  lv_obj_t* row = lv_obj_create(parent);  // no tile (Rost): label left, control right
  lv_obj_remove_style_all(row);
  lv_obj_set_scrollable(row, false);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(row, 4, 0);
  lv_obj_set_style_pad_ver(row, 1, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(row, 8, 0);
  lv_obj_t* col = tCol(row, 0);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_t* l = tLabel(col, &tfont_16b, label);
  (void)l;
  lv_obj_t* h = nullptr;
  if (hint) {
    h = tLabel(col, &tfont_12, hint);
    lv_obj_add_style(h, &S_.muted, 0);
    lv_obj_set_width(h, 280);
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
  }
  if (hintOut) *hintOut = h;
  return row;
}

static void saveQuiet() { saveSettingsQuiet(); }

// ------------------------------------------------------------ display
static lv_obj_t *dBright, *dDark, *dRotate, *dPeriod, *dAcc[6];
static const uint32_t ACCENTS[6] = {0x2196F3, 0xD1449B, 0x6C71C4, 0xD65D0E, 0x8839EF, 0x50A14F};
static const char* const ACC_NAMES[6] = {"Blue", "Pink", "Violet", "Orange", "Mauve", "Green"};
static bool dLoading = false;
// what the screen shows: a change from the web page is picked up (displayUpdate)
struct DShown {
  uint8_t bright, dark, rot;
  uint32_t acc;
  uint16_t liveMin;
};
static DShown dShown;

static void onBright(lv_event_t* e) {
  if (dLoading) return;
  const int v = lv_slider_get_value(dBright);
  {
    Guard g;
    gSettings.brightness = (uint8_t)v;
  }
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) saveQuiet();
}

static void onDark(lv_event_t*) {
  if (dLoading) return;
  {
    Guard g;
    gSettings.themeDark = lv_obj_has_state(dDark, LV_STATE_CHECKED) ? 1 : 0;
  }
  saveQuiet();
}

static void onRotate(lv_event_t*) {
  if (dLoading) return;
  {
    Guard g;
    gSettings.rotate180 = lv_obj_has_state(dRotate, LV_STATE_CHECKED) ? 1 : 0;
  }
  saveQuiet();
}

static void onAccent(lv_event_t* e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  {
    Guard g;
    gSettings.accentColor = ACCENTS[i];
  }
  saveQuiet();
  displayShow();
}

// periods up to what the live history holds (CYD: 30 min)
static void onPeriodPick(int i, int) {
  const uint32_t cap = liveHistSec() / 60;
  int k = 0;
  for (uint16_t m : LIVE_PERIODS) {
    if (m > cap) break;
    if (k++ == i) {
      Guard g;
      gSettings.liveMin = m;
    }
  }
  saveQuiet();
  displayShow();
}

static void fmtMinutes(uint32_t m, char* b, size_t n) {
  if (m % 60) snprintf(b, n, "%lu min", (unsigned long)m);
  else snprintf(b, n, "%lu h", (unsigned long)(m / 60));
}

static void onPeriod(lv_event_t*) {
  const uint32_t cap = liveHistSec() / 60;
  uint32_t cur;
  {
    Guard g;
    cur = liveMinEff();
  }
  char items[200] = "";
  int n = 0, sel = -1;
  for (uint16_t m : LIVE_PERIODS) {
    if (m > cap) break;
    char b[16];
    fmtMinutes(m, b, sizeof(b));
    if (n) strlcat(items, "\n", sizeof(items));
    strlcat(items, b, sizeof(items));
    if (m == cur) sel = n;
    ++n;
  }
  tChoice("Live chart period", items, sel, onPeriodPick, 0);
}

lv_obj_t* displayCreate() {
  TPage p = tPageCreate("Display", backToMenu);
  lv_obj_t* r = settingRow(p.body, "Brightness", nullptr);
  lv_obj_set_style_pad_ver(r, 8, 0);  // the knob reaches 6 px above / below the bar
  dBright = lv_slider_create(r);
  lv_obj_set_size(dBright, 220, 18);
  lv_slider_set_range(dBright, 10, 255);
  lv_obj_set_style_pad_all(dBright, 6, LV_PART_KNOB);
  lv_obj_set_ext_click_area(dBright, 12);
  lv_obj_add_event_cb(dBright, onBright, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(dBright, onBright, LV_EVENT_RELEASED, nullptr);
  r = settingRow(p.body, "Dark mode", "The web page follows its own setting");
  dDark = tSwitch(r, onDark);
  r = settingRow(p.body, "Accent colour", "Channel colours follow it (touch and web)");
  lv_obj_t* sw = tRow(r, 8);
  lv_obj_set_width(sw, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(sw, 5, 0);  // room for the ring of the chosen colour
  for (int i = 0; i < 6; ++i) {
    dAcc[i] = lv_button_create(sw);
    lv_obj_remove_style_all(dAcc[i]);
    lv_obj_set_size(dAcc[i], 30, 30);
    lv_obj_set_style_radius(dAcc[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dAcc[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dAcc[i], lv_color_hex(ACCENTS[i]), 0);  // the preset itself, not a theme role
    lv_obj_add_style(dAcc[i], &S_.ring, LV_STATE_CHECKED);
    lv_obj_set_ext_click_area(dAcc[i], 4);
    lv_obj_add_event_cb(dAcc[i], onAccent, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  r = settingRow(p.body, "Turn the screen", "Upside down (180\xC2\xB0)");
  dRotate = tSwitch(r, onRotate);
  r = settingRow(p.body, "Live chart period", "Also on the web page");
  dPeriod = tButton(r, "", onPeriod, nullptr, 110, 38);
  return p.scr;
}

void displayShow() {
  Settings S;
  settingsCopy(S);
  dLoading = true;
  lv_slider_set_value(dBright, S.brightness, LV_ANIM_OFF);
  tSetChecked(dDark, S.themeDark != 0);
  tSetChecked(dRotate, S.rotate180 != 0);
  for (int i = 0; i < 6; ++i) tSetChecked(dAcc[i], (S.accentColor & 0xFFFFFF) == ACCENTS[i]);
  char b[24];
  uint32_t cur;
  {
    Guard g;
    cur = liveMinEff();
  }
  fmtMinutes(cur, b, sizeof(b));
  tButtonText(dPeriod, b);
  dShown = {S.brightness, S.themeDark, S.rotate180, S.accentColor, S.liveMin};
  dLoading = false;
  (void)ACC_NAMES;
}

void displayUpdate() {
  if (lv_obj_has_state(dBright, LV_STATE_PRESSED)) return;  // being moved here
  Settings S;
  settingsCopy(S);
  if (S.brightness != dShown.bright || S.themeDark != dShown.dark || S.rotate180 != dShown.rot ||
      S.accentColor != dShown.acc || S.liveMin != dShown.liveMin)
    displayShow();
}

// ------------------------------------------------------------ network
static lv_obj_t *nStatus, *nSsid, *nPass, *nAp, *nHost, *nStatic, *nIpBox, *nIp[5];
static Settings nEdit;  // edited copy, saved with "Save and connect"
static bool nPassChanged, nApChanged;
static bool scanning = false, scanShown = false;
static char scanNames[1200];
static const char* const IP_LABELS[5] = {"IP address", "Subnet mask", "Gateway", "DNS server", "Second DNS"};

static uint8_t* ipField(Settings& s, int i) {
  switch (i) {
    case 0: return s.ip;
    case 1: return s.subnet;
    case 2: return s.gateway;
    case 3: return s.dns1;
    default: return s.dns2;
  }
}

static void networkFill() {
  char b[40];
  tButtonText(nSsid, nEdit.ssid[0] ? nEdit.ssid : "(none)");
  tButtonText(nPass, nEdit.pass[0] ? "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2" : "(none)");
  tButtonText(nAp, nEdit.apPass[0] ? "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2" : "(none)");
  tButtonText(nHost, nEdit.hostname);
  tSetChecked(nStatic, nEdit.staticIp != 0);
  tSetHidden(nIpBox, !nEdit.staticIp);
  for (int i = 0; i < 5; ++i) {
    const uint8_t* a = ipField(nEdit, i);
    if (i >= 3 && !(a[0] | a[1] | a[2] | a[3])) strlcpy(b, i == 3 ? "(gateway)" : "(none)", sizeof(b));
    else snprintf(b, sizeof(b), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    tButtonText(nIp[i], b);
  }
}

static void onSsidText(const char* t, int) {
  strlcpy(nEdit.ssid, t, sizeof(nEdit.ssid));
  networkFill();
}
static void onPassText(const char* t, int) {
  strlcpy(nEdit.pass, t, sizeof(nEdit.pass));
  nPassChanged = true;
  networkFill();
}
static void onApText(const char* t, int) {
  if (*t && strlen(t) < 8) {
    tToast("The access point password needs 8 or more characters.", true);
    return;
  }
  strlcpy(nEdit.apPass, t, sizeof(nEdit.apPass));
  nApChanged = true;
  networkFill();
}
static void onHostText(const char* t, int) {
  strlcpy(nEdit.hostname, t, sizeof(nEdit.hostname));
  networkFill();
}
static void onIpText(const char* t, int i) {
  unsigned a, b, c, d;
  char tail;
  uint8_t* f = ipField(nEdit, i);
  if (!*t && i >= 3) {
    memset(f, 0, 4);
  } else if (sscanf(t, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 && a < 256 && b < 256 && c < 256 && d < 256) {
    f[0] = a, f[1] = b, f[2] = c, f[3] = d;
  } else {
    tToast("Enter the address as four numbers, e.g. 192.168.1.50", true);
    return;
  }
  networkFill();
}

static void onSsid(lv_event_t*) { tKeyboard("Network name", nEdit.ssid, 32, false, false, onSsidText, 0); }
static void onPass(lv_event_t*) { tKeyboard("Wi-Fi password", "", 64, true, false, onPassText, 0); }
static void onAp(lv_event_t*) { tKeyboard("Access point password", "", 32, true, false, onApText, 0); }
static void onHost(lv_event_t*) { tKeyboard("Host name", nEdit.hostname, 23, false, false, onHostText, 0); }
static void onIp(lv_event_t* e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  const uint8_t* a = ipField(nEdit, i);
  char b[20] = "";
  if (a[0] | a[1] | a[2] | a[3]) snprintf(b, sizeof(b), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
  tKeyboard(IP_LABELS[i], b, 15, false, true, onIpText, i);
}
static void onStatic(lv_event_t*) {
  nEdit.staticIp = lv_obj_has_state(nStatic, LV_STATE_CHECKED) ? 1 : 0;
  networkFill();
}

static void onScanPick(int i, int) {
  const char* p = scanNames;
  for (int k = 0; k < i && p; ++k) {
    p = strchr(p, '\n');
    if (p) ++p;
  }
  if (!p) return;
  const char* e = strchr(p, '\n');
  const size_t n = e ? (size_t)(e - p) : strlen(p);
  char ssid[33];
  snprintf(ssid, sizeof(ssid), "%.*s", (int)n, p);
  strlcpy(nEdit.ssid, ssid, sizeof(nEdit.ssid));
  networkFill();
  tKeyboard("Wi-Fi password", "", 64, true, false, onPassText, 0);
}

static void onScanCancel(int, int) { scanShown = false; }

static void onScan(lv_event_t*) {
  if (!scanning) netScanStart();
  scanning = true;
  scanShown = true;
  tDialog("Wi-Fi networks", "Scanning \xE2\x80\xA6", "Cancel", "", "", onScanCancel, 0);
}

// the scan result must be collected even when the list was closed (see net.h)
static void scanPoll() {
  if (!scanning) return;
  const int n = netScanResult(scanNames, sizeof(scanNames));
  if (n == -1) return;
  scanning = false;
  if (!scanShown) return;
  scanShown = false;
  tModalClose();
  if (n <= 0) tToast(n == 0 ? "No networks found." : "The scan failed.", n < 0);
  else tChoice("Wi-Fi networks", scanNames, -1, onScanPick, 0);
}

static void onNetSave(lv_event_t*) {
  Settings cur;
  settingsCopy(cur);
  Settings s = cur;
  strlcpy(s.ssid, nEdit.ssid, sizeof(s.ssid));
  if (nPassChanged) strlcpy(s.pass, nEdit.pass, sizeof(s.pass));
  if (nApChanged) strlcpy(s.apPass, nEdit.apPass, sizeof(s.apPass));
  strlcpy(s.hostname, nEdit.hostname, sizeof(s.hostname));
  s.staticIp = nEdit.staticIp;
  for (int i = 0; i < 5; ++i) memcpy(ipField(s, i), ipField(nEdit, i), 4);
  char msg[160];
  if (!settingsValidate(s, boardLimits(), msg, sizeof(msg))) {
    tToast(msg, true);
    return;
  }
  {
    Guard g;
    strlcpy(gSettings.ssid, s.ssid, sizeof(s.ssid));
    strlcpy(gSettings.pass, s.pass, sizeof(s.pass));
    strlcpy(gSettings.apPass, s.apPass, sizeof(s.apPass));
    strlcpy(gSettings.hostname, s.hostname, sizeof(s.hostname));
    gSettings.staticIp = s.staticIp;
    for (int i = 0; i < 5; ++i) memcpy(ipField(gSettings, i), ipField(s, i), 4);
  }
  saveSettingsQuiet();
  netApplyWifi();
  nPassChanged = nApChanged = false;
  tToast("Saved. Connecting \xE2\x80\xA6");
}

lv_obj_t* networkCreate() {
  TPage p = tPageCreate("Network", backToMenu);
  nStatus = tLabel(p.body, &tfont_14, "");
  lv_obj_set_width(nStatus, LV_PCT(100));
  lv_label_set_long_mode(nStatus, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_t* r = settingRow(p.body, "Network name", nullptr);
  nSsid = tButton(r, "", onSsid, nullptr, 170, 38);
  tOneLine(lv_obj_get_child(nSsid, 0));
  lv_obj_set_width(lv_obj_get_child(nSsid, 0), 150);
  tButton(r, "Scan", onScan, nullptr, 70, 38);
  r = settingRow(p.body, "Password", nullptr);
  nPass = tButton(r, "", onPass, nullptr, 170, 38);
  r = settingRow(p.body, "Access point password", "Used when no network is reachable; 8+ characters");
  nAp = tButton(r, "", onAp, nullptr, 170, 38);
  r = settingRow(p.body, "Host name", "http://<name>.local");
  nHost = tButton(r, "", onHost, nullptr, 170, 38);
  r = settingRow(p.body, "Fixed IP address", "No DHCP: use an address outside the router's DHCP range");
  nStatic = tSwitch(r, onStatic);
  nIpBox = tCol(p.body, 6);
  lv_obj_set_width(nIpBox, LV_PCT(100));
  for (int i = 0; i < 5; ++i) {
    r = settingRow(nIpBox, IP_LABELS[i], i == 3 ? "Empty = the gateway" : nullptr);
    nIp[i] = tButton(r, "", onIp, (void*)(intptr_t)i, 170, 38);
  }
  lv_obj_t* b = tButton(p.header, "Save and connect", onNetSave, nullptr, LV_SIZE_CONTENT, 36);  // always visible
  lv_obj_add_style(b, &S_.btnPri, 0);
  return p.scr;
}

void networkShow() {
  settingsCopy(nEdit);
  nEdit.pass[0] = 0;  // not shown; empty = keep
  nPassChanged = nApChanged = false;
  {
    Settings s;
    settingsCopy(s);
    if (s.pass[0]) strlcpy(nEdit.pass, "x", sizeof(nEdit.pass));  // "set" marker for the button
  }
  networkFill();
  networkUpdate();
}

void networkUpdate() {
  static uint32_t at = 0;
  if (at && lv_tick_elaps(at) < 2000) return;
  at = lv_tick_get() | 1;
  tSetText(nStatus, netInfo().c_str());
}

// ------------------------------------------------------------ system
static lv_obj_t *sInfo, *sPoll, *sBand;

static void onPollNum(float v, int) {
  {
    Guard g;
    gSettings.pollMs = (uint16_t)v;
  }
  saveSettingsAndNotify();
  systemShow();
}
static void onBandNum(float v, int) {
  {
    Guard g;
    gSettings.bottomBand = v;
  }
  saveSettingsAndNotify();
  systemShow();
}
static void onPoll(lv_event_t*) {
  Settings s;
  settingsCopy(s);
  tNumPad("Polling interval", s.pollMs, 250, 10000, 0, "ms", onPollNum, 0);
}
static void onBand(lv_event_t*) {
  Settings s;
  settingsCopy(s);
  tNumPad("\"Bottom reached\" band", s.bottomBand, 0.5f, 50, 1, "\xC2\xB0" "C", onBandNum, 0);
}
static void onRestartAnswer(int b, int) {
  if (b != 2) return;
  appRequestRestart("on the touch screen");
}
static void onRestart(lv_event_t*) {
  LiveData L;
  liveCopy(L);
  if (L.state == RunState::Running || L.state == RunState::Paused) {
    tToast("A program is running - stop it first.", true);
    return;
  }
  tDialog("Restart the device?", "The touch screen and the web page are back after a few seconds.", "Cancel", "Restart",
          "", onRestartAnswer, 0, true);
}

lv_obj_t* systemCreate() {
  TPage p = tPageCreate("System", backToMenu);
  lv_obj_t* r = settingRow(p.body, "Polling interval", "How often PV, SP and output are read. Two Modbus channels: each every 2 \xC3\x97 this");
  sPoll = tButton(r, "", onPoll, nullptr, 110, 38);
  r = settingRow(p.body, "\"Bottom reached\" band", "\"Wait for bottom\" goes on within \xC2\xB1 this");
  sBand = tButton(r, "", onBand, nullptr, 110, 38);
  lv_obj_t* info = tPanel(p.body);
  lv_obj_set_size(info, LV_PCT(100), LV_SIZE_CONTENT);
  sInfo = tLabel(info, &tfont_14, "");
  lv_obj_set_width(sInfo, LV_PCT(100));
  lv_label_set_long_mode(sInfo, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_t* b = tButton(p.body, "Restart device", onRestart, nullptr, LV_PCT(100), 44);
  lv_obj_add_style(b, &S_.btnDanger, 0);
  return p.scr;
}

void systemShow() {
  Settings s;
  settingsCopy(s);
  char b[24];
  snprintf(b, sizeof(b), "%u ms", s.pollMs);
  tButtonText(sPoll, b);
  snprintf(b, sizeof(b), "%.1f \xC2\xB0" "C", s.bottomBand);
  tButtonText(sBand, b);
  systemUpdate();
}

void systemUpdate() {
  static uint32_t at = 0;
  if (at && lv_tick_elaps(at) < 2000) return;
  at = lv_tick_get() | 1;
  char up[16], b[400], hist[16];
  fmtTime(millis() / 1000.0f, up, sizeof(up));
  fmtMinutes(liveHistSec() / 60, hist, sizeof(hist));
  snprintf(b, sizeof(b),
           PRODUCT_NAME " " FW_VERSION " \xC2\xB7 " BOARD_NAME "%s%s\n%s\nUp %s \xC2\xB7 RAM %u kB free \xC2\xB7 PSRAM %u kB free\n"
           "Live history in memory: %s",
           BOARD_VARIANT[0] ? " " : "", BOARD_VARIANT, netInfo().c_str(), up, (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned)(ESP.getFreePsram() / 1024), hist);
  tSetText(sInfo, b);
}

// network scan + page updates that run whatever screen is shown
void tuiBackground() { scanPoll(); }
#endif
