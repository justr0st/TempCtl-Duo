// LVGL 8.3 user interface for the 320x240 CYD screen.
// Tabs: Run (live values + control), Program (6 steps), Chart, Setup.
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
    long x = map(p.x, calMinX, calMaxX, 0, SCREEN_W - 1);
    long y = map(p.y, calMinY, calMaxY, 0, SCREEN_H - 1);
    data->point.x = constrain(x, 0, SCREEN_W - 1);
    data->point.y = constrain(y, 0, SCREEN_H - 1);
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
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

static void toast(const char* title, const char* text) {
  lv_obj_t* m = lv_msgbox_create(nullptr, title, text, nullptr, true);
  lv_obj_set_width(m, 280);
  lv_obj_center(m);
}

// ------------------------------------------------------------ numeric editor (modal)
typedef void (*NumApply)(float v, int a);
static lv_obj_t* numPanel = nullptr;
static lv_obj_t* numTa = nullptr;
static NumApply numApply = nullptr;
static int numArg = 0;

static void numClose() {
  if (numPanel) lv_obj_del_async(numPanel);
  numPanel = nullptr;
}

static void numKbEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    const char* t = lv_textarea_get_text(numTa);
    if (t && *t && numApply) numApply((float)atof(t), numArg);
    numClose();
  } else if (code == LV_EVENT_CANCEL) {
    numClose();
  }
}

static void openNumEditor(const char* title, float cur, NumApply cb, int arg) {
  if (numPanel) return;
  numApply = cb;
  numArg = arg;
  numPanel = lv_obj_create(lv_layer_top());
  lv_obj_set_size(numPanel, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(numPanel, 0, 0);
  lv_obj_set_style_radius(numPanel, 0, 0);
  lv_obj_set_style_pad_all(numPanel, 6, 0);
  lv_obj_clear_flag(numPanel, LV_OBJ_FLAG_SCROLLABLE);
  mkLabel(numPanel, &lv_font_montserrat_14, 0, 4, title);
  numTa = lv_textarea_create(numPanel);
  lv_textarea_set_one_line(numTa, true);
  lv_textarea_set_accepted_chars(numTa, "0123456789.-");
  lv_textarea_set_max_length(numTa, 8);
  lv_obj_set_width(numTa, 180);
  lv_obj_set_pos(numTa, 0, 26);
  char buf[16];
  snprintf(buf, sizeof(buf), "%g", cur);
  lv_textarea_set_text(numTa, buf);
  lv_obj_t* kb = lv_keyboard_create(numPanel);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  lv_keyboard_set_textarea(kb, numTa);
  lv_obj_set_size(kb, SCREEN_W - 12, 150);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(kb, numKbEvent, LV_EVENT_ALL, nullptr);
}

// ------------------------------------------------------------ widgets
static lv_obj_t* tabview;
// Run tab
static lv_obj_t *lblPv, *lblSp, *lblTarget, *lblStep, *lblPhase, *lblTotal, *lblOut, *lblMode, *lblMsg,
    *barProg, *ledLink, *btnStart, *lblStartTxt, *btnPause;
// Program tab
static lv_obj_t *table, *lblProgTotal, *lblProgDirty;
static Program uiProg;
static bool progDirty = false;
// Chart tab
static lv_obj_t *chart, *lblChartInfo;
static lv_chart_series_t *serPlan, *serSp, *serPv;
// Setup tab
static lv_obj_t *ddProfile, *btnSlave, *ddBaud, *ddFormat, *ddRamp, *swSoak, *btnBand, *swLock, *sldBright,
    *lblInfo;
static bool setupLoading = false;

// ------------------------------------------------------------ Run tab
static void onStart(lv_event_t*) {
  RunState st;
  {
    Guard g;
    st = gLive.state;
  }
  sendCmd(st == RunState::Paused ? CMD_RESUME : CMD_START);
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
  lv_obj_t* m = lv_msgbox_create(nullptr, "Stop program?", "The end action from Setup will be applied.",
                                 btns, false);
  lv_obj_set_width(m, 260);
  lv_obj_center(m);
  lv_obj_add_event_cb(m, onStopConfirm, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void buildRun(lv_obj_t* tab) {
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  mkLabel(tab, &lv_font_montserrat_12, 2, 0, "Process value (C)");
  lblPv = mkLabel(tab, &lv_font_montserrat_48, 0, 14, "---.-");
  lv_obj_set_style_text_color(lblPv, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);
  lblSp = mkLabel(tab, &lv_font_montserrat_20, 2, 68, "SP ---.-");
  lv_obj_set_style_text_color(lblSp, lv_palette_lighten(LV_PALETTE_BLUE, 1), 0);
  lblTarget = mkLabel(tab, &lv_font_montserrat_12, 2, 94, "");

  ledLink = lv_led_create(tab);
  lv_obj_set_size(ledLink, 12, 12);
  lv_obj_set_pos(ledLink, 296, 2);
  lblStep = mkLabel(tab, &lv_font_montserrat_20, 176, 0, "Idle");
  lblPhase = mkLabel(tab, &lv_font_montserrat_12, 176, 28, "");
  lblTotal = mkLabel(tab, &lv_font_montserrat_12, 176, 46, "");
  lblOut = mkLabel(tab, &lv_font_montserrat_12, 176, 64, "");
  lblMode = mkLabel(tab, &lv_font_montserrat_12, 176, 82, "");

  barProg = lv_bar_create(tab);
  lv_obj_set_size(barProg, 308, 8);
  lv_obj_set_pos(barProg, 2, 116);
  lv_bar_set_range(barProg, 0, 1000);

  lblMsg = mkLabel(tab, &lv_font_montserrat_12, 2, 130, "");
  lv_label_set_long_mode(lblMsg, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lblMsg, 308);

  btnStart = mkButton(tab, LV_SYMBOL_PLAY " Start", 0, 154, 92, 44, onStart);
  lblStartTxt = lv_obj_get_child(btnStart, 0);
  btnPause = mkButton(tab, LV_SYMBOL_PAUSE, 98, 154, 64, 44, onPause);
  lv_obj_t* stop = mkButton(tab, LV_SYMBOL_STOP, 168, 154, 64, 44, onStop);
  lv_obj_set_style_bg_color(stop, lv_palette_main(LV_PALETTE_RED), 0);
  mkButton(tab, LV_SYMBOL_NEXT, 238, 154, 72, 44, onSkip);
}

static void updateRun(const LiveData& L, const Settings& S) {
  char a[24], b[24], buf[96];
  if (L.pvValid) fmtTemp(true, L.pv, a, sizeof(a));
  else if (L.commOk && L.pvFlag != VAL_OK) strlcpy(a, L.pvFlag == VAL_SENSOR_FAULT ? "Sensor" : "---.-", sizeof(a));
  else strlcpy(a, "---.-", sizeof(a));
  lv_label_set_text(lblPv, a);

  fmtTemp(L.spEffValid, L.spEff, a, sizeof(a));
  lv_label_set_text_fmt(lblSp, "SP %s", a);

  const bool active = L.state == RunState::Running || L.state == RunState::Paused;
  if (active) {
    fmtTemp(true, L.target, a, sizeof(a));
    fmtTemp(true, L.hostSp, b, sizeof(b));
    lv_label_set_text_fmt(lblTarget, "Step target %s   program %s", a, b);
  } else if (S.reg.sp == REG_UNSET) {
    lv_label_set_text(lblTarget, "Setpoint register not set - see web UI");
  } else {
    lv_label_set_text(lblTarget, "");
  }

  if (L.commOk) {
    lv_led_set_color(ledLink, lv_palette_main(LV_PALETTE_GREEN));
    lv_led_on(ledLink);
  } else {
    lv_led_set_color(ledLink, lv_palette_main(LV_PALETTE_RED));
    lv_led_on(ledLink);
  }

  switch (L.state) {
    case RunState::Idle: lv_label_set_text(lblStep, "Idle"); break;
    case RunState::Finished: lv_label_set_text(lblStep, "Finished"); break;
    case RunState::Aborted: lv_label_set_text(lblStep, "Stopped"); break;
    default:
      lv_label_set_text_fmt(lblStep, "%s %d/6", L.state == RunState::Paused ? "Paused" : "Step", L.step + 1);
  }
  if (active) {
    fmtDur(L.phaseDur - L.phaseEl, a, sizeof(a));
    if (L.soakWait) lv_label_set_text(lblPhase, "Hold: waiting for PV");
    else lv_label_set_text_fmt(lblPhase, "%s  %s left", L.phase == Phase::Ramp ? "Ramp" : "Hold", a);
    fmtDur(L.total - L.elapsed, a, sizeof(a));
    lv_label_set_text_fmt(lblTotal, "Total %s left", a);
    lv_bar_set_value(barProg, L.total > 0 ? (int)(1000 * L.elapsed / L.total) : 0, LV_ANIM_OFF);
  } else {
    lv_label_set_text(lblPhase, "");
    fmtDur(ProgramEngine::plannedTotal(S.program), a, sizeof(a));
    lv_label_set_text_fmt(lblTotal, "Program %s", a);
    lv_bar_set_value(barProg, L.state == RunState::Finished ? 1000 : 0, LV_ANIM_OFF);
  }
  if (L.outValid) lv_label_set_text_fmt(lblOut, "Output %.0f %%", L.out);
  else lv_label_set_text(lblOut, "");
  if (L.statusValid && S.profile == PROFILE_PMA_KS4X) lv_label_set_text(lblMode, pmaModeText(L.status));
  else lv_label_set_text(lblMode, "");

  if (!L.commOk) snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", L.lastErr);
  else if (!L.spSynced && active) snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", L.lastErr);
  else strlcpy(buf, L.runMsg, sizeof(buf));
  lv_label_set_text(lblMsg, buf);

  lv_label_set_text(lblStartTxt, L.state == RunState::Paused ? LV_SYMBOL_PLAY " Resume" : LV_SYMBOL_PLAY " Start");
  if (active) lv_obj_add_state(btnStart, LV_STATE_DISABLED);
  if (L.state == RunState::Paused || !active) lv_obj_clear_state(btnStart, LV_STATE_DISABLED);
  if (L.state == RunState::Running) lv_obj_clear_state(btnPause, LV_STATE_DISABLED);
  else lv_obj_add_state(btnPause, LV_STATE_DISABLED);
}

// ------------------------------------------------------------ Program tab
static void refreshTable() {
  for (int i = 0; i < NUM_STEPS; ++i) {
    const Step& s = uiProg.s[i];
    const int r = i + 1;
    lv_table_set_cell_value_fmt(table, r, 0, "%d", r);
    lv_table_set_cell_value(table, r, 1, s.enabled ? LV_SYMBOL_OK : "-");
    lv_table_set_cell_value_fmt(table, r, 2, "%.1f", s.target);
    lv_table_set_cell_value_fmt(table, r, 3, "%.1f", s.rampMin);
    lv_table_set_cell_value_fmt(table, r, 4, "%.1f", s.holdMin);
  }
  char d[16];
  fmtDur(ProgramEngine::plannedTotal(uiProg), d, sizeof(d));
  lv_label_set_text_fmt(lblProgTotal, "Total %s", d);
  lv_label_set_text(lblProgDirty, progDirty ? "unsaved" : "");
}

static void loadProgram() {
  Guard g;
  uiProg = gSettings.program;
  progDirty = false;
}

static void applyCell(float v, int arg) {
  const int step = arg / 10, col = arg % 10;
  Step& s = uiProg.s[step];
  float lo, hi;
  {
    Guard g;
    lo = gSettings.spMin;
    hi = gSettings.spMax;
  }
  if (col == 2) s.target = constrain(v, lo, hi);
  if (col == 3) s.rampMin = constrain(v, 0.0f, 9999.0f);
  if (col == 4) s.holdMin = constrain(v, 0.0f, 9999.0f);
  progDirty = true;
  refreshTable();
}

static void onTable(lv_event_t*) {
  uint16_t row, col;
  lv_table_get_selected_cell(table, &row, &col);
  if (row == LV_TABLE_CELL_NONE || row == 0 || row > NUM_STEPS) return;
  const int step = row - 1;
  if (col == 1) {
    uiProg.s[step].enabled = !uiProg.s[step].enabled;
    progDirty = true;
    refreshTable();
    return;
  }
  char title[48];
  float cur;
  if (col == 2) { snprintf(title, sizeof(title), "Step %d target temperature (C)", row); cur = uiProg.s[step].target; }
  else if (col == 3) { snprintf(title, sizeof(title), "Step %d ramp time (minutes)", row); cur = uiProg.s[step].rampMin; }
  else if (col == 4) { snprintf(title, sizeof(title), "Step %d hold time (minutes)", row); cur = uiProg.s[step].holdMin; }
  else return;
  openNumEditor(title, cur, applyCell, step * 10 + col);
}

static void onProgSave(lv_event_t*) {
  {
    Guard g;
    gSettings.program = uiProg;
  }
  saveSettingsAndNotify();
  progDirty = false;
  refreshTable();
  RunState st;
  {
    Guard g;
    st = gLive.state;
  }
  if (st == RunState::Running || st == RunState::Paused) toast("Saved", "Changes apply to the next run.");
}

static void onProgRevert(lv_event_t*) {
  loadProgram();
  refreshTable();
}

static void buildProgram(lv_obj_t* tab) {
  lv_obj_set_style_pad_all(tab, 2, 0);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  table = lv_table_create(tab);
  lv_obj_set_pos(table, 0, 0);
  lv_obj_set_size(table, 316, 168);
  lv_table_set_col_cnt(table, 5);
  lv_table_set_row_cnt(table, NUM_STEPS + 1);
  const int widths[5] = {30, 42, 84, 80, 80};
  for (int c = 0; c < 5; ++c) lv_table_set_col_width(table, c, widths[c]);
  lv_obj_set_style_pad_top(table, 3, LV_PART_ITEMS);
  lv_obj_set_style_pad_bottom(table, 3, LV_PART_ITEMS);
  lv_obj_set_style_pad_left(table, 4, LV_PART_ITEMS);
  lv_obj_set_style_pad_right(table, 4, LV_PART_ITEMS);
  lv_table_set_cell_value(table, 0, 0, "#");
  lv_table_set_cell_value(table, 0, 1, "On");
  lv_table_set_cell_value(table, 0, 2, "Target C");
  lv_table_set_cell_value(table, 0, 3, "Ramp min");
  lv_table_set_cell_value(table, 0, 4, "Hold min");
  lv_obj_add_event_cb(table, onTable, LV_EVENT_VALUE_CHANGED, nullptr);

  lblProgTotal = mkLabel(tab, &lv_font_montserrat_14, 2, 178, "");
  lblProgDirty = mkLabel(tab, &lv_font_montserrat_12, 110, 180, "");
  lv_obj_set_style_text_color(lblProgDirty, lv_palette_main(LV_PALETTE_AMBER), 0);
  mkButton(tab, "Revert", 170, 170, 66, 34, onProgRevert);
  mkButton(tab, LV_SYMBOL_SAVE " Save", 240, 170, 74, 34, onProgSave);
  loadProgram();
  refreshTable();
}

// ------------------------------------------------------------ Chart tab
static void buildChart(lv_obj_t* tab) {
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  chart = lv_chart_create(tab);
  lv_obj_set_pos(chart, 34, 0);
  lv_obj_set_size(chart, 276, 172);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, HIST_N);
  lv_chart_set_div_line_count(chart, 5, 6);
  lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 4, 2, 6, 1, true, 34);
  lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);  // no point markers
  lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_text_font(chart, &lv_font_montserrat_12, LV_PART_TICKS);
  serPlan = lv_chart_add_series(chart, lv_palette_darken(LV_PALETTE_BLUE, 1), LV_CHART_AXIS_PRIMARY_Y);
  serSp = lv_chart_add_series(chart, lv_palette_lighten(LV_PALETTE_BLUE, 2), LV_CHART_AXIS_PRIMARY_Y);
  serPv = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_DEEP_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
  lblChartInfo = mkLabel(tab, &lv_font_montserrat_12, 2, 180, "");
  lv_label_set_long_mode(lblChartInfo, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lblChartInfo, 310);
}

static lv_coord_t toCoord(float v) { return isnan(v) ? LV_CHART_POINT_NONE : (lv_coord_t)lroundf(v); }

static void updateChart() {
  float lo = 1e9f, hi = -1e9f;
  uint8_t mode;
  float total, bin;
  {
    Guard g;
    mode = gHist.mode;
    total = gHist.total;
    bin = gHist.binSec;
    for (int i = 0; i < HIST_N; ++i) {
      const float plan = histPlanAt(i);
      const float pv = gHist.pv[i], sp = gHist.sp[i];
      serPlan->y_points[i] = toCoord(plan);
      serSp->y_points[i] = toCoord(sp);
      serPv->y_points[i] = toCoord(pv);
      for (float v : {plan, pv, sp}) {
        if (isnan(v)) continue;
        lo = fminf(lo, v);
        hi = fmaxf(hi, v);
      }
    }
  }
  if (lo > hi) { lo = 0; hi = 100; }
  const float pad = fmaxf(5.0f, (hi - lo) * 0.08f);
  lo = floorf((lo - pad) / 10.0f) * 10.0f;
  hi = ceilf((hi + pad) / 10.0f) * 10.0f;
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, (lv_coord_t)lo, (lv_coord_t)hi);
  lv_chart_refresh(chart);
  char d[16];
  fmtDur(mode ? total : HIST_N * bin, d, sizeof(d));
  if (mode) lv_label_set_text_fmt(lblChartInfo, "Run 0 - %s  | dark blue plan, light blue SP, orange PV", d);
  else lv_label_set_text_fmt(lblChartInfo, "Last %s  | light blue SP, orange PV", d);
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

static const uint32_t BAUDS[] = {2400, 4800, 9600, 19200, 38400};

static void refreshSetup() {
  setupLoading = true;
  Settings S;
  {
    Guard g;
    S = gSettings;
  }
  lv_dropdown_set_selected(ddProfile, S.profile);
  lv_label_set_text_fmt(lv_obj_get_child(btnSlave, 0), "%u", S.slaveId);
  for (int i = 0; i < 5; ++i)
    if (BAUDS[i] == S.baud) lv_dropdown_set_selected(ddBaud, i);
  lv_dropdown_set_selected(ddFormat, S.parity == 1 ? 1 : S.parity == 2 ? 2 : (S.stopBits == 2 ? 3 : 0));
  lv_dropdown_set_selected(ddRamp, S.rampMode);
  if (S.guaranteedSoak) lv_obj_add_state(swSoak, LV_STATE_CHECKED);
  else lv_obj_clear_state(swSoak, LV_STATE_CHECKED);
  lv_label_set_text_fmt(lv_obj_get_child(btnBand, 0), "%.1f", S.soakBand);
  if (S.lockFront) lv_obj_add_state(swLock, LV_STATE_CHECKED);
  else lv_obj_clear_state(swLock, LV_STATE_CHECKED);
  lv_slider_set_value(sldBright, S.brightness, LV_ANIM_OFF);
  char sp[12], rr[12];
  if (S.reg.sp == REG_UNSET) strcpy(sp, "not set"); else snprintf(sp, sizeof(sp), "%u", S.reg.sp);
  if (S.reg.rampRate == REG_UNSET) strcpy(rr, "not set"); else snprintf(rr, sizeof(rr), "%u", S.reg.rampRate);
  String info = netInfo();
  lv_label_set_text_fmt(lblInfo, "%s\nSP register %s, ramp register %s\nFirmware " FW_VERSION, info.c_str(), sp,
                        rr);
  setupLoading = false;
}

static void onSetupChanged(lv_event_t* e) {
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
  } else if (t == sldBright) {
    S.brightness = (uint8_t)lv_slider_get_value(sldBright);
  }
}

static void applySlave(float v, int) {
  {
    Guard g;
    gSettings.slaveId = (uint8_t)constrain((int)v, 1, 247);
  }
  refreshSetup();
}
static void applyBand(float v, int) {
  {
    Guard g;
    gSettings.soakBand = constrain(v, 0.1f, 500.0f);
  }
  refreshSetup();
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
static void onSetupSave(lv_event_t*) {
  saveSettingsAndNotify();
  toast("Saved", "Settings stored. The serial link was reopened.");
}

static void onTestLink(lv_event_t*) {
  BusRequest r{};
  r.op = BUS_READ;
  {
    Guard g;
    r.addr = gSettings.reg.pv;
  }
  r.count = 1;
  char msg[120];
  if (r.addr == REG_UNSET) strlcpy(msg, "The PV register address is not set.", sizeof(msg));
  else if (busRequest(r, 5000)) snprintf(msg, sizeof(msg), "Controller answered.\nPV register %u = %d (raw)", r.addr, r.values[0]);
  else snprintf(msg, sizeof(msg), "No valid answer.\n%s\nSave settings first if you changed them.", r.err);
  toast("Link test", msg);
}

static void buildSetup(lv_obj_t* tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab, 2, 0);
  lv_obj_set_style_pad_all(tab, 4, 0);

  lv_obj_t* r = mkRow(tab, "Controller");
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

  r = mkRow(tab, "Brightness");
  sldBright = lv_slider_create(r);
  lv_slider_set_range(sldBright, 10, 255);
  lv_obj_set_width(sldBright, 150);

  lblInfo = lv_label_create(tab);
  lv_obj_set_width(lblInfo, lv_pct(100));
  lv_obj_set_style_text_font(lblInfo, &lv_font_montserrat_12, 0);

  r = mkRow(tab, "");
  mkButton(r, LV_SYMBOL_REFRESH " Test link", 0, 0, 130, 36, onTestLink);
  mkButton(r, LV_SYMBOL_SAVE " Save", 0, 0, 110, 36, onSetupSave);

  for (lv_obj_t* o : {ddProfile, ddBaud, ddFormat, ddRamp, swSoak, swLock, sldBright})
    lv_obj_add_event_cb(o, onSetupChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  refreshSetup();
}

// ------------------------------------------------------------ periodic update
static void onTabChanged(lv_event_t*) {
  const uint16_t t = lv_tabview_get_tab_act(tabview);
  if (t == 1 && !progDirty) { loadProgram(); refreshTable(); }
  if (t == 2) updateChart();
  if (t == 3) refreshSetup();
}

static void uiTimer(lv_timer_t*) {
  static uint32_t lastChart = 0;
  LiveData L;
  Settings S;
  {
    Guard g;
    L = gLive;
    S = gSettings;
  }
  if (S.brightness != appliedBrightness) uiSetBrightness(S.brightness);
  const uint16_t tab = lv_tabview_get_tab_act(tabview);
  if (tab == 0) updateRun(L, S);
  if (tab == 2 && millis() - lastChart > 2000) {
    lastChart = millis();
    updateChart();
  }
}

// ------------------------------------------------------------ init
void uiInit() {
  {
    Guard g;
    calMinX = gSettings.tsMinX; calMaxX = gSettings.tsMaxX;
    calMinY = gSettings.tsMinY; calMaxY = gSettings.tsMaxY;
  }
  tft.init();
  tft.setRotation(1);  // landscape 320x240
  tft.fillScreen(TFT_BLACK);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BACKLIGHT, 0);
  uint8_t b;
  {
    Guard g;
    b = gSettings.brightness;
  }
  uiSetBrightness(b);

  touchSpi.begin(PIN_TOUCH_CLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);

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
  lv_obj_t* tRun = lv_tabview_add_tab(tabview, "Run");
  lv_obj_t* tProg = lv_tabview_add_tab(tabview, "Program");
  lv_obj_t* tChart = lv_tabview_add_tab(tabview, "Chart");
  lv_obj_t* tSetup = lv_tabview_add_tab(tabview, "Setup");
  for (lv_obj_t* t : {tRun, tProg, tChart}) lv_obj_set_style_pad_all(t, 4, 0);
  // swiping between tabs conflicts with the table/chart; use the tab buttons
  lv_obj_clear_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);

  buildRun(tRun);
  buildProgram(tProg);
  buildChart(tChart);
  buildSetup(tSetup);

  lv_timer_create(uiTimer, 500, nullptr);
}

void uiLoop() { lv_timer_handler(); }
