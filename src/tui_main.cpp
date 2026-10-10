// Touch UI main screen (480x320, spec 9.7): header with program, state, time and run buttons;
// chart of both channels; one tile per channel (PV, SP, output, status, target stepper, manual
// switch / extend hold / acknowledge). Overlays: "Action required", "Run interrupted by a reset",
// "Save the run's changes?", messages.
#include "tui.h"
#include "local_ctrl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const ChartData& chartData();

static lv_obj_t* scr;
static lv_obj_t *btnRun, *btnStop, *btnNext, *titleBox, *lblProg, *lblSub, *badge, *lblTime, *lblRem;
static lv_obj_t *chart, *lblChartMode;
static bool chartLive = false;
static uint32_t chartAt = 0;

struct Tile {
  lv_obj_t *box, *name, *status, *pv, *pvUnit, *spCap, *sp, *out, *barBg, *bar;
  lv_obj_t *minus, *value, *plus, *manSw, *extend, *ack;
  float pend;          // stepper value not sent yet (NaN = none)
  uint32_t pendAt;     // last stepper press
  uint32_t downAt;     // - / + pressed down (held > 3 s: steps of 5 degC)
  bool repeated;       // the press repeated (no extra step on release)
  int8_t spStyled;
  bool faultStyled;    // the status text shows the fault colour     // 320x240: the SP button shows the SP colour (-1 unknown)
  bool runTarget;      // the stepper edits the run's target (else the manual setpoint)
  float lo, hi;        // setpoint limits
  float shown;         // value the stepper shows
};
static Tile tiles[NUM_CHANNELS];

static LiveData L;      // last snapshot (main screen only)
static Settings S;
static RunState prevState = RunState::Idle;
static char lastMsg[128] = "";
static bool intrAsked = false;
static uint32_t intrDlg = 0;  // the "Run interrupted" dialog (closed when answered elsewhere)
static lv_obj_t* halt = nullptr;  // "Action required" overlay

static bool active(RunState s) { return s == RunState::Running || s == RunState::Paused; }

// ------------------------------------------------------------ run buttons
static void onStopAnswer(int b, int) {
  if (b == 2) sendCmd(CMD_STOP);
}
static void onStartAnswer(int b, int) {
  if (b == 2) sendCmd(CMD_START, -1);
}
static void onRun(lv_event_t*) {
  if (L.state == RunState::Running && L.progPause) sendCmd(CMD_CONTINUE);
  else if (L.state == RunState::Running) sendCmd(CMD_PAUSE);
  else if (L.state == RunState::Paused) sendCmd(L.progPause ? CMD_CONTINUE : CMD_RESUME);
  else if (L.intrValid)
    tDialog("Start a new run?", "The interrupted run can no longer be resumed afterwards.", "Cancel", "Start", "",
            onStartAnswer, 0);
  else sendCmd(CMD_START, -1);
}
static void onStop(lv_event_t*) {
  tDialog("Stop the program?", "Each channel applies its end action.", "Cancel", "Stop", "", onStopAnswer, 0, true);
}
static void onNext(lv_event_t*) { sendCmd(CMD_NEXT); }
static void onMenu(lv_event_t*) { tShow(SCR_MENU); }

// ------------------------------------------------------------ program picker
static int8_t pickSlot[PROG_MAX];

static void onPick(int i, int) {
  if (i >= 0 && i < PROG_MAX && pickSlot[i] >= 0) selectProgram(pickSlot[i]);
  chartAt = 0;
}

static void onTitle(lv_event_t*) {
  if (active(L.state)) {
    tToast("The program can be changed when no run is active.");
    return;
  }
  static ProgSummary sums[PROG_MAX];
  programSummaries(sums);
  static char items[PROG_MAX * 64];
  items[0] = 0;
  int n = 0, sel = -1;
  for (int i = 0; i < PROG_MAX; ++i) {
    pickSlot[i] = -1;
    if (!sums[i].used) continue;
    char t[16], row[72];
    fmtTime(sums[i].total, t, sizeof(t));
    snprintf(row, sizeof(row), "%s%d  %s  \xC2\xB7  %s %s", n ? "\n" : "", i + 1, sums[i].name,
             sums[i].type == PROG_TL ? "Timeline" : "Sequence", t);
    strlcat(items, row, sizeof(items));
    if (i == S.selProgram) sel = n;
    pickSlot[n++] = (int8_t)i;
  }
  if (!n) {
    tToast("No programs yet - create one on the web page.");
    return;
  }
  tChoice("Program for the next run", items, sel, onPick, 0);
}

// ------------------------------------------------------------ chart
static void onChart(lv_event_t*) {
  chartLive = !chartLive;
  chartAt = 0;
}

// ------------------------------------------------------------ channel tiles
static void sendTarget(int ch, float v) {
  Tile& t = tiles[ch];
  if (t.runTarget) sendCmd(CMD_RUN_EDIT, ch, v, -1, EDIT_TARGET);
  else setManualSp(v, ch);
}

#if !TUI_SMALL
static void onStep(lv_event_t* e) {
  const int k = (int)(intptr_t)lv_event_get_user_data(e);  // ch * 2 + (0 minus, 1 plus)
  Tile& t = tiles[k / 2];
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    t.downAt = lv_tick_get();
    t.repeated = false;
    return;
  }
  if (code == LV_EVENT_CLICKED && t.repeated) return;  // held: the repeats did the steps
  if (code == LV_EVENT_LONG_PRESSED_REPEAT) t.repeated = true;
  float v = isnan(t.pend) ? t.shown : t.pend;
  if (isnan(v)) v = t.lo > 0 ? t.lo : 25;
  if (code == LV_EVENT_LONG_PRESSED_REPEAT && lv_tick_elaps(t.downAt) > 3000) {
    v = k & 1 ? floorf(v / 5) * 5 + 5 : ceilf(v / 5) * 5 - 5;  // held > 3 s: 5 degC steps (round values)
  } else {
    v = roundf(v) + (k & 1 ? 1 : -1);
  }
  if (v < t.lo) v = t.lo;
  if (v > t.hi) v = t.hi;
  t.pend = v;
  t.pendAt = lv_tick_get();
  char b[16];
  snprintf(b, sizeof(b), "%.0f \xC2\xB0" "C", v);
  tButtonText(t.value, b);
}

#endif

static void onValueNum(float v, int ch) {
  tiles[ch].pend = NAN;
  sendTarget(ch, v);
}

static void onValue(lv_event_t* e) {
  const int ch = (int)(intptr_t)lv_event_get_user_data(e);
  Tile& t = tiles[ch];
  char title[48];
  snprintf(title, sizeof(title), "%s %s", L.ch[ch].name, t.runTarget ? "target" : "manual setpoint");
  tNumPad(title, isnan(t.pend) ? t.shown : t.pend, t.lo, t.hi, 1, "\xC2\xB0" "C", onValueNum, ch);
}

// manual button: shows the state (channel colour = on); a tap switches it
static void onManual(lv_event_t* e) {
  const int ch = (int)(intptr_t)lv_event_get_user_data(e);
  const bool on = !L.ch[ch].manualOn;
  tSetChecked(tiles[ch].manSw, on);  // at once; the next update shows the real state
  sendCmd(on ? CMD_MANUAL_ON : CMD_MANUAL_OFF, ch);
}

static void onExtendPick(int i, int ch) {
  static const float SEC[] = {10, 30, 60, 300};
  if (i >= 0 && i < 4) sendCmd(CMD_RUN_EDIT, ch, SEC[i], -1, EDIT_EXTEND);
}
static void onExtend(lv_event_t* e) {
  const int ch = (int)(intptr_t)lv_event_get_user_data(e);
  char t[48];
  snprintf(t, sizeof(t), "Extend the %s hold", L.ch[ch].name);
  tChoice(t, "+10 s\n+30 s\n+1 min\n+5 min", -1, onExtendPick, ch);
}

static void onAck(lv_event_t* e) { sendCmd(CMD_ACK_FAULT, (int)(intptr_t)lv_event_get_user_data(e)); }

#if !TUI_SMALL
static void tileCreate(int ch, lv_obj_t* parent, int x, int y, int w, int h) {
  Tile& t = tiles[ch];
  t.pend = NAN;
  t.shown = NAN;
  t.box = tPanel(parent);
  lv_obj_set_pos(t.box, x, y);
  lv_obj_set_size(t.box, w, h);
  const int iw = w - 16;
  t.name = tLabel(t.box, &tfont_16b, "");
  lv_obj_add_style(t.name, &S_.pv[ch], 0);
  lv_obj_set_pos(t.name, 0, -2);
  lv_obj_set_width(t.name, 110);
  tOneLine(t.name);
  t.status = tLabel(t.box, &tfont_12, "");
  lv_obj_add_style(t.status, &S_.muted, 0);
  lv_obj_set_width(t.status, iw - 112);
  lv_obj_set_style_text_align(t.status, LV_TEXT_ALIGN_RIGHT, 0);
  tOneLine(t.status);
  lv_obj_align(t.status, LV_ALIGN_TOP_RIGHT, 0, 1);

  t.pv = tLabel(t.box, &tfont_40c, "");
  lv_obj_set_pos(t.pv, 0, 16);
  t.pvUnit = tLabel(t.box, &tfont_14, "\xC2\xB0" "C");
  lv_obj_align_to(t.pvUnit, t.pv, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -8);
  t.spCap = tLabel(t.box, &tfont_12, "SP");
  lv_obj_add_style(t.spCap, &S_.muted, 0);
  lv_obj_align(t.spCap, LV_ALIGN_TOP_RIGHT, -52, 26);
  t.sp = tLabel(t.box, &tfont_20c, "");
  lv_obj_add_style(t.sp, &S_.sp[ch], 0);
  lv_obj_set_width(t.sp, 50);
  lv_obj_set_style_text_align(t.sp, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(t.sp, LV_ALIGN_TOP_RIGHT, 0, 20);
  t.out = tLabel(t.box, &tfont_12, "");
  lv_obj_add_style(t.out, &S_.muted, 0);
  lv_obj_set_width(t.out, 90);
  lv_obj_set_style_text_align(t.out, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(t.out, LV_ALIGN_TOP_RIGHT, 0, 46);

  t.barBg = lv_obj_create(t.box);
  lv_obj_remove_style_all(t.barBg);
  lv_obj_add_style(t.barBg, &S_.barBg, 0);
  lv_obj_set_size(t.barBg, iw, 7);
  lv_obj_set_pos(t.barBg, 0, 66);
  lv_obj_set_scrollable(t.barBg, false);
  t.bar = lv_obj_create(t.barBg);
  lv_obj_remove_style_all(t.bar);
  lv_obj_add_style(t.bar, &S_.bar[ch], 0);
  lv_obj_set_size(t.bar, 0, LV_PCT(100));

  const int by = h - 16 - 38;
  t.minus = tIconButton(t.box, LV_SYMBOL_MINUS, onStep, (void*)(intptr_t)(ch * 2), 38, 38);
  lv_obj_set_pos(t.minus, 0, by);
  lv_obj_add_event_cb(t.minus, onStep, LV_EVENT_LONG_PRESSED_REPEAT, (void*)(intptr_t)(ch * 2));
  lv_obj_add_event_cb(t.minus, onStep, LV_EVENT_PRESSED, (void*)(intptr_t)(ch * 2));
  t.value = tButton(t.box, "", onValue, (void*)(intptr_t)ch, 74, 38);
  lv_obj_set_style_text_font(t.value, &tfont_16b, 0);
  lv_obj_set_pos(t.value, 42, by);
  t.plus = tIconButton(t.box, LV_SYMBOL_PLUS, onStep, (void*)(intptr_t)(ch * 2 + 1), 38, 38);
  lv_obj_set_pos(t.plus, 120, by);
  lv_obj_add_event_cb(t.plus, onStep, LV_EVENT_LONG_PRESSED_REPEAT, (void*)(intptr_t)(ch * 2 + 1));
  lv_obj_add_event_cb(t.plus, onStep, LV_EVENT_PRESSED, (void*)(intptr_t)(ch * 2 + 1));

  t.manSw = tIconButton(t.box, LV_SYMBOL_POWER, onManual, (void*)(intptr_t)ch, 48, 38);
  lv_obj_add_style(t.manSw, &S_.btnOn[ch], LV_STATE_CHECKED);
  lv_obj_align(t.manSw, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  t.extend = tButton(t.box, "+ Hold", onExtend, (void*)(intptr_t)ch, 58, 38);
  lv_obj_align(t.extend, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  t.ack = tButton(t.box, "Acknowledge", onAck, (void*)(intptr_t)ch, iw, 38);
  lv_obj_add_style(t.ack, &S_.btnDanger, 0);
  lv_obj_set_pos(t.ack, 0, by);
}
#endif

#if TUI_SMALL
// 320x240: one SP button (current setpoint; tap: number entry for the target / manual setpoint)
// instead of the stepper and the SP readout; manual / + Hold / Acknowledge in the bottom row
static void tileCreateSmall(int ch, lv_obj_t* parent, int x, int y, int w, int h) {
  Tile& t = tiles[ch];
  t.pend = NAN;
  t.shown = NAN;
  t.spStyled = -1;
  t.box = tPanel(parent);
  lv_obj_set_pos(t.box, x, y);
  lv_obj_set_size(t.box, w, h);
  lv_obj_set_style_pad_all(t.box, 6, 0);
  const int iw = w - 12;
  t.name = tLabel(t.box, &tfont_14, "");
  lv_obj_add_style(t.name, &S_.pv[ch], 0);
  lv_obj_set_pos(t.name, 0, -2);
  lv_obj_set_width(t.name, 64);
  tOneLine(t.name);
  t.status = tLabel(t.box, &tfont_12, "");
  lv_obj_add_style(t.status, &S_.muted, 0);
  lv_obj_set_width(t.status, iw - 66);
  lv_obj_set_style_text_align(t.status, LV_TEXT_ALIGN_RIGHT, 0);
  tOneLine(t.status);
  lv_obj_align(t.status, LV_ALIGN_TOP_RIGHT, 0, 0);

  t.pv = tLabel(t.box, &tfont_28c, "");
  lv_obj_set_pos(t.pv, 0, 15);
  t.pvUnit = tLabel(t.box, &tfont_12, "\xC2\xB0" "C");
  lv_obj_add_style(t.pvUnit, &S_.muted, 0);
  lv_obj_align_to(t.pvUnit, t.pv, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -6);
  t.value = tButton(t.box, "", onValue, (void*)(intptr_t)ch, 62, 30);
  lv_obj_set_style_text_font(t.value, &tfont_16b, 0);
  lv_obj_set_style_pad_hor(t.value, 2, 0);
  lv_obj_align(t.value, LV_ALIGN_TOP_RIGHT, 0, 16);

  t.barBg = lv_obj_create(t.box);
  lv_obj_remove_style_all(t.barBg);
  lv_obj_add_style(t.barBg, &S_.barBg, 0);
  lv_obj_set_size(t.barBg, iw, 5);
  lv_obj_set_pos(t.barBg, 0, 51);
  lv_obj_set_scrollable(t.barBg, false);
  t.bar = lv_obj_create(t.barBg);
  lv_obj_remove_style_all(t.bar);
  lv_obj_add_style(t.bar, &S_.bar[ch], 0);
  lv_obj_set_size(t.bar, 0, LV_PCT(100));

  t.out = tLabel(t.box, &tfont_12, "");
  lv_obj_add_style(t.out, &S_.muted, 0);
  lv_obj_set_width(t.out, 80);
  lv_obj_align(t.out, LV_ALIGN_BOTTOM_LEFT, 0, -5);
  t.manSw = tIconButton(t.box, LV_SYMBOL_POWER, onManual, (void*)(intptr_t)ch, 46, 26);
  lv_obj_set_style_text_font(t.manSw, &tfont_16b, 0);
  lv_obj_add_style(t.manSw, &S_.btnOn[ch], LV_STATE_CHECKED);
  lv_obj_align(t.manSw, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  t.extend = tButton(t.box, "+ Hold", onExtend, (void*)(intptr_t)ch, 56, 26);
  lv_obj_align(t.extend, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  t.ack = tButton(t.box, "Acknowledge", onAck, (void*)(intptr_t)ch, iw, 30);
  lv_obj_add_style(t.ack, &S_.btnDanger, 0);
  lv_obj_align(t.ack, LV_ALIGN_TOP_LEFT, 0, 16);
}
#endif

static const char* chStateText(int ch, char* b, size_t n) {
  const ChannelLive& C = L.ch[ch];
  const ChOut& o = L.run[ch];
  char tv[16];
  if (TUI_SMALL && !isnan(o.target)) snprintf(tv, sizeof(tv), "%.0f\xC2\xB0" "C", o.target);  // short
  else fmtTemp(o.target, tv, sizeof(tv));
  if (C.type == CH_OFF) return "Off (settings)";
  if (C.fault) return C.faultText[0] ? C.faultText : "Fault";
  if (!C.commOk) return C.lastErr[0] ? C.lastErr : "No connection";
  switch ((ChState)L.chSt[ch]) {
    case ChState::Off: return "Off";
    case ChState::Ramp: snprintf(b, n, "Ramp to %s", tv); return b;
    case ChState::Hold: snprintf(b, n, "Hold %s", tv); return b;
    case ChState::Wait: snprintf(b, n, "Waiting for %s", L.ch[0].name); return b;
    case ChState::Jump: snprintf(b, n, "Jump to %s", tv); return b;
    case ChState::Keep:
      if (L.kept[ch] >= 0 && !TUI_SMALL) snprintf(b, n, "Keeps %s (%s%d)", tv, L.progType == PROG_SEQ ? "P" : "S", L.kept[ch] + 1);
      else snprintf(b, n, "Keeps %s", tv);
      return b;
    case ChState::Halt: return "Paused";
    case ChState::Pause: return "Program pause";
    case ChState::Done: snprintf(b, n, "Done \xC2\xB7 %s", tv); return b;
    default: break;
  }
  if (C.type == CH_LOCAL) {  // autotune (Channels screen) running / waiting for take-over
    LocalTune lt;
    localCtrlTuneGet(ch, lt);
    if (lt.state == TuneState::Running) {
      snprintf(b, n, "Autotune %.0f \xC2\xB0" "C", lt.sp);
      return b;
    }
    if (lt.state == TuneState::Done) return "Autotune done";
  }
  if (C.manualOn) {
    if (TUI_SMALL) snprintf(tv, sizeof(tv), "%.0f\xC2\xB0" "C", C.manualSp);
    else fmtTemp(C.manualSp, tv, sizeof(tv));
    snprintf(b, n, "Manual %s", tv);
    return b;
  }
  return active(L.state) && !o.inProgram ? "Not in the program" : "Idle";
}

static void tileUpdate(int ch) {
  Tile& t = tiles[ch];
  const ChannelLive& C = L.ch[ch];
  const ChOut& o = L.run[ch];
  const bool off = C.type == CH_OFF;
  char b[96];
  tSetText(t.name, C.name);
  tSetText(t.status, chStateText(ch, b, sizeof(b)));
  if ((C.fault != 0) != t.faultStyled) {
    t.faultStyled = C.fault != 0;
    if (t.faultStyled) lv_obj_add_style(t.status, &S_.danger, 0);
    else lv_obj_remove_style(t.status, &S_.danger, 0);
  }
  if (C.pvValid) snprintf(b, sizeof(b), "%.1f", C.pv);
  else strlcpy(b, "\xE2\x80\x94", sizeof(b));
  tSetText(t.pv, b);
  if (TUI_SMALL) lv_obj_align_to(t.pvUnit, t.pv, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -6);
  else lv_obj_align_to(t.pvUnit, t.pv, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -8);
  float sp = C.spEffValid ? C.spEff : NAN;
  if (isnan(sp) && active(L.state) && o.inProgram && !o.idle) sp = o.sp;
  if (isnan(sp) && C.manualOn) sp = C.manualSp;
  if (isnan(sp)) strlcpy(b, "\xE2\x80\x94", sizeof(b));
  else snprintf(b, sizeof(b), "%.1f", sp);
  tSetText(t.sp, b);
  if (C.outValid) snprintf(b, sizeof(b), TUI_SMALL ? "Out %.0f %%" : "Output %.0f %%", C.out);
  else b[0] = 0;
  tSetText(t.out, b);
  const float outPct = C.outValid ? fmaxf(0, fminf(100, C.out)) : 0;
  lv_obj_set_width(t.bar, (int32_t)(lv_obj_get_width(t.barBg) * outPct / 100.0f));

  // stepper: the run's target (current / kept) or the manual setpoint
  const bool runCh = active(L.state) && o.inProgram;
  t.runTarget = runCh && !isnan(o.target) && !o.idle;
  t.lo = S.ch[ch].spMin;
  t.hi = S.ch[ch].spMax;
  t.shown = t.runTarget ? o.target : C.manualSp;  // (after a run: the kept / end setpoint)
  if (!isnan(t.pend) && lv_tick_elaps(t.pendAt) > 700) {  // send after the last press
    sendTarget(ch, t.pend);
    t.pend = NAN;
  }
#if TUI_SMALL
  // the button shows the setpoint in force (ramping: the moving one), else what would be applied
  const float shown = !isnan(sp) ? sp : t.shown;
  if (isnan(shown)) strlcpy(b, "\xE2\x80\x94", sizeof(b));
  else snprintf(b, sizeof(b), "%.0f\xC2\xB0" "C", shown);
  tButtonText(t.value, b);
  if (t.spStyled != (int8_t)!isnan(sp)) {  // only on a change (a style change restyles the object)
    t.spStyled = (int8_t)!isnan(sp);
    lv_obj_t* vl = lv_obj_get_child(t.value, 0);
    if (t.spStyled) lv_obj_add_style(vl, &S_.sp[ch], 0);
    else lv_obj_remove_style(vl, &S_.sp[ch], 0);
  }
#else
  const float shown = isnan(t.pend) ? t.shown : t.pend;
  if (isnan(shown)) strlcpy(b, "\xE2\x80\x94", sizeof(b));
  else snprintf(b, sizeof(b), "%.0f \xC2\xB0" "C", shown);
  tButtonText(t.value, b);
#endif
  const bool stepOk = !off && !C.fault && (t.runTarget || !runCh);
  tSetEnabled(t.minus, stepOk);
  tSetEnabled(t.plus, stepOk);
  tSetEnabled(t.value, stepOk);

  // right-hand control: acknowledge (fault) / extend hold (run) / manual switch
  const bool fault = C.fault != 0;
  tSetHidden(t.ack, !fault);
  tSetHidden(t.minus, fault);
  tSetHidden(t.value, fault);
  tSetHidden(t.plus, fault);
  const bool ext = !fault && runCh && L.canExt[ch];
  tSetHidden(t.extend, !ext);
  const bool man = !fault && !runCh;
  tSetHidden(t.manSw, !man);
  if (man) {
    tSetChecked(t.manSw, C.manualOn);
    tSetEnabled(t.manSw, !off);
  }
}

// ------------------------------------------------------------ overlays
static void onHaltCont(lv_event_t*) { sendCmd(CMD_CONTINUE); }
static void onHaltStop(lv_event_t*) { onStop(nullptr); }

static void haltUpdate() {
  const bool show = active(L.state) && L.progPause && L.pauseMsg;
  if (!show) {
    if (halt) {
      lv_obj_delete(halt);
      halt = nullptr;
    }
    return;
  }
  if (!halt) {
    tModalClose();  // the request to act comes first
    halt = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(halt);
    lv_obj_add_style(halt, &S_.overlay, 0);
    lv_obj_set_size(halt, LV_PCT(100), LV_PCT(100));
    lv_obj_set_clickable(halt, true);
    lv_obj_set_scrollable(halt, false);
    lv_obj_move_to_index(halt, 0);  // under a popup opened from it (Stop?)
    lv_obj_t* box = lv_obj_create(halt);
    lv_obj_remove_style_all(box);
    lv_obj_add_style(box, &S_.dialog, 0);
    lv_obj_set_style_border_width(box, 3, 0);
    lv_obj_set_style_border_color(box, tc(C_AMBER), 0);
    lv_obj_set_size(box, TUI_SMALL ? SCREEN_W - 12 : 420, LV_SIZE_CONTENT);
    lv_obj_center(box);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(box, 8, 0);
    lv_obj_t* h = tLabel(box, TUI_SMALL ? &tfont_28c : &tfont_40c, "Action required");
    lv_obj_add_style(h, &S_.amber, 0);
    lv_obj_t* tx = tLabel(box, &tfont_20c, "");
    lv_obj_set_width(tx, LV_PCT(100));
    lv_obj_set_style_text_align(tx, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(tx, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t* wh = tLabel(box, &tfont_14, "");
    lv_obj_add_style(wh, &S_.muted, 0);
    lv_obj_set_width(wh, LV_PCT(100));
    lv_obj_set_style_text_align(wh, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(wh, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t* row = tRow(box, 10);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* st = tButton(row, "Stop", onHaltStop, nullptr, TUI_SMALL ? 110 : 120, TUI_SMALL ? 40 : 48);
    lv_obj_add_style(st, &S_.btnDanger, 0);
    lv_obj_t* co = tButton(row, "Continue", onHaltCont, nullptr, TUI_SMALL ? 150 : 160, TUI_SMALL ? 40 : 48);
    lv_obj_add_style(co, &S_.btnPri, 0);
    lv_obj_set_style_text_font(co, &tfont_20c, 0);
    lv_obj_set_style_text_font(st, &tfont_20c, 0);
  }
  lv_obj_t* box = lv_obj_get_child(halt, 0);
  tSetText(lv_obj_get_child(box, 1), L.pauseText[0] ? L.pauseText : "Continue when ready.");
  char b[96];
  if (L.progType == PROG_SEQ) snprintf(b, sizeof(b), "%s, phase %d. Setpoints are kept until you continue.", L.runName, L.seq.idx + 1);
  else snprintf(b, sizeof(b), "%s. Setpoints are kept until you continue.", L.runName);
  tSetText(lv_obj_get_child(box, 2), b);
}

static void onIntrAnswer(int b, int) {
  if (b == 3) sendCmd(CMD_RUN_RECOVER);
  else if (b == 1) sendCmd(CMD_RUN_DISMISS);
}

static void stageText(uint8_t st, char* b, size_t n) {
  static const char* const N[] = {"", "ramp", "waiting", "hold", "off", "pause", "done"};
  strlcpy(b, st < 7 ? N[st] : "", n);
}

static void intrOpen() {
  const RunPos& p = L.intrPos;
  char t[16], txt[400], where[128], s0[12], s1[12];
  fmtTime(p.el, t, sizeof(t));
  stageText(p.st[0], s0, sizeof(s0));
  stageText(p.st[1], s1, sizeof(s1));
  if (L.progType == PROG_SEQ) snprintf(where, sizeof(where), "phase %d of %d (%s)", p.i[0] + 1, L.phases, s0);
  else snprintf(where, sizeof(where), "%s step %d %s, %s step %d %s", L.ch[0].name, p.i[0] + 1, s0, L.ch[1].name, p.i[1] + 1, s1);
  snprintf(txt, sizeof(txt), "%s: \"%s\" at run time %s%s, %s. Its channels were switched off.%s", L.intrReason, L.runName,
           L.intrExact ? "" : "about ", t, where,
           L.progType == PROG_SEQ ? " Resume starts this phase again from the measured temperatures."
                                  : " Resume goes on at that run time.");
  static char resume[64];
  if (L.progType == PROG_SEQ) snprintf(resume, sizeof(resume), "Resume P%d", p.i[0] + 1);
  else snprintf(resume, sizeof(resume), "Resume at %s", t);
  tDialog("Run interrupted by a reset", txt, "Dismiss", "Later", resume, onIntrAnswer, 0);
  intrDlg = tModalId();
}

static void onBadge(lv_event_t*) {
  if (L.intrValid) intrOpen();
}

static void onSaveAnswer(int b, int) {
  if (b == 2) sendCmd(CMD_SAVE_RUN);
}

// ------------------------------------------------------------ header
static void headerUpdate() {
  const bool act = active(L.state);
  // the last run's result stays until another program is chosen
  const bool ended = (L.state == RunState::Finished || L.state == RunState::Aborted) && L.runProg == S.selProgram;
  char b[96], t[16];
  // badge
  const char* bt = "Ready";
  TColor bc = C_MUTED;
  if (L.intrValid && !act) bt = "Interrupted", bc = C_DANGER;
  else if (act && L.progPause) bt = L.pauseMsg ? "Action required" : "Program pause", bc = C_AMBER;
  else if (L.state == RunState::Running) bt = L.waiting ? "Waiting" : "Running", bc = L.waiting ? C_AMBER : C_OK;
  else if (L.state == RunState::Paused) bt = "Paused", bc = C_AMBER;
  else if (ended && L.state == RunState::Finished) bt = "Finished", bc = C_ACC;
  else if (ended) bt = "Stopped", bc = C_DANGER;
  else if (L.ch[0].manualOn || L.ch[1].manualOn) bt = "Manual", bc = C_MANUAL;
  tSetText(badge, bt);
  lv_obj_set_style_text_color(badge, tc(bc), 0);
  lv_obj_set_style_border_color(badge, tc(bc), 0);
  // program + where
  static ProgSummary sel;
  const bool haveSel = programSummary(S.selProgram, sel);
  if (act || ended || L.intrValid) {
    tSetText(lblProg, L.runName);
  } else {
    snprintf(b, sizeof(b), "%d  %s", S.selProgram + 1, haveSel ? sel.name : "(empty slot)");
    tSetText(lblProg, b);
  }
  if (act) {
    if (L.progType == PROG_SEQ) {
      static const char* const ST[] = {"", "ramp", "waiting for bottom", "hold", "off", "program pause", "done"};
      const uint8_t st = (uint8_t)L.seq.st;
      snprintf(b, sizeof(b), "Phase %d / %d \xC2\xB7 %s", L.seq.idx + 1, L.phases, st < 7 ? ST[st] : "");
      if (L.state == RunState::Paused) strlcat(b, " (paused)", sizeof(b));
    } else {
      snprintf(b, sizeof(b), "Timeline \xC2\xB7 %s step %d/%d \xC2\xB7 %s step %d/%d", L.ch[0].name, L.lane[0].idx + 1,
               L.steps[0], L.ch[1].name, L.lane[1].idx + 1, L.steps[1]);
    }
  } else if (ended) {
    fmtTime(L.elapsed, t, sizeof(t));
    snprintf(b, sizeof(b), "%s after %s \xC2\xB7 tap for the next program", L.state == RunState::Finished ? "Finished" : "Stopped", t);
  } else if (haveSel) {
    fmtTime(sel.total, t, sizeof(t));
    snprintf(b, sizeof(b), "%s \xC2\xB7 %u %s \xC2\xB7 %s%s", sel.type == PROG_TL ? "Timeline" : "Sequence", sel.items,
             sel.type == PROG_TL ? "steps" : "phases", t, sel.waits ? " + waits" : "");
  } else {
    strlcpy(b, "Tap to choose a program", sizeof(b));
  }
  tSetText(lblSub, b);
  // time
  if (act) {
    fmtTime(L.elapsed, t, sizeof(t));
    tSetText(lblTime, t);
    fmtTime(L.remaining, t, sizeof(t));
    snprintf(b, sizeof(b), "%s left", t);
    tSetText(lblRem, b);
  } else {
    tSetText(lblTime, ended ? (fmtTime(L.elapsed, t, sizeof(t)), t) : "0:00");
    if (haveSel && !ended) {
      fmtTime(sel.total, t, sizeof(t));
      snprintf(b, sizeof(b), "%s total", t);
    } else {
      b[0] = 0;
    }
    tSetText(lblRem, b);
  }
  // buttons
  const bool running = L.state == RunState::Running;
  tButtonText(btnRun, running && !L.progPause ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  tSetEnabled(btnRun, act || (haveSel && sel.items > 0));
  tSetEnabled(btnStop, act);
  tSetEnabled(btnNext, act && running && !L.progPause && L.progType == PROG_SEQ);
}

// ------------------------------------------------------------ create / update
#if TUI_SMALL
// 320x240: header (program, where, badge, time), chart, the two tiles, run buttons + menu at the bottom
lv_obj_t* mainCreate() {
  scr = lv_obj_create(nullptr);
  lv_obj_add_style(scr, &S_.scr, 0);
  lv_obj_set_scrollable(scr, false);

  lv_obj_t* hdr = tRow(scr, 6);
  lv_obj_set_pos(hdr, 0, 0);
  lv_obj_set_size(hdr, SCREEN_W, 30);
  lv_obj_set_style_pad_hor(hdr, 6, 0);
  titleBox = tCol(hdr, -2);
  lv_obj_set_flex_grow(titleBox, 1);
  lv_obj_set_clickable(titleBox, true);
  lv_obj_add_event_cb(titleBox, onTitle, LV_EVENT_CLICKED, nullptr);
  lblProg = tLabel(titleBox, &tfont_14, "");
  lv_obj_set_width(lblProg, 170);
  tOneLine(lblProg);
  lblSub = tLabel(titleBox, &tfont_12, "");
  lv_obj_add_style(lblSub, &S_.muted, 0);
  lv_obj_set_width(lblSub, 170);
  tOneLine(lblSub);
  badge = tLabel(hdr, &tfont_12, "");
  lv_obj_set_style_border_width(badge, 1, 0);
  lv_obj_set_style_radius(badge, 9, 0);
  lv_obj_set_style_pad_hor(badge, 5, 0);
  lv_obj_set_style_pad_ver(badge, 1, 0);
  lv_obj_set_clickable(badge, true);
  lv_obj_set_ext_click_area(badge, 8);
  lv_obj_add_event_cb(badge, onBadge, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* tcol = tCol(hdr, -3);
  lv_obj_set_flex_align(tcol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lblTime = tLabel(tcol, &tfont_16b, "");
  lblRem = tLabel(tcol, &tfont_12, "");
  lv_obj_add_style(lblRem, &S_.muted, 0);

  chart = chartCreate(scr);
  lv_obj_set_pos(chart, 2, 30);
  lv_obj_set_size(chart, SCREEN_W - 4, 76);
  lv_obj_add_event_cb(chart, onChart, LV_EVENT_CLICKED, nullptr);
  lblChartMode = tLabel(chart, &tfont_12, "");
  lv_obj_add_style(lblChartMode, &S_.muted, 0);
  lv_obj_align(lblChartMode, LV_ALIGN_TOP_RIGHT, -4, 0);

  const int ty = 30 + 76 + 2, tw = (SCREEN_W - 10) / 2, th = 96;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) tileCreateSmall(ch, scr, 3 + ch * (tw + 4), ty, tw, th);

  lv_obj_t* bar = tRow(scr, 4);
  lv_obj_set_pos(bar, 0, ty + th + 2);
  lv_obj_set_size(bar, SCREEN_W, SCREEN_H - (ty + th + 2));
  lv_obj_set_style_pad_hor(bar, 3, 0);
  const int bw = (SCREEN_W - 6 - 3 * 4) / 4, bh = SCREEN_H - (ty + th + 2) - 2;
  tIconButton(bar, LV_SYMBOL_LIST, onMenu, nullptr, bw, bh);
  btnRun = tIconButton(bar, LV_SYMBOL_PLAY, onRun, nullptr, bw, bh);
  btnStop = tIconButton(bar, LV_SYMBOL_STOP, onStop, nullptr, bw, bh);
  btnNext = tIconButton(bar, LV_SYMBOL_NEXT, onNext, nullptr, bw, bh);
  return scr;
}
#else
lv_obj_t* mainCreate() {
  scr = lv_obj_create(nullptr);
  lv_obj_add_style(scr, &S_.scr, 0);
  lv_obj_set_scrollable(scr, false);

  lv_obj_t* hdr = tRow(scr, 6);
  lv_obj_set_pos(hdr, 0, 0);
  lv_obj_set_size(hdr, SCREEN_W, 46);
  lv_obj_set_style_pad_hor(hdr, 4, 0);
  tIconButton(hdr, LV_SYMBOL_LIST, onMenu, nullptr, 42, 38);
  titleBox = tCol(hdr, 0);
  lv_obj_set_flex_grow(titleBox, 1);
  lv_obj_set_clickable(titleBox, true);
  lv_obj_add_event_cb(titleBox, onTitle, LV_EVENT_CLICKED, nullptr);
  lblProg = tLabel(titleBox, &tfont_16b, "");
  lv_obj_set_width(lblProg, 190);
  tOneLine(lblProg);
  lblSub = tLabel(titleBox, &tfont_12, "");
  lv_obj_add_style(lblSub, &S_.muted, 0);
  lv_obj_set_width(lblSub, 190);
  tOneLine(lblSub);
  badge = tLabel(hdr, &tfont_12, "");
  lv_obj_set_style_border_width(badge, 1, 0);
  lv_obj_set_style_radius(badge, 10, 0);
  lv_obj_set_style_pad_hor(badge, 7, 0);
  lv_obj_set_style_pad_ver(badge, 2, 0);
  lv_obj_set_clickable(badge, true);
  lv_obj_set_ext_click_area(badge, 8);
  lv_obj_add_event_cb(badge, onBadge, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* tcol = tCol(hdr, -2);
  lv_obj_set_flex_align(tcol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lblTime = tLabel(tcol, &tfont_20c, "");
  lblRem = tLabel(tcol, &tfont_12, "");
  lv_obj_add_style(lblRem, &S_.muted, 0);
  btnRun = tIconButton(hdr, LV_SYMBOL_PLAY, onRun, nullptr, 44, 38);
  btnStop = tIconButton(hdr, LV_SYMBOL_STOP, onStop, nullptr, 40, 38);
  btnNext = tIconButton(hdr, LV_SYMBOL_NEXT, onNext, nullptr, 40, 38);

  chart = chartCreate(scr);
  lv_obj_set_pos(chart, 4, 46);
  lv_obj_set_size(chart, SCREEN_W - 8, 132);
  lv_obj_add_event_cb(chart, onChart, LV_EVENT_CLICKED, nullptr);
  lblChartMode = tLabel(chart, &tfont_12, "");
  lv_obj_add_style(lblChartMode, &S_.muted, 0);
  lv_obj_align(lblChartMode, LV_ALIGN_TOP_RIGHT, -6, 3);

  const int ty = 46 + 132 + 4, tw = (SCREEN_W - 12) / 2, th = SCREEN_H - ty - 4;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) tileCreate(ch, scr, 4 + ch * (tw + 4), ty, tw, th);
  return scr;
}
#endif

void mainUpdate(const LiveData& live, const Settings& set) {
  L = live;
  S = set;
  headerUpdate();
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) tileUpdate(ch);
  // chart: every second (more often brings nothing: one point per second at most)
  if (!chartAt || lv_tick_elaps(chartAt) >= 1000) {
    chartAt = tTickNow();
    chartUpdate(chart, chartLive);
    char b[40];
    if (chartLive) {
      char p[16];
      fmtTime(chartData().span, p, sizeof(p));
      snprintf(b, sizeof(b), TUI_SMALL ? "Live \xC2\xB7 %s" : "Live \xC2\xB7 %s \xC2\xB7 tap: program", p);
    } else {
      strlcpy(b, TUI_SMALL ? (chartData().run ? "Run" : "Preview") : chartData().run ? "Run \xC2\xB7 tap: live" : "Program preview \xC2\xB7 tap: live", sizeof(b));
    }
    tSetText(lblChartMode, b);
  }
}

// on every screen: "Action required", interrupted run (once), end of a run with edits, messages
void mainEvents(const LiveData& live, const Settings& set) {
  L = live;
  S = set;
  haltUpdate();
  if (L.intrValid && !intrAsked && !tModalOpen()) {
    intrAsked = true;
    intrOpen();
  }
  if (!L.intrValid) {
    intrAsked = false;
    if (intrDlg && tModalId() == intrDlg) tModalClose();  // resumed / dismissed on the web page
    intrDlg = 0;
  }
  const bool ended = active(prevState) && (L.state == RunState::Finished || L.state == RunState::Aborted);
  if (ended && L.runEdited) {
    char b[120];
    snprintf(b, sizeof(b), "Save the changes made during the run into \"%s\"?", L.runName);
    tDialog("Run changed", b, "Discard", "Save", "", onSaveAnswer, 0);
  }
  if (active(L.state) && !active(prevState)) chartLive = false;
  prevState = L.state;
  if (strcmp(L.runMsg, lastMsg) != 0) {
    strlcpy(lastMsg, L.runMsg, sizeof(lastMsg));
    const bool bad = strstr(L.runMsg, "Cannot") || strstr(L.runMsg, "fault") || strstr(L.runMsg, "failed") ||
                     strstr(L.runMsg, "not possible") || strstr(L.runMsg, "No answer") || strstr(L.runMsg, "no answer");
    if (L.runMsg[0] && !strstr(L.runMsg, "Run interrupted")) tToast(L.runMsg, bad);
  }
}
