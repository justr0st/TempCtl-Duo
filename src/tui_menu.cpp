// Touch UI menu and setup pages: Menu, Event log, Display, Network, System.
#include "syslog.h"
#include "tui.h"
#include "safety.h"
#include "ota.h"
#include "sdlog.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "local_ctrl.h"
#include "net.h"
#include "settings_store.h"
#include <esp_heap_caps.h>

// control sizes in the setting rows (480x320 / 320x240)
constexpr int CW = TUI_SMALL ? 128 : 170, CW2 = TUI_SMALL ? 90 : 110, BH = TUI_SMALL ? 32 : 38;

static void backToMenu(lv_event_t*) { tShow(SCR_MENU); }
static void backToMain(lv_event_t*) { tShow(SCR_MAIN); }

// ------------------------------------------------------------ menu
static void onMenuItem(lv_event_t* e) { tShow((TScreen)(intptr_t)lv_event_get_user_data(e)); }

lv_obj_t* menuCreate() {
  TPage p = tPageCreate("Menu", backToMain);
  lv_obj_set_flex_flow(p.body, LV_FLEX_FLOW_ROW_WRAP);
  constexpr int G = TUI_SMALL ? 6 : 8;
  lv_obj_set_style_pad_gap(p.body, G, 0);
  lv_obj_set_style_pad_all(p.body, G, 0);
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
  const int w = (SCREEN_W - 2 * G - 2 * G) / 3, h = (SCREEN_H - T_HDR - 2 * G - G) / 2;
  for (const Item& it : items) {
    lv_obj_t* b = tButton(p.body, "", onMenuItem, (void*)(intptr_t)it.s, w, h);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(b, 4, 0);
    lv_obj_t* ic = lv_obj_get_child(b, 0);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_20, 0);
    lv_label_set_text(ic, it.icon);
    lv_obj_add_style(ic, &S_.acc, 0);
    tLabel(b, &tfont_16b, it.text);
    if (!TUI_SMALL) {  // 320x240: no room for the hint
      lv_obj_t* s = tLabel(b, &tfont_12, it.sub);
      lv_obj_add_style(s, &S_.muted, 0);
    }
    tSetEnabled(b, it.on);
  }
  return p.scr;
}

// ------------------------------------------------------------ event log
static lv_obj_t* logList;
static uint32_t logShownSeq = 0xFFFFFFFF;

lv_obj_t* logCreate() {
  logShownSeq = 0xFFFFFFFF;  // (320x240: the screen is built again each time)
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
    lv_obj_set_width(tl, TUI_SMALL ? 54 : 70);
    lv_obj_t* m = tLabel(row, TUI_SMALL ? &tfont_12 : &tfont_14, e[i].msg);
    lv_obj_set_flex_grow(m, 1);
    // colours as on the web page: errors, warnings, run records
    if (e[i].kind == LK_ERROR) lv_obj_add_style(m, &S_.danger, 0);
    else if (e[i].kind == LK_WARN) lv_obj_add_style(m, &S_.amber, 0);
    else if (e[i].kind == LK_RUN) lv_obj_add_style(m, &S_.acc, 0);
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
  lv_obj_t* l = tLabel(col, TUI_SMALL ? &tfont_14 : &tfont_16b, label);
  if (TUI_SMALL) lv_obj_set_width(l, LV_PCT(100));  // long labels wrap
  lv_obj_t* h = nullptr;
  if (hint) {
    h = tLabel(col, &tfont_12, hint);
    lv_obj_add_style(h, &S_.muted, 0);
    lv_obj_set_width(h, LV_PCT(100));  // wraps where the control leaves too little room
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
  }
  if (hintOut) *hintOut = h;
  return row;
}

static void saveQuiet() { saveSettingsQuiet(); }

// ------------------------------------------------------------ display
static lv_obj_t *dBright, *dDark, *dRotate, *dCheck, *dPeriod, *dAcc[7], *dAccIcon;  // dAcc[6]: custom colour
static const uint32_t ACCENTS[6] = {0x2196F3, 0xD1449B, 0x6C71C4, 0xD65D0E, 0x8839EF, 0x50A14F};
static const char* const ACC_NAMES[6] = {"Blue", "Pink", "Violet", "Orange", "Mauve", "Green"};
static bool dLoading = false;
// what the screen shows: a change from the web page is picked up (displayUpdate)
struct DShown {
  uint8_t bright, dark, rot, check;
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

static void onCheck(lv_event_t*) {
  if (dLoading) return;
  {
    Guard g;
    gSettings.screenCheck = lv_obj_has_state(dCheck, LV_STATE_CHECKED) ? 1 : 0;
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

// --- custom accent colour: hue, saturation and brightness sliders in a dialog (OK applies it)
static lv_obj_t *pkSw, *pkHex, *pkSl[3];
static uint16_t pkH;
static uint8_t pkS, pkV;
static uint32_t pkOrig;  // the colour when opened: kept exactly as long as no slider was moved
static bool pkMoved;
static constexpr int PK_VMIN = 20;  // darker than this is no usable accent

static uint32_t hsvRgb(uint16_t h, uint8_t s, uint8_t v) { return lv_color_to_u32(lv_color_hsv_to_rgb(h, s, v)) & 0xFFFFFF; }

static uint32_t pkRgb() { return pkMoved ? hsvRgb(pkH, pkS, pkV) : pkOrig; }

static void pkShow() {
  const uint32_t rgb = pkRgb();
  lv_obj_set_style_bg_color(pkSw, lv_color_hex(rgb), 0);
  char b[12];
  snprintf(b, sizeof(b), "#%06lX", (unsigned long)rgb);
  lv_label_set_text(pkHex, b);
  // saturation: grey ... full colour; brightness: dark ... bright (at the other two values)
  lv_obj_set_style_bg_color(pkSl[1], lv_color_hex(hsvRgb(pkH, 0, pkV)), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(pkSl[1], lv_color_hex(hsvRgb(pkH, 100, pkV)), LV_PART_MAIN);
  lv_obj_set_style_bg_color(pkSl[2], lv_color_hex(hsvRgb(pkH, pkS, PK_VMIN)), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(pkSl[2], lv_color_hex(hsvRgb(pkH, pkS, 100)), LV_PART_MAIN);
}

static void pkSlider(lv_event_t*) {
  pkH = lv_slider_get_value(pkSl[0]);
  pkS = lv_slider_get_value(pkSl[1]);
  pkV = lv_slider_get_value(pkSl[2]);
  pkMoved = true;
  pkShow();
}

static void pkAnswer(int b, int) {
  if (b != 2) return;  // 2 = OK
  {
    Guard g;
    gSettings.accentColor = pkRgb();
  }
  saveQuiet();
  displayShow();
}

// a slider whose bar is a colour scale: the knob is white with a dark edge, no indicator
static lv_obj_t* pkMakeSlider(lv_obj_t* parent, int lo, int hi, int v) {
  lv_obj_t* sl = lv_slider_create(parent);
  lv_obj_set_height(sl, TUI_SMALL ? 12 : 14);
  lv_obj_set_flex_grow(sl, 1);
  lv_slider_set_range(sl, lo, hi);
  lv_slider_set_value(sl, v, LV_ANIM_OFF);
  lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(sl, LV_GRAD_DIR_HOR, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_border_color(sl, lv_color_black(), LV_PART_KNOB);
  lv_obj_set_style_border_opa(sl, LV_OPA_60, LV_PART_KNOB);
  lv_obj_set_style_border_width(sl, 2, LV_PART_KNOB);
  lv_obj_set_style_pad_all(sl, TUI_SMALL ? 5 : 6, LV_PART_KNOB);
  lv_obj_set_ext_click_area(sl, 10);
  lv_obj_add_event_cb(sl, pkSlider, LV_EVENT_VALUE_CHANGED, nullptr);
  return sl;
}

static void onAccentCustom(lv_event_t*) {
  Settings S;
  settingsCopy(S);
  const uint32_t cur = S.accentColor & 0xFFFFFF;
  const lv_color_hsv_t hsv = lv_color_rgb_to_hsv((cur >> 16) & 0xFF, (cur >> 8) & 0xFF, cur & 0xFF);
  pkH = hsv.h % 360;
  pkS = hsv.s;
  pkV = hsv.v < PK_VMIN ? PK_VMIN : hsv.v;
  pkOrig = cur;
  pkMoved = false;
  lv_obj_t* box = tDialog("Custom colour", "", "Cancel", "OK", "", pkAnswer, 0);
  tSetHidden(lv_obj_get_child(box, 1), true);  // the dialog's (empty) text
  lv_obj_t* content = tCol(box, TUI_SMALL ? 8 : 10);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_move_to_index(content, 1);  // above the buttons
  lv_obj_t* top = tRow(content, 10);
  pkSw = lv_obj_create(top);
  lv_obj_remove_style_all(pkSw);
  lv_obj_set_size(pkSw, TUI_SMALL ? 26 : 32, TUI_SMALL ? 26 : 32);
  lv_obj_set_style_radius(pkSw, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(pkSw, LV_OPA_COVER, 0);
  pkHex = tLabel(top, TUI_SMALL ? &tfont_16b : &tfont_20c, "");
  static const char* const NAMES[3] = {"Hue", "Saturation", "Brightness"};
  for (int i = 0; i < 3; ++i) {
    lv_obj_t* r = tRow(content, 8);
    lv_obj_t* l = tLabel(r, TUI_SMALL ? &tfont_12 : &tfont_14, NAMES[i]);
    lv_obj_set_width(l, TUI_SMALL ? 66 : 84);
    lv_obj_add_style(l, &S_.muted, 0);
    if (i == 0) {
      // the hue scale: 6 segments with a 2-colour gradient each, the slider (knob only) on top
      lv_obj_t* w = lv_obj_create(r);
      lv_obj_remove_style_all(w);
      lv_obj_set_flex_grow(w, 1);
      lv_obj_set_height(w, TUI_SMALL ? 22 : 26);
      lv_obj_set_scrollable(w, false);
      lv_obj_t* bar = tRow(w, 0);
      lv_obj_set_height(bar, TUI_SMALL ? 12 : 14);
      lv_obj_center(bar);
      lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_clip_corner(bar, true, 0);
      for (int k = 0; k < 6; ++k) {
        lv_obj_t* seg = lv_obj_create(bar);
        lv_obj_remove_style_all(seg);
        lv_obj_set_height(seg, LV_PCT(100));
        lv_obj_set_flex_grow(seg, 1);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(hsvRgb(k * 60, 100, 100)), 0);
        lv_obj_set_style_bg_grad_color(seg, lv_color_hex(hsvRgb((k + 1) * 60 % 360, 100, 100)), 0);
        lv_obj_set_style_bg_grad_dir(seg, LV_GRAD_DIR_HOR, 0);
      }
      pkSl[0] = pkMakeSlider(w, 0, 359, pkH);
      lv_obj_set_width(pkSl[0], LV_PCT(100));
      lv_obj_center(pkSl[0]);
      lv_obj_set_style_bg_opa(pkSl[0], LV_OPA_TRANSP, LV_PART_MAIN);
    } else {
      pkSl[i] = pkMakeSlider(r, i == 1 ? 0 : PK_VMIN, 100, i == 1 ? pkS : pkV);
    }
  }
  pkShow();
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
  lv_obj_set_size(dBright, TUI_SMALL ? 140 : 220, 18);
  lv_slider_set_range(dBright, 10, 255);
  lv_obj_set_style_pad_all(dBright, 6, LV_PART_KNOB);
  lv_obj_set_ext_click_area(dBright, 12);
  lv_obj_add_event_cb(dBright, onBright, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(dBright, onBright, LV_EVENT_RELEASED, nullptr);
  r = settingRow(p.body, "Dark mode", "The web page follows its own setting");
  dDark = tSwitch(r, onDark);
  r = settingRow(p.body, "Accent colour", "Channel colours follow it (touch and web)");
  lv_obj_t* sw = tRow(r, TUI_SMALL ? 4 : 8);
  lv_obj_set_width(sw, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(sw, 5, 0);  // room for the ring of the chosen colour
  for (int i = 0; i < 6; ++i) {
    dAcc[i] = lv_button_create(sw);
    lv_obj_remove_style_all(dAcc[i]);
    lv_obj_set_size(dAcc[i], TUI_SMALL ? 22 : 30, TUI_SMALL ? 22 : 30);
    lv_obj_set_style_radius(dAcc[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dAcc[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dAcc[i], lv_color_hex(ACCENTS[i]), 0);  // the preset itself, not a theme role
    lv_obj_add_style(dAcc[i], &S_.ring, LV_STATE_CHECKED);
    lv_obj_set_ext_click_area(dAcc[i], 4);
    lv_obj_add_event_cb(dAcc[i], onAccent, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  dAcc[6] = lv_button_create(sw);  // any colour: opens the picker (filled with the colour when chosen)
  lv_obj_remove_style_all(dAcc[6]);
  lv_obj_set_size(dAcc[6], TUI_SMALL ? 22 : 30, TUI_SMALL ? 22 : 30);
  lv_obj_set_style_radius(dAcc[6], LV_RADIUS_CIRCLE, 0);
  lv_obj_add_style(dAcc[6], &S_.ring, LV_STATE_CHECKED);
  lv_obj_set_ext_click_area(dAcc[6], 4);
  lv_obj_add_event_cb(dAcc[6], onAccentCustom, LV_EVENT_CLICKED, nullptr);
  dAccIcon = lv_label_create(dAcc[6]);
  lv_obj_set_style_text_font(dAccIcon, TUI_SMALL ? &lv_font_montserrat_12 : &lv_font_montserrat_14, 0);
  lv_label_set_text(dAccIcon, LV_SYMBOL_EDIT);
  lv_obj_center(dAccIcon);
  r = settingRow(p.body, "Turn the screen", "Upside down (180\xC2\xB0)");
  dRotate = tSwitch(r, onRotate);
  r = settingRow(p.body, "Live chart period", "Also on the web page");
  dPeriod = tButton(r, "", onPeriod, nullptr, CW2, BH);
  r = settingRow(p.body, "Screen check", "Restarts a hanging display or touch by itself");
  dCheck = tSwitch(r, onCheck);
  return p.scr;
}

void displayShow() {
  Settings S;
  settingsCopy(S);
  dLoading = true;
  lv_slider_set_value(dBright, S.brightness, LV_ANIM_OFF);
  tSetChecked(dDark, S.themeDark != 0);
  tSetChecked(dRotate, S.rotate180 != 0);
  tSetChecked(dCheck, S.screenCheck != 0);
  const uint32_t acc = S.accentColor & 0xFFFFFF;
  bool preset = false;
  for (int i = 0; i < 6; ++i) {
    tSetChecked(dAcc[i], acc == ACCENTS[i]);
    preset = preset || acc == ACCENTS[i];
  }
  tSetChecked(dAcc[6], !preset);
  if (preset) {  // an outlined circle with the pen
    lv_obj_set_style_bg_opa(dAcc[6], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dAcc[6], 2, 0);
    lv_obj_set_style_border_color(dAcc[6], tc(C_MUTED), 0);
    lv_obj_set_style_text_color(dAccIcon, tc(C_MUTED), 0);
  } else {  // filled with the custom colour, the pen in black or white (whichever reads better)
    const lv_color_t c = lv_color_hex(acc);
    lv_obj_set_style_bg_opa(dAcc[6], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dAcc[6], c, 0);
    lv_obj_set_style_border_width(dAcc[6], 0, 0);
    lv_obj_set_style_text_color(dAccIcon, lv_color_luminance(c) > 150 ? lv_color_black() : lv_color_white(), 0);
  }
  char b[24];
  uint32_t cur;
  {
    Guard g;
    cur = liveMinEff();
  }
  fmtMinutes(cur, b, sizeof(b));
  tButtonText(dPeriod, b);
  dShown = {S.brightness, S.themeDark, S.rotate180, S.screenCheck, S.accentColor, S.liveMin};
  dLoading = false;
  (void)ACC_NAMES;
}

void displayUpdate() {
  if (lv_obj_has_state(dBright, LV_STATE_PRESSED)) return;  // being moved here
  Settings S;
  settingsCopy(S);
  if (S.brightness != dShown.bright || S.themeDark != dShown.dark || S.rotate180 != dShown.rot || S.screenCheck != dShown.check ||
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
  if (!scanShown || tCurrent() != SCR_NETWORK) return;  // the page was left meanwhile
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
  nSsid = tButton(r, "", onSsid, nullptr, CW, BH);
  tOneLine(lv_obj_get_child(nSsid, 0));
  lv_obj_set_width(lv_obj_get_child(nSsid, 0), CW - 20);
  tButton(r, "Scan", onScan, nullptr, TUI_SMALL ? 52 : 70, BH);
  r = settingRow(p.body, "Password", nullptr);
  nPass = tButton(r, "", onPass, nullptr, CW, BH);
  r = settingRow(p.body, "Access point password", "Used when no network is reachable; 8+ characters");
  nAp = tButton(r, "", onAp, nullptr, CW, BH);
  r = settingRow(p.body, "Host name", "http://<name>.local");
  nHost = tButton(r, "", onHost, nullptr, CW, BH);
  r = settingRow(p.body, "Fixed IP address", "No DHCP: use an address outside the router's DHCP range");
  nStatic = tSwitch(r, onStatic);
  nIpBox = tCol(p.body, 6);
  lv_obj_set_width(nIpBox, LV_PCT(100));
  for (int i = 0; i < 5; ++i) {
    r = settingRow(nIpBox, IP_LABELS[i], i == 3 ? "Empty = the gateway" : nullptr);
    nIp[i] = tButton(r, "", onIp, (void*)(intptr_t)i, CW, BH);
  }
  lv_obj_t* b = tButton(p.header, TUI_SMALL ? "Save + connect" : "Save and connect", onNetSave, nullptr, LV_SIZE_CONTENT, T_HDR - 8);  // always visible
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
  at = tTickNow();
  tSetText(nStatus, netInfo().c_str());
}

// ------------------------------------------------------------ system
static lv_obj_t *sInfo, *sPoll, *sBand, *sTz, *sFw, *sIdle, *sEstop, *sSound, *sRelay, *sChip;

// ------------------------------------------------------------ time zone (two steps: region, then the place)
static const char* const TZ_OFFSETS[] = {"UTC-12:00", "UTC-11:00", "UTC-10:00", "UTC-09:30", "UTC-09:00", "UTC-08:00",
                                         "UTC-07:00", "UTC-06:00", "UTC-05:00", "UTC-04:00", "UTC-03:30", "UTC-03:00",
                                         "UTC-02:00", "UTC-01:00", "UTC+01:00", "UTC+02:00", "UTC+03:00", "UTC+03:30",
                                         "UTC+04:00", "UTC+04:30", "UTC+05:00", "UTC+05:30", "UTC+05:45", "UTC+06:00",
                                         "UTC+06:30", "UTC+07:00", "UTC+08:00", "UTC+09:00", "UTC+09:30", "UTC+10:00",
                                         "UTC+11:00", "UTC+12:00", "UTC+13:00", "UTC+14:00"};
static constexpr int TZ_NOFF = sizeof(TZ_OFFSETS) / sizeof(TZ_OFFSETS[0]);
static char tzRegions[10][16];  // the list's regions ("Europe", ...); [0] "UTC", last: fixed offsets
static int tzNReg = 0;
static int16_t tzPick[40];      // the list index of each place in the last region list

static void tzSet(const char* name) {
  char rule[48];
  if (*name && !tzLookup(name, rule, sizeof(rule))) return;
  {
    Guard g;
    strlcpy(gSettings.tzName, name, sizeof(gSettings.tzName));
  }
  saveQuiet();
  logEvent("Time zone: %s", *name ? name : "not set (UTC)");
  systemShow();
}

static void onTzPlace(int i, int) {
  if (i >= 0 && i < 40 && tzPick[i] >= 0) tzSet(tzListName(tzPick[i]));
}
static void onTzOffset(int i, int) {
  if (i >= 0 && i < TZ_NOFF) tzSet(TZ_OFFSETS[i]);
}

static void onTzRegion(int r, int) {
  if (r < 0) return;
  char cur[sizeof(gSettings.tzName)];
  {
    Guard g;
    strlcpy(cur, gSettings.tzName, sizeof(cur));
  }
  if (r == 0) return tzSet("UTC");
  constexpr size_t ISZ = 1400;
  char* items = (char*)malloc(ISZ);  // (only while the popup is built: its labels copy the text)
  if (!items) return;
  items[0] = 0;
  int n = 0, sel = -1;
  if (r == tzNReg - 1) {  // fixed offsets
    for (int i = 0; i < TZ_NOFF; ++i) {
      if (i) strlcat(items, "\n", ISZ);
      strlcat(items, TZ_OFFSETS[i], ISZ);
      if (!strcmp(cur, TZ_OFFSETS[i])) sel = i;
    }
    tChoice("UTC offset (no summer time)", items, sel, onTzOffset, 0);
    free(items);
    return;
  }
  const size_t rl = strlen(tzRegions[r]);
  for (int i = 0; i < tzCount() && n < 40; ++i) {
    const char* z = tzListName(i);
    if (strncmp(z, tzRegions[r], rl) || z[rl] != '/') continue;
    char place[40];
    strlcpy(place, z + rl + 1, sizeof(place));
    for (char* c = place; *c; ++c)
      if (*c == '_') *c = ' ';
    if (n) strlcat(items, "\n", ISZ);
    strlcat(items, place, ISZ);
    if (!strcmp(cur, z)) sel = n;
    tzPick[n++] = (int16_t)i;
  }
  tChoice(tzRegions[r], items, sel, onTzPlace, 0);
  free(items);
}

static void onTz(lv_event_t*) {
  tzNReg = 0;
  strlcpy(tzRegions[tzNReg++], "UTC", sizeof(tzRegions[0]));
  for (int i = 0; i < tzCount(); ++i) {
    const char* z = tzListName(i);
    const char* s = strchr(z, '/');
    if (!s) continue;
    char reg[16];
    snprintf(reg, sizeof(reg), "%.*s", (int)(s - z), z);
    bool have = false;
    for (int k = 0; k < tzNReg; ++k) have |= !strcmp(tzRegions[k], reg);
    if (!have && tzNReg < 9) strlcpy(tzRegions[tzNReg++], reg, sizeof(tzRegions[0]));
  }
  strlcpy(tzRegions[tzNReg++], "UTC offset", sizeof(tzRegions[0]));
  char cur[sizeof(gSettings.tzName)];
  {
    Guard g;
    strlcpy(cur, gSettings.tzName, sizeof(cur));
  }
  char items[200] = "";
  int sel = -1;
  for (int k = 0; k < tzNReg; ++k) {
    if (k) strlcat(items, "\n", sizeof(items));
    strlcat(items, k == tzNReg - 1 ? "UTC offset (no summer time)" : tzRegions[k], sizeof(items));
    const size_t rl = strlen(tzRegions[k]);
    if (k == 0 ? !strcmp(cur, "UTC") : k == tzNReg - 1 ? !strncmp(cur, "UTC", 3) && cur[3] : !strncmp(cur, tzRegions[k], rl) && cur[rl] == '/')
      sel = k;
  }
  tChoice("Time zone", items, sel, onTzRegion, 0);
}

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

// ---- safety (spec 9.2a)
static const uint16_t IDLE_MIN[] = {10, 15, 30, 60, 120, 240};
static const uint8_t CHIP_C[] = {60, 70, 80, 90, 100};
static void onIdlePick(int i, int) {
  if (i < 0) return;
  {
    Guard g;
    gSettings.manualIdle = i > 0;
    if (i > 0) gSettings.manualIdleMin = IDLE_MIN[i - 1];
  }
  saveSettingsAndNotify();
  systemShow();
}
static void onIdle(lv_event_t*) {
  Settings s;
  settingsCopy(s);
  int sel = 0;
  for (int i = 0; i < 6; ++i)
    if (s.manualIdle && s.manualIdleMin == IDLE_MIN[i]) sel = i + 1;
  tChoice("Manual mode ends without activity after", "Never\n10 min\n15 min\n30 min\n1 h\n2 h\n4 h", sel, onIdlePick, 0);
}
static void onChipPick(int i, int) {
  if (i < 0) return;
  {
    Guard g;
    gSettings.chipWarn = i > 0;
    if (i > 0) gSettings.chipMaxC = CHIP_C[i - 1];
  }
  saveSettingsAndNotify();
  systemShow();
}
static void onChip(lv_event_t*) {
  Settings s;
  settingsCopy(s);
  int sel = 0;
  for (int i = 0; i < 5; ++i)
    if (s.chipWarn && (int)s.chipMaxC == CHIP_C[i]) sel = i + 1;
  tChoice("Chip temperature warning above", "Off\n60 \xC2\xB0" "C\n70 \xC2\xB0" "C\n80 \xC2\xB0" "C\n90 \xC2\xB0" "C\n100 \xC2\xB0" "C", sel,
          onChipPick, 0);
}
static void onSafetySw(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  const bool on = lv_obj_has_state(o, LV_STATE_CHECKED);
  {
    Guard g;
    if (o == sEstop) gSettings.estop = on;
    if (o == sSound) gSettings.sound = on;
    if (o == sRelay) gSettings.relay = on;
  }
  saveSettingsAndNotify();
  if (o == sSound && on) safetyBeepTest();
}

// ---- firmware update from the SD card (S3; the web page: any board)
static SdFirmware sdFw;
static void onSdFwAnswer(int b, int) {
  if (b != 2) return;
  char err[120];
  if (!sdlogFirmwareInstall(err, sizeof(err))) tToast(err, true);
}
static void onSdFw(lv_event_t*) {
  char err[120];
  if (!otaCanStart(err, sizeof(err))) return tToast(err, true);
  if (!sdlogFirmwareCheck(sdFw, err, sizeof(err))) return tToast(err, true);
  if (!sdFw.found) return tToast("No firmware.bin on the SD card (in its root folder).", true);
  if (sdFw.problem[0]) return tToast(sdFw.problem, true);
  char t[64], m[160];
  snprintf(t, sizeof(t), "Install firmware %s?", sdFw.version);
  snprintf(m, sizeof(m), "From the SD card (%u kB), now %s. The device restarts afterwards; the new firmware is checked for 30 s.",
           (unsigned)(sdFw.size / 1024), FW_VERSION);
  tDialog(t, m, "Cancel", "Install", "", onSdFwAnswer, 0);
}

lv_obj_t* systemCreate() {
  TPage p = tPageCreate("System", backToMenu);
  lv_obj_t* r = settingRow(p.body, "Polling interval", "How often PV, SP and output are read. Two Modbus channels: each every 2 \xC3\x97 this");
  sPoll = tButton(r, "", onPoll, nullptr, CW2, BH);
  r = settingRow(p.body, "\"Bottom reached\" band", "\"Wait for bottom\" goes on within \xC2\xB1 this");
  sBand = tButton(r, "", onBand, nullptr, CW2, BH);
  r = settingRow(p.body, "Time zone", "Local time in the SD run logs");
  sTz = tButton(r, "", onTz, nullptr, CW2, BH);
  sFw = nullptr;
  if (sdlogSlot()) {
    r = settingRow(p.body, "Firmware update", "firmware.bin in the SD card's root folder (or: web page, Settings)");
    sFw = tButton(r, "SD card", onSdFw, nullptr, CW2, BH);
  }
  r = settingRow(p.body, "Manual mode timeout", "Manual mode ends after this long without a touch, web or serial action (a beep 1 min before)");
  sIdle = tButton(r, "", onIdle, nullptr, CW2, BH);
  sEstop = sSound = sRelay = sChip = nullptr;
  if (safetyEstopWired()) {
    r = settingRow(p.body, "E-stop input", "GPIO 5, NC contact to GND. Open: all outputs off, run stopped");
    sEstop = tSwitch(r, onSafetySw);
  }
  if (localCtrlRelayWired()) {
    r = settingRow(p.body, "SSR supply relay", "GPIO 4, +5 V of the SSR inputs. Switch off only if no relay is fitted");
    sRelay = tSwitch(r, onSafetySw);
  }
  if (safetySoundWired()) {
    r = settingRow(p.body, "Alarm sound", "Faults, E-stop, \"Action required\", run finished. A touch silences it");
    sSound = tSwitch(r, onSafetySw);
  }
  if (safetyChipTempWired()) {
    r = settingRow(p.body, "Chip temperature warning", "ESP32-S3 internal sensor");
    sChip = tButton(r, "", onChip, nullptr, CW2, BH);
  }
  lv_obj_t* info = tPanel(p.body);
  lv_obj_set_size(info, LV_PCT(100), LV_SIZE_CONTENT);
  sInfo = tLabel(info, &tfont_14, "");
  lv_obj_set_width(sInfo, LV_PCT(100));
  lv_label_set_long_mode(sInfo, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_t* b = tButton(p.body, "Restart device", onRestart, nullptr, LV_PCT(100), TUI_SMALL ? 36 : 44);
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
  const char* tz = s.tzName[0] ? (strchr(s.tzName, '/') ? strrchr(s.tzName, '/') + 1 : s.tzName) : "Not set (UTC)";
  char tzb[32];
  strlcpy(tzb, tz, sizeof(tzb));
  for (char* c = tzb; *c; ++c)
    if (*c == '_') *c = ' ';
  tButtonText(sTz, tzb);
  if (s.manualIdle) {
    if (s.manualIdleMin % 60) snprintf(b, sizeof(b), "%u min", s.manualIdleMin);
    else snprintf(b, sizeof(b), "%u h", s.manualIdleMin / 60);
  } else {
    strlcpy(b, "Never", sizeof(b));
  }
  tButtonText(sIdle, b);
  if (sEstop) tSetChecked(sEstop, s.estop != 0);
  if (sSound) tSetChecked(sSound, s.sound != 0);
  if (sRelay) tSetChecked(sRelay, s.relay != 0);
  if (sChip) {
    if (s.chipWarn) snprintf(b, sizeof(b), "%.0f \xC2\xB0" "C", s.chipMaxC);
    else strlcpy(b, "Off", sizeof(b));
    tButtonText(sChip, b);
  }
  systemUpdate();
}

void systemUpdate() {
  static uint32_t at = 0;
  if (at && lv_tick_elaps(at) < 2000) return;
  at = tTickNow();
  char up[16], b[400], hist[16];
  fmtTime(millis() / 1000.0f, up, sizeof(up));
  fmtMinutes(liveHistSec() / 60, hist, sizeof(hist));
  // RAM: the internal byte-addressable heap (what runs short), its lowest point since the start
  const uint32_t caps = MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL;
  char ram[64];
  snprintf(ram, sizeof(ram), "RAM %u kB free (lowest %u)", (unsigned)(heap_caps_get_free_size(caps) / 1024),
           (unsigned)(heap_caps_get_minimum_free_size(caps) / 1024));
#ifdef BOARD_HAS_PSRAM
  char ps[32];
  snprintf(ps, sizeof(ps), " \xC2\xB7 PSRAM %u kB free", (unsigned)(ESP.getFreePsram() / 1024));
  strlcat(ram, ps, sizeof(ram));
#endif
  LiveData* L = (LiveData*)malloc(sizeof(LiveData));
  char sf[96] = "";
  if (L) {
    liveCopy(*L);
    char t[40] = "";
    if (safetyEstopWired())
      snprintf(sf, sizeof(sf), "\nE-stop: %s (contact %s)", L->estop ? "ACTIVE" : gSettings.estop ? "ok" : "not watched",
               L->estopRaw ? "open" : "closed");
    if (!isnan(L->chipC)) {
      snprintf(t, sizeof(t), "%sChip %.0f \xC2\xB0" "C%s", sf[0] ? " \xC2\xB7 " : "\n", L->chipC, L->chipHot ? " (hot)" : "");
      strlcat(sf, t, sizeof(sf));
    }
    free(L);
  }
  snprintf(b, sizeof(b), PRODUCT_NAME " " FW_VERSION " \xC2\xB7 " BOARD_NAME "%s%s\n%s\nUp %s \xC2\xB7 %s\nLive history in memory: %s%s%s",
           BOARD_VARIANT[0] ? " " : "", BOARD_VARIANT, netInfo().c_str(), up, ram, hist,
           !localCtrlRelayUsed() ? "" : localCtrlRelayOn() ? "\nSSR supply relay: on" : "\nSSR supply relay: off", sf);
  tSetText(sInfo, b);
}

// network scan + page updates that run whatever screen is shown
void tuiBackground() { scanPoll(); }
