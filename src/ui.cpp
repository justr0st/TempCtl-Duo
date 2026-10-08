// LVGL 9.6 user interface.
//
// The screen layout is an LVGL Pro project in ui/ (XML). LVGL Pro exports it
// as C code (ui/**/*_gen.c, compiled as the library in ui/library.json), which
// creates the screen and the popups. This file only contains the behaviour:
// it looks widgets up by their `name`, attaches the event handlers (UI_EVENTS)
// and updates them.
//
// Display and touch drivers: lcd.cpp (LVGL's ST7796 / ILI9341 driver),
// touch_ft6336.cpp (S3) / touch_xpt2046.cpp (CYD). During the S3 bring-up the
// 320x240 TempCtl screens run in the top left corner of the 480x320 display
// (UI_LEGACY_W/H in board_s3.h), next to a small bring-up panel.
#include "ui.h"
#include "config.h"
#ifndef UI_V2  // the S3 has the new touch UI (tui_*.cpp)
#include <lvgl.h>
#include <math.h>
#include "app_state.h"
#include "controller.h"
#include "lcd.h"
#include "net.h"
#include "touch.h"
#include "ui_gen.h"  // UI code exported by LVGL Pro (ui/)

// ------------------------------------------------------------ drivers
static lv_display_t* disp = nullptr;
static uint8_t appliedBrightness = 0;
static bool rotated = false;
static void bringupUpdate();  // S3 bring-up panel (see "bring-up panel" below)

// The touch drivers report screen coordinates (as the user sees the image, SCREEN_W x SCREEN_H).
// LVGL rotates pointer input by the display rotation itself (lv_display_rotate_point), so give it
// the point in the panel's native orientation; otherwise it is rotated twice and lands outside
// the screen ("Y is 269 which is greater than ver. res" on the CYD).
static void touchToNative(int32_t& x, int32_t& y) {
  const int32_t sx = x, sy = y;
  switch (disp ? lv_display_get_rotation(disp) : LV_DISPLAY_ROTATION_0) {
    case LV_DISPLAY_ROTATION_90:  // LVGL: x' = H - 1 - y, y' = x  (H = native height)
      x = sy;
      y = LCD_NATIVE_H - 1 - sx;
      break;
    case LV_DISPLAY_ROTATION_270:  // LVGL: x' = y, y' = W - 1 - x  (W = native width)
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
  lcdSetRotation(r);  // landscape / landscape upside down (MADCTL)
  touchSetRotation(r);
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

// Text from ui/translations.xml (tag -> text of the current language)
static const char* T(const char* tag) { return lv_tr(tag); }

// Translated text with %1..%3 replaced by the given strings. The values are
// formatted by the caller, so a changed text can never break the formatting.
// (Not {1}: LVGL Pro Editor reads an attribute value in braces as an expression.)
static const char* trf(char* out, size_t n, const char* tag, const char* a1 = nullptr, const char* a2 = nullptr,
                       const char* a3 = nullptr) {
  const char* args[3] = {a1, a2, a3};
  const char* s = lv_tr(tag);
  size_t o = 0;
  while (*s && o + 1 < n) {
    if (s[0] == '%' && s[1] >= '1' && s[1] <= '3') {
      for (const char* a = args[s[1] - '1']; a && *a && o + 1 < n; ++a) out[o++] = *a;
      s += 2;
    } else {
      out[o++] = *s++;
    }
  }
  out[o] = 0;
  return out;
}

// Step time as shown in the program table: whole minutes as "N min", else "N s".
static void fmtStepTime(uint32_t sec, char* out, size_t n) {
  char num[12];
  if (sec == 0) {
    snprintf(out, n, "0");
  } else if (sec % 60 == 0) {
    snprintf(num, sizeof(num), "%lu", (unsigned long)(sec / 60));
    trf(out, n, "time_min", num);
  } else {
    snprintf(num, sizeof(num), "%lu", (unsigned long)sec);
    trf(out, n, "time_s", num);
  }
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

// Popups are LVGL Pro components (ui/components/popups), created on the top
// layer with their generated create function when needed.
typedef lv_obj_t* (*PopupCreate)(lv_obj_t* parent);
static lv_obj_t* popupCreate(PopupCreate create) {
  lv_obj_t* p = create(lv_layer_top());
  if (!p) Serial.println("[ui] popup could not be created");
  else lv_obj_set_pos(p, 0, 0);
  return p;
}
static lv_obj_t* childByName(lv_obj_t* parent, const char* name) {
  return parent ? lv_obj_find_by_name(parent, name) : nullptr;
}

static void dialogCloseClicked(lv_event_t* e) { lv_obj_delete_async((lv_obj_t*)lv_event_get_user_data(e)); }

// Message box (component "dialog") with up to 3 footer buttons ("" terminated
// list); cb gets the dialog as user data. The text label is returned through
// textOut. Close it with lv_obj_delete / lv_obj_delete_async.
static lv_obj_t* msgBox(const char* title, const char* text, const char* const* buttons = nullptr,
                        lv_event_cb_t cb = nullptr, bool closeBtn = true, lv_obj_t** textOut = nullptr) {
  lv_obj_t* m = popupCreate(uiGenDialog);
  if (!m) return nullptr;
  setText(childByName(m, "dlg_title"), title);
  lv_obj_t* t = childByName(m, "dlg_text");
  setText(t, text);
  if (textOut) *textOut = t;
  lv_obj_t* close = childByName(m, "dlg_close");
  if (close) {
    if (closeBtn) lv_obj_add_event_cb(close, dialogCloseClicked, LV_EVENT_CLICKED, m);
    else lv_obj_set_hidden(close, true);
  }
  int n = 0;
  for (int i = 1; i <= 3; ++i) {
    char name[12];
    snprintf(name, sizeof(name), "dlg_btn%d", i);
    lv_obj_t* b = childByName(m, name);
    if (!b) continue;
    const char* bt = (buttons && n == i - 1 && buttons[n] && buttons[n][0]) ? buttons[n] : nullptr;
    if (!bt) {
      lv_obj_set_hidden(b, true);
      continue;
    }
    ++n;
    setText(lv_obj_get_child(b, 0), bt);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, m);
  }
  lv_obj_t* footer = childByName(m, "dlg_footer");
  if (n == 0 && footer) lv_obj_set_hidden(footer, true);
  return m;
}

static void toast(const char* title, const char* text) { msgBox(title, text); }

// which dialog button sent the event: 1..3 (dlg_btn1..3), 0 = other
static int dialogButton(lv_event_t* e) {
  const char* n = lv_obj_get_name(evTarget(e));
  return (n && strncmp(n, "dlg_btn", 7) == 0) ? n[7] - '0' : 0;
}

static bool programActive(RunState s) { return s == RunState::Running || s == RunState::Paused; }

// "1 Name\n2 Name ..." for the program dropdowns
static void buildProgramOptions(char* out, size_t n) {
  out[0] = 0;
  static ProgSummary list[MAX_PROGRAMS];
  programSummaries(list);
  for (int i = 0; i < MAX_PROGRAMS; ++i) {
    char line[64];
    if (list[i].used) snprintf(line, sizeof(line), "%s%d %s", i ? "\n" : "", i + 1, list[i].name);
    else snprintf(line, sizeof(line), "%s%d %s", i ? "\n" : "", i + 1, T("prog_empty"));
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
static lv_obj_t* scanPopup = nullptr;   // the Wi-Fi list popup while it is open

static void modalClose() {
  if (modal) lv_obj_delete_async(modal);
  modal = nullptr;
  scanPopup = nullptr;
  modalTa = nullptr;
  modalUnits = nullptr;
}

static lv_obj_t* modalCreate(PopupCreate component, const char* titleName, const char* title) {
  modal = popupCreate(component);
  setText(childByName(modal, titleName), title);
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
  if (!modalCreate(uiGenNumEditor, "num_title", title)) return;
  numApply = cb;
  numArg = arg;
  modalTa = childByName(modal, "num_ta");
  modalUnits = childByName(modal, "num_units");
  lv_obj_t* kb = childByName(modal, "num_kb");
  if (!modalTa || !kb) {
    Serial.println("[ui] num_editor needs num_ta and num_kb");
    modalClose();
    return;
  }
  lv_textarea_set_accepted_chars(modalTa, timeSec ? "0123456789." : "0123456789.-");
  lv_textarea_set_max_length(modalTa, 8);
  char buf[16];
  if (timeSec) {
    const uint32_t s = (uint32_t)lroundf(cur);
    const bool mins = (s % 60 == 0);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(mins ? s / 60 : s));
    if (modalUnits) {
      lv_buttonmatrix_set_one_checked(modalUnits, true);
      lv_buttonmatrix_clear_button_ctrl_all(modalUnits, LV_BUTTONMATRIX_CTRL_CHECKED);
      lv_buttonmatrix_set_button_ctrl(modalUnits, mins ? 0 : 1, LV_BUTTONMATRIX_CTRL_CHECKED);
    }
  } else {
    if (modalUnits) lv_obj_set_hidden(modalUnits, true);
    modalUnits = nullptr;
    snprintf(buf, sizeof(buf), "%.1f", cur);
    const size_t l = strlen(buf);  // drop a trailing ".0" for integer values
    if (l > 2 && strcmp(buf + l - 2, ".0") == 0) buf[l - 2] = 0;
  }
  lv_textarea_set_text(modalTa, buf);
  lv_keyboard_set_textarea(kb, modalTa);
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
  if (!modalCreate(uiGenTextEditor, "text_title", title)) return;
  textApply = cb;
  textArg = arg;
  modalTa = childByName(modal, "text_ta");
  lv_obj_t* kb = childByName(modal, "text_kb");
  if (!modalTa || !kb) {
    Serial.println("[ui] text_editor needs text_ta and text_kb");
    modalClose();
    return;
  }
  lv_textarea_set_max_length(modalTa, maxLen);
  lv_textarea_set_password_mode(modalTa, password);
  lv_textarea_set_text(modalTa, cur);
  lv_keyboard_set_textarea(kb, modalTa);
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
static uint32_t loadedGen = 0;  // programStoreGen() when uiProg was loaded
static int selRow = -1;         // selected step (-1 = none)
static bool progDirty = false;
static int pendingSwitch = -1;  // program to switch to after the save/discard question
// Chart tab
static lv_obj_t *chart, *chartYScale, *chartXScale, *lblChartInfo;
static lv_chart_series_t *serPv, *serSp, *serPlan;
// Setup tab
static lv_obj_t *swRotate, *sldBright, *ddTheme, *swDark, *lblSsid, *lblPass, *lblApPass, *lblSysInfo;
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
extern "C" void ui_on_skip(lv_event_t*) { sendCmd(CMD_NEXT); }  // Continue in a program pause

static void onStopAnswer(lv_event_t* e) {
  lv_obj_t* m = (lv_obj_t*)lv_event_get_user_data(e);
  if (dialogButton(e) == 1) sendCmd(CMD_STOP);  // btn1 = Stop
  lv_obj_delete_async(m);
}

extern "C" void ui_on_stop(lv_event_t*) {
  const char* btns[] = {T("btn_stop"), T("btn_cancel"), ""};
  msgBox(T("stop_title"), T("stop_text"), btns, onStopAnswer, false);
}

extern "C" void ui_on_main_prog(lv_event_t*) { selectProgram(lv_dropdown_get_selected(ddMainProg)); }

static void applyManualSp(float v, int) {
  setManualSp(v);
  char b[16];
  float cur;
  {
    Guard g;
    cur = gSettings.ch[ENGINE_CH].manualSp;
  }
  fmtTemp(true, cur, b, sizeof(b));
  setText(lblManSp, b);
}

extern "C" void ui_on_manual_sp(lv_event_t*) {
  float cur;
  char title[80], lo[12], hi[12];
  {
    Guard g;
    cur = gSettings.ch[ENGINE_CH].manualSp;
    snprintf(lo, sizeof(lo), "%.0f", gSettings.ch[ENGINE_CH].spMin);
    snprintf(hi, sizeof(hi), "%.0f", gSettings.ch[ENGINE_CH].spMax);
  }
  trf(title, sizeof(title), "ed_manual_sp", lo, hi);
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
}

static void updateMain(const LiveData& L, const Settings& S) {
  char a[24], b[24], buf[128];
  if (L.pvValid) fmtTemp(true, L.pv, a, sizeof(a));
  else if (L.commOk && L.pvFlag != VAL_OK) strlcpy(a, L.pvFlag == VAL_SENSOR_FAULT ? T("pv_sensor") : "---.-", sizeof(a));
  else strlcpy(a, "---.-", sizeof(a));
  setText(lblPv, a);

  fmtTemp(L.spEffValid, L.spEff, a, sizeof(a));
  setText(lblSp, trf(buf, sizeof(buf), "sp_value", a));

  const bool active = programActive(L.state);
  if (active) {
    fmtTemp(!isnan(L.target), L.target, a, sizeof(a));
    setText(lblTarget, trf(buf, sizeof(buf), "step_target", a));
  } else {
    setText(lblTarget, S.ch[ENGINE_CH].mb.reg.sp == REG_UNSET ? T("sp_reg_unset") : "");
  }

  // link dot: the XML style for state user_1 shows "no answer"
  if (ledLink && lv_obj_has_state(ledLink, LV_STATE_USER_1) == L.commOk) {
    if (L.commOk) lv_obj_remove_state(ledLink, LV_STATE_USER_1);
    else lv_obj_add_state(ledLink, LV_STATE_USER_1);
  }

  switch (L.state) {
    case RunState::Running:
    case RunState::Paused:
      snprintf(a, sizeof(a), "%d", L.step + 1);
      snprintf(b, sizeof(b), "%d", L.stepCount);
      setText(lblState, trf(buf, sizeof(buf), L.state == RunState::Paused ? "st_paused" : "st_step", a, b));
      break;
    default:
      if (L.manualOn) setText(lblState, T("st_manual"));
      else if (L.state == RunState::Finished) setText(lblState, T("st_finished"));
      else if (L.state == RunState::Aborted) setText(lblState, T("st_stopped"));
      else setText(lblState, T("st_idle"));
  }
  if (active) {
    fmtDur(L.phaseDur - L.phaseEl, a, sizeof(a));
    if (L.soakWait) setText(lblPhase, T("ph_soak_wait"));
    else if (L.progPause) setText(lblPhase, L.pauseMsg ? "Action required - Next = Continue" : "Program pause - Next = Continue");
    else setText(lblPhase, trf(buf, sizeof(buf), L.phase == Stage::Ramp ? "ph_ramp" : "ph_hold", a));
    fmtDur(L.total - L.elapsed, a, sizeof(a));
    setText(lblTotal, trf(buf, sizeof(buf), "run_left", L.runName, a));
  } else {
    setText(lblPhase, L.manualOn ? (L.manualSynced ? T("man_written") : T("man_writing")) : "");
    ProgSummary sel;
    if (!programSummary(S.selProgram, sel)) strlcpy(sel.name, T("prog_empty"), sizeof(sel.name));
    fmtDur(sel.used ? sel.total : 0, a, sizeof(a));
    setText(lblTotal, trf(buf, sizeof(buf), "prog_total", sel.name, a));
  }
  if (barProg) {  // progress in the bar's own range (min/max in the XML)
    const int32_t lo = lv_bar_get_min_value(barProg), hi = lv_bar_get_max_value(barProg);
    float f = 0;
    if (active && L.total > 0) f = L.elapsed / L.total;
    else if (!active && L.state == RunState::Finished) f = 1;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    lv_bar_set_value(barProg, lo + (int32_t)lroundf(f * (hi - lo)), LV_ANIM_OFF);
  }
  if (L.outValid) {
    snprintf(a, sizeof(a), "%.0f", L.out);
    setText(lblOut, trf(buf, sizeof(buf), "out_value", a));
  } else {
    setText(lblOut, "");
  }
  setText(lblMode, (L.statusValid && S.ch[ENGINE_CH].mb.profile == PROFILE_PMA_KS4X) ? pmaModeText(L.status) : "");

  if (!L.commOk) snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", L.lastErr);
  else if (!L.spSynced && (active || L.manualOn)) snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", L.lastErr);
  else strlcpy(buf, L.runMsg, sizeof(buf));
  setText(lblMsg, buf);

  // manual setpoint widgets
  if (!modal) {
    fmtTemp(true, S.ch[ENGINE_CH].manualSp, a, sizeof(a));
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
    lv_table_set_cell_value(table, 0, COL_TARGET, T("tbl_no_steps"));
    lv_table_set_cell_value(table, 0, COL_RAMP, T("tbl_tap_plus"));
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
  // TempCtl Duo: read-only view (bottom channel as steps) until the new touch UI (step 6)
  char d[16], info[96];
  fmtDur(programTotal(uiProg), d, sizeof(d));
  snprintf(info, sizeof(info), "%d steps, %s - read only (Bottom channel)", uiProg.count, d);
  setText(lblProgInfo, info);
  setText(lblProgUnsaved, "");
  setEnabled(btnAdd, false);
  setEnabled(btnDel, false);
  setEnabled(btnUp, false);
  setEnabled(btnDown, false);
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
  loadedGen = programStoreGen();
  loadLegacyView(idx, uiProg);
  editIdx = idx;
  progDirty = false;
  selRow = uiProg.count ? 0 : -1;
}

static void markDirty() {
  progDirty = true;
  refreshTable();
}

static void readOnlyNote() {
  toast("Read only", "Programs are edited on the new web page / touch UI (coming). Until then: web API POST /api/program.");
}

static void applyCell(float v, int arg) {
  const int step = arg / 10, col = arg % 10;
  if (step < 0 || step >= uiProg.count) return;
  Step& s = uiProg.s[step];
  float lo, hi;
  {
    Guard g;
    lo = gSettings.ch[ENGINE_CH].spMin;
    hi = gSettings.ch[ENGINE_CH].spMax;
  }
  const uint32_t sec = (v <= 0) ? 0 : (uint32_t)lroundf(fminf(v, (float)PROG_MAX_SEC));
  if (col == COL_TARGET) s.target = constrain(v, lo, hi);
  if (col == COL_RAMP) s.rampSec = sec;
  if (col == COL_HOLD) s.holdSec = sec;
  markDirty();
}

// highlight the selected row, grey step numbers (colors from ui/globals.xml)
static lv_color_t colRowSelected, colStepNo;

static void onTableDraw(lv_event_t* e) {
  lv_draw_task_t* t = lv_event_get_draw_task(e);
  lv_draw_dsc_base_t* base = (lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(t);
  if (!base || base->part != LV_PART_ITEMS) return;
  const int row = (int)base->id1, col = (int)base->id2;
  const lv_draw_task_type_t type = lv_draw_task_get_type(t);
  if (type == LV_DRAW_TASK_TYPE_FILL && row == selRow && uiProg.count) {
    lv_draw_fill_dsc_t* f = lv_draw_task_get_fill_dsc(t);
    if (f) {
      f->color = colRowSelected;
      f->opa = LV_OPA_COVER;
    }
  } else if (type == LV_DRAW_TASK_TYPE_LABEL && col == COL_NUM) {
    lv_draw_label_dsc_t* l = lv_draw_task_get_label_dsc(t);
    if (l) l->color = colStepNo;
  }
}

extern "C" void ui_on_table(lv_event_t*) {
  uint32_t row, col;
  lv_table_get_selected_cell(table, &row, &col);
  if (row == LV_TABLE_CELL_NONE || (int)row >= uiProg.count) return;
  selRow = row;
  refreshTable();
  if (col == COL_NUM) return;  // the number column only selects the row
  readOnlyNote();
  return;
  char title[80], n[12];
  snprintf(n, sizeof(n), "%lu", (unsigned long)row + 1);
  if (col == COL_TARGET) {
    openNumEditor(trf(title, sizeof(title), "ed_step_target", n), uiProg.s[row].target, applyCell, row * 10 + col);
  } else if (col == COL_RAMP) {
    openNumEditor(trf(title, sizeof(title), "ed_step_ramp", n), uiProg.s[row].rampSec, applyCell, row * 10 + col,
                  true);
  } else if (col == COL_HOLD) {
    openNumEditor(trf(title, sizeof(title), "ed_step_hold", n), uiProg.s[row].holdSec, applyCell, row * 10 + col,
                  true);
  }
}

extern "C" void ui_on_add_step(lv_event_t*) {
  readOnlyNote();
  return;
  Step st{100, 10 * 60, 10 * 60};
  float lo, hi;
  {
    Guard g;
    lo = gSettings.ch[ENGINE_CH].spMin;
    hi = gSettings.ch[ENGINE_CH].spMax;
  }
  if (selRow >= 0 && selRow < uiProg.count) st = uiProg.s[selRow];
  else if (uiProg.count) st = uiProg.s[uiProg.count - 1];
  st.target = constrain(st.target, lo, hi);
  const int at = (selRow >= 0) ? selRow + 1 : uiProg.count;
  if (programInsertStep(uiProg, at, st)) selRow = at;
  markDirty();
  scrollToRow(selRow);
}

extern "C" void ui_on_del_step(lv_event_t*) { readOnlyNote(); }

static void moveStep(int) { readOnlyNote(); }
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
  readOnlyNote();
  return;
  char title[64], n[12];
  snprintf(n, sizeof(n), "%d", editIdx + 1);
  openTextEditor(trf(title, sizeof(title), "ed_prog_name", n), uiProg.name, PROG_NAME_LEN - 1, false, applyName, 0);
}

static void saveEdited() {  // read-only view: nothing to save
  progDirty = false;
  refreshTable();
}

extern "C" void ui_on_prog_save(lv_event_t*) { readOnlyNote(); }

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
  const int b = dialogButton(e);  // 1 = Save, 2 = Discard, 3 = Cancel
  if (b == 1) {
    saveEdited();
    switchToProgram(pendingSwitch);
  } else if (b == 2) {
    switchToProgram(pendingSwitch);
  } else {
    lv_dropdown_set_selected(ddEditProg, editIdx);
  }
  pendingSwitch = -1;
  lv_obj_delete_async(m);
}

extern "C" void ui_on_edit_prog(lv_event_t*) {
  const int idx = lv_dropdown_get_selected(ddEditProg);
  if (idx == editIdx) return;
  if (!progDirty) {
    switchToProgram(idx);
    return;
  }
  pendingSwitch = idx;
  const char* btns[] = {T("btn_save"), T("btn_discard"), T("btn_cancel"), ""};
  char text[96];
  msgBox(T("unsaved_title"), trf(text, sizeof(text), "unsaved_text", uiProg.name), btns, onSwitchAnswer, false);
}

static void bindProgram() {
  colRowSelected = uiGenColor(UI_COLOR_ROW_SELECTED);  // constants from ui/globals.xml
  colStepNo = uiGenColor(UI_COLOR_MUTED);
  ddEditProg = find("prog_select");
  table = find("prog_table");
  lblProgInfo = find("prog_info");
  lblProgUnsaved = find("prog_unsaved");
  btnAdd = find("prog_add");
  btnDel = find("prog_del");
  btnUp = find("prog_up");
  btnDown = find("prog_down");
  if (table) {
    lv_obj_set_send_draw_task_events(table, true);
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
  if (!progDirty && !modal && pendingSwitch < 0 && (S.selProgram != editIdx || programStoreGen() != loadedGen)) {
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

static float pairMean(const Tenth16* a, int k) {
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

  char a[16], b[16], buf[128];
  if (d.run) {
    fmtDur(d.elapsed, a, sizeof(a));
    fmtDur(d.planTotal, b, sizeof(b));
    trf(buf, sizeof(buf), "chart_run_info", d.progName, a, b);
  } else {
    fmtDur(d.planTotal, b, sizeof(b));
    trf(buf, sizeof(buf), "chart_idle_info", d.progName, b);
  }
  setText(lblChartInfo, buf);
}

// ------------------------------------------------------------ Setup tab
static const uint32_t BAUDS[] = {2400, 4800, 9600, 19200, 38400};

// --- System
static void refreshWifiButtons() {
  setText(lblSsid, wifiSsid[0] ? wifiSsid : T("wifi_none"));
  setText(lblPass, wifiPass[0] ? "********" : T("pass_none"));
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
  if (ddTheme) lv_dropdown_set_selected(ddTheme, S.themePalette);
  setChecked(swDark, S.themeDark);
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
  char buf[192];
  setText(lblSysInfo, trf(buf, sizeof(buf), "sys_info", info.c_str(), host, FW_VERSION));
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
  const int32_t v = lv_slider_get_value(sldBright);  // range from the XML
  const uint8_t b = (uint8_t)(v < 1 ? 1 : v > 255 ? 255 : v);
  {
    Guard g;
    gSettings.brightness = b;
  }
  uiSetBrightness(b);
}

extern "C" void ui_on_brightness_done(lv_event_t*) {
  if (!setupLoading) saveSettingsQuiet();
}

// Colour palette + dark / light mode (System > Display). Recolours the styles
// from ui/globals.xml (uiGenSetPalette), the LVGL theme (buttons, switches,
// dropdown lists, keyboards ...), the chart series and the colours the program
// table draws itself. Popups take the colours when they are opened.
static int appliedPalette = -1, appliedDark = -1;

static void applyTheme(int palette, bool dark) {
  uiGenSetPalette(palette, dark);
  lv_theme_default_init(disp, uiGenColor(UI_COLOR_ACCENT), uiGenColor(UI_COLOR_ACCENT2), dark,
                        &lv_font_montserrat_14);
  colRowSelected = uiGenColor(UI_COLOR_ROW_SELECTED);
  colStepNo = uiGenColor(UI_COLOR_MUTED);
  if (chart && serPlan) {
    lv_chart_set_series_color(chart, serPv, uiGenColor(UI_COLOR_PV));
    lv_chart_set_series_color(chart, serSp, uiGenColor(UI_COLOR_SP));
    lv_chart_set_series_color(chart, serPlan, uiGenColor(UI_COLOR_PLAN));
  }
  if (screen) lv_obj_invalidate(screen);
  appliedPalette = palette;
  appliedDark = dark;
}

extern "C" void ui_on_theme(lv_event_t*) {
  if (setupLoading || !ddTheme) return;
  const uint8_t p = (uint8_t)lv_dropdown_get_selected(ddTheme);
  bool dark;
  {
    Guard g;
    gSettings.themePalette = p;
    dark = gSettings.themeDark;
  }
  applyTheme(p, dark);
  saveSettingsQuiet();
}

extern "C" void ui_on_dark(lv_event_t*) {
  if (setupLoading || !swDark) return;
  const bool dark = lv_obj_has_state(swDark, LV_STATE_CHECKED);
  uint8_t p;
  {
    Guard g;
    gSettings.themeDark = dark;
    p = gSettings.themePalette;
  }
  applyTheme(p, dark);
  saveSettingsQuiet();
}

const char* uiPaletteNames() { return uiGenPaletteNames(); }
int uiPaletteCount() { return uiGenPaletteCount(); }

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
    toast(T("too_short_title"), T("too_short_text"));
    return;
  }
  strlcpy(wifiApPass, t, sizeof(wifiApPass));
  refreshWifiButtons();
}
extern "C" void ui_on_ssid(lv_event_t*) { openTextEditor(T("ed_ssid"), wifiSsid, 32, false, applySsid, 0); }
extern "C" void ui_on_pass(lv_event_t*) { openTextEditor(T("ed_pass"), wifiPass, 64, true, applyPass, 0); }
extern "C" void ui_on_ap_pass(lv_event_t*) {
  openTextEditor(T("ed_ap_pass"), wifiApPass, 32, true, applyApPass, 0);
}

static void onScanPick(lv_event_t* e) {
  lv_obj_t* l = childByName(evTarget(e), "wifi_ssid");
  if (l) strlcpy(wifiSsid, lv_label_get_text(l), sizeof(wifiSsid));
  refreshWifiButtons();
  modalClose();
}
static void onScanCancel(lv_event_t*) { modalClose(); }

// The Wi-Fi list popup opens as soon as Scan is tapped and shows a spinner
// (scan_spinner) until the scan is done; then the spinner is hidden and the
// networks are listed. Cancel closes it at any time (the scan keeps running
// in the background and is collected by pollScan()).
static void scanPopupOpen() {
  if (modal) return;
  if (!modalCreate(uiGenWifiList, "scan_title", T("scan_running"))) return;
  scanPopup = modal;
  lv_obj_t* cancel = childByName(modal, "scan_cancel");
  if (cancel) lv_obj_add_event_cb(cancel, onScanCancel, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* list = childByName(modal, "scan_list");
  if (list) lv_obj_clean(list);  // remove the example rows
  lv_obj_t* spinner = childByName(modal, "scan_spinner");
  if (spinner) lv_obj_set_hidden(spinner, false);
}

static void scanPopupShowResult(const char* names, int count) {
  if (!scanPopup || modal != scanPopup) return;  // closed meanwhile
  setText(childByName(modal, "scan_title"),
          count > 0 ? T("scan_pick") : count == 0 ? T("scan_none") : T("scan_failed"));
  lv_obj_t* spinner = childByName(modal, "scan_spinner");
  if (spinner) lv_obj_set_hidden(spinner, true);
  lv_obj_t* list = childByName(modal, "scan_list");
  if (!list) return;
  const char* p = names;
  for (int i = 0; i < count && p && *p; ++i) {
    const char* e = strchr(p, '\n');
    char name[33];
    snprintf(name, sizeof(name), "%.*s", (int)(e ? e - p : strlen(p)), p);
    lv_obj_t* b = uiGenWifiItem(list, name);
    if (b) lv_obj_add_event_cb(b, onScanPick, LV_EVENT_CLICKED, nullptr);
    p = e ? e + 1 : nullptr;
  }
}

extern "C" void ui_on_scan(lv_event_t*) {
  if (!scanWaiting) {  // tapped again while a scan runs: just reopen the popup
    netScanStart();
    scanWaiting = true;
    setText(lblSysInfo, T("scan_running"));
  }
  scanPopupOpen();
}

static void pollScan() {
  if (!scanWaiting) return;
  static char names[640];
  const int n = netScanResult(names, sizeof(names));
  if (n == -1) return;
  scanWaiting = false;
  updateSysInfo();
  scanPopupShowResult(names, n);
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
    lv_obj_delete(wifiBox);  // triggers onWifiBoxDeleted
    return;
  }
  if (!wifiBoxWaiting) return;
  char buf[200];
  if (netConnectedSinceApply()) {
    setText(wifiBoxLabel, trf(buf, sizeof(buf), "wifi_connected", wifiSsid, netStaIp().c_str()));
    wifiBoxWaiting = false;
    wifiBoxCloseAt = now + 2500;
    updateSysInfo();
  } else if (now - wifiBoxMs > 30000) {
    setText(wifiBoxLabel, trf(buf, sizeof(buf), "wifi_failed", wifiSsid));
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
  if (wifiBox) lv_obj_delete(wifiBox);
  char buf[128];
  if (wifiSsid[0]) trf(buf, sizeof(buf), "wifi_connecting", wifiSsid);
  else strlcpy(buf, T("wifi_ap_only"), sizeof(buf));
  wifiBox = msgBox(T("wifi_title"), buf, nullptr, nullptr, true, &wifiBoxLabel);
  if (wifiBox) lv_obj_add_event_cb(wifiBox, onWifiBoxDeleted, LV_EVENT_DELETE, nullptr);
  wifiBoxMs = millis();
  wifiBoxWaiting = wifiSsid[0] != 0;
  wifiBoxCloseAt = wifiSsid[0] ? 0 : wifiBoxMs + 2500;
}

static void bindSystem() {
  swRotate = find("sys_rotate");
  sldBright = find("sys_bright");
  ddTheme = find("sys_theme");
  if (ddTheme) lv_dropdown_set_options(ddTheme, uiGenPaletteNames());
  swDark = find("sys_dark");
  lblSsid = find("sys_ssid_text");
  lblPass = find("sys_pass_text");
  lblApPass = find("sys_ap_pass_text");
  lblSysInfo = find("sys_info");
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
  if (ddProfile) lv_dropdown_set_selected(ddProfile, S.ch[ENGINE_CH].mb.profile);
  char b[16];
  snprintf(b, sizeof(b), "%u", S.ch[ENGINE_CH].mb.slaveId);
  setText(lblSlave, b);
  for (int i = 0; i < 5; ++i)
    if (BAUDS[i] == S.baud && ddBaud) lv_dropdown_set_selected(ddBaud, i);
  if (ddFormat) lv_dropdown_set_selected(ddFormat, S.parity == 1 ? 1 : S.parity == 2 ? 2 : (S.stopBits == 2 ? 3 : 0));
  if (ddRamp) lv_dropdown_set_selected(ddRamp, S.ch[ENGINE_CH].mb.rampMode);
  setChecked(swSoak, false);  // guaranteed soak is gone in TempCtl Duo
  setEnabled(swSoak, false);
  snprintf(b, sizeof(b), "%.1f", S.bottomBand);  // field reused: "bottom reached" band
  setText(lblBand, b);
  setChecked(swLock, S.ch[ENGINE_CH].mb.lockFront);
  if (ddEnd) lv_dropdown_set_selected(ddEnd, S.ch[ENGINE_CH].endAction);
  char sp[24], rr[24], off[24], info[160];
  if (S.ch[ENGINE_CH].mb.reg.sp == REG_UNSET) strlcpy(sp, T("reg_not_set"), sizeof(sp)); else snprintf(sp, sizeof(sp), "%u", S.ch[ENGINE_CH].mb.reg.sp);
  if (S.ch[ENGINE_CH].mb.reg.rampRate == REG_UNSET) strlcpy(rr, T("reg_not_set"), sizeof(rr)); else snprintf(rr, sizeof(rr), "%u", S.ch[ENGINE_CH].mb.reg.rampRate);
  if (S.ch[ENGINE_CH].mb.reg.ctrlOff == REG_UNSET) strlcpy(off, T("reg_not_set"), sizeof(off)); else snprintf(off, sizeof(off), "%u", S.ch[ENGINE_CH].mb.reg.ctrlOff);
  setText(lblCtlInfo, trf(info, sizeof(info), "ctl_regs", sp, rr, off));
  setupLoading = false;
}

extern "C" void ui_on_controller_changed(lv_event_t* e) {
  if (setupLoading) return;
  lv_obj_t* t = evTarget(e);
  Guard g;
  Settings& S = gSettings;
  if (t == ddProfile) {
    const uint8_t p = lv_dropdown_get_selected(ddProfile) ? PROFILE_GENERIC : PROFILE_PMA_KS4X;
    if (p != S.ch[ENGINE_CH].mb.profile) {
      S.ch[ENGINE_CH].mb.profile = p;
      regMapDefaults(S.ch[ENGINE_CH].mb.reg, p);
    }
  } else if (t == ddBaud) {
    const uint32_t i = lv_dropdown_get_selected(ddBaud);
    if (i < 5) S.baud = BAUDS[i];
  } else if (t == ddFormat) {
    const uint32_t f = lv_dropdown_get_selected(ddFormat);
    S.parity = (f == 1) ? 1 : (f == 2) ? 2 : 0;
    S.stopBits = (f == 3) ? 2 : 1;
  } else if (t == ddRamp) {
    S.ch[ENGINE_CH].mb.rampMode = lv_dropdown_get_selected(ddRamp) ? RAMP_CONTROLLER : RAMP_HOST;
  } else if (t == swLock) {
    S.ch[ENGINE_CH].mb.lockFront = lv_obj_has_state(swLock, LV_STATE_CHECKED);
  } else if (t == ddEnd) {
    const uint32_t a = lv_dropdown_get_selected(ddEnd);
    if (a <= END_OUTPUTS_OFF) S.ch[ENGINE_CH].endAction = a;
  }
}

static void applySlave(float v, int) {
  {
    Guard g;
    gSettings.ch[ENGINE_CH].mb.slaveId = (uint8_t)constrain((int)v, 1, 247);
  }
  refreshController();
}
static void applyBand(float v, int) {
  {
    Guard g;
    gSettings.bottomBand = constrain(v, 0.5f, 50.0f);
  }
  refreshController();
}
extern "C" void ui_on_slave(lv_event_t*) {
  float cur;
  {
    Guard g;
    cur = gSettings.ch[ENGINE_CH].mb.slaveId;
  }
  openNumEditor(T("ed_slave"), cur, applySlave, 0);
}
extern "C" void ui_on_band(lv_event_t*) {
  float cur;
  {
    Guard g;
    cur = gSettings.bottomBand;
  }
  openNumEditor(T("ed_band"), cur, applyBand, 0);
}
extern "C" void ui_on_controller_save(lv_event_t*) {
  saveSettingsAndNotify();
  toast(T("saved_title"), T("ctl_saved_text"));
}

extern "C" void ui_on_test_link(lv_event_t*) {
  BusRequest r{};
  r.op = BUS_READ;
  {
    Guard g;
    r.addr = gSettings.ch[ENGINE_CH].mb.reg.pv;
  }
  r.count = 1;
  char msg[200], a[12], b[12];
  if (r.addr == REG_UNSET) {
    strlcpy(msg, T("link_no_pv"), sizeof(msg));
  } else if (busRequest(r, 5000)) {
    snprintf(a, sizeof(a), "%u", r.addr);
    snprintf(b, sizeof(b), "%d", r.values[0]);
    trf(msg, sizeof(msg), "link_ok", a, b);
  } else {
    trf(msg, sizeof(msg), "link_fail", r.err);
  }
  toast(T("link_title"), msg);
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
  if (S.themePalette != appliedPalette || (S.themeDark != 0) != appliedDark) {  // changed from the web page
    applyTheme(S.themePalette, S.themeDark != 0);
    setupLoading = true;
    if (ddTheme) lv_dropdown_set_selected(ddTheme, S.themePalette);
    setChecked(swDark, S.themeDark);
    setupLoading = false;
  }
  bringupUpdate();
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
    {"sys_theme", LV_EVENT_VALUE_CHANGED, ui_on_theme},
    {"sys_dark", LV_EVENT_VALUE_CHANGED, ui_on_dark},
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

static bool uiCreateScreen(int palette, bool dark) {
  // palette and dark / light mode from the settings; the theme is set before
  // the screen is created (tempctl_ui_init() creates it), the styles are
  // recoloured by uiGenInit()
  uiGenSetPalette(palette, dark);
  lv_theme_t* th = lv_theme_default_init(disp, uiGenColor(UI_COLOR_ACCENT), uiGenColor(UI_COLOR_ACCENT2), dark,
                                         &lv_font_montserrat_14);
  lv_display_set_theme(disp, th);

  // styles, fonts, translations (ui/translations.xml) and the permanent
  // screen `gui` (ui/screens/gui.xml)
  screen = uiGenInit();
  if (!screen) {
    Serial.println("[ui] the screen from ui/screens/gui.xml was not created");
    return false;
  }
  lv_screen_load(screen);
  tabview = find("tabs");
  return true;
}

// ------------------------------------------------------------ bring-up panel
#ifdef UI_LEGACY_W
// Right of / below the 320x240 screens: identity, heap and the touch
// coordinates (raw controller values and the mapped screen point), to check
// the display, touch mapping and memory on new hardware.
static lv_obj_t* bringupInfo = nullptr;
static lv_obj_t* touchDot = nullptr;

static void bringupCreate() {
  lv_obj_t* tabs = find("tabs");
  if (tabs) lv_obj_set_size(tabs, UI_LEGACY_W, UI_LEGACY_H);
  bringupInfo = lv_label_create(screen);
  lv_obj_set_pos(bringupInfo, UI_LEGACY_W + 6, 6);
  lv_obj_set_width(bringupInfo, SCREEN_W - UI_LEGACY_W - 12);
  lv_label_set_long_mode(bringupInfo, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_font(bringupInfo, &lv_font_montserrat_12, 0);
  touchDot = lv_obj_create(lv_layer_sys());
  lv_obj_remove_style_all(touchDot);
  lv_obj_set_size(touchDot, 7, 7);
  lv_obj_set_style_radius(touchDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(touchDot, lv_color_hex(0xFF3030), 0);
  lv_obj_set_style_bg_opa(touchDot, LV_OPA_COVER, 0);
  lv_obj_set_hidden(touchDot, true);
  lv_obj_set_ignore_layout(touchDot, true);
  lv_obj_set_clickable(touchDot, false);
}

static void bringupUpdate() {
  if (!bringupInfo) return;
  static char buf[256];
  int32_t rx, ry;
  touchLastRaw(&rx, &ry);
  lv_indev_t* indev = lv_indev_get_next(nullptr);
  lv_point_t p = {-1, -1};
  const bool pressed = indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
  if (indev) lv_indev_get_point(indev, &p);
  snprintf(buf, sizeof(buf),
           PRODUCT_NAME " " FW_VERSION "\n" BOARD_NAME " bring-up\n%dx%d\n\nheap %u k\nlargest %u k\nPSRAM free %u k\n\n"
           "touch %s\nraw %ld, %ld\nscreen %ld, %ld",
           SCREEN_W, SCREEN_H, (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024),
           (unsigned)(ESP.getFreePsram() / 1024), pressed ? "down" : "up", (long)rx, (long)ry, (long)p.x, (long)p.y);
  lv_label_set_text(bringupInfo, buf);
  if (pressed) {
    lv_obj_set_pos(touchDot, p.x - 3, p.y - 3);
    lv_obj_set_hidden(touchDot, false);
  } else {
    lv_obj_set_hidden(touchDot, true);
  }
}
#else
static void bringupCreate() {}
static void bringupUpdate() {}
#endif

// ------------------------------------------------------------ init
void uiInit() {
  bool rot;
  uint8_t b;
  TouchCal cal;
  {
    Guard g;
    cal = {gSettings.tsMinX, gSettings.tsMaxX, gSettings.tsMinY, gSettings.tsMaxY};
    rot = gSettings.rotate180;
    b = gSettings.brightness;
  }
  rotated = rot;
  touchInit(cal);
  touchSetRotation(rot);

  lv_init();
  lv_tick_set_cb(tickCb);
  lv_delay_set_cb(delay);
  disp = lcdInit(rot);  // landscape SCREEN_W x SCREEN_H
  if (!disp) {
    Serial.println("[ui] display could not be set up - no touch UI");
    return;
  }
  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);

  int palette;
  bool dark;
  {
    Guard g;
    if (gSettings.themePalette >= uiGenPaletteCount()) gSettings.themePalette = 0;
    if (gSettings.themeDark == THEME_DARK_DEFAULT) gSettings.themeDark = uiGenThemeDark();  // first start
    palette = gSettings.themePalette;
    dark = gSettings.themeDark != 0;
  }
  uiCreateScreen(palette, dark);
  if (!screen) {  // show something useful instead of a black screen
    lv_obj_t* l = lv_label_create(lv_screen_active());
    lv_label_set_text(l, "UI could not be created.\nSee the serial log (115200 baud).");
    lv_obj_center(l);
    lv_refr_now(disp);
    uiSetBrightness(b);
    return;
  }
  bringupCreate();
  uiBindEvents();
  bindMain();
  bindProgram();
  bindChart();
  bindSystem();
  bindController();
  applyTheme(palette, dark);  // chart series, table colours

  lv_timer_create(uiTimer, 250, nullptr);
  lv_refr_now(disp);  // first frame, then the backlight (no flash of garbage)
  uiSetBrightness(b);
}

void uiLoop() {
  if (disp) lv_timer_handler();
}
#endif  // UI_V2
