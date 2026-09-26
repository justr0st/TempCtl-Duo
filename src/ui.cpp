// LVGL 8.3 user interface for the 320x240 CYD screen.
// Tabs: Main (live values, manual setpoint, program run), Program (editor for
// up to 10 named programs), Chart, Setup (System / Controller).
#include "ui.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include <math.h>
#include "app_state.h"
#include "controller.h"
#include "net.h"

// ------------------------------------------------------------ drivers
static TFT_eSPI tft;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen ts(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
static lv_disp_draw_buf_t drawBuf;
static lv_color_t buf1[SCREEN_W * 24];
static lv_disp_drv_t dispDrv;
static lv_indev_drv_t indevDrv;
static uint16_t calMinX, calMaxX, calMinY, calMaxY;
static uint8_t appliedBrightness = 0;
static bool rotated = false;

static void flushCb(lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* c) {
  const uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.pushColors((uint16_t*)&c->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(d);
}

static void touchCb(lv_indev_drv_t*, lv_indev_data_t* data) {
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
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void uiSetBrightness(uint8_t b) {
  if (b < 10) b = 10;
  ledcWrite(0, b);
  appliedBrightness = b;
}

static void applyRotation(bool r) {
  rotated = r;
  tft.setRotation(r ? 3 : 1);  // 1 = landscape, 3 = landscape upside down
  lv_obj_invalidate(lv_scr_act());
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

static lv_obj_t* mkButton(lv_obj_t* parent, const char* txt, int x, int y, int w, int h,
                          lv_event_cb_t cb, void* ud = nullptr) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_pad_all(b, 2, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

static void setBtnText(lv_obj_t* b, const char* t) { lv_label_set_text(lv_obj_get_child(b, 0), t); }

static void setEnabled(lv_obj_t* o, bool en) {
  if (en) lv_obj_clear_state(o, LV_STATE_DISABLED);
  else lv_obj_add_state(o, LV_STATE_DISABLED);
}

static void setChecked(lv_obj_t* o, bool on) {
  if (on) lv_obj_add_state(o, LV_STATE_CHECKED);
  else lv_obj_clear_state(o, LV_STATE_CHECKED);
}

static void toast(const char* title, const char* text) {
  lv_obj_t* m = lv_msgbox_create(nullptr, title, text, nullptr, true);
  lv_obj_set_width(m, 280);
  lv_obj_center(m);
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
  if (lv_dropdown_is_open(dd)) return;
  char opts[MAX_PROGRAMS * 40];
  buildProgramOptions(opts, sizeof(opts));
  if (strcmp(opts, cache) != 0) {
    strlcpy(cache, opts, n);
    lv_dropdown_set_options(dd, opts);
  }
  if (lv_dropdown_get_selected(dd) != sel) lv_dropdown_set_selected(dd, sel);
}

// ------------------------------------------------------------ modal editors
// Only one modal editor (number, time or text) is open at a time.
static lv_obj_t* modal = nullptr;
static lv_obj_t* modalTa = nullptr;
static lv_obj_t* modalUnits = nullptr;  // time editor: "min" / "s"

static void modalClose() {
  if (modal) lv_obj_del_async(modal);
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
  lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = mkLabel(modal, &lv_font_montserrat_14, 0, 0, title);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
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
      if (modalUnits && lv_btnmatrix_has_btn_ctrl(modalUnits, 0, LV_BTNMATRIX_CTRL_CHECKED)) v *= 60.0f;
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
    modalUnits = lv_btnmatrix_create(modal);
    lv_btnmatrix_set_map(modalUnits, units);
    lv_btnmatrix_set_btn_ctrl_all(modalUnits, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(modalUnits, true);
    lv_btnmatrix_set_btn_ctrl(modalUnits, mins ? 0 : 1, LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_set_size(modalUnits, 136, 44);
    lv_obj_set_pos(modalUnits, 172, 20);
  } else {
    snprintf(buf, sizeof(buf), "%.1f", cur);
    // drop a trailing ".0" for integer values
    const size_t l = strlen(buf);
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

// ------------------------------------------------------------ widgets
static lv_obj_t* tabview;
enum { TAB_MAIN = 0, TAB_PROGRAM, TAB_CHART, TAB_SETUP };
// Main tab
static lv_obj_t *lblPv, *lblSp, *lblTarget, *lblState, *lblPhase, *lblTotal, *lblOut, *lblMode, *lblMsg, *barProg,
    *ledLink, *btnManSp, *swManual, *ddMainProg, *btnStart, *btnPause, *btnStop, *btnSkip;
static char mainProgOpts[MAX_PROGRAMS * 40];
static uint32_t manualTouchedMs = 0;  // ignore live state briefly after the user toggled
// Program tab
static lv_obj_t *ddEditProg, *table, *lblProgInfo, *btnAdd, *btnDel, *btnUp, *btnDown;
static char editProgOpts[MAX_PROGRAMS * 40];
static Program uiProg;          // program being edited
static int editIdx = 0;         // which stored program uiProg belongs to
static uint32_t loadedGen = 0;  // gProgGen when uiProg was loaded
static int selRow = -1;         // selected step (-1 = none)
static bool progDirty = false;
static int pendingSwitch = -1;  // program to switch to after the save/discard question
// Chart tab
static lv_obj_t *chart, *lblChartInfo, *lblChartLegend;
static lv_chart_series_t *serPlan, *serSp, *serPv;
static bool chartRun = false;
static float chartSpan = 300;
// Setup tab
static lv_obj_t *swRotate, *sldBright, *btnSsid, *btnPass, *btnApPass, *lblSysInfo;
static lv_obj_t *ddProfile, *btnSlave, *ddBaud, *ddFormat, *ddRamp, *swSoak, *btnBand, *swLock, *ddEnd, *lblCtlInfo;
static bool setupLoading = false;
static char wifiSsid[33], wifiPass[65], wifiApPass[33];
static bool scanWaiting = false;

// ------------------------------------------------------------ Main tab
static void onStart(lv_event_t*) {
  RunState st;
  {
    Guard g;
    st = gLive.state;
  }
  if (st == RunState::Paused) sendCmd(CMD_RESUME);
  else sendCmd(CMD_START, lv_dropdown_get_selected(ddMainProg));
}
static void onPause(lv_event_t*) { sendCmd(CMD_PAUSE); }
static void onSkip(lv_event_t*) { sendCmd(CMD_SKIP); }

static void onStopConfirm(lv_event_t* e) {
  lv_obj_t* m = lv_event_get_current_target(e);
  const char* t = lv_msgbox_get_active_btn_text(m);
  if (t && strcmp(t, "Stop") == 0) sendCmd(CMD_STOP);
  lv_msgbox_close_async(m);
}

static void onStop(lv_event_t*) {
  static const char* btns[] = {"Stop", "Cancel", ""};
  lv_obj_t* m = lv_msgbox_create(nullptr, "Stop program?", "The end action from Setup > Controller is applied.",
                                 btns, false);
  lv_obj_set_width(m, 260);
  lv_obj_center(m);
  lv_obj_add_event_cb(m, onStopConfirm, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void onMainProg(lv_event_t*) { selectProgram(lv_dropdown_get_selected(ddMainProg)); }

static void applyManualSp(float v, int) {
  setManualSp(v);
  char b[16];
  float cur;
  {
    Guard g;
    cur = gSettings.manualSp;
  }
  fmtTemp(true, cur, b, sizeof(b));
  setBtnText(btnManSp, b);
}

static void onManSp(lv_event_t*) {
  float cur;
  char title[64];
  {
    Guard g;
    cur = gSettings.manualSp;
    snprintf(title, sizeof(title), "Manual setpoint (C), %.0f..%.0f", gSettings.spMin, gSettings.spMax);
  }
  openNumEditor(title, cur, applyManualSp, 0);
}

static void onManualSwitch(lv_event_t*) {
  manualTouchedMs = millis();
  sendCmd(lv_obj_has_state(swManual, LV_STATE_CHECKED) ? CMD_MANUAL_ON : CMD_MANUAL_OFF);
}

static void buildMain(lv_obj_t* tab) {
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  mkLabel(tab, &lv_font_montserrat_12, 2, 0, "Process value (C)");
  lblPv = mkLabel(tab, &lv_font_montserrat_48, 0, 12, "---.-");
  lv_obj_set_style_text_color(lblPv, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);
  lblSp = mkLabel(tab, &lv_font_montserrat_20, 2, 62, "SP ---.-");
  lv_obj_set_style_text_color(lblSp, lv_palette_lighten(LV_PALETTE_BLUE, 1), 0);
  lblTarget = mkLabel(tab, &lv_font_montserrat_12, 176, 68, "");

  ledLink = lv_led_create(tab);
  lv_obj_set_size(ledLink, 12, 12);
  lv_obj_set_pos(ledLink, 296, 2);
  lblState = mkLabel(tab, &lv_font_montserrat_20, 176, 0, "Idle");
  lblPhase = mkLabel(tab, &lv_font_montserrat_12, 176, 24, "");
  lblTotal = mkLabel(tab, &lv_font_montserrat_12, 176, 38, "");
  lv_label_set_long_mode(lblTotal, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lblTotal, 140);
  lblOut = mkLabel(tab, &lv_font_montserrat_12, 176, 52, "");
  lblMode = mkLabel(tab, &lv_font_montserrat_12, 236, 52, "");

  // manual setpoint
  mkLabel(tab, &lv_font_montserrat_14, 2, 100, "Manual SP");
  btnManSp = mkButton(tab, "---.-", 90, 92, 92, 34, onManSp);
  swManual = lv_switch_create(tab);
  lv_obj_set_size(swManual, 54, 28);
  lv_obj_set_pos(swManual, 192, 95);
  lv_obj_add_event_cb(swManual, onManualSwitch, LV_EVENT_VALUE_CHANGED, nullptr);
  mkLabel(tab, &lv_font_montserrat_12, 254, 102, "on / off");

  // program selection + run controls
  ddMainProg = lv_dropdown_create(tab);
  lv_obj_set_pos(ddMainProg, 0, 132);
  lv_obj_set_size(ddMainProg, 140, 36);
  lv_obj_set_style_pad_all(ddMainProg, 8, 0);
  lv_obj_add_event_cb(ddMainProg, onMainProg, LV_EVENT_VALUE_CHANGED, nullptr);
  btnStart = mkButton(tab, LV_SYMBOL_PLAY, 144, 132, 46, 36, onStart);
  btnPause = mkButton(tab, LV_SYMBOL_PAUSE, 194, 132, 38, 36, onPause);
  btnStop = mkButton(tab, LV_SYMBOL_STOP, 236, 132, 38, 36, onStop);
  lv_obj_set_style_bg_color(btnStop, lv_palette_main(LV_PALETTE_RED), 0);
  btnSkip = mkButton(tab, LV_SYMBOL_NEXT, 278, 132, 38, 36, onSkip);

  barProg = lv_bar_create(tab);
  lv_obj_set_size(barProg, 312, 6);
  lv_obj_set_pos(barProg, 2, 174);
  lv_bar_set_range(barProg, 0, 1000);

  lblMsg = mkLabel(tab, &lv_font_montserrat_12, 2, 184, "");
  lv_label_set_long_mode(lblMsg, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lblMsg, 312);
}

static void updateMain(const LiveData& L, const Settings& S) {
  char a[24], buf[96];
  if (L.pvValid) fmtTemp(true, L.pv, a, sizeof(a));
  else if (L.commOk && L.pvFlag != VAL_OK) strlcpy(a, L.pvFlag == VAL_SENSOR_FAULT ? "Sensor" : "---.-", sizeof(a));
  else strlcpy(a, "---.-", sizeof(a));
  lv_label_set_text(lblPv, a);

  fmtTemp(L.spEffValid, L.spEff, a, sizeof(a));
  snprintf(buf, sizeof(buf), "SP %s", a);
  lv_label_set_text(lblSp, buf);

  const bool active = programActive(L.state);
  if (active) {
    fmtTemp(true, L.target, a, sizeof(a));
    snprintf(buf, sizeof(buf), "Step target %s", a);
    lv_label_set_text(lblTarget, buf);
  } else if (S.reg.sp == REG_UNSET) {
    lv_label_set_text(lblTarget, "SP register not set!");
  } else {
    lv_label_set_text(lblTarget, "");
  }

  lv_led_set_color(ledLink, lv_palette_main(L.commOk ? LV_PALETTE_GREEN : LV_PALETTE_RED));
  lv_led_on(ledLink);

  switch (L.state) {
    case RunState::Running:
    case RunState::Paused:
      snprintf(buf, sizeof(buf), "%s %d/%d", L.state == RunState::Paused ? "Paused" : "Step", L.step + 1,
               L.stepCount);
      lv_label_set_text(lblState, buf);
      break;
    default:
      if (L.manualOn) lv_label_set_text(lblState, "Manual");
      else if (L.state == RunState::Finished) lv_label_set_text(lblState, "Finished");
      else if (L.state == RunState::Aborted) lv_label_set_text(lblState, "Stopped");
      else lv_label_set_text(lblState, "Idle");
  }
  const Program& sel = gPrograms[S.selProgram < MAX_PROGRAMS ? S.selProgram : 0];
  if (active) {
    fmtDur(L.phaseDur - L.phaseEl, a, sizeof(a));
    if (L.soakWait) lv_label_set_text(lblPhase, "Hold: waiting for PV");
    else {
      snprintf(buf, sizeof(buf), "%s  %s left", L.phase == Phase::Ramp ? "Ramp" : "Hold", a);
      lv_label_set_text(lblPhase, buf);
    }
    fmtDur(L.total - L.elapsed, a, sizeof(a));
    snprintf(buf, sizeof(buf), "%s: %s left", L.runName, a);
    lv_label_set_text(lblTotal, buf);
    lv_bar_set_value(barProg, L.total > 0 ? (int)(1000 * L.elapsed / L.total) : 0, LV_ANIM_OFF);
  } else {
    lv_label_set_text(lblPhase, L.manualOn ? (L.manualSynced ? "setpoint written" : "writing...") : "");
    {
      Guard g;
      fmtDur(ProgramEngine::plannedTotal(sel), a, sizeof(a));
      snprintf(buf, sizeof(buf), "%s, %s", sel.name, a);
    }
    lv_label_set_text(lblTotal, buf);
    lv_bar_set_value(barProg, L.state == RunState::Finished ? 1000 : 0, LV_ANIM_OFF);
  }
  if (L.outValid) {
    snprintf(buf, sizeof(buf), "Out %.0f%%", L.out);
    lv_label_set_text(lblOut, buf);
  } else {
    lv_label_set_text(lblOut, "");
  }
  if (L.statusValid && S.profile == PROFILE_PMA_KS4X) lv_label_set_text(lblMode, pmaModeText(L.status));
  else lv_label_set_text(lblMode, "");

  if (!L.commOk) snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", L.lastErr);
  else if (!L.spSynced && (active || L.manualOn)) snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", L.lastErr);
  else strlcpy(buf, L.runMsg, sizeof(buf));
  lv_label_set_text(lblMsg, buf);

  // manual setpoint widgets
  if (!modal) {
    fmtTemp(true, S.manualSp, a, sizeof(a));
    setBtnText(btnManSp, a);
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
  const int rows = uiProg.count ? uiProg.count : 1;
  lv_table_set_row_cnt(table, rows);
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
  char d[16], info[80];
  fmtDur(ProgramEngine::plannedTotal(uiProg), d, sizeof(d));
  snprintf(info, sizeof(info), "%d/%d steps  total %s%s", uiProg.count, MAX_STEPS, d,
           progDirty ? "   #FFC107 unsaved#" : "");
  lv_label_set_text(lblProgInfo, info);
  setEnabled(btnAdd, uiProg.count < MAX_STEPS);
  setEnabled(btnDel, selRow >= 0);
  setEnabled(btnUp, selRow > 0);
  setEnabled(btnDown, selRow >= 0 && selRow < uiProg.count - 1);
  lv_obj_invalidate(table);
}

static void scrollToRow(int row) {
  if (row < 0) return;
  // rows have the same height; estimate from the table's content height
  const lv_coord_t total = lv_obj_get_self_height(table);
  const int rows = uiProg.count ? uiProg.count : 1;
  const lv_coord_t rh = total / rows;
  const lv_coord_t y = rh * row, vis = lv_obj_get_content_height(table);
  const lv_coord_t top = lv_obj_get_scroll_y(table);
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

static void onTableDraw(lv_event_t* e) {
  lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->part != LV_PART_ITEMS || !dsc->rect_dsc) return;
  const int row = dsc->id / COL_CNT, col = dsc->id % COL_CNT;
  if (row == selRow) {
    dsc->rect_dsc->bg_color = lv_palette_darken(LV_PALETTE_BLUE, 3);
    dsc->rect_dsc->bg_opa = LV_OPA_COVER;
  }
  if (col == COL_NUM && dsc->label_dsc) dsc->label_dsc->color = lv_palette_main(LV_PALETTE_GREY);
}

static void onTable(lv_event_t*) {
  uint16_t row, col;
  lv_table_get_selected_cell(table, &row, &col);
  if (row == LV_TABLE_CELL_NONE || row >= uiProg.count) return;
  selRow = row;
  refreshTable();
  if (col == COL_NUM) return;  // the number column only selects the row
  char title[48];
  if (col == COL_TARGET) {
    snprintf(title, sizeof(title), "Step %d target temperature (C)", row + 1);
    openNumEditor(title, uiProg.s[row].target, applyCell, row * 10 + col);
  } else if (col == COL_RAMP) {
    snprintf(title, sizeof(title), "Step %d ramp time (0 = jump)", row + 1);
    openNumEditor(title, uiProg.s[row].rampSec, applyCell, row * 10 + col, true);
  } else if (col == COL_HOLD) {
    snprintf(title, sizeof(title), "Step %d hold time", row + 1);
    openNumEditor(title, uiProg.s[row].holdSec, applyCell, row * 10 + col, true);
  }
}

static void onAddStep(lv_event_t*) {
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

static void onDelStep(lv_event_t*) {
  if (programRemoveStep(uiProg, selRow)) {
    if (selRow >= uiProg.count) selRow = uiProg.count - 1;
    markDirty();
  }
}

static void onMoveStep(lv_event_t* e) {
  const int dir = (int)(intptr_t)lv_event_get_user_data(e);
  if (programMoveStep(uiProg, selRow, selRow + dir)) {
    selRow += dir;
    markDirty();
    scrollToRow(selRow);
  }
}

static void applyName(const char* t, int) {
  if (!t) return;
  while (*t == ' ') ++t;
  if (!*t) return;
  strlcpy(uiProg.name, t, sizeof(uiProg.name));
  editProgOpts[0] = 0;  // force the dropdown to show the new name
  markDirty();
}

static void onRename(lv_event_t*) {
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

static void onProgSave(lv_event_t*) { saveEdited(); }

static void onProgRevert(lv_event_t*) {
  loadProgram(editIdx);
  refreshTable();
}

static void switchToProgram(int idx) {
  selectProgram(idx);
  loadProgram(idx);
  refreshTable();
  lv_obj_scroll_to_y(table, 0, LV_ANIM_OFF);
}

static void onSwitchAnswer(lv_event_t* e) {
  lv_obj_t* m = lv_event_get_current_target(e);
  const char* t = lv_msgbox_get_active_btn_text(m);
  if (t && strcmp(t, "Save") == 0) {
    saveEdited();
    switchToProgram(pendingSwitch);
  } else if (t && strcmp(t, "Discard") == 0) {
    switchToProgram(pendingSwitch);
  } else {
    lv_dropdown_set_selected(ddEditProg, editIdx);
  }
  pendingSwitch = -1;
  lv_msgbox_close_async(m);
}

static void onEditProg(lv_event_t*) {
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
  lv_obj_t* m = lv_msgbox_create(nullptr, "Unsaved changes", text, btns, false);
  lv_obj_set_width(m, 290);
  lv_obj_center(m);
  lv_obj_add_event_cb(m, onSwitchAnswer, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void buildProgram(lv_obj_t* tab) {
  lv_obj_set_style_pad_all(tab, 2, 0);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

  ddEditProg = lv_dropdown_create(tab);
  lv_obj_set_pos(ddEditProg, 0, 0);
  lv_obj_set_size(ddEditProg, 196, 32);
  lv_obj_set_style_pad_all(ddEditProg, 7, 0);
  lv_obj_add_event_cb(ddEditProg, onEditProg, LV_EVENT_VALUE_CHANGED, nullptr);
  mkButton(tab, LV_SYMBOL_EDIT " Name", 200, 0, 116, 32, onRename);

  // fixed header above the scrolling table
  static const int widths[COL_CNT] = {34, 94, 94, 94};
  static const char* heads[COL_CNT] = {"#", "Target C", "Ramp", "Hold"};
  int x = 0;
  for (int c = 0; c < COL_CNT; ++c) {
    lv_obj_t* l = mkLabel(tab, &lv_font_montserrat_12, x + 5, 36, heads[c]);
    lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_GREY), 0);
    x += widths[c];
  }

  table = lv_table_create(tab);
  lv_obj_set_pos(table, 0, 52);
  lv_obj_set_size(table, 316, 104);
  lv_obj_set_style_pad_all(table, 0, 0);
  lv_obj_set_style_border_width(table, 1, 0);
  lv_table_set_col_cnt(table, COL_CNT);
  for (int c = 0; c < COL_CNT; ++c) lv_table_set_col_width(table, c, widths[c] - (c == COL_CNT - 1 ? 2 : 0));
  lv_obj_set_style_pad_top(table, 3, LV_PART_ITEMS);
  lv_obj_set_style_pad_bottom(table, 3, LV_PART_ITEMS);
  lv_obj_set_style_pad_left(table, 4, LV_PART_ITEMS);
  lv_obj_set_style_pad_right(table, 4, LV_PART_ITEMS);
  lv_obj_add_event_cb(table, onTable, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(table, onTableDraw, LV_EVENT_DRAW_PART_BEGIN, nullptr);

  lblProgInfo = mkLabel(tab, &lv_font_montserrat_12, 2, 158, "");
  lv_label_set_recolor(lblProgInfo, true);

  const int y = 174, h = 30;
  btnAdd = mkButton(tab, LV_SYMBOL_PLUS, 0, y, 40, h, onAddStep);
  btnDel = mkButton(tab, LV_SYMBOL_MINUS, 44, y, 40, h, onDelStep);
  btnUp = mkButton(tab, LV_SYMBOL_UP, 88, y, 40, h, onMoveStep, (void*)(intptr_t)-1);
  btnDown = mkButton(tab, LV_SYMBOL_DOWN, 132, y, 40, h, onMoveStep, (void*)(intptr_t)1);
  mkButton(tab, "Revert", 176, y, 62, h, onProgRevert);
  mkButton(tab, LV_SYMBOL_SAVE " Save", 242, y, 74, h, onProgSave);

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
  // follow a selection made on the Main tab or the web page
  // (or a program saved from the web page)
  if (!progDirty && !modal && pendingSwitch < 0 && (S.selProgram != editIdx || gProgGen != loadedGen)) {
    loadProgram(S.selProgram);
    refreshTable();
  }
  // show the edited (possibly renamed, unsaved) name in the dropdown
  if (!lv_dropdown_is_open(ddEditProg)) {
    char opts[MAX_PROGRAMS * 40];
    buildProgramOptions(opts, sizeof(opts));
    if (progDirty) {
      // replace the line of the edited program with the local name
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
    if (pendingSwitch < 0 && lv_dropdown_get_selected(ddEditProg) != editIdx)
      lv_dropdown_set_selected(ddEditProg, editIdx);
  }
}

// ------------------------------------------------------------ Chart tab
// The chart shows CHART_N/2 points (each the mean of two samples): fewer points
// than pixels makes LVGL draw real line segments, so gaps render as dashes.
static constexpr int CHART_PTS = CHART_N / 2;
static constexpr int DASH = 3;  // points per dash / gap of a broken line (~5 px)

static void fmtAxisTime(float sec, char* out, size_t n) {
  const bool neg = sec < -0.5f;
  uint32_t t = (uint32_t)(fabsf(sec) + 0.5f);
  if (t >= 3600) snprintf(out, n, "%s%luh%02lu", neg ? "-" : "", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60));
  else snprintf(out, n, "%s%lu:%02lu", neg ? "-" : "", (unsigned long)(t / 60), (unsigned long)(t % 60));
}

static void onChartDraw(lv_event_t* e) {
  lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->part != LV_PART_TICKS || !dsc->text) return;
  if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
    lv_snprintf(dsc->text, dsc->text_length, "%d", (int)lroundf(dsc->value / 10.0f));
  } else if (dsc->id == LV_CHART_AXIS_PRIMARY_X) {
    // 6 major ticks -> 5 intervals
    const float t = dsc->value * chartSpan / 5.0f;
    fmtAxisTime(chartRun ? t : t - chartSpan, dsc->text, dsc->text_length);
  }
}

static void buildChart(lv_obj_t* tab) {
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  chart = lv_chart_create(tab);
  lv_obj_set_pos(chart, 36, 0);
  lv_obj_set_size(chart, 262, 146);
  lv_obj_set_style_pad_all(chart, 4, 0);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, CHART_PTS);
  lv_chart_set_div_line_count(chart, 5, 6);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 4, 2, 6, 1, true, 34);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 4, 2, 6, 1, true, 20);
  lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);  // no point markers
  lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_text_font(chart, &lv_font_montserrat_12, LV_PART_TICKS);
  lv_obj_add_event_cb(chart, onChartDraw, LV_EVENT_DRAW_PART_BEGIN, nullptr);
  // added in drawing order: the process value is drawn last, on top
  serPlan = lv_chart_add_series(chart, lv_palette_lighten(LV_PALETTE_BLUE_GREY, 2), LV_CHART_AXIS_PRIMARY_Y);
  serSp = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_CHART_AXIS_PRIMARY_Y);
  serPv = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_DEEP_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
  lblChartLegend = mkLabel(tab, &lv_font_montserrat_12, 2, 170, "");
  lv_label_set_recolor(lblChartLegend, true);
  lv_label_set_long_mode(lblChartLegend, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lblChartLegend, 312);
  lv_label_set_text(lblChartLegend, "#FF5722 PV#   #03A9F4 SP#   #90A4AE Program#   (dashed = not active)");
  lblChartInfo = mkLabel(tab, &lv_font_montserrat_12, 2, 186, "");
  lv_label_set_long_mode(lblChartInfo, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lblChartInfo, 312);
}

static lv_coord_t toCoord(float v) { return isnan(v) ? LV_CHART_POINT_NONE : (lv_coord_t)lroundf(v * 10.0f); }

static float pairMean(const float* a, int k) {
  const float x = a[2 * k], y = a[2 * k + 1];
  if (isnan(x)) return y;
  if (isnan(y)) return x;
  return (x + y) * 0.5f;
}

static void updateChart() {
  static ChartData d;  // ~3.6 kB, keep it off the stack
  chartSnapshot(d);
  chartRun = d.run;
  chartSpan = d.span;
  float lo = 1e9f, hi = -1e9f;
  for (int k = 0; k < CHART_PTS; ++k) {
    const float plan = pairMean(d.plan, k), sp = pairMean(d.sp, k), pv = pairMean(d.pv, k);
    const bool gap = (k / DASH) % 2 == 1;  // broken line pattern
    serPlan->y_points[k] = (!d.planActive && gap) ? LV_CHART_POINT_NONE : toCoord(plan);
    serSp->y_points[k] = (!d.spActive && gap) ? LV_CHART_POINT_NONE : toCoord(sp);
    serPv->y_points[k] = toCoord(pv);
    for (float v : {plan, pv, sp}) {
      if (isnan(v)) continue;
      lo = fminf(lo, v);
      hi = fmaxf(hi, v);
    }
  }
  if (lo > hi) { lo = 0; hi = 100; }
  const float pad = fmaxf(2.0f, (hi - lo) * 0.08f);
  const float step = (hi - lo) > 200 ? 50.0f : (hi - lo) > 50 ? 10.0f : 5.0f;
  lo = floorf((lo - pad) / step) * step;
  hi = ceilf((hi + pad) / step) * step;
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, (lv_coord_t)(lo * 10), (lv_coord_t)(hi * 10));
  lv_chart_refresh(chart);
  lv_obj_invalidate(chart);  // tick labels depend on chartSpan

  char a[16], b[16], buf[96];
  if (d.run) {
    fmtDur(d.elapsed, a, sizeof(a));
    fmtDur(d.planTotal, b, sizeof(b));
    snprintf(buf, sizeof(buf), "%s  %s / %s", d.progName, a, b);
  } else {
    fmtDur(d.planTotal, b, sizeof(b));
    snprintf(buf, sizeof(buf), "Last 5 min. Preview: %s (%s over full width)", d.progName, b);
  }
  lv_label_set_text(lblChartInfo, buf);
}

// ------------------------------------------------------------ Setup tab
static lv_obj_t* mkRow(lv_obj_t* parent, const char* text) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(row, 2, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = lv_label_create(row);
  lv_label_set_text(l, text);
  return row;
}

static lv_obj_t* mkSection(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_pad_top(l, 6, 0);
  return l;
}

static const uint32_t BAUDS[] = {2400, 4800, 9600, 19200, 38400};

// --- System
static void refreshWifiButtons() {
  setBtnText(btnSsid, wifiSsid[0] ? wifiSsid : "(none: AP only)");
  setBtnText(btnPass, wifiPass[0] ? "********" : "(none)");
  setBtnText(btnApPass, "********");
}

static void refreshSystem() {
  setupLoading = true;
  Settings S;
  {
    Guard g;
    S = gSettings;
  }
  setChecked(swRotate, S.rotate180);
  lv_slider_set_value(sldBright, S.brightness, LV_ANIM_OFF);
  strlcpy(wifiSsid, S.ssid, sizeof(wifiSsid));
  strlcpy(wifiPass, S.pass, sizeof(wifiPass));
  strlcpy(wifiApPass, S.apPass, sizeof(wifiApPass));
  refreshWifiButtons();
  setupLoading = false;
}

static void updateSysInfo() {
  String info = netInfo();
  char host[24];
  {
    Guard g;
    strlcpy(host, gSettings.hostname, sizeof(host));
  }
  lv_label_set_text_fmt(lblSysInfo, "%s\nhttp://%s.local   firmware " FW_VERSION, info.c_str(), host);
}

static void onSystemChanged(lv_event_t* e) {
  if (setupLoading) return;
  lv_obj_t* t = lv_event_get_target(e);
  const lv_event_code_t code = lv_event_get_code(e);
  if (t == swRotate) {
    const bool r = lv_obj_has_state(swRotate, LV_STATE_CHECKED);
    {
      Guard g;
      gSettings.rotate180 = r;
    }
    applyRotation(r);
    saveSettingsQuiet();
  } else if (t == sldBright) {
    const uint8_t b = (uint8_t)lv_slider_get_value(sldBright);
    {
      Guard g;
      gSettings.brightness = b;
    }
    uiSetBrightness(b);
    if (code == LV_EVENT_RELEASED) saveSettingsQuiet();
  }
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
static void onSsid(lv_event_t*) { openTextEditor("Wi-Fi network name (SSID)", wifiSsid, 32, false, applySsid, 0); }
static void onPass(lv_event_t*) { openTextEditor("Wi-Fi password", wifiPass, 64, true, applyPass, 0); }
static void onApPass(lv_event_t*) {
  openTextEditor("Access point password (8+ characters)", wifiApPass, 32, true, applyApPass, 0);
}

static void onScanPick(lv_event_t* e) {
  lv_obj_t* btn = lv_event_get_target(e);
  lv_obj_t* list = lv_obj_get_parent(btn);
  strlcpy(wifiSsid, lv_list_get_btn_text(list, btn), sizeof(wifiSsid));
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
    lv_obj_t* b = lv_list_add_btn(list, LV_SYMBOL_WIFI, name);
    lv_obj_add_event_cb(b, onScanPick, LV_EVENT_CLICKED, nullptr);
    p = e ? e + 1 : nullptr;
  }
  mkButton(modal, "Cancel", 220, 198, 92, 34, onScanCancel);
}

static void onScan(lv_event_t*) {
  if (scanWaiting) return;
  netScanStart();
  scanWaiting = true;
  lv_label_set_text(lblSysInfo, "Scanning for networks...");
}

static void pollScan() {
  if (!scanWaiting) return;
  static char names[640];
  const int n = netScanResult(names, sizeof(names));
  if (n == -1) return;
  scanWaiting = false;
  updateSysInfo();
  // only pop up the list if the user is still on the Setup tab
  if (lv_tabview_get_tab_act(tabview) == TAB_SETUP) showScanResult(names, n);
}

static void onConnect(lv_event_t*) {
  {
    Guard g;
    strlcpy(gSettings.ssid, wifiSsid, sizeof(gSettings.ssid));
    strlcpy(gSettings.pass, wifiPass, sizeof(gSettings.pass));
    strlcpy(gSettings.apPass, wifiApPass, sizeof(gSettings.apPass));
  }
  saveSettingsQuiet();
  netApplyWifi();
  toast("Wi-Fi", wifiSsid[0] ? "Saved. Connecting to the network..." : "Saved. Running as access point only.");
}

static void buildSystem(lv_obj_t* tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab, 2, 0);
  lv_obj_set_style_pad_all(tab, 4, 0);

  mkSection(tab, "Display");
  lv_obj_t* r = mkRow(tab, "Rotate 180 deg");
  swRotate = lv_switch_create(r);
  r = mkRow(tab, "Brightness");
  lv_obj_set_style_pad_right(r, 14, 0);  // room for the slider knob
  sldBright = lv_slider_create(r);
  lv_slider_set_range(sldBright, 10, 255);
  lv_obj_set_width(sldBright, 140);

  mkSection(tab, "Wi-Fi");
  r = mkRow(tab, "Network");
  lv_obj_t* box = lv_obj_create(r);
  lv_obj_set_size(box, 196, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  btnSsid = mkButton(box, "", 0, 0, 140, 34, onSsid);
  lv_label_set_long_mode(lv_obj_get_child(btnSsid, 0), LV_LABEL_LONG_DOT);
  lv_obj_set_width(lv_obj_get_child(btnSsid, 0), 132);
  mkButton(box, LV_SYMBOL_REFRESH, 144, 0, 50, 34, onScan);
  r = mkRow(tab, "Password");
  btnPass = mkButton(r, "", 0, 0, 140, 34, onPass);
  r = mkRow(tab, "AP password");
  btnApPass = mkButton(r, "", 0, 0, 140, 34, onApPass);
  r = mkRow(tab, "");
  mkButton(r, LV_SYMBOL_WIFI " Save + connect", 0, 0, 200, 36, onConnect);

  lblSysInfo = lv_label_create(tab);
  lv_obj_set_width(lblSysInfo, lv_pct(100));
  lv_obj_set_style_text_font(lblSysInfo, &lv_font_montserrat_12, 0);

  lv_obj_add_event_cb(swRotate, onSystemChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sldBright, onSystemChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sldBright, onSystemChanged, LV_EVENT_RELEASED, nullptr);
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
  lv_dropdown_set_selected(ddProfile, S.profile);
  char b[16];
  snprintf(b, sizeof(b), "%u", S.slaveId);
  setBtnText(btnSlave, b);
  for (int i = 0; i < 5; ++i)
    if (BAUDS[i] == S.baud) lv_dropdown_set_selected(ddBaud, i);
  lv_dropdown_set_selected(ddFormat, S.parity == 1 ? 1 : S.parity == 2 ? 2 : (S.stopBits == 2 ? 3 : 0));
  lv_dropdown_set_selected(ddRamp, S.rampMode);
  setChecked(swSoak, S.guaranteedSoak);
  snprintf(b, sizeof(b), "%.1f", S.soakBand);
  setBtnText(btnBand, b);
  setChecked(swLock, S.lockFront);
  lv_dropdown_set_selected(ddEnd, S.endAction);
  char sp[12], rr[12], off[12];
  if (S.reg.sp == REG_UNSET) strcpy(sp, "not set"); else snprintf(sp, sizeof(sp), "%u", S.reg.sp);
  if (S.reg.rampRate == REG_UNSET) strcpy(rr, "not set"); else snprintf(rr, sizeof(rr), "%u", S.reg.rampRate);
  if (S.reg.ctrlOff == REG_UNSET) strcpy(off, "not set"); else snprintf(off, sizeof(off), "%u", S.reg.ctrlOff);
  lv_label_set_text_fmt(lblCtlInfo, "Registers (edit on the web page): SP %s,\nramp r.SP %s, outputs off %s", sp, rr,
                        off);
  setupLoading = false;
}

static void onControllerChanged(lv_event_t* e) {
  if (setupLoading) return;
  lv_obj_t* t = lv_event_get_target(e);
  Guard g;
  Settings& S = gSettings;
  if (t == ddProfile) {
    const uint8_t p = lv_dropdown_get_selected(ddProfile);
    if (p != S.profile) {
      S.profile = p;
      regMapDefaults(S.reg, p);
    }
  } else if (t == ddBaud) {
    S.baud = BAUDS[lv_dropdown_get_selected(ddBaud)];
  } else if (t == ddFormat) {
    const uint16_t f = lv_dropdown_get_selected(ddFormat);
    S.parity = (f == 1) ? 1 : (f == 2) ? 2 : 0;
    S.stopBits = (f == 3) ? 2 : 1;
  } else if (t == ddRamp) {
    S.rampMode = lv_dropdown_get_selected(ddRamp);
  } else if (t == swSoak) {
    S.guaranteedSoak = lv_obj_has_state(swSoak, LV_STATE_CHECKED);
  } else if (t == swLock) {
    S.lockFront = lv_obj_has_state(swLock, LV_STATE_CHECKED);
  } else if (t == ddEnd) {
    S.endAction = lv_dropdown_get_selected(ddEnd);
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
static void onSlave(lv_event_t*) {
  float cur;
  {
    Guard g;
    cur = gSettings.slaveId;
  }
  openNumEditor("Modbus slave address (1-247)", cur, applySlave, 0);
}
static void onBand(lv_event_t*) {
  float cur;
  {
    Guard g;
    cur = gSettings.soakBand;
  }
  openNumEditor("Soak band +/- (C)", cur, applyBand, 0);
}
static void onControllerSave(lv_event_t*) {
  saveSettingsAndNotify();
  toast("Saved", "Controller settings stored. The serial link was reopened.");
}

static void onTestLink(lv_event_t*) {
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

static void buildController(lv_obj_t* tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab, 2, 0);
  lv_obj_set_style_pad_all(tab, 4, 0);

  mkSection(tab, "Serial link");
  lv_obj_t* r = mkRow(tab, "Type");
  ddProfile = lv_dropdown_create(r);
  lv_dropdown_set_options(ddProfile, "PMA KS 4x-1\nGeneric / LCTC");
  lv_obj_set_width(ddProfile, 170);

  r = mkRow(tab, "Slave address");
  btnSlave = mkButton(r, "1", 0, 0, 80, 32, onSlave);

  r = mkRow(tab, "Baud rate");
  ddBaud = lv_dropdown_create(r);
  lv_dropdown_set_options(ddBaud, "2400\n4800\n9600\n19200\n38400");
  lv_obj_set_width(ddBaud, 110);

  r = mkRow(tab, "Format");
  ddFormat = lv_dropdown_create(r);
  lv_dropdown_set_options(ddFormat, "8N1\n8E1\n8O1\n8N2");
  lv_obj_set_width(ddFormat, 110);

  mkSection(tab, "Program behaviour");
  r = mkRow(tab, "Setpoint ramp");
  ddRamp = lv_dropdown_create(r);
  lv_dropdown_set_options(ddRamp, "ESP32 steps\nController r.SP");
  lv_obj_set_width(ddRamp, 170);

  r = mkRow(tab, "Guaranteed soak");
  swSoak = lv_switch_create(r);

  r = mkRow(tab, "Soak band +/-");
  btnBand = mkButton(r, "5", 0, 0, 80, 32, onBand);

  r = mkRow(tab, "Lock front panel in run");
  swLock = lv_switch_create(r);

  r = mkRow(tab, "At end");
  ddEnd = lv_dropdown_create(r);
  lv_dropdown_set_options(ddEnd, "Keep setpoint\nSet end SP\nOutputs off");
  lv_obj_set_width(ddEnd, 170);

  lblCtlInfo = lv_label_create(tab);
  lv_obj_set_width(lblCtlInfo, lv_pct(100));
  lv_obj_set_style_text_font(lblCtlInfo, &lv_font_montserrat_12, 0);

  r = mkRow(tab, "");
  mkButton(r, LV_SYMBOL_REFRESH " Test link", 0, 0, 130, 36, onTestLink);
  mkButton(r, LV_SYMBOL_SAVE " Save", 0, 0, 110, 36, onControllerSave);

  for (lv_obj_t* o : {ddProfile, ddBaud, ddFormat, ddRamp, swSoak, swLock, ddEnd})
    lv_obj_add_event_cb(o, onControllerChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  refreshController();
}

static void buildSetup(lv_obj_t* tab) {
  lv_obj_set_style_pad_all(tab, 0, 0);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* tv = lv_tabview_create(tab, LV_DIR_TOP, 28);
  lv_obj_set_size(tv, lv_pct(100), lv_pct(100));
  lv_obj_clear_flag(lv_tabview_get_content(tv), LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* btns = lv_tabview_get_tab_btns(tv);
  lv_obj_set_style_text_font(btns, &lv_font_montserrat_14, 0);
  buildSystem(lv_tabview_add_tab(tv, "System"));
  buildController(lv_tabview_add_tab(tv, "Controller"));
}

// ------------------------------------------------------------ periodic update
static void onTabChanged(lv_event_t* e) {
  if (lv_event_get_target(e) != tabview) return;  // ignore the nested Setup tabview
  const uint16_t t = lv_tabview_get_tab_act(tabview);
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
    if (!setupLoading) setChecked(swRotate, S.rotate180);
  }
  const uint16_t tab = lv_tabview_get_tab_act(tabview);
  if (tab == TAB_MAIN) updateMain(L, S);
  if (tab == TAB_PROGRAM) updateProgramTab(S);
  if (tab == TAB_CHART && millis() - lastChart >= 1000) {
    lastChart = millis();
    updateChart();
  }
  pollScan();  // always: the scan must be collected so Wi-Fi can reconnect
  if (tab == TAB_SETUP) {
    if (!scanWaiting && millis() - lastInfo > 3000) {
      lastInfo = millis();
      updateSysInfo();
    }
  }
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
  lv_disp_draw_buf_init(&drawBuf, buf1, nullptr, SCREEN_W * 24);
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = SCREEN_W;
  dispDrv.ver_res = SCREEN_H;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_t* disp = lv_disp_drv_register(&dispDrv);
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchCb;
  lv_indev_drv_register(&indevDrv);

  lv_theme_t* th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                         lv_palette_main(LV_PALETTE_DEEP_ORANGE), true, &lv_font_montserrat_14);
  lv_disp_set_theme(disp, th);

  tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 32);
  lv_obj_add_event_cb(tabview, onTabChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t* tMain = lv_tabview_add_tab(tabview, "Main");
  lv_obj_t* tProg = lv_tabview_add_tab(tabview, "Program");
  lv_obj_t* tChart = lv_tabview_add_tab(tabview, "Chart");
  lv_obj_t* tSetup = lv_tabview_add_tab(tabview, "Setup");
  for (lv_obj_t* t : {tMain, tProg, tChart}) lv_obj_set_style_pad_all(t, 4, 0);
  // swiping between tabs conflicts with the table/chart; use the tab buttons
  lv_obj_clear_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);

  buildMain(tMain);
  buildProgram(tProg);
  buildChart(tChart);
  buildSetup(tSetup);

  lv_timer_create(uiTimer, 250, nullptr);
}

void uiLoop() { lv_timer_handler(); }
