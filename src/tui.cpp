// Touch UI (step 6, S3): display + touch glue, screen switching, the periodic update.
// Screens: tui_main.cpp (main), tui_menu.cpp (menu, log, display, network, system), tui_chart.cpp,
// building blocks and popups: tui_widgets.cpp, colours and styles: tui_theme.cpp.
#include "tui.h"
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
  cur = s;
  tModalClose();
  if (s == SCR_DISPLAY) displayShow();
  if (s == SCR_NETWORK) networkShow();
  if (s == SCR_SYSTEM) systemShow();
  if (s == SCR_PROGRAMS) progListShow();
  if (s == SCR_CHANNELS) chanShow();
  lv_screen_load(screens[s]);
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
    Serial.println("[ui] display could not be set up - no touch UI");
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

// old palette API (ui.h): the palettes are gone in TempCtl Duo
const char* uiPaletteNames() { return "Default"; }
int uiPaletteCount() { return 1; }
#endif
