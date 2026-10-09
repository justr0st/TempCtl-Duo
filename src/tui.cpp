// Touch UI (step 6, S3): display + touch glue, screen switching, the periodic update.
// Screens: tui_main.cpp (main), tui_menu.cpp (menu, log, display, network, system), tui_chart.cpp,
// building blocks and popups: tui_widgets.cpp, colours and styles: tui_theme.cpp.
#include "tui.h"
#include "console.h"
#ifdef UI_V2
#include "lcd.h"
#include "touch.h"
#include "ui.h"

void tuiBackground();

static lv_display_t* disp = nullptr;
static uint8_t appliedBrightness = 0;
static bool rotated = false;
static uint32_t appliedAccent = 0xFFFFFFFF;
static int appliedDark = -1;

// The touch drivers report screen coordinates; LVGL rotates pointer input by the display rotation
// itself, so it gets the point in the panel's native orientation (see ui.cpp for the CYD).
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

static void touchCb(lv_indev_t*, lv_indev_data_t* data) {
  int32_t x, y;
  if (touchRead(&x, &y)) {
    touchToNative(x, y);
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
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
static void uiTimer(lv_timer_t*) {
  static LiveData L;  // static: large
  static Settings S;
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
  mainEvents(L, S);
  switch (cur) {
    case SCR_MAIN: mainUpdate(L, S); break;
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

void uiLoop() {
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

// old palette API (ui.h): the palettes are gone in TempCtl Duo
const char* uiPaletteNames() { return "Default"; }
int uiPaletteCount() { return 1; }
#endif
