// LVGL 9.4 user interface for the 320x240 CYD screen.
//
// The screen layout is an LVGL Pro project in ui/ (XML). It is embedded into
// the firmware at build time (scripts/embed_ui_xml.py -> ui_xml_data.h) and
// created with LVGL's XML loader. This file only contains the behaviour:
// it looks widgets up by their `name`, attaches the event handlers (UI_EVENTS)
// and updates them. Modal editors, keyboards and message boxes are created
// here in code.
#include "ui.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include <math.h>
#include "app_state.h"
#include "controller.h"
#include "net.h"
#include "ui_xml_data.h"

// ------------------------------------------------------------ drivers
static TFT_eSPI tft;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen ts(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
alignas(4) static uint8_t drawBuf[SCREEN_W * 24 * 2];  // RGB565, 24 lines
static lv_display_t* disp = nullptr;
static uint16_t calMinX, calMaxX, calMinY, calMaxY;
static uint8_t appliedBrightness = 0;
static bool rotated = false;

static void flushCb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.pushColors((uint16_t*)px, w * h, true);  // swap bytes for the SPI display
  tft.endWrite();
  lv_display_flush_ready(d);
}

static void touchCb(lv_indev_t*, lv_indev_data_t* data) {
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();
    long x = constrain(map(p.x, calMinX, calMaxX, 0, SCREEN_W - 1), 0, SCREEN_W - 1);
    long y = constrain(map(p.y, calMinY, calMaxY, 0, SCREEN_H - 1), 0, SCREEN_H - 1);
    if (rotated) {  // display turned by 180 degrees: mirror both axes
      x = SCREEN_W - 1 - x;
      y = SCREEN_H - 1 - y;
    }
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
  ledcWrite(0, b);
  appliedBrightness = b;
}

static void applyRotation(bool r) {
  rotated = r;
  tft.setRotation(r ? 3 : 1);  // 1 = landscape, 3 = landscape upside down
  lv_obj_invalidate(lv_screen_active());
  lv_obj_invalidate(lv_layer_top());
}

// ------------------------------------------------------------ helpers
static void fmtDur(float sec, char* out, size_t n) {
  if (isnan(sec) || sec < 0) sec = 0;
  const uint32_t t = (uint32_t)(sec + 0.5f);
  const uint32_t h = t / 3600, m = (t / 60) % 60, s = t % 60;
  if (h) snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
  else snprintf(out, n, "%lu:%02lu", (unsigned long)m, (unsigned long)s);
}

static void fmtTemp(bool valid, float v, char* out, size_t n) {
  if (valid && !isnan(v)) snprintf(out, n, "%.1f", v);
  else snprintf(out, n, "---.-");
}

// Step time as shown in the program table: whole minutes as "N min", else "N s".
static void fmtStepTime(uint32_t sec, char* out, size_t n) {
  if (sec == 0) snprintf(out, n, "0");
  else if (sec % 60 == 0) snprintf(out, n, "%lu min", (unsigned long)(sec / 60));
  else snprintf(out, n, "%lu s", (unsigned long)sec);
}

static lv_obj_t* mkLabel(lv_obj_t* parent, const lv_font_t* font, int x, int y, const char* txt) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, txt);
  return l;
}

static lv_obj_t* mkButton(lv_obj_t* parent, const char* txt, int x, int y, int w, int h, lv_event_cb_t cb,
                          void* ud = nullptr) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_pad_all(b, 2, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

static void setEnabled(lv_obj_t* o, bool en) {
  if (!o) return;
  if (en) lv_obj_remove_state(o, LV_STATE_DISABLED);
  else lv_obj_add_state(o, LV_STATE_DISABLED);
}

static void setChecked(lv_obj_t* o, bool on) {
  if (!o) return;
  if (on) lv_obj_add_state(o, LV_STATE_CHECKED);
  else lv_obj_remove_state(o, LV_STATE_CHECKED);
}

static void setText(lv_obj_t* label, const char* t) {
  if (label && strcmp(lv_label_get_text(label), t) != 0) lv_label_set_text(label, t);
}

static lv_obj_t* evTarget(lv_event_t* e) { return lv_event_get_target_obj(e); }

// Message box with optional footer buttons ("" terminated list); the text
// label is returned through textOut.
static lv_obj_t* msgBox(const char* title, const char* text, const char* const* buttons = nullptr,
                        lv_event_cb_t cb = nullptr, bool closeBtn = true, lv_obj_t** textOut = nullptr) {
  lv_obj_t* m = lv_msgbox_create(nullptr);  // modal, on the top layer
  lv_msgbox_add_title(m, title);
  lv_obj_t* t = lv_msgbox_add_text(m, text);
  if (textOut) *textOut = t;
  if (closeBtn) lv_msgbox_add_close_button(m);
  for (int i = 0; buttons && buttons[i] && buttons[i][0]; ++i) {
    lv_obj_t* b = lv_msgbox_add_footer_button(m, buttons[i]);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, m);
  }
  lv_obj_set_width(m, 290);
  lv_obj_center(m);
  return m;
}

static void toast(const char* title, const char* text) { msgBox(title, text); }

// text of the footer button that sent the event
static const char* footerText(lv_event_t* e) {
  lv_obj_t* l = lv_obj_get_child(evTarget(e), 0);
  return l ? lv_label_get_text(l) : "";
}

static bool programActive(RunState s) { return s == RunState::Running || s == RunState::Paused; }

// "1 Name\n2 Name ..." for the program dropdowns
static void buildProgramOptions(char* out, size_t n) {
  out[0] = 0;
  Guard g;
  for (int i = 0; i < MAX_PROGRAMS; ++i) {
    char line[48];
    snprintf(line, sizeof(line), "%s%d %s%s", i ? "\n" : "", i + 1, gPrograms[i].name,
             gPrograms[i].count ? "" : " (empty)");
    strlcat(out, line, n);
  }
}

static void syncDropdown(lv_obj_t* dd, char* cache, size_t n, int sel) {
  if (!dd || lv_dropdown_is_open(dd)) return;
  char opts[MAX_PROGRAMS * 40];
  buildProgramOptions(opts, sizeof(opts));
  if (strcmp(opts, cache) != 0) {
    strlcpy(cache, opts, n);
    lv_dropdown_set_options(dd, opts);
  }
  if ((int)lv_dropdown_get_selected(dd) != sel) lv_dropdown_set_selected(dd, sel);
}

// ------------------------------------------------------------ modal editors
// Only one modal editor (number, time or text) is open at a time.
static lv_obj_t* modal = nullptr;
static lv_obj_t* modalTa = nullptr;
static lv_obj_t* modalUnits = nullptr;  // time editor: "min" / "s"

static void modalClose() {
  if (modal) lv_obj_delete_async(modal);
  modal = nullptr;
  modalTa = nullptr;
  modalUnits = nullptr;
}

static lv_obj_t* modalCreate(const char* title) {
  modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(modal, 0, 0);
  lv_obj_set_style_radius(modal, 0, 0);
  lv_obj_set_style_pad_all(modal, 4, 0);
  lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = mkLabel(modal, &lv_font_montserrat_14, 0, 0, title);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(l, SCREEN_W - 8);
  return modal;
}

// --- number / time editor
typedef void (*NumApply)(float v, int a);
static NumApply numApply = nullptr;
static int numArg = 0;

static void numKbEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    const char* t = lv_textarea_get_text(modalTa);
    if (t && *t && numApply) {
      float v = (float)atof(t);
      if (modalUnits && lv_buttonmatrix_has_button_ctrl(modalUnits, 0, LV_BUTTONMATRIX_CTRL_CHECKED)) v *= 60.0f;
      numApply(v, numArg);
    }
    modalClose();
  } else if (code == LV_EVENT_CANCEL) {
    modalClose();
  }
}

// timeSec: `cur` is in seconds and the user picks minutes or seconds.
static void openNumEditor(const char* title, float cur, NumApply cb, int arg, bool timeSec = false) {
  if (modal) return;
  numApply = cb;
  numArg = arg;
  modalCreate(title);
  modalTa = lv_textarea_create(modal);
  lv_textarea_set_one_line(modalTa, true);
  lv_textarea_set_accepted_chars(modalTa, timeSec ? "0123456789." : "0123456789.-");
  lv_textarea_set_max_length(modalTa, 8);
  lv_obj_set_width(modalTa, 160);
  lv_obj_set_pos(modalTa, 0, 24);
  char buf[16];
  if (timeSec) {
    const uint32_t s = (uint32_t)lroundf(cur);
    const bool mins = (s % 60 == 0);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(mins ? s / 60 : s));
    static const char* units[] = {"min", "s", ""};
    modalUnits = lv_buttonmatrix_create(modal);
    lv_buttonmatrix_set_map(modalUnits, units);
    lv_buttonmatrix_set_button_ctrl_all(modalUnits, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(modalUnits, true);
    lv_buttonmatrix_set_button_ctrl(modalUnits, mins ? 0 : 1, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_size(modalUnits, 136, 44);
    lv_obj_set_pos(modalUnits, 172, 20);
  } else {
    snprintf(buf, sizeof(buf), "%.1f", cur);
    const size_t l = strlen(buf);  // drop a trailing ".0" for integer values
    if (l > 2 && strcmp(buf + l - 2, ".0") == 0) buf[l - 2] = 0;
  }
  lv_textarea_set_text(modalTa, buf);
  lv_obj_t* kb = lv_keyboard_create(modal);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  lv_keyboard_set_textarea(kb, modalTa);
  lv_obj_set_size(kb, SCREEN_W - 8, 150);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(kb, numKbEvent, LV_EVENT_ALL, nullptr);
}

// --- text editor (on-screen keyboard)
typedef void (*TextApply)(const char* t, int a);
static TextApply textApply = nullptr;
static int textArg = 0;

static void textKbEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    if (textApply) textApply(lv_textarea_get_text(modalTa), textArg);
    modalClose();
  } else if (code == LV_EVENT_CANCEL) {
    modalClose();
  }
}

static void openTextEditor(const char* title, const char* cur, uint16_t maxLen, bool password, TextApply cb,
                           int arg) {
  if (modal) return;
  textApply = cb;
  textArg = arg;
  modalCreate(title);
  modalTa = lv_textarea_create(modal);
  lv_textarea_set_one_line(modalTa, true);
  lv_textarea_set_max_length(modalTa, maxLen);
  lv_textarea_set_password_mode(modalTa, password);
  lv_obj_set_width(modalTa, SCREEN_W - 8);
  lv_obj_set_pos(modalTa, 0, 20);
  lv_textarea_set_text(modalTa, cur);
  lv_obj_t* kb = lv_keyboard_create(modal);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(kb, modalTa);
  lv_obj_set_size(kb, SCREEN_W - 8, 166);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(kb, textKbEvent, LV_EVENT_ALL, nullptr);
}

// ------------------------------------------------------------ widgets (from the XML)
static lv_obj_t* screen = nullptr;
static lv_obj_t* tabview = nullptr;
enum { TAB_MAIN = 0, TAB_PROGRAM, TAB_CHART, TAB_SETUP };
// Main tab
static lv_obj_t *lblPv, *lblSp, *lblTarget, *lblState, *lblPhase, *lblTotal, *lblOut, *lblMode, *lblMsg, *barProg,
    *ledLink, *lblManSp, *swManual, *ddMainProg, *btnStart, *btnPause, *btnStop, *btnSkip;
static char mainProgOpts[MAX_PROGRAMS * 40];
static uint32_t manualTouchedMs = 0;  // ignore live state briefly after the user toggled
// Program tab
static lv_obj_t *ddEditProg, *table, *lblProgInfo, *lblProgUnsaved, *btnAdd, *btnDel, *btnUp, *btnDown;
static char editProgOpts[MAX_PROGRAMS * 40];
static Program uiProg;          // program being edited
static int editIdx = 0;         // which stored program uiProg belongs to
static uint32_t loadedGen = 0;  // gProgGen when uiProg was loaded
static int selRow = -1;         // selected step (-1 = none)
static bool progDirty = false;
static int pendingSwitch = -1;  // program to switch to after the save/discard question
// Chart tab
static lv_obj_t *chart, *chartYScale, *chartXScale, *lblChartInfo;
static lv_chart_series_t *serPv, *serSp, *serPlan;
// Setup tab
static lv_obj_t *swRotate, *sldBright, *lblSsid, *lblPass, *lblApPass, *lblSysInfo;
static lv_obj_t *ddProfile, *lblSlave, *ddBaud, *ddFormat, *ddRamp, *swSoak, *lblBand, *swLock, *ddEnd, *lblCtlInfo;
static bool setupLoading = false;
static char wifiSsid[33], wifiPass[65], wifiApPass[33];
static bool scanWaiting = false;

static lv_obj_t* find(const char* name) {
  lv_obj_t* o = lv_obj_find_by_name(screen, name);
  if (!o) Serial.printf("[ui] widget '%s' not found in the XML screen\n", name);
  return o;
}

static int activeTab() { return tabview ? (int)lv_tabview_get_tab_active(tabview) : 0; }

// ------------------------------------------------------------ Main tab
extern "C" void ui_on_start(lv_event_t*) {
  RunState st;
  {
    Guard g;
    st = gLive.state;
  }
  if (st == RunState::Paused) sendCmd(CMD_RESUME);
  else sendCmd(CMD_START, ddMainProg ? (int)lv_dropdown_get_selected(ddMainProg) : -1);
}
extern "C" void ui_on_pause(lv_event_t*) { sendCmd(CMD_PAUSE); }
extern "C" void ui_on_skip(lv_event_t*) { sendCmd(CMD_SKIP); }

static void onStopAnswer(lv_event_t* e) {
  lv_obj_t* m = (lv_obj_t*)lv_event_get_user_data(e);
  if (strcmp(footerText(e), "Stop") == 0) sendCmd(CMD_STOP);
  lv_msgbox_close_async(m);
}

extern "C" void ui_on_stop(lv_event_t*) {
  static const char* btns[] = {"Stop", "Cancel", ""};
  msgBox("Stop program?", "The end action from Setup > Controller is applied.", btns, onStopAnswer, false);
}

extern "C" void ui_on_main_prog(lv_event_t*) { selectProgram(lv_dropdown_get_selected(ddMainProg)); }

static void applyManualSp(float v, int) {
  setManualSp(v);
  char b[16];
  float cur;
  {
    Guard g;
    cur = gSettings.manualSp;
  }
  fmtTemp(true, cur, b, sizeof(b));
  setText(lblManSp, b);
}

extern "C" void ui_on_manual_sp(lv_event_t*) {
  float cur;
  char title[64];
  {
    Guard g;
    cur = gSettings.manualSp;
    snprintf(title, sizeof(title), "Manual setpoint (C), %.0f..%.0f", gSettings.spMin, gSettings.spMax);
  }
  openNumEditor(title, cur, applyManualSp, 0);
}

extern "C" void ui_on_manual_switch(lv_event_t*) {
  manualTouchedMs = millis();
  sendCmd(lv_obj_has_state(swManual, LV_STATE_CHECKED) ? CMD_MANUAL_ON : CMD_MANUAL_OFF);
}

static void bindMain() {
  lblPv = find("main_pv");
  lblSp = find("main_sp");
  lblTarget = find("main_target");
  ledLink = find("main_link");
  lblState = find("main_state");
  lblPhase = find("main_phase");
  lblTotal = find("main_total");
  lblOut = find("main_out");
  lblMode = find("main_mode");
  lblManSp = find("main_man_sp_text");
  swManual = find("main_man_sw");
  ddMainProg = find("main_prog");
  btnStart = find("main_start");
  btnPause = find("main_pause");
  btnStop = find("main_stop");
  btnSkip = find("main_skip");
  barProg = find("main_progress");
  lblMsg = find("main_msg");
  if (barProg) lv_bar_set_range(barProg, 0, 1000);
}

static void updateMain(const LiveData& L, const Settings& S) {
  char a[24], buf[96];
  if (L.pvValid) fmtTemp(true, L.pv, a, sizeof(a));
  else if (L.commOk && L.pvFlag != VAL_OK) strlcpy(a, L.pvFlag == VAL_SENSOR_FAULT ? "Sensor" : "---.-", sizeof(a));
  else strlcpy(a, "---.-", sizeof(a));
  setText(lblPv, a);

  fmtTemp(L.spEffValid, L.spEff, a, sizeof(a));
  snprintf(buf, sizeof(buf), "SP %s", a);
  setText(lblSp, buf);

  const bool active = programActive(L.state);
  if (active) {
    fmtTemp(true, L.target, a, sizeof(a));
    snprintf(buf, sizeof(buf), "Step target %s", a);
    setText(lblTarget, buf);
  } else {
    setText(lblTarget, S.reg.sp == REG_UNSET ? "SP register not set!" : "");
  }

  if (ledLink)
    lv_obj_set_style_bg_color(ledLink, lv_palette_main(L.commOk ? LV_PALETTE_GREEN : LV_PALETTE_RED), 0);

  switch (L.state) {
    case RunState::Running:
    case RunState::Paused:
      snprintf(buf, sizeof(buf), "%s %d/%d", L.state == RunState::Paused ? "Paused" : "Step", L.step + 1,
               L.stepCount);
      setText(lblState, buf);
      break;
    default:
      if (L.manualOn) setText(lblState, "Manual");
      else if (L.state == RunState::Finished) setText(lblState, "Finished");
      else if (L.state == RunState::Aborted) setText(lblState, "Stopped");
      else setText(lblState, "Idle");
  }
  if (active) {
    fmtDur(L.phaseDur - L.phaseEl, a, sizeof(a));
    if (L.soakWait) setText(lblPhase, "Hold: waiting for PV");
    else {
      snprintf(buf, sizeof(buf), "%s  %s left", L.phase == Phase::Ramp ? "Ramp" : "Hold", a);
      setText(lblPhase, buf);
    }
    fmtDur(L.total - L.elapsed, a, sizeof(a));
    snprintf(buf, sizeof(buf), "%s: %s left", L.runName, a);
    setText(lblTotal, buf);
    if (barProg) lv_bar_set_value(barProg, L.total > 0 ? (int)(1000 * L.elapsed / L.total) : 0, LV_ANIM_OFF);
  } else {
    setText(lblPhase, L.manualOn ? (L.manualSynced ? "setpoint written" : "writing...") : "");
    {
      Guard g;
      const Program& sel = gPrograms[S.selProgram < MAX_PROGRAMS ? S.selProgram : 0];
      fmtDur(ProgramEngine::plannedTotal(sel), a, sizeof(a));
      snprintf(buf, sizeof(buf), "%s, %s", sel.name, a);
    }
    setText(lblTotal, buf);
    if (barProg) lv_bar_set_value(barProg, L.state == RunState::Finished ? 1000 : 0, LV_ANIM_OFF);
  }
  if (L.outValid) {
    snprintf(buf, sizeof(buf), "Out %.0f%%", L.out);
    setText(lblOut, buf);
  } else {
    setText(lblOut, "");
  }
  setText(lblMode, (L.statusValid && S.profile == PROFILE_PMA_KS4X) ? pmaModeText(L.status) : "");

  if (!L.commOk) snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", L.lastErr);
  else if (!L.spSynced && (active || L.manualOn)) snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", L.lastErr);
  else strlcpy(buf, L.runMsg, sizeof(buf));
  setText(lblMsg, buf);

  // manual setpoint widgets
  if (!modal) {
    fmtTemp(true, S.manualSp, a, sizeof(a));
    setText(lblManSp, a);
  }
  if (millis() - manualTouchedMs > 1500) setChecked(swManual, L.manualOn);
  setEnabled(swManual, !active);

  // program controls
  syncDropdown(ddMainProg, mainProgOpts, sizeof(mainProgOpts), S.selProgram);
  setEnabled(ddMainProg, !active);
  setEnabled(btnStart, L.state != RunState::Running);
  setEnabled(btnPause, L.state == RunState::Running);
  setEnabled(btnStop, active);
  setEnabled(btnSkip, active);
}

// ------------------------------------------------------------ Program tab
enum { COL_NUM = 0, COL_TARGET, COL_RAMP, COL_HOLD, COL_CNT };

static void refreshTable() {
  if (!table) return;
  const int rows = uiProg.count ? uiProg.count : 1;
  lv_table_set_row_count(table, rows);
  char buf[16];
  if (!uiProg.count) {
    lv_table_set_cell_value(table, 0, COL_NUM, "");
    lv_table_set_cell_value(table, 0, COL_TARGET, "no steps");
    lv_table_set_cell_value(table, 0, COL_RAMP, "tap +");
    lv_table_set_cell_value(table, 0, COL_HOLD, "");
  }
  for (int i = 0; i < uiProg.count; ++i) {
    const Step& s = uiProg.s[i];
    snprintf(buf, sizeof(buf), "%d", i + 1);
    lv_table_set_cell_value(table, i, COL_NUM, buf);
    snprintf(buf, sizeof(buf), "%.1f", s.target);
    lv_table_set_cell_value(table, i, COL_TARGET, buf);
    fmtStepTime(s.rampSec, buf, sizeof(buf));
    lv_table_set_cell_value(table, i, COL_RAMP, buf);
    fmtStepTime(s.holdSec, buf, sizeof(buf));
    lv_table_set_cell_value(table, i, COL_HOLD, buf);
  }
  if (selRow >= uiProg.count) selRow = uiProg.count - 1;
  char d[16], info[64];
  fmtDur(ProgramEngine::plannedTotal(uiProg), d, sizeof(d));
  snprintf(info, sizeof(info), "%d/%d steps  total %s", uiProg.count, MAX_STEPS, d);
  setText(lblProgInfo, info);
  setText(lblProgUnsaved, progDirty ? "unsaved" : "");
  setEnabled(btnAdd, uiProg.count < MAX_STEPS);
  setEnabled(btnDel, selRow >= 0);
  setEnabled(btnUp, selRow > 0);
  setEnabled(btnDown, selRow >= 0 && selRow < uiProg.count - 1);
  lv_obj_invalidate(table);
}

static void scrollToRow(int row) {
  if (row < 0 || !table) return;
  // rows have the same height; estimate from the table's content height
  const int32_t total = lv_obj_get_self_height(table);
  const int rows = uiProg.count ? uiProg.count : 1;
  const int32_t rh = total / rows;
  const int32_t y = rh * row, vis = lv_obj_get_content_height(table);
  const int32_t top = lv_obj_get_scroll_y(table);
  if (y < top) lv_obj_scroll_to_y(table, y, LV_ANIM_OFF);
  else if (y + rh > top + vis) lv_obj_scroll_to_y(table, y + rh - vis, LV_ANIM_OFF);
}

static void loadProgram(int idx) {
  if (idx < 0 || idx >= MAX_PROGRAMS) idx = 0;
  {
    Guard g;
    uiProg = gPrograms[idx];
    loadedGen = gProgGen;
  }
  editIdx = idx;
  progDirty = false;
  selRow = uiProg.count ? 0 : -1;
}

static void markDirty() {
  progDirty = true;
  refreshTable();
}

static void applyCell(float v, int arg) {
  const int step = arg / 10, col = arg % 10;
  if (step < 0 || step >= uiProg.count) return;
  Step& s = uiProg.s[step];
  float lo, hi;
  {
    Guard g;
    lo = gSettings.spMin;
    hi = gSettings.spMax;
  }
  const uint32_t sec = (v <= 0) ? 0 : (uint32_t)lroundf(fminf(v, (float)MAX_STEP_SEC));
  if (col == COL_TARGET) s.target = constrain(v, lo, hi);
  if (col == COL_RAMP) s.rampSec = sec;
  if (col == COL_HOLD) s.holdSec = sec;
  markDirty();
}

// highlight the selected row, grey step numbers
static void onTableDraw(lv_event_t* e) {
  lv_draw_task_t* t = lv_event_get_draw_task(e);
  lv_draw_dsc_base_t* base = (lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(t);
  if (!base || base->part != LV_PART_ITEMS) return;
  const int row = (int)base->id1, col = (int)base->id2;
  const lv_draw_task_type_t type = lv_draw_task_get_type(t);
  if (type == LV_DRAW_TASK_TYPE_FILL && row == selRow && uiProg.count) {
    lv_draw_fill_dsc_t* f = lv_draw_task_get_fill_dsc(t);
    if (f) {
      f->color = lv_palette_darken(LV_PALETTE_BLUE, 3);
      f->opa = LV_OPA_COVER;
    }
  } else if (type == LV_DRAW_TASK_TYPE_LABEL && col == COL_NUM) {
    lv_draw_label_dsc_t* l = lv_draw_task_get_label_dsc(t);
    if (l) l->color = lv_palette_main(LV_PALETTE_GREY);
  }
}

extern "C" void ui_on_table(lv_event_t*) {
  uint32_t row, col;
  lv_table_get_selected_cell(table, &row, &col);
  if (row == LV_TABLE_CELL_NONE || (int)row >= uiProg.count) return;
  selRow = row;
  refreshTable();
  if (col == COL_NUM) return;  // the number column only selects the row
  char title[48];
  if (col == COL_TARGET) {
    snprintf(title, sizeof(title), "Step %lu target temperature (C)", (unsigned long)row + 1);
    openNumEditor(title, uiProg.s[row].target, applyCell, row * 10 + col);
  } else if (col == COL_RAMP) {
    snprintf(title, sizeof(title), "Step %lu ramp time (0 = jump)", (unsigned long)row + 1);
    openNumEditor(title, uiProg.s[row].rampSec, applyCell, row * 10 + col, true);
  } else if (col == COL_HOLD) {
    snprintf(title, sizeof(title), "Step %lu hold time", (unsigned long)row + 1);
    openNumEditor(title, uiProg.s[row].holdSec, applyCell, row * 10 + col, true);
  }
}

extern "C" void ui_on_add_step(lv_event_t*) {
  Step st{100, 10 * 60, 10 * 60};
  float lo, hi;
  {
    Guard g;
    lo = gSettings.spMin;
    hi = gSettings.spMax;
  }
  if (selRow >= 0 && selRow < uiProg.count) st = uiProg.s[selRow];
  else if (uiProg.count) st = uiProg.s[uiProg.count - 1];
  st.target = constrain(st.target, lo, hi);
  const int at = (selRow >= 0) ? selRow + 1 : uiProg.count;
  if (programInsertStep(uiProg, at, st)) selRow = at;
  markDirty();
  scrollToRow(selRow);
}

extern "C" void ui_on_del_step(lv_event_t*) {
  if (programRemoveStep(uiProg, selRow)) {
    if (selRow >= uiProg.count) selRow = uiProg.count - 1;
    markDirty();
  }
}

static void moveStep(int dir) {
  if (programMoveStep(uiProg, selRow, selRow + dir)) {
    selRow += dir;
    markDirty();
    scrollToRow(selRow);
  }
}
extern "C" void ui_on_step_up(lv_event_t*) { moveStep(-1); }
extern "C" void ui_on_step_down(lv_event_t*) { moveStep(1); }

static void applyName(const char* t, int) {
  if (!t) return;
  while (*t == ' ') ++t;
  if (!*t) return;
  strlcpy(uiProg.name, t, sizeof(uiProg.name));
  editProgOpts[0] = 0;  // force the dropdown to show the new name
  markDirty();
}

extern "C" void ui_on_rename(lv_event_t*) {
  char title[48];
  snprintf(title, sizeof(title), "Name of program %d", editIdx + 1);
  openTextEditor(title, uiProg.name, PROG_NAME_LEN - 1, false, applyName, 0);
}

static void saveEdited() {
  {
    Guard g;
    gPrograms[editIdx] = uiProg;
  }
  const bool ok = saveProgram(editIdx);
  {
    Guard g;
    uiProg = gPrograms[editIdx];  // sanitized copy
    loadedGen = gProgGen;
  }
  progDirty = false;
  editProgOpts[0] = 0;
  mainProgOpts[0] = 0;
  refreshTable();
  RunState st;
  int8_t running;
  {
    Guard g;
    st = gLive.state;
    running = gLive.runProg;
  }
  if (!ok) toast("Not saved", "Writing the program to flash failed.");
  else if (programActive(st) && running == editIdx) toast("Saved", "Changes apply to the next run.");
}

extern "C" void ui_on_prog_save(lv_event_t*) { saveEdited(); }

extern "C" void ui_on_prog_revert(lv_event_t*) {
  loadProgram(editIdx);
  refreshTable();
}

static void switchToProgram(int idx) {
  selectProgram(idx);
  loadProgram(idx);
  refreshTable();
  if (table) lv_obj_scroll_to_y(table, 0, LV_ANIM_OFF);
}

static void onSwitchAnswer(lv_event_t* e) {
  lv_obj_t* m = (lv_obj_t*)lv_event_get_user_data(e);
  const char* t = footerText(e);
  if (strcmp(t, "Save") == 0) {
    saveEdited();
    switchToProgram(pendingSwitch);
  } else if (strcmp(t, "Discard") == 0) {
    switchToProgram(pendingSwitch);
  } else {
    lv_dropdown_set_selected(ddEditProg, editIdx);
  }
  pendingSwitch = -1;
  lv_msgbox_close_async(m);
}

extern "C" void ui_on_edit_prog(lv_event_t*) {
  const int idx = lv_dropdown_get_selected(ddEditProg);
  if (idx == editIdx) return;
  if (!progDirty) {
    switchToProgram(idx);
    return;
  }
  pendingSwitch = idx;
  static const char* btns[] = {"Save", "Discard", "Cancel", ""};
  char text[64];
  snprintf(text, sizeof(text), "\"%s\" has unsaved changes.", uiProg.name);
  msgBox("Unsaved changes", text, btns, onSwitchAnswer, false);
}

static void bindProgram() {
  ddEditProg = find("prog_select");
  table = find("prog_table");
  lblProgInfo = find("prog_info");
  lblProgUnsaved = find("prog_unsaved");
  btnAdd = find("prog_add");
  btnDel = find("prog_del");
  btnUp = find("prog_up");
  btnDown = find("prog_down");
  if (table) {
    lv_obj_add_flag(table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_add_event_cb(table, onTableDraw, LV_EVENT_DRAW_TASK_ADDED, nullptr);
  }
  int sel;
  {
    Guard g;
    sel = gSettings.selProgram;
  }
  loadProgram(sel);
  syncDropdown(ddEditProg, editProgOpts, sizeof(editProgOpts), editIdx);
  refreshTable();
}

static void updateProgramTab(const Settings& S) {
  // follow a selection made on the Main tab or the web page (or a program saved from the web)
  if (!progDirty && !modal && pendingSwitch < 0 && (S.selProgram != editIdx || gProgGen != loadedGen)) {
    loadProgram(S.selProgram);
    refreshTable();
  }
  // show the edited (possibly renamed, unsaved) name in the dropdown
  if (ddEditProg && !lv_dropdown_is_open(ddEditProg)) {
    char opts[MAX_PROGRAMS * 40];
    buildProgramOptions(opts, sizeof(opts));
    if (progDirty) {
      char out[MAX_PROGRAMS * 40] = "";
      const char* p = opts;
      for (int i = 0; i < MAX_PROGRAMS && p; ++i) {
        const char* e = strchr(p, '\n');
        char line[48];
        if (i == editIdx) snprintf(line, sizeof(line), "%d %s*", i + 1, uiProg.name);
        else snprintf(line, sizeof(line), "%.*s", (int)(e ? e - p : strlen(p)), p);
        if (i) strlcat(out, "\n", sizeof(out));
        strlcat(out, line, sizeof(out));
        p = e ? e + 1 : nullptr;
      }
      strlcpy(opts, out, sizeof(opts));
    }
    if (strcmp(opts, editProgOpts) != 0) {
      strlcpy(editProgOpts, opts, sizeof(editProgOpts));
      lv_dropdown_set_options(ddEditProg, opts);
    }
    if (pendingSwitch < 0 && (int)lv_dropdown_get_selected(ddEditProg) != editIdx)
      lv_dropdown_set_selected(ddEditProg, editIdx);
  }
}

// ------------------------------------------------------------ Chart tab
// The chart shows CHART_N/2 points (each the mean of two samples): fewer points
// than pixels makes LVGL draw real line segments, so gaps render as dashes.
static constexpr int CHART_PTS = CHART_N / 2;
static constexpr int DASH = 3;  // points per dash / gap of a broken line (~5 px)
static char xLabelBuf[6][12];
static const char* xLabels[7];

static void fmtAxisTime(float sec, char* out, size_t n) {
  const bool neg = sec < -0.5f;
  uint32_t t = (uint32_t)(fabsf(sec) + 0.5f);
  if (t >= 3600) snprintf(out, n, "%s%luh%02lu", neg ? "-" : "", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60));
  else snprintf(out, n, "%s%lu:%02lu", neg ? "-" : "", (unsigned long)(t / 60), (unsigned long)(t % 60));
}

static int32_t toCoord(float v) { return isnan(v) ? LV_CHART_POINT_NONE : (int32_t)lroundf(v * 10.0f); }

static float pairMean(const float* a, int k) {
  const float x = a[2 * k], y = a[2 * k + 1];
  if (isnan(x)) return y;
  if (isnan(y)) return x;
  return (x + y) * 0.5f;
}

static void bindChart() {
  chart = find("chart");
  chartYScale = find("chart_yscale");
  chartXScale = find("chart_xscale");
  lblChartInfo = find("chart_info");
  // numbers are off in the XML (LVGL Pro Editor preview crash, see chart_page.xml)
  if (chartYScale) lv_scale_set_label_show(chartYScale, true);
  if (chartXScale) lv_scale_set_label_show(chartXScale, true);
  if (!chart) return;
  // series order in the XML: process value, setpoint, program
  serPv = lv_chart_get_series_next(chart, nullptr);
  serSp = serPv ? lv_chart_get_series_next(chart, serPv) : nullptr;
  serPlan = serSp ? lv_chart_get_series_next(chart, serSp) : nullptr;
  if (!serPlan) Serial.println("[ui] the chart needs 3 series (PV, SP, program)");
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR);
  lv_chart_set_point_count(chart, CHART_PTS);
  for (int i = 0; i < 6; ++i) xLabels[i] = xLabelBuf[i];
  xLabels[6] = nullptr;
}

static void updateChart() {
  if (!chart || !serPlan) return;
  static ChartData d;  // ~3.6 kB, keep it off the stack
  chartSnapshot(d);
  int32_t* yPlan = lv_chart_get_series_y_array(chart, serPlan);
  int32_t* ySp = lv_chart_get_series_y_array(chart, serSp);
  int32_t* yPv = lv_chart_get_series_y_array(chart, serPv);
  float lo = 1e9f, hi = -1e9f;
  for (int k = 0; k < CHART_PTS; ++k) {
    const float plan = pairMean(d.plan, k), sp = pairMean(d.sp, k), pv = pairMean(d.pv, k);
    const bool gap = (k / DASH) % 2 == 1;  // broken line pattern
    yPlan[k] = (!d.planActive && gap) ? LV_CHART_POINT_NONE : toCoord(plan);
    ySp[k] = (!d.spActive && gap) ? LV_CHART_POINT_NONE : toCoord(sp);
    yPv[k] = toCoord(pv);
    for (float v : {plan, pv, sp}) {
      if (isnan(v)) continue;
      lo = fminf(lo, v);
      hi = fmaxf(hi, v);
    }
  }
  if (lo > hi) { lo = 0; hi = 100; }
  // 5 equal intervals (6 scale labels) on round numbers, covering lo..hi
  const float pad = fmaxf(2.0f, (hi - lo) * 0.08f);
  lo = (lo >= 0 && lo - pad < 0) ? 0 : lo - pad;  // don't go below 0 just for padding
  hi += pad;
  static const float STEPS[] = {1, 2, 4, 5, 10, 15, 20, 25, 30, 40, 50, 60, 75, 100, 120, 150, 200, 250, 300, 400, 500, 1000, 2000};
  float base = floorf(lo / 1000.0f) * 1000.0f, step = 2000;
  for (float st : STEPS) {
    const float b = floorf(lo / st) * st;
    if (b + 5 * st >= hi) { base = b; step = st; break; }
  }
  lo = base;
  hi = base + 5 * step;
  lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)(lo * 10), (int32_t)(hi * 10));
  lv_chart_refresh(chart);
  if (chartYScale) lv_scale_set_range(chartYScale, (int32_t)lo, (int32_t)hi);
  if (chartXScale) {  // 6 labels: 5 intervals
    for (int i = 0; i < 6; ++i) {
      const float t = i * d.span / 5.0f;
      fmtAxisTime(d.run ? t : t - d.span, xLabelBuf[i], sizeof(xLabelBuf[i]));
    }
    lv_scale_set_text_src(chartXScale, xLabels);
    lv_obj_invalidate(chartXScale);
  }

  char a[16], b[16], buf[96];
  if (d.run) {
    fmtDur(d.elapsed, a, sizeof(a));
    fmtDur(d.planTotal, b, sizeof(b));
    snprintf(buf, sizeof(buf), "%s  %s / %s", d.progName, a, b);
  } else {
    fmtDur(d.planTotal, b, sizeof(b));
    snprintf(buf, sizeof(buf), "Last 5 min. Preview: %s (%s over full width)", d.progName, b);
  }
  setText(lblChartInfo, buf);
}

// ------------------------------------------------------------ Setup tab
static const uint32_t BAUDS[] = {2400, 4800, 9600, 19200, 38400};

// --- System
static void refreshWifiButtons() {
  setText(lblSsid, wifiSsid[0] ? wifiSsid : "(none: AP only)");
  setText(lblPass, wifiPass[0] ? "********" : "(none)");
  setText(lblApPass, "********");
}

static void refreshSystem() {
  setupLoading = true;
  Settings S;
  {
    Guard g;
    S = gSettings;
  }
  setChecked(swRotate, S.rotate180);
  if (sldBright) lv_slider_set_value(sldBright, S.brightness, LV_ANIM_OFF);
  strlcpy(wifiSsid, S.ssid, sizeof(wifiSsid));
  strlcpy(wifiPass, S.pass, sizeof(wifiPass));
  strlcpy(wifiApPass, S.apPass, sizeof(wifiApPass));
  refreshWifiButtons();
  setupLoading = false;
}

static void updateSysInfo() {
  if (!lblSysInfo) return;
  String info = netInfo();
  char host[24];
  {
    Guard g;
    strlcpy(host, gSettings.hostname, sizeof(host));
  }
  char buf[160];
  snprintf(buf, sizeof(buf), "%s\nhttp://%s.local   firmware " FW_VERSION, info.c_str(), host);
  setText(lblSysInfo, buf);
}

extern "C" void ui_on_rotate(lv_event_t*) {
  if (setupLoading) return;
  const bool r = lv_obj_has_state(swRotate, LV_STATE_CHECKED);
  {
    Guard g;
    gSettings.rotate180 = r;
  }
  applyRotation(r);
  saveSettingsQuiet();
}

extern "C" void ui_on_brightness(lv_event_t*) {
  if (setupLoading) return;
  const uint8_t b = (uint8_t)lv_slider_get_value(sldBright);
  {
    Guard g;
    gSettings.brightness = b;
  }
  uiSetBrightness(b);
}

extern "C" void ui_on_brightness_done(lv_event_t*) {
  if (!setupLoading) saveSettingsQuiet();
}

static void applySsid(const char* t, int) {
  strlcpy(wifiSsid, t, sizeof(wifiSsid));
  refreshWifiButtons();
}
static void applyPass(const char* t, int) {
  strlcpy(wifiPass, t, sizeof(wifiPass));
  refreshWifiButtons();
}
static void applyApPass(const char* t, int) {
  if (strlen(t) < 8) {
    toast("Too short", "The access point password needs at least 8 characters.");
    return;
  }
  strlcpy(wifiApPass, t, sizeof(wifiApPass));
  refreshWifiButtons();
}
extern "C" void ui_on_ssid(lv_event_t*) { openTextEditor("Wi-Fi network name (SSID)", wifiSsid, 32, false, applySsid, 0); }
extern "C" void ui_on_pass(lv_event_t*) { openTextEditor("Wi-Fi password", wifiPass, 64, true, applyPass, 0); }
extern "C" void ui_on_ap_pass(lv_event_t*) {
  openTextEditor("Access point password (8+ characters)", wifiApPass, 32, true, applyApPass, 0);
}

static void onScanPick(lv_event_t* e) {
  lv_obj_t* btn = evTarget(e);
  lv_obj_t* list = lv_obj_get_parent(btn);
  strlcpy(wifiSsid, lv_list_get_button_text(list, btn), sizeof(wifiSsid));
  refreshWifiButtons();
  modalClose();
}
static void onScanCancel(lv_event_t*) { modalClose(); }

static void showScanResult(const char* names, int count) {
  if (modal) return;
  modalCreate(count > 0 ? "Pick a network" : count == 0 ? "No networks found" : "Scan failed - Wi-Fi busy, try again");
  lv_obj_t* list = lv_list_create(modal);
  lv_obj_set_size(list, SCREEN_W - 8, 172);
  lv_obj_set_pos(list, 0, 22);
  const char* p = names;
  for (int i = 0; i < count && p && *p; ++i) {
    const char* e = strchr(p, '\n');
    char name[33];
    snprintf(name, sizeof(name), "%.*s", (int)(e ? e - p : strlen(p)), p);
    lv_obj_t* b = lv_list_add_button(list, LV_SYMBOL_WIFI, name);
    lv_obj_add_event_cb(b, onScanPick, LV_EVENT_CLICKED, nullptr);
    p = e ? e + 1 : nullptr;
  }
  mkButton(modal, "Cancel", 220, 198, 92, 34, onScanCancel);
}

extern "C" void ui_on_scan(lv_event_t*) {
  if (scanWaiting) return;
  netScanStart();
  scanWaiting = true;
  setText(lblSysInfo, "Scanning for networks...");
}

static void pollScan() {
  if (!scanWaiting) return;
  static char names[640];
  const int n = netScanResult(names, sizeof(names));
  if (n == -1) return;
  scanWaiting = false;
  updateSysInfo();
  // only pop up the list if the user is still on the Setup tab
  if (activeTab() == TAB_SETUP) showScanResult(names, n);
}

// "Connecting..." message: closes itself once the station has an IP address,
// turns into a hint if the connection does not come up within 30 s.
static lv_obj_t* wifiBox = nullptr;
static lv_obj_t* wifiBoxLabel = nullptr;
static uint32_t wifiBoxMs = 0;       // when the connection attempt started
static uint32_t wifiBoxCloseAt = 0;  // 0 = stay open
static bool wifiBoxWaiting = false;

static void onWifiBoxDeleted(lv_event_t*) {
  wifiBox = nullptr;
  wifiBoxLabel = nullptr;
  wifiBoxWaiting = false;
  wifiBoxCloseAt = 0;
}

static void pollWifiBox() {
  if (!wifiBox) return;
  const uint32_t now = millis();
  if (wifiBoxCloseAt && (int32_t)(now - wifiBoxCloseAt) >= 0) {
    lv_msgbox_close(wifiBox);  // triggers onWifiBoxDeleted
    return;
  }
  if (!wifiBoxWaiting) return;
  char buf[160];
  if (netConnectedSinceApply()) {
    snprintf(buf, sizeof(buf), "Connected to %s\nIP %s", wifiSsid, netStaIp().c_str());
    setText(wifiBoxLabel, buf);
    wifiBoxWaiting = false;
    wifiBoxCloseAt = now + 2500;
    updateSysInfo();
  } else if (now - wifiBoxMs > 30000) {
    snprintf(buf, sizeof(buf), "Could not join \"%s\". Check the name and password. The access point stays available.",
             wifiSsid);
    setText(wifiBoxLabel, buf);
    wifiBoxWaiting = false;  // stays open until closed
  }
}

extern "C" void ui_on_connect(lv_event_t*) {
  {
    Guard g;
    strlcpy(gSettings.ssid, wifiSsid, sizeof(gSettings.ssid));
    strlcpy(gSettings.pass, wifiPass, sizeof(gSettings.pass));
    strlcpy(gSettings.apPass, wifiApPass, sizeof(gSettings.apPass));
  }
  saveSettingsQuiet();
  netApplyWifi();
  if (wifiBox) lv_msgbox_close(wifiBox);
  char buf[64];
  if (wifiSsid[0]) snprintf(buf, sizeof(buf), "Connecting to \"%s\"...", wifiSsid);
  else strlcpy(buf, "Saved. Running as access point only.", sizeof(buf));
  wifiBox = msgBox("Wi-Fi", buf, nullptr, nullptr, true, &wifiBoxLabel);
  lv_obj_add_event_cb(wifiBox, onWifiBoxDeleted, LV_EVENT_DELETE, nullptr);
  wifiBoxMs = millis();
  wifiBoxWaiting = wifiSsid[0] != 0;
  wifiBoxCloseAt = wifiSsid[0] ? 0 : wifiBoxMs + 2500;
}

static void bindSystem() {
  swRotate = find("sys_rotate");
  sldBright = find("sys_bright");
  lblSsid = find("sys_ssid_text");
  lblPass = find("sys_pass_text");
  lblApPass = find("sys_ap_pass_text");
  lblSysInfo = find("sys_info");
  if (sldBright) lv_slider_set_range(sldBright, 10, 255);
  refreshSystem();
  updateSysInfo();
}

// --- Controller
static void refreshController() {
  setupLoading = true;
  Settings S;
  {
    Guard g;
    S = gSettings;
  }
  if (ddProfile) lv_dropdown_set_selected(ddProfile, S.profile);
  char b[16];
  snprintf(b, sizeof(b), "%u", S.slaveId);
  setText(lblSlave, b);
  for (int i = 0; i < 5; ++i)
    if (BAUDS[i] == S.baud && ddBaud) lv_dropdown_set_selected(ddBaud, i);
  if (ddFormat) lv_dropdown_set_selected(ddFormat, S.parity == 1 ? 1 : S.parity == 2 ? 2 : (S.stopBits == 2 ? 3 : 0));
  if (ddRamp) lv_dropdown_set_selected(ddRamp, S.rampMode);
  setChecked(swSoak, S.guaranteedSoak);
  snprintf(b, sizeof(b), "%.1f", S.soakBand);
  setText(lblBand, b);
  setChecked(swLock, S.lockFront);
  if (ddEnd) lv_dropdown_set_selected(ddEnd, S.endAction);
  char sp[12], rr[12], off[12], info[120];
  if (S.reg.sp == REG_UNSET) strcpy(sp, "not set"); else snprintf(sp, sizeof(sp), "%u", S.reg.sp);
  if (S.reg.rampRate == REG_UNSET) strcpy(rr, "not set"); else snprintf(rr, sizeof(rr), "%u", S.reg.rampRate);
  if (S.reg.ctrlOff == REG_UNSET) strcpy(off, "not set"); else snprintf(off, sizeof(off), "%u", S.reg.ctrlOff);
  snprintf(info, sizeof(info), "Registers (edit on the web page): SP %s,\nramp r.SP %s, outputs off %s", sp, rr, off);
  setText(lblCtlInfo, info);
  setupLoading = false;
}

extern "C" void ui_on_controller_changed(lv_event_t* e) {
  if (setupLoading) return;
  lv_obj_t* t = evTarget(e);
  Guard g;
  Settings& S = gSettings;
  if (t == ddProfile) {
    const uint8_t p = lv_dropdown_get_selected(ddProfile) ? PROFILE_GENERIC : PROFILE_PMA_KS4X;
    if (p != S.profile) {
      S.profile = p;
      regMapDefaults(S.reg, p);
    }
  } else if (t == ddBaud) {
    const uint32_t i = lv_dropdown_get_selected(ddBaud);
    if (i < 5) S.baud = BAUDS[i];
  } else if (t == ddFormat) {
    const uint32_t f = lv_dropdown_get_selected(ddFormat);
    S.parity = (f == 1) ? 1 : (f == 2) ? 2 : 0;
    S.stopBits = (f == 3) ? 2 : 1;
  } else if (t == ddRamp) {
    S.rampMode = lv_dropdown_get_selected(ddRamp) ? RAMP_CONTROLLER : RAMP_HOST;
  } else if (t == swSoak) {
    S.guaranteedSoak = lv_obj_has_state(swSoak, LV_STATE_CHECKED);
  } else if (t == swLock) {
    S.lockFront = lv_obj_has_state(swLock, LV_STATE_CHECKED);
  } else if (t == ddEnd) {
    const uint32_t a = lv_dropdown_get_selected(ddEnd);
    if (a <= END_OUTPUTS_OFF) S.endAction = a;
  }
}

static void applySlave(float v, int) {
  {
    Guard g;
    gSettings.slaveId = (uint8_t)constrain((int)v, 1, 247);
  }
  refreshController();
}
static void applyBand(float v, int) {
  {
    Guard g;
    gSettings.soakBand = constrain(v, 0.1f, 500.0f);
  }
  refreshController();
}
extern "C" void ui_on_slave(lv_event_t*) {
  float cur;
  {
    Guard g;
    cur = gSettings.slaveId;
  }
  openNumEditor("Modbus slave address (1-247)", cur, applySlave, 0);
}
extern "C" void ui_on_band(lv_event_t*) {
  float cur;
  {
    Guard g;
    cur = gSettings.soakBand;
  }
  openNumEditor("Soak band +/- (C)", cur, applyBand, 0);
}
extern "C" void ui_on_controller_save(lv_event_t*) {
  saveSettingsAndNotify();
  toast("Saved", "Controller settings stored. The serial link was reopened.");
}

extern "C" void ui_on_test_link(lv_event_t*) {
  BusRequest r{};
  r.op = BUS_READ;
  {
    Guard g;
    r.addr = gSettings.reg.pv;
  }
  r.count = 1;
  char msg[160];
  if (r.addr == REG_UNSET) strlcpy(msg, "The PV register address is not set.", sizeof(msg));
  else if (busRequest(r, 5000)) snprintf(msg, sizeof(msg), "Controller answered.\nPV register %u = %d (raw)", r.addr, r.values[0]);
  else snprintf(msg, sizeof(msg), "No valid answer.\n%s\nSave settings first if you changed them.", r.err);
  toast("Link test", msg);
}

static void bindController() {
  ddProfile = find("ctl_profile");
  lblSlave = find("ctl_slave_text");
  ddBaud = find("ctl_baud");
  ddFormat = find("ctl_format");
  ddRamp = find("ctl_ramp");
  swSoak = find("ctl_soak");
  lblBand = find("ctl_band_text");
  swLock = find("ctl_lock");
  ddEnd = find("ctl_end");
  lblCtlInfo = find("ctl_info");
  refreshController();
}

// ------------------------------------------------------------ periodic update
extern "C" void ui_on_tab_changed(lv_event_t* e) {
  if (evTarget(e) != tabview) return;  // ignore the nested Setup tab view
  const int t = activeTab();
  if (t == TAB_CHART) updateChart();
  if (t == TAB_SETUP) {
    refreshSystem();
    refreshController();
    updateSysInfo();
  }
}

static void uiTimer(lv_timer_t*) {
  static uint32_t lastChart = 0, lastInfo = 0;
  static LiveData L;
  static Settings S;
  {
    Guard g;
    L = gLive;
    S = gSettings;
  }
  if (S.brightness != appliedBrightness) uiSetBrightness(S.brightness);
  if ((S.rotate180 != 0) != rotated) {  // changed from the web page
    applyRotation(S.rotate180);
    setupLoading = true;
    setChecked(swRotate, S.rotate180);
    setupLoading = false;
  }
  const int tab = activeTab();
  if (tab == TAB_MAIN) updateMain(L, S);
  if (tab == TAB_PROGRAM) updateProgramTab(S);
  if (tab == TAB_CHART && millis() - lastChart >= 1000) {
    lastChart = millis();
    updateChart();
  }
  pollScan();  // always: the scan must be collected so Wi-Fi can reconnect
  pollWifiBox();
  if (tab == TAB_SETUP && !scanWaiting && millis() - lastInfo > 3000) {
    lastInfo = millis();
    updateSysInfo();
  }
}

// ------------------------------------------------------------ XML loading
// Event handlers, attached to the XML widgets by name after the screen has
// been created. (They are not declared in the XML, so the LVGL Pro Editor
// preview has nothing unresolved to complain about.)
struct UiEvent {
  const char* widget;
  lv_event_code_t code;
  lv_event_cb_t cb;
};

static const UiEvent UI_EVENTS[] = {
    {"tabs", LV_EVENT_VALUE_CHANGED, ui_on_tab_changed},
    // Main
    {"main_man_sp", LV_EVENT_CLICKED, ui_on_manual_sp},
    {"main_man_sw", LV_EVENT_VALUE_CHANGED, ui_on_manual_switch},
    {"main_prog", LV_EVENT_VALUE_CHANGED, ui_on_main_prog},
    {"main_start", LV_EVENT_CLICKED, ui_on_start},
    {"main_pause", LV_EVENT_CLICKED, ui_on_pause},
    {"main_stop", LV_EVENT_CLICKED, ui_on_stop},
    {"main_skip", LV_EVENT_CLICKED, ui_on_skip},
    // Program
    {"prog_select", LV_EVENT_VALUE_CHANGED, ui_on_edit_prog},
    {"prog_rename", LV_EVENT_CLICKED, ui_on_rename},
    {"prog_table", LV_EVENT_VALUE_CHANGED, ui_on_table},
    {"prog_add", LV_EVENT_CLICKED, ui_on_add_step},
    {"prog_del", LV_EVENT_CLICKED, ui_on_del_step},
    {"prog_up", LV_EVENT_CLICKED, ui_on_step_up},
    {"prog_down", LV_EVENT_CLICKED, ui_on_step_down},
    {"prog_revert", LV_EVENT_CLICKED, ui_on_prog_revert},
    {"prog_save", LV_EVENT_CLICKED, ui_on_prog_save},
    // Setup > System
    {"sys_rotate", LV_EVENT_VALUE_CHANGED, ui_on_rotate},
    {"sys_bright", LV_EVENT_VALUE_CHANGED, ui_on_brightness},
    {"sys_bright", LV_EVENT_RELEASED, ui_on_brightness_done},
    {"sys_ssid", LV_EVENT_CLICKED, ui_on_ssid},
    {"sys_scan", LV_EVENT_CLICKED, ui_on_scan},
    {"sys_pass", LV_EVENT_CLICKED, ui_on_pass},
    {"sys_ap_pass", LV_EVENT_CLICKED, ui_on_ap_pass},
    {"sys_connect", LV_EVENT_CLICKED, ui_on_connect},
    // Setup > Controller
    {"ctl_profile", LV_EVENT_VALUE_CHANGED, ui_on_controller_changed},
    {"ctl_slave", LV_EVENT_CLICKED, ui_on_slave},
    {"ctl_baud", LV_EVENT_VALUE_CHANGED, ui_on_controller_changed},
    {"ctl_format", LV_EVENT_VALUE_CHANGED, ui_on_controller_changed},
    {"ctl_ramp", LV_EVENT_VALUE_CHANGED, ui_on_controller_changed},
    {"ctl_soak", LV_EVENT_VALUE_CHANGED, ui_on_controller_changed},
    {"ctl_band", LV_EVENT_CLICKED, ui_on_band},
    {"ctl_lock", LV_EVENT_VALUE_CHANGED, ui_on_controller_changed},
    {"ctl_end", LV_EVENT_VALUE_CHANGED, ui_on_controller_changed},
    {"ctl_test", LV_EVENT_CLICKED, ui_on_test_link},
    {"ctl_save", LV_EVENT_CLICKED, ui_on_controller_save},
};

static void uiBindEvents() {
  for (const UiEvent& ev : UI_EVENTS) {
    lv_obj_t* o = find(ev.widget);
    if (o) lv_obj_add_event_cb(o, ev.cb, ev.code, nullptr);
  }
}

// Font names used in ui/globals.xml -> built-in fonts (which include the symbols)
static void uiRegisterFonts() {
  lv_xml_register_font(nullptr, "font_12", &lv_font_montserrat_12);
  lv_xml_register_font(nullptr, "font_14", &lv_font_montserrat_14);
  lv_xml_register_font(nullptr, "font_20", &lv_font_montserrat_20);
  lv_xml_register_font(nullptr, "font_48", &lv_font_montserrat_48);
  lv_xml_register_font(nullptr, "font_icons_14", &lv_font_montserrat_14);
}

static bool uiLoadXml() {
  uiRegisterFonts();
  bool ok = true;
  for (int i = 0; i < UI_XML_COUNT; ++i) {
    if (lv_xml_register_component_from_data(UI_XML_FILES[i].name, UI_XML_FILES[i].xml) != LV_RESULT_OK) {
      Serial.printf("[ui] XML error in %s/%s.xml\n", UI_XML_FILES[i].kind, UI_XML_FILES[i].name);
      ok = false;
    }
  }
  screen = lv_xml_create_screen("main");
  if (!screen) {
    Serial.println("[ui] could not create screen 'main' from the XML");
    return false;
  }
  lv_screen_load(screen);
  tabview = find("tabs");
  // The screen is created once, so the parsed component definitions are no
  // longer needed (~20 kB). "globals" stays: it owns the styles in use.
  for (int i = 0; i < UI_XML_COUNT; ++i)
    if (strcmp(UI_XML_FILES[i].kind, "globals") != 0) lv_xml_component_unregister(UI_XML_FILES[i].name);
  return ok;
}

// ------------------------------------------------------------ init
void uiInit() {
  bool rot;
  uint8_t b;
  {
    Guard g;
    calMinX = gSettings.tsMinX; calMaxX = gSettings.tsMaxX;
    calMinY = gSettings.tsMinY; calMaxY = gSettings.tsMaxY;
    rot = gSettings.rotate180;
    b = gSettings.brightness;
  }
  tft.init();
  rotated = rot;
  tft.setRotation(rot ? 3 : 1);  // landscape 320x240
  tft.fillScreen(TFT_BLACK);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BACKLIGHT, 0);
  uiSetBrightness(b);

  touchSpi.begin(PIN_TOUCH_CLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);  // raw orientation; 180 degree rotation is handled in touchCb

  lv_init();
  lv_tick_set_cb(tickCb);
  disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, drawBuf, nullptr, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);

  lv_theme_t* th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                         lv_palette_main(LV_PALETTE_DEEP_ORANGE), true, &lv_font_montserrat_14);
  lv_display_set_theme(disp, th);

  uiLoadXml();
  if (!screen) {  // show something useful instead of a black screen
    lv_obj_t* l = lv_label_create(lv_screen_active());
    lv_label_set_text(l, "UI XML could not be loaded.\nSee the serial log (115200 baud).");
    lv_obj_center(l);
    return;
  }
  uiBindEvents();
  bindMain();
  bindProgram();
  bindChart();
  bindSystem();
  bindController();

  lv_timer_create(uiTimer, 250, nullptr);
}

void uiLoop() { lv_timer_handler(); }
