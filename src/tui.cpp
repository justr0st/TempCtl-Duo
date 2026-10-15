// Touch UI of both boards: display + touch glue, screen switching, the periodic update.
// Screens: tui_main.cpp (main), tui_menu.cpp (menu, log, display, network, system), tui_chart.cpp,
// building blocks and popups: tui_widgets.cpp, colours and styles: tui_theme.cpp.
#include "tui.h"
#include "crumbs.h"
#include "console.h"
#include "lcd.h"
#include "touch.h"
#include "safety.h"
#include "ota.h"
#include "ui.h"

void tuiBackground();

static lv_display_t* disp = nullptr;
static uint8_t appliedBrightness = 0;
static bool rotated = false;
static uint32_t appliedAccent = 0xFFFFFFFF;
static int appliedDark = -1;

// The touch drivers report screen coordinates; LVGL rotates pointer input by the display rotation
// itself, so it gets the point in the panel's native orientation.
static void touchToNative(int32_t& x, int32_t& y) {
  const int32_t sx = x, sy = y;
  switch (disp ? lv_display_get_rotation(disp) : LV_DISPLAY_ROTATION_0) {
    case LV_DISPLAY_ROTATION_90:
      x = sy;
      y = LCD_NATIVE_H - 1 - sx;
      break;
    case LV_DISPLAY_ROTATION_270:
      x = LCD_NATIVE_W - 1 - sy;
      y = sx;
      break;
    case LV_DISPLAY_ROTATION_180:
      x = LCD_NATIVE_W - 1 - sx;
      y = LCD_NATIVE_H - 1 - sy;
      break;
    default:
      break;
  }
}

// A press that does not move at all for a minute is a hanging touch controller (a finger always moves
// a little, and nothing on the screen is held that long): noticed here, handled by the screen check.
static constexpr uint32_t STUCK_MS = 60000;
static constexpr int STUCK_PX = 3;
static uint32_t stillSince = 0;
static int32_t stillX, stillY;
static volatile bool touchStuck = false;
static int stuckRestarts = 0;  // restarts for a stuck press since the last release (at most 3)

static void stuckCheck(bool pressed, int32_t x, int32_t y) {
  if (!pressed) {
    stillSince = 0;
    stuckRestarts = 0;
    return;
  }
  const uint32_t now = tTickNow();
  if (!stillSince || abs(x - stillX) > STUCK_PX || abs(y - stillY) > STUCK_PX) {
    stillSince = now;
    stillX = x;
    stillY = y;
  } else if (now - stillSince > STUCK_MS) {
    touchStuck = true;
    stillSince = now;
  }
}

static void touchCb(lv_indev_t*, lv_indev_data_t* data) {
  int32_t x, y;
  const bool pressed = touchRead(&x, &y);
  stuckCheck(pressed, pressed ? x : 0, pressed ? y : 0);
  if (pressed) {
    safetyActivity();
    touchToNative(x, y);
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// Slide off to cancel (Rost): a press that is released away from the widget it started on does nothing (LVGL
// keeps a pressed widget pressed when the finger leaves it and would click it on release). The touch input's
// own event comes before the widget's: on a release outside, the input is reset, so the widget gets neither
// RELEASED (switches would toggle there) nor CLICKED, only INDEV_RESET (pressed look removed). Long-press repeats
// (steppers) pause while the finger is outside. Sliders and the keyboard / number keys (button matrix: it
// takes the key under the finger itself) keep LVGL's behaviour; a scroll has cancelled the click already.
static constexpr int32_t SLIDE_MARGIN = TUI_SMALL ? 6 : 10;  // px of play around the widget (finger jitter)
static void slideOffCancels(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_RELEASED && code != LV_EVENT_LONG_PRESSED && code != LV_EVENT_LONG_PRESSED_REPEAT) return;
  lv_indev_t* in = (lv_indev_t*)lv_event_get_current_target(e);
  lv_obj_t* obj = (lv_obj_t*)lv_event_get_param(e);
  if (!in || !obj || lv_indev_get_scroll_obj(in)) return;
  if (lv_obj_check_type(obj, &lv_slider_class) || lv_obj_check_type(obj, &lv_buttonmatrix_class) ||
      lv_obj_check_type(obj, &lv_keyboard_class) || lv_obj_check_type(obj, &lv_textarea_class))
    return;
  lv_point_t p;
  lv_indev_get_point(in, &p);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  if (p.x >= a.x1 - SLIDE_MARGIN && p.x <= a.x2 + SLIDE_MARGIN && p.y >= a.y1 - SLIDE_MARGIN && p.y <= a.y2 + SLIDE_MARGIN)
    return;
  if (code == LV_EVENT_RELEASED) lv_indev_reset(in, obj);
  else lv_indev_stop_processing(in);  // (this repeat only; it goes on when the finger is back)
}

static uint32_t tickCb() { return millis(); }

void uiSetBrightness(uint8_t b) {
  if (b < 10) b = 10;
  lcdSetBrightness(b);
  appliedBrightness = b;
}

static void applyRotation(bool r) {
  rotated = r;
  lcdSetRotation(r);
  touchSetRotation(r);
  lv_obj_invalidate(lv_screen_active());
  lv_obj_invalidate(lv_layer_top());
}

void settingsCopy(Settings& out) {
  Guard g;
  out = gSettings;
}

void liveCopy(LiveData& out) {
  Guard g;
  out = gLive;
}

// ------------------------------------------------------------ screens
static lv_obj_t* screens[SCR_COUNT];
static TScreen cur = SCR_MAIN;

TScreen tCurrent() { return cur; }

void tShow(TScreen s) {
  if (!screens[s]) {
    switch (s) {
      case SCR_MAIN: screens[s] = mainCreate(); break;
      case SCR_MENU: screens[s] = menuCreate(); break;
      case SCR_LOG: screens[s] = logCreate(); break;
      case SCR_DISPLAY: screens[s] = displayCreate(); break;
      case SCR_NETWORK: screens[s] = networkCreate(); break;
      case SCR_SYSTEM: screens[s] = systemCreate(); break;
      case SCR_PROGRAMS: screens[s] = progListCreate(); break;
      case SCR_PROGEDIT: screens[s] = progEditCreate(); break;
      case SCR_CHANNELS: screens[s] = chanCreate(); break;
      default: return;  // not there yet
    }
  }
  const TScreen prev = cur;
  (void)prev;
  cur = s;
  tModalClose();
  if (s == SCR_DISPLAY) displayShow();
  if (s == SCR_NETWORK) networkShow();
  if (s == SCR_SYSTEM) systemShow();
  if (s == SCR_PROGRAMS) progListShow();
  if (s == SCR_CHANNELS) chanShow();
  if (s == SCR_PROGEDIT) progEditShow();
  lv_screen_load(screens[s]);
#if TUI_SMALL
  // 320x240 (no PSRAM, short internal RAM): only the main screen stays; the others are built again when
  // opened (a few ms), so their objects don't use RAM while they aren't shown
  if (prev != s && prev != SCR_MAIN && screens[prev]) {
    lv_obj_delete_async(screens[prev]);
    screens[prev] = nullptr;
    if (prev == SCR_PROGEDIT) progEditRelease();
  }
#endif
}

// ------------------------------------------------------------ periodic update
// the snapshot of gLive / gSettings taken on every UI tick (one copy for all screens: the main screen
// reads it directly)
static LiveData& snapLive() {
  static LiveData L;
  return L;
}
static Settings& snapSettings() {
  static Settings S;
  return S;
}
const LiveData& tuiLive() { return snapLive(); }
const Settings& tuiSettings() { return snapSettings(); }

static void uiTimer(lv_timer_t*) {
  LiveData& L = snapLive();
  Settings& S = snapSettings();
  {
    Guard g;
    L = gLive;
    S = gSettings;
  }
  if (S.brightness != appliedBrightness) uiSetBrightness(S.brightness);
  if ((S.rotate180 != 0) != rotated) applyRotation(S.rotate180);
  const bool dark = S.themeDark != 0;
  if ((S.accentColor & 0xFFFFFF) != appliedAccent || (int)dark != appliedDark) {
    appliedAccent = S.accentColor & 0xFFFFFF;
    appliedDark = dark;
    themeApply(appliedAccent, dark);
  }
  tuiBackground();
  mainEvents();
  switch (cur) {
    case SCR_MAIN: mainUpdate(); break;
    case SCR_LOG: logUpdate(); break;
    case SCR_DISPLAY: displayUpdate(); break;
    case SCR_NETWORK: networkUpdate(); break;
    case SCR_SYSTEM: systemUpdate(); break;
    case SCR_PROGRAMS: progListShow(); break;
    case SCR_PROGEDIT: progEditUpdate(); break;
    case SCR_CHANNELS: chanUpdate(); break;
    default: break;
  }
}

void uiInit() {
  bool rot;
  uint8_t b;
  TouchCal cal;
  {
    Guard g;
    cal = {gSettings.tsMinX, gSettings.tsMaxX, gSettings.tsMinY, gSettings.tsMaxY};
    rot = gSettings.rotate180;
    b = gSettings.brightness;
    if (gSettings.themeDark == THEME_DARK_DEFAULT) gSettings.themeDark = 1;  // first start: dark
  }
  rotated = rot;
  touchInit(cal);
  touchSetRotation(rot);

  lv_init();
  lv_tick_set_cb(tickCb);
  lv_delay_set_cb(delay);
  disp = lcdInit(rot);
  if (!disp) {
    LogSerial.println("[ui] display could not be set up - no touch UI");
    return;
  }
  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
  lv_indev_add_event_cb(indev, slideOffCancels, LV_EVENT_ALL, nullptr);

  Settings S;
  settingsCopy(S);
  appliedAccent = S.accentColor & 0xFFFFFF;
  appliedDark = S.themeDark != 0;
  themeApply(appliedAccent, appliedDark);
  tShow(SCR_MAIN);
  uiTimer(nullptr);
  lv_timer_create(uiTimer, 250, nullptr);
  lv_refr_now(disp);  // first frame, then the backlight
  uiSetBrightness(b);
}

// Screen restart (serial "screen restart", web): asked from any task, done here in the UI task
static volatile bool scrRestartReq = false;
static const char* volatile scrFrom = "";
static volatile uint32_t scrRestarts = 0;
static volatile bool scrLcdOk = false, scrTouchOk = false;

void uiScreenRestart(const char* from) {
  scrFrom = from;
  scrRestartReq = true;
}

static void screenRestartNow(int what);

// a restart asked for by hand: an event with what the check saw just before (to learn what hung)
static void screenRestartAsked() {
  char d[80], t[80];
  const int dl = lcdCheck(d, sizeof(d)), tc = touchCheck(t, sizeof(t));
  char ds[96];
  if (dl > 0) snprintf(ds, sizeof(ds), "display OK (%s)", d);
  else if (dl == 0) snprintf(ds, sizeof(ds), "display LOST (%s)", d);
  else snprintf(ds, sizeof(ds), "display not readable");
  logKind(tc > 0 ? LK_EVENT : LK_WARN, "Screen restarted %s - before: %s, touch %s", scrFrom, ds, tc > 0 ? "OK" : tc == 0 ? "NO ANSWER" : "?");
  screenRestartNow(3);
}

uint32_t uiScreenRestartPoll(bool& lcdOk, bool& touchOk) {
  lcdOk = scrLcdOk;
  touchOk = scrTouchOk;
  return scrRestarts;
}

// what: 1 = touch controller, 2 = display controller, 3 = both
static void screenRestartNow(int what) {
  TouchCal cal;
  uint8_t b;
  bool rot;
  {
    Guard g;
    cal = {gSettings.tsMinX, gSettings.tsMaxX, gSettings.tsMinY, gSettings.tsMaxY};
    b = gSettings.brightness;
    rot = gSettings.rotate180;
  }
  LogSerial.printf("[ui] screen restart: %s\n", what == 3 ? "display and touch controller" : what == 2 ? "display controller" : "touch controller");
  if (what & 1) {
    scrTouchOk = touchReset(cal);
    touchSetRotation(rot);
    lv_indev_reset(nullptr, nullptr);  // a press that got stuck is dropped
    stillSince = 0;
  }
  if (what & 2) {
    scrLcdOk = lcdReinit();
    if (scrLcdOk) {
      lv_refr_now(disp);
      uiSetBrightness(b);
    }
  }
  ++scrRestarts;
}

// ------------------------------------------------------------ automatic screen check (every second)
// Touch controller asked (FT6336), a press stuck in one place, display status read back (lcdCheck).
// Two failed checks in a row restart the part concerned; at most one automatic restart per 30 s, and
// after 3 restarts that did not help (the next check fails again) it waits until a check passes.
static uint32_t autoRestarts = 0;
static int touchFails = 0, lcdFails = 0, autoInARow = 0;
static uint32_t lastAuto = 0;
static bool gaveUp = false;

static void autoRestart(int what, const char* why) {
  if (lastAuto && millis() - lastAuto < 30000) return;
  if (autoInARow >= 3) {
    if (!gaveUp) logWarn("Screen check: restarting did not help (%s) - no more automatic restarts for now", why);
    gaveUp = true;
    return;
  }
  lastAuto = millis();
  if (!lastAuto) lastAuto = 1;
  ++autoInARow;
  ++autoRestarts;
  logWarn("Screen restarted automatically: %s", why);
  screenRestartNow(what);
  touchFails = lcdFails = 0;
}

static void screenWatch() {
  static uint32_t at = 0;
  if (!disp || millis() - at < 1000) return;
  at = millis();
  bool on;
  {
    Guard g;
    on = gSettings.screenCheck != 0;
  }
  if (!on) {
    touchStuck = false;
    return;
  }
  char info[112];
  const int t = touchCheck(info, sizeof(info));
  touchFails = t == 0 ? touchFails + 1 : 0;
  const int d = lcdCheck(info, sizeof(info));
  lcdFails = d == 0 ? lcdFails + 1 : 0;
  if (t != 0 && d != 0) {  // all fine (or cannot tell): automatic restarts allowed again
    autoInARow = 0;
    gaveUp = false;
  }
  if (lcdFails >= 2) {
    char why[160];
    snprintf(why, sizeof(why), "display lost its setup (%s)", info);
    autoRestart(touchFails >= 2 || touchStuck ? 3 : 2, why);
  } else if (touchFails >= 2) {
    autoRestart(1, "touch controller does not answer");
  } else if (touchStuck) {
    touchStuck = false;
    if (stuckRestarts < 3) {  // (a press the hardware keeps reporting: 3 tries, then left as it is)
      ++stuckRestarts;
      lastAuto = 0;
      autoInARow = 0;
      autoRestart(1, "touch stuck pressed in one place for a minute");
    } else if (stuckRestarts == 3) {
      ++stuckRestarts;
      logWarn("Screen check: touch still stuck after 3 restarts - left as it is");
    }
  }
}

int uiScreenCheckTest() {
  bool on;
  {
    Guard g;
    on = gSettings.screenCheck != 0;
  }
  char i[112];
  if (!on) return -1;
  if (lcdCheck(i, sizeof(i)) < 0) return -2;  // status not readable: the check cannot see it
  lastAuto = 0;  // (no wait for an earlier automatic restart)
  autoInARow = 0;
  gaveUp = false;
  return lcdTestBreak() ? 1 : -3;
}

void uiScreenCheckInfo(void (*line)(const char*)) {
  char b[240], i[200];
  bool on;
  {
    Guard g;
    on = gSettings.screenCheck != 0;
  }
  const int d = lcdCheck(i, sizeof(i));
  snprintf(b, sizeof(b), "display  %s%s", d > 0 ? "OK: " : d == 0 ? "LOST ITS SETUP: " : "", i);
  line(b);
  const int t = touchCheck(i, sizeof(i));
  snprintf(b, sizeof(b), "touch    %s", i);
  line(b);
  touchStats(i, sizeof(i));
  if (i[0]) {
    snprintf(b, sizeof(b), "         %s", i);
    line(b);
  }
  snprintf(b, sizeof(b), "check    %s, %lu automatic restart(s) since the start%s", on ? "on (every second)" : "off",
           (unsigned long)autoRestarts, gaveUp ? " - given up for now" : "");
  line(b);
  (void)t;
}

// ------------------------------------------------------------ firmware update in progress (ota.h)
// A full-screen overlay while an update is written (web upload or SD card) until the restart; a failed
// update closes it with a message.
static lv_obj_t *otaBox = nullptr, *otaText = nullptr, *otaBar = nullptr;
void tuiOtaPoll() {
  static uint32_t at = 0;
  if (at && lv_tick_elaps(at) < 250) return;
  at = tTickNow();
  OtaStatus st;
  otaStatus(st);
  static uint8_t lastPhase = OTA_IDLE;
  const bool show = st.phase == OTA_RUNNING || st.phase == OTA_DONE;
  if (st.phase == OTA_FAILED && lastPhase == OTA_RUNNING) {
    char m[128];
    snprintf(m, sizeof(m), "Firmware update failed: %s", st.msg);
    tToast(m, true);
  }
  lastPhase = st.phase;
  if (!show) {
    if (otaBox) lv_obj_delete(otaBox);
    otaBox = nullptr;
    return;
  }
  if (!otaBox) {
    tModalClose();
    otaBox = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(otaBox);
    lv_obj_add_style(otaBox, &S_.scr, 0);
    lv_obj_set_size(otaBox, SCREEN_W, SCREEN_H);
    lv_obj_set_clickable(otaBox, true);  // (touches don't reach the screens below)
    lv_obj_set_scrollable(otaBox, false);
    otaText = tLabel(otaBox, &tfont_16b, "");
    lv_obj_set_width(otaText, SCREEN_W - 40);
    lv_obj_set_style_text_align(otaText, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(otaText, LV_ALIGN_CENTER, 0, -20);
    otaBar = lv_bar_create(otaBox);
    lv_obj_set_size(otaBar, SCREEN_W - 80, 12);
    lv_obj_align(otaBar, LV_ALIGN_CENTER, 0, TUI_SMALL ? 30 : 40);
    lv_bar_set_range(otaBar, 0, 100);
  }
  const int pct = st.phase == OTA_DONE ? 100 : st.total ? (int)((uint64_t)st.done * 100 / st.total) : -1;
  char b[160];
  if (st.phase == OTA_DONE) {
    snprintf(b, sizeof(b), "Firmware %s written\nRestarting...", st.version);
  } else {
    char amount[32];
    if (pct >= 0) snprintf(amount, sizeof(amount), "%d %%", pct);
    else snprintf(amount, sizeof(amount), "%u kB", (unsigned)(st.done / 1024));
    snprintf(b, sizeof(b), "Firmware update%s%s\n%s %s\nDon't switch off.", st.version[0] ? " to " : "", st.version,
             st.source == OTA_SD ? "From the SD card:" : "Over Wi-Fi:", amount);
  }
  lv_label_set_text(otaText, b);
  lv_bar_set_value(otaBar, pct >= 0 ? pct : 50, LV_ANIM_OFF);
}

void uiOtaRefresh() {
  if (!disp) return;
  tuiOtaPoll();
  lv_timer_handler();
}

void uiLoop() {
  tuiOtaPoll();
  if (scrRestartReq) {
    scrRestartReq = false;
    screenRestartAsked();
  }
  crumb(CR_LOOP, L_SCREENCHECK);
  screenWatch();
  crumb(CR_LOOP, L_LVGL);
  if (disp) lv_timer_handler();
}

// ------------------------------------------------------------ touch calibration (CYD, serial "touch cal")
// Four crosses near the corners, tapped one after the other. While a cross is pressed the raw
// readings are averaged; from the four averages the raw value at the screen edges is extrapolated
// (in the panel's unrotated orientation, which the driver's calibration uses).
#if defined(TOUCH_XPT2046)
static constexpr int CAL_M = 24;  // cross distance from the edges
static lv_obj_t *calBox = nullptr, *calCross, *calText;
static int calStep = 0, calState = 0;  // calState: 0 running, 1 done, -1 cancelled
static uint32_t calAt = 0;
static float calSumX, calSumY, calRawX[4], calRawY[4];
static int calN;
static uint16_t calRes[4];

static void calTarget(int i, int32_t& x, int32_t& y) {
  x = (i == 1 || i == 2) ? SCREEN_W - 1 - CAL_M : CAL_M;
  y = (i >= 2) ? SCREEN_H - 1 - CAL_M : CAL_M;
}

static void calShow() {
  int32_t x, y;
  calTarget(calStep, x, y);
  lv_obj_set_pos(calCross, x - 15, y - 15);
  char b[64];
  snprintf(b, sizeof(b), "Touch calibration\nTap the centre of the cross (%d / 4)", calStep + 1);
  lv_label_set_text(calText, b);
}

static void calClose() {
  if (calBox) lv_obj_delete_async(calBox);
  calBox = nullptr;
}

static void calEvent(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  int32_t rx, ry;
  touchLastRaw(&rx, &ry);
  if (c == LV_EVENT_PRESSED) {
    calSumX = calSumY = 0;
    calN = 0;
  } else if (c == LV_EVENT_PRESSING && rx >= 0) {
    calSumX += rx;
    calSumY += ry;
    ++calN;
  } else if (c == LV_EVENT_RELEASED && calN > 0) {
    calRawX[calStep] = calSumX / calN;
    calRawY[calStep] = calSumY / calN;
    calAt = millis();
    if (++calStep < 4) return calShow();
    // unrotated screen position of the crosses
    float u[4], v[4];
    for (int i = 0; i < 4; ++i) {
      int32_t x, y;
      calTarget(i, x, y);
      u[i] = rotated ? SCREEN_W - 1 - x : x;
      v[i] = rotated ? SCREEN_H - 1 - y : y;
    }
    // x from the crosses 0/3 (one side) and 1/2, y from 0/1 and 2/3
    const float xa = (calRawX[0] + calRawX[3]) / 2, xb = (calRawX[1] + calRawX[2]) / 2;
    const float ya = (calRawY[0] + calRawY[1]) / 2, yb = (calRawY[2] + calRawY[3]) / 2;
    const float ua = u[0], ub = u[1], va = v[0], vb = v[2];
    if (fabsf(xb - xa) < 300 || fabsf(yb - ya) < 300) {  // crosses not hit / axes swapped
      calState = -1;
      return calClose();
    }
    const float kx = (xb - xa) / (ub - ua), ky = (yb - ya) / (vb - va);
    auto clamp = [](float r) { return (uint16_t)fmaxf(0, fminf(4095, roundf(r))); };
    calRes[0] = clamp(xa - kx * ua);
    calRes[1] = clamp(xa + kx * (SCREEN_W - 1 - ua));
    calRes[2] = clamp(ya - ky * va);
    calRes[3] = clamp(ya + ky * (SCREEN_H - 1 - va));
    calState = 1;
    calClose();
  }
}

bool uiTouchCalStart() {
  if (!disp) return false;
  calClose();
  tModalClose();
  calBox = lv_obj_create(lv_layer_sys());
  lv_obj_remove_style_all(calBox);
  lv_obj_add_style(calBox, &S_.scr, 0);
  lv_obj_set_size(calBox, SCREEN_W, SCREEN_H);
  lv_obj_set_clickable(calBox, true);
  lv_obj_set_scrollable(calBox, false);
  lv_obj_add_event_cb(calBox, calEvent, LV_EVENT_ALL, nullptr);
  calText = tLabel(calBox, &tfont_16b, "");
  lv_obj_set_style_text_align(calText, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(calText);
  calCross = lv_obj_create(calBox);
  lv_obj_remove_style_all(calCross);
  lv_obj_set_size(calCross, 31, 31);
  lv_obj_set_clickable(calCross, false);
  for (int i = 0; i < 2; ++i) {  // the two bars of the cross
    lv_obj_t* bar = lv_obj_create(calCross);
    lv_obj_remove_style_all(bar);
    lv_obj_add_style(bar, &S_.bar[0], 0);
    lv_obj_set_size(bar, i ? 3 : 31, i ? 31 : 3);
    lv_obj_center(bar);
    lv_obj_set_clickable(bar, false);
  }
  calStep = 0;
  calState = 0;
  calAt = millis();
  calShow();
  return true;
}

int uiTouchCalPoll(int& step, uint16_t& minX, uint16_t& maxX, uint16_t& minY, uint16_t& maxY) {
  if (calState == 0 && calBox && millis() - calAt > 60000) {  // nobody taps: give up
    calState = -1;
    calClose();
  }
  step = calStep + 1;
  if (calState == 1) {
    minX = calRes[0];
    maxX = calRes[1];
    minY = calRes[2];
    maxY = calRes[3];
  }
  return calState;
}

void uiTouchCalCancel() {
  calState = -1;
  calClose();
}
#else
bool uiTouchCalStart() { return false; }
int uiTouchCalPoll(int& step, uint16_t&, uint16_t&, uint16_t&, uint16_t&) {
  step = 0;
  return -1;
}
void uiTouchCalCancel() {}
#endif

