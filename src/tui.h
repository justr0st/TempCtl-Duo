// TempCtl Duo touch UI: screens written in LVGL C code, one code for both boards
// (S3 480x320, CYD 320x240 with TUI_SMALL).
// Internal header of the tui_*.cpp files.
#pragma once
#include "config.h"
#include <lvgl.h>
#include "app_state.h"

// ------------------------------------------------------------ sizes
// Two screen sets from one code: 480x320 (S3) and the compact 320x240 (CYD, TUI_SMALL)
#if SCREEN_W < 400
#define TUI_SMALL 1
#else
#define TUI_SMALL 0
#endif
constexpr int T_HDR = TUI_SMALL ? 36 : 44;   // page header height
constexpr int T_BTN = TUI_SMALL ? 32 : 36;   // standard button height
constexpr int T_DLG_W = TUI_SMALL ? SCREEN_W - 12 : 380;  // dialog / list width

// ------------------------------------------------------------ theme (tui_theme.cpp)
// Same palette as the web page: fixed neutrals (dark / light), accent and channel colours derived
// from the accent (split-complementary, contrast rule, spec 6).
enum TColor : uint8_t {
  C_BG, C_PANEL, C_SUNK, C_LINE, C_TEXT, C_MUTED, C_ACC, C_ACC_INK, C_AMBER, C_AMBER_BG, C_DANGER, C_OK, C_MANUAL,
  C_PV0, C_SP0, C_PV1, C_SP1, C_COUNT
};
lv_color_t tc(TColor c);
lv_color_t tcPv(int ch);
lv_color_t tcSp(int ch);
void themeApply(uint32_t accentRgb, bool dark);  // recolours every style, refreshes all screens
bool themeDark();
uint32_t themeGen();                              // changes with every themeApply()

extern "C" {
extern const lv_image_dsc_t tui_icon_programs, tui_icon_channels, tui_icon_system, tui_icon_log;  // tui_icons.c
extern const lv_font_t tfont_12, tfont_14, tfont_16b, tfont_20c, tfont_28c, tfont_40c;
}

// Named styles (colours come only from these; themeApply() recolours them)
struct TStyles {
  lv_style_t scr, panel, sunk, btn, btnPressed, btnPri, btnDanger, btnDis, btnChecked, muted, small, title, danger,
      amber, ok, acc, ring, cell, btnOn[2], tileOn[2], pv[2], sp[2], barBg, bar[2], overlay, dialog, listRow, sep;
};
extern TStyles S_;

// ------------------------------------------------------------ widgets (tui_widgets.cpp)
lv_obj_t* tLabel(lv_obj_t* parent, const lv_font_t* f, const char* text = "");
lv_obj_t* tButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user = nullptr, int w = LV_SIZE_CONTENT,
                  int h = 36);
lv_obj_t* tIconButton(lv_obj_t* parent, const char* symbol, lv_event_cb_t cb, void* user = nullptr, int w = 40, int h = 36);
lv_obj_t* tValueButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user, int w, int h);  // one label
void tButtonText(lv_obj_t* btn, const char* text);
void tSetEnabled(lv_obj_t* o, bool en);
void tSetHidden(lv_obj_t* o, bool hidden);
void tSetText(lv_obj_t* label, const char* t);  // only when changed (no needless redraw)
void tOneLine(lv_obj_t* label);                 // one line, "..." at the end (width set before)
lv_obj_t* tPanel(lv_obj_t* parent);
lv_obj_t* tRow(lv_obj_t* parent, int gap = 6);  // transparent flex row, full width
lv_obj_t* tCol(lv_obj_t* parent, int gap = 4);  // transparent flex column
lv_obj_t* tSwitch(lv_obj_t* parent, lv_event_cb_t cb, void* user = nullptr);
lv_style_t* tStyleGrow();       // shared styles (no per-object local style): flex_grow 1,
lv_style_t* tStyleFullWidth();  // width 100 %
void tSetChecked(lv_obj_t* o, bool on);
// A page with a header (back button, title, optional right-side area) and a scrolling body
struct TPage {
  lv_obj_t* scr;
  lv_obj_t* header;
  lv_obj_t* title;
  lv_obj_t* body;
};
TPage tPageCreate(const char* title, lv_event_cb_t back);

// Popups (on the top layer; one modal at a time, closed with tModalClose)
typedef void (*TNumCb)(float v, int arg);
typedef void (*TTextCb)(const char* t, int arg);
typedef void (*TChoiceCb)(int idx, int arg);
typedef void (*TAnswerCb)(int button, int arg);  // 1..3, 0 = closed
// Number entry: unit "°C", "s" ...; lo..hi checked on OK (message, stays open)
void tNumPad(const char* title, float cur, float lo, float hi, int decimals, const char* unit, TNumCb cb, int arg);
// Text entry with the on-screen keyboard; password: hidden with an eye button; numeric: digits + '.'
void tKeyboard(const char* title, const char* cur, uint16_t maxLen, bool password, bool numeric, TTextCb cb, int arg);
// Message box: up to 3 buttons ("" = none); the last one is the primary (accent) one unless danger
lv_obj_t* tDialog(const char* title, const char* text, const char* b1, const char* b2, const char* b3, TAnswerCb cb,
                  int arg, bool dangerPrimary = false);
// List of choices ('\n'-separated, max 32); sel = marked row (-1 none)
void tChoice(const char* title, const char* items, int sel, TChoiceCb cb, int arg);
void tModalClose();
bool tModalOpen();
uint32_t tModalId();  // the open popup (0 = none), to close one that is no longer needed
// Short message at the bottom (4 s); bad = error colour
void tToast(const char* text, bool bad = false);
// main screen: lower edge of the chart (messages sit there, the tile controls stay free)
constexpr int TOAST_MAIN_BOTTOM = TUI_SMALL ? 30 + 76 - 2 : 46 + 132 - 2;

// now as a time stamp where 0 means "none" (not "lv_tick_get() | 1": that can be 1 ms ahead, and
// lv_tick_elaps() of a time ahead is huge)
inline uint32_t tTickNow() {
  const uint32_t t = lv_tick_get();
  return t ? t : 1;
}

// ------------------------------------------------------------ formatting
void fmtTime(float sec, char* out, size_t n);  // m:ss or h:mm:ss
void fmtTemp(float v, char* out, size_t n, bool unit = true);  // "123.4 °C", "—" for NaN

// ------------------------------------------------------------ screens
enum TScreen : uint8_t { SCR_MAIN, SCR_MENU, SCR_LOG, SCR_DISPLAY, SCR_NETWORK, SCR_SYSTEM, SCR_PROGRAMS, SCR_PROGEDIT, SCR_CHANNELS, SCR_COUNT };
void tShow(TScreen s);
TScreen tCurrent();
// per screen: create (once), update (every 250 ms while shown), onShow
lv_obj_t* mainCreate();
void mainUpdate();
void mainEvents();  // overlays and messages, on every screen
lv_obj_t* menuCreate();
lv_obj_t* logCreate();
void logUpdate();
lv_obj_t* displayCreate();
void displayShow();
void displayUpdate();
lv_obj_t* networkCreate();
void networkShow();
void networkUpdate();
lv_obj_t* systemCreate();
void systemShow();
void tuiOtaPoll();  // firmware update overlay (uiLoop, every 250 ms)
void systemUpdate();
lv_obj_t* progListCreate();
void progListShow();
lv_obj_t* progEditCreate();
void progEditUpdate();
void progEditOpen(int slot);
void progEditShow();
void progEditRelease();  // frees the draft and the undo steps (320x240: when the editor is left)
lv_obj_t* chanCreate();
void chanShow();
void chanUpdate();

// chart (tui_chart.cpp): custom-drawn, both channels
lv_obj_t* chartCreate(lv_obj_t* parent);
void chartUpdate(lv_obj_t* chart, bool live);  // reads chartSnapshot()

// ------------------------------------------------------------ helpers shared by the screens
void settingsCopy(Settings& out);             // gSettings under the lock
void liveCopy(LiveData& out);                 // gLive under the lock
const LiveData& tuiLive();                    // the UI tick's snapshot of gLive
const Settings& tuiSettings();                // ... and of gSettings
void tApplyDisplaySettings();                 // brightness, rotation, theme from gSettings
