// Touch UI building blocks: labels, buttons, pages and the popups (number pad, keyboard, dialog,
// choice list, toast).
#include "tui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------ formatting
void fmtTime(float sec, char* out, size_t n) {
  if (isnan(sec) || sec < 0) sec = 0;
  const uint32_t t = (uint32_t)(sec + 0.5f);
  if (t >= 3600) snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60), (unsigned long)(t % 60));
  else snprintf(out, n, "%lu:%02lu", (unsigned long)(t / 60), (unsigned long)(t % 60));
}

void fmtTemp(float v, char* out, size_t n, bool unit) {
  if (isnan(v)) snprintf(out, n, "\xE2\x80\x94");  // em dash
  else snprintf(out, n, unit ? "%.1f \xC2\xB0" "C" : "%.1f", v);
}

// ------------------------------------------------------------ basics
lv_obj_t* tLabel(lv_obj_t* parent, const lv_font_t* f, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  if (f) lv_obj_set_style_text_font(l, f, 0);
  lv_label_set_text(l, text);
  return l;
}

void tOneLine(lv_obj_t* l) {
  lv_obj_set_height(l, lv_font_get_line_height(lv_obj_get_style_text_font(l, LV_PART_MAIN)));
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
}

void tSetText(lv_obj_t* label, const char* t) {
  if (!label) return;
  const char* cur = lv_label_get_text(label);
  if (!cur || strcmp(cur, t) != 0) lv_label_set_text(label, t);
}

static void styleButton(lv_obj_t* b) {
  lv_obj_remove_style_all(b);
  lv_obj_add_style(b, &S_.btn, 0);
  lv_obj_add_style(b, &S_.btnPressed, LV_STATE_PRESSED);
  lv_obj_add_style(b, &S_.btnDis, LV_STATE_DISABLED);
  lv_obj_add_style(b, &S_.btnChecked, LV_STATE_CHECKED);
  lv_obj_set_style_layout(b, LV_LAYOUT_FLEX, 0);
  lv_obj_set_style_flex_main_place(b, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_style_flex_cross_place(b, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_style_flex_track_place(b, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_ext_click_area(b, 4);
}

lv_obj_t* tButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user, int w, int h) {
  lv_obj_t* b = lv_button_create(parent);
  styleButton(b);
  lv_obj_set_size(b, w, h);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}

lv_obj_t* tIconButton(lv_obj_t* parent, const char* symbol, lv_event_cb_t cb, void* user, int w, int h) {
  lv_obj_t* b = tButton(parent, symbol, cb, user, w, h);
  lv_obj_set_style_pad_hor(b, 0, 0);
  lv_obj_set_style_text_font(b, &tfont_20c, 0);
  return b;
}

void tButtonText(lv_obj_t* btn, const char* text) {
  lv_obj_t* l = lv_obj_get_child(btn, 0);
  if (l) tSetText(l, text);
}

void tSetEnabled(lv_obj_t* o, bool en) {
  if (!o) return;
  if (en == !lv_obj_has_state(o, LV_STATE_DISABLED)) return;
  if (en) lv_obj_remove_state(o, LV_STATE_DISABLED);
  else lv_obj_add_state(o, LV_STATE_DISABLED);
}

void tSetHidden(lv_obj_t* o, bool hidden) {
  if (!o || lv_obj_is_hidden(o) == hidden) return;
  lv_obj_set_hidden(o, hidden);
}

lv_obj_t* tPanel(lv_obj_t* parent) {
  lv_obj_t* p = lv_obj_create(parent);
  lv_obj_remove_style_all(p);
  lv_obj_add_style(p, &S_.panel, 0);
  lv_obj_set_scrollable(p, false);
  return p;
}

static lv_obj_t* flexBox(lv_obj_t* parent, lv_flex_flow_t flow, int gap) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_set_flex_flow(r, flow);
  lv_obj_set_style_pad_gap(r, gap, 0);
  lv_obj_set_scrollable(r, false);
  return r;
}

lv_obj_t* tRow(lv_obj_t* parent, int gap) {
  lv_obj_t* r = flexBox(parent, LV_FLEX_FLOW_ROW, gap);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return r;
}

lv_obj_t* tCol(lv_obj_t* parent, int gap) {
  lv_obj_t* c = flexBox(parent, LV_FLEX_FLOW_COLUMN, gap);
  lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  return c;
}

lv_obj_t* tSwitch(lv_obj_t* parent, lv_event_cb_t cb, void* user) {
  lv_obj_t* s = lv_switch_create(parent);
  lv_obj_set_size(s, 50, 28);
  lv_obj_set_ext_click_area(s, 8);
  if (cb) lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, user);
  return s;
}

void tSetChecked(lv_obj_t* o, bool on) {
  if (!o || lv_obj_has_state(o, LV_STATE_CHECKED) == on) return;
  if (on) lv_obj_add_state(o, LV_STATE_CHECKED);
  else lv_obj_remove_state(o, LV_STATE_CHECKED);
}

TPage tPageCreate(const char* title, lv_event_cb_t back) {
  TPage p;
  p.scr = lv_obj_create(nullptr);
  lv_obj_add_style(p.scr, &S_.scr, 0);
  lv_obj_set_scrollable(p.scr, false);
  p.header = tRow(p.scr, 8);
  lv_obj_set_style_pad_all(p.header, TUI_SMALL ? 3 : 4, 0);
  lv_obj_set_style_pad_gap(p.header, TUI_SMALL ? 6 : 8, 0);
  lv_obj_set_height(p.header, T_HDR);
  tIconButton(p.header, LV_SYMBOL_LEFT, back, nullptr, TUI_SMALL ? 40 : 44, T_HDR - 8);
  p.title = tLabel(p.header, TUI_SMALL ? &tfont_16b : &tfont_20c, title);
  lv_obj_set_flex_grow(p.title, 1);
  p.body = lv_obj_create(p.scr);
  lv_obj_remove_style_all(p.body);
  lv_obj_set_pos(p.body, 0, T_HDR);
  lv_obj_set_size(p.body, SCREEN_W, SCREEN_H - T_HDR);
  lv_obj_set_flex_flow(p.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(p.body, TUI_SMALL ? 4 : 6, 0);
  lv_obj_set_style_pad_top(p.body, 2, 0);
  lv_obj_set_style_pad_gap(p.body, 2, 0);  // setting rows close together
  lv_obj_set_scroll_dir(p.body, LV_DIR_VER);
  return p;
}

// ------------------------------------------------------------ modal popups
static lv_obj_t* modal = nullptr;  // overlay on the top layer
static uint32_t modalGen = 0;      // counts the popups opened

bool tModalOpen() { return modal != nullptr; }
uint32_t tModalId() { return modal ? modalGen : 0; }

void tModalClose() {
  if (!modal) return;
  lv_obj_delete_async(modal);
  modal = nullptr;
}

// dark overlay + centred dialog box
static lv_obj_t* modalBox(int w, int h) {
  tModalClose();
  modal = lv_obj_create(lv_layer_top());
  ++modalGen;
  lv_obj_remove_style_all(modal);
  lv_obj_add_style(modal, &S_.overlay, 0);
  lv_obj_set_size(modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_clickable(modal, true);  // swallows taps on the screen behind
  lv_obj_set_scrollable(modal, false);
  lv_obj_t* box = lv_obj_create(modal);
  lv_obj_remove_style_all(box);
  lv_obj_add_style(box, &S_.dialog, 0);
  lv_obj_set_size(box, w, h);
  lv_obj_center(box);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(box, 8, 0);
  lv_obj_set_scrollable(box, false);
  return box;
}

// --- dialog
static TAnswerCb dlgCb = nullptr;
static int dlgArg = 0;

static void dlgClicked(lv_event_t* e) {
  const int b = (int)(intptr_t)lv_event_get_user_data(e);
  TAnswerCb cb = dlgCb;
  const int arg = dlgArg;
  dlgCb = nullptr;
  tModalClose();
  if (cb) cb(b, arg);
}

lv_obj_t* tDialog(const char* title, const char* text, const char* b1, const char* b2, const char* b3, TAnswerCb cb,
                  int arg, bool dangerPrimary) {
  lv_obj_t* box = modalBox(T_DLG_W, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(box, SCREEN_H - (TUI_SMALL ? 8 : 24), 0);
  if (TUI_SMALL) lv_obj_set_style_pad_all(box, 10, 0);
  dlgCb = cb;
  dlgArg = arg;
  lv_obj_t* t = tLabel(box, TUI_SMALL ? &tfont_16b : &tfont_20c, title);
  lv_obj_set_width(t, LV_PCT(100));
  lv_obj_t* l = tLabel(box, TUI_SMALL ? &tfont_12 : &tfont_14, text);
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_t* row = tRow(box, 8);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(row, 6, 0);
  const char* bs[3] = {b1, b2, b3};
  int last = 0;
  for (int i = 0; i < 3; ++i)
    if (bs[i] && *bs[i]) last = i;
  for (int i = 0; i < 3; ++i) {
    if (!bs[i] || !*bs[i]) continue;
    lv_obj_t* b = tButton(row, bs[i], dlgClicked, (void*)(intptr_t)(i + 1), LV_SIZE_CONTENT, TUI_SMALL ? 34 : 40);
    lv_obj_set_style_min_width(b, TUI_SMALL ? 70 : 84, 0);
    if (i == last) lv_obj_add_style(b, dangerPrimary ? &S_.btnDanger : &S_.btnPri, 0);
  }
  if (!b1 || !*b1) {  // no buttons: an OK to close
    lv_obj_t* b = tButton(row, "OK", dlgClicked, (void*)(intptr_t)0, TUI_SMALL ? 70 : 84, TUI_SMALL ? 34 : 40);
    lv_obj_add_style(b, &S_.btnPri, 0);
  }
  return box;
}

// --- number pad
static TNumCb numCb = nullptr;
static int numArg = 0;
static float numLo, numHi;
static int numDec;
static char numBuf[16];
static bool numFresh;  // the shown value is the old one: the first key replaces it
static lv_obj_t *numValue, *numHint;
static char numUnit[8];

static void numShow() {
  char b[32];
  snprintf(b, sizeof(b), "%s%s%s", numBuf[0] ? numBuf : "0", numUnit[0] ? " " : "", numUnit);
  tSetText(numValue, b);
}

static void numKey(lv_event_t* e) {
  const char* k = (const char*)lv_event_get_user_data(e);
  size_t n = strlen(numBuf);
  if (!strcmp(k, "del")) {
    if (numFresh) numBuf[0] = 0;
    else if (n) numBuf[n - 1] = 0;
    numFresh = false;
  } else if (!strcmp(k, "-")) {
    if (numBuf[0] == '-') memmove(numBuf, numBuf + 1, n);
    else if (n + 1 < sizeof(numBuf)) {
      memmove(numBuf + 1, numBuf, n + 1);
      numBuf[0] = '-';
    }
    numFresh = false;
  } else {
    if (numFresh) {
      numBuf[0] = 0;
      n = 0;
      numFresh = false;
    }
    if (!strcmp(k, ".") && (numDec == 0 || strchr(numBuf, '.'))) return;
    const char* dot = strchr(numBuf, '.');
    if (dot && (int)strlen(dot + 1) >= numDec) return;
    if (n + 1 < sizeof(numBuf) - 1) strcat(numBuf, k);
  }
  numShow();
}

static void numOk(lv_event_t*) {
  const float v = atof(numBuf);
  if (!numBuf[0] || v < numLo - 1e-6f || v > numHi + 1e-6f) {
    char b[64], lo[16], hi[16];
    snprintf(lo, sizeof(lo), "%.*f", numDec, numLo);
    snprintf(hi, sizeof(hi), "%.*f", numDec, numHi);
    snprintf(b, sizeof(b), "Allowed: %s \xE2\x80\xA6 %s %s", lo, hi, numUnit);
    tSetText(numHint, b);
    lv_obj_add_style(numHint, &S_.danger, 0);
    return;
  }
  TNumCb cb = numCb;
  const int arg = numArg;
  tModalClose();
  if (cb) cb(v, arg);
}

static void numCancel(lv_event_t*) { tModalClose(); }

void tNumPad(const char* title, float cur, float lo, float hi, int decimals, const char* unit, TNumCb cb, int arg) {
  // 480x320: 300 x 308; 320x240: almost the whole screen, lower keys
  constexpr int NW = TUI_SMALL ? 312 : 300, NH = TUI_SMALL ? 234 : 308, KH = TUI_SMALL ? 37 : 44, PAD = TUI_SMALL ? 6 : 10,
                GAP = TUI_SMALL ? 4 : 6;
  lv_obj_t* box = modalBox(NW, NH);
  lv_obj_set_style_pad_all(box, PAD, 0);
  lv_obj_set_style_pad_gap(box, GAP, 0);
  numCb = cb;
  numArg = arg;
  numLo = lo;
  numHi = hi;
  numDec = decimals;
  strlcpy(numUnit, unit ? unit : "", sizeof(numUnit));
  if (isnan(cur)) numBuf[0] = 0;
  else snprintf(numBuf, sizeof(numBuf), "%.*f", decimals, cur);
  numFresh = true;
  lv_obj_t* head = tRow(box, 6);
  lv_obj_t* t = tLabel(head, &tfont_16b, title);
  lv_obj_set_flex_grow(t, 1);
  tOneLine(t);
  numValue = tLabel(head, &tfont_28c, "");
  numShow();
  char b[64], l1[16], h1[16];
  snprintf(l1, sizeof(l1), "%.*f", decimals, lo);
  snprintf(h1, sizeof(h1), "%.*f", decimals, hi);
  snprintf(b, sizeof(b), "%s \xE2\x80\xA6 %s %s", l1, h1, numUnit);
  numHint = tLabel(box, &tfont_12, b);
  lv_obj_add_style(numHint, &S_.muted, 0);
  static const char* const K[] = {"7", "8", "9", "del", "4", "5", "6", "-", "1", "2", "3", ".", "0"};
  lv_obj_t* grid = lv_obj_create(box);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(grid, GAP, 0);
  const int kw = (NW - 2 * PAD - 2 - 3 * GAP) / 4;  // (2: the border)
  for (const char* k : K) {
    const bool del = !strcmp(k, "del");
    lv_obj_t* kb = tButton(grid, del ? LV_SYMBOL_BACKSPACE : !strcmp(k, "-") ? "\xC2\xB1" : k, numKey, (void*)k, kw, KH);
    lv_obj_set_style_text_font(kb, del || TUI_SMALL ? &tfont_20c : &tfont_28c, 0);
    if ((!strcmp(k, ".") && decimals == 0) || (!strcmp(k, "-") && lo >= 0)) tSetEnabled(kb, false);
  }
  lv_obj_t* cancel = tButton(grid, "Cancel", numCancel, nullptr, kw, KH);
  (void)cancel;
  lv_obj_t* ok = tButton(grid, "OK", numOk, nullptr, kw * 2 + GAP, KH);
  lv_obj_add_style(ok, &S_.btnPri, 0);
  lv_obj_set_style_text_font(ok, &tfont_16b, 0);
}

// --- keyboard
static TTextCb kbCb = nullptr;
static int kbArg = 0;
static lv_obj_t *kbTa, *kbEye;

static void kbEvent(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_READY) {
    char t[80];
    strlcpy(t, lv_textarea_get_text(kbTa), sizeof(t));
    TTextCb cb = kbCb;
    const int arg = kbArg;
    tModalClose();
    if (cb) cb(t, arg);
  } else if (c == LV_EVENT_CANCEL) {
    tModalClose();
  }
}

static void kbEyeClicked(lv_event_t*) {
  const bool hidden = lv_textarea_get_password_mode(kbTa);
  lv_textarea_set_password_mode(kbTa, !hidden);
  tButtonText(kbEye, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

void tKeyboard(const char* title, const char* cur, uint16_t maxLen, bool password, bool numeric, TTextCb cb, int arg) {
  tModalClose();
  kbCb = cb;
  kbArg = arg;
  modal = lv_obj_create(lv_layer_top());
  ++modalGen;
  lv_obj_remove_style_all(modal);
  lv_obj_add_style(modal, &S_.scr, 0);
  lv_obj_set_size(modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_clickable(modal, true);
  lv_obj_set_scrollable(modal, false);
  lv_obj_t* head = tRow(modal, TUI_SMALL ? 4 : 6);
  lv_obj_set_style_pad_all(head, TUI_SMALL ? 3 : 6, 0);
  constexpr int KBH = TUI_SMALL ? 32 : 40;  // header row height
  tIconButton(head, LV_SYMBOL_CLOSE, [](lv_event_t*) { tModalClose(); }, nullptr, TUI_SMALL ? 36 : 44, KBH);  // cancel: no change
  lv_obj_t* t = tLabel(head, TUI_SMALL ? &tfont_14 : &tfont_16b, title);
  lv_obj_set_width(t, TUI_SMALL ? 74 : 120);
  if (TUI_SMALL) lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
  tOneLine(t);
  kbTa = lv_textarea_create(head);
  lv_obj_set_flex_grow(kbTa, 1);
  lv_obj_set_height(kbTa, KBH);
  lv_textarea_set_one_line(kbTa, true);
  lv_textarea_set_max_length(kbTa, maxLen);
  lv_obj_set_style_text_font(kbTa, &tfont_16b, 0);
  lv_textarea_set_text(kbTa, cur ? cur : "");
  lv_textarea_set_password_mode(kbTa, password);
  kbEye = nullptr;
  if (password) kbEye = tIconButton(head, LV_SYMBOL_EYE_OPEN, kbEyeClicked, nullptr, TUI_SMALL ? 36 : 44, KBH);
  lv_obj_t* kb = lv_keyboard_create(modal);
  lv_obj_set_size(kb, SCREEN_W, SCREEN_H - KBH - (TUI_SMALL ? 8 : 14));
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_text_font(kb, TUI_SMALL ? &tfont_14 : &tfont_16b, 0);
  lv_keyboard_set_textarea(kb, kbTa);
  if (numeric) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  lv_obj_add_event_cb(kb, kbEvent, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(kb, kbEvent, LV_EVENT_CANCEL, nullptr);
}

// --- choice list
static TChoiceCb chCb = nullptr;
static int chArg = 0;

static void choiceClicked(lv_event_t* e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  TChoiceCb cb = chCb;
  const int arg = chArg;
  tModalClose();
  if (cb && i >= 0) cb(i, arg);
}

void tChoice(const char* title, const char* items, int sel, TChoiceCb cb, int arg) {
  int rows = 1;
  for (const char* q = items; q && *q; ++q) rows += *q == '\n';
  int h = TUI_SMALL ? 12 + 38 + rows * 34 + 4 : 20 + 44 + rows * 38 + 8;
  if (h > SCREEN_H - (TUI_SMALL ? 8 : 24)) h = SCREEN_H - (TUI_SMALL ? 8 : 24);
  lv_obj_t* box = modalBox(T_DLG_W, h);
  lv_obj_set_style_pad_all(box, TUI_SMALL ? 6 : 10, 0);
  chCb = cb;
  chArg = arg;
  lv_obj_t* head = tRow(box, 6);
  lv_obj_t* t = tLabel(head, TUI_SMALL ? &tfont_16b : &tfont_20c, title);
  lv_obj_set_flex_grow(t, 1);
  tOneLine(t);
  tIconButton(head, LV_SYMBOL_CLOSE, choiceClicked, (void*)(intptr_t)-1, 40, TUI_SMALL ? 30 : 34);
  lv_obj_t* list = lv_obj_create(box);
  lv_obj_remove_style_all(list);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  int i = 0;
  const char* p = items;
  lv_obj_t* selRow = nullptr;
  while (p && *p && i < 32) {
    const char* e = strchr(p, '\n');
    const size_t len = e ? (size_t)(e - p) : strlen(p);
    char text[96];
    snprintf(text, sizeof(text), "%.*s", (int)len, p);
    lv_obj_t* row = lv_button_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, &S_.listRow, 0);
    lv_obj_add_style(row, &S_.btnPressed, LV_STATE_PRESSED);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_t* l = tLabel(row, TUI_SMALL ? &tfont_14 : &tfont_16b, text);
    lv_obj_set_width(l, LV_PCT(100));
    tOneLine(l);
    if (i == sel) {
      lv_obj_add_style(l, &S_.acc, 0);
      selRow = row;
    }
    lv_obj_add_event_cb(row, choiceClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    ++i;
    p = e ? e + 1 : nullptr;
  }
  if (selRow) lv_obj_scroll_to_view(selRow, LV_ANIM_OFF);
}

// ------------------------------------------------------------ toast
static lv_obj_t* toast = nullptr;
static lv_timer_t* toastTimer = nullptr;

// t = the timer (it deletes itself after its one run); nullptr = closed by a tap
static void toastGone(lv_timer_t* t) {
  if (!t && toastTimer) lv_timer_delete(toastTimer);
  if (toast) lv_obj_delete_async(toast);
  toast = nullptr;
  toastTimer = nullptr;
}

void tToast(const char* text, bool bad) {
  if (toast) toastGone(nullptr);
  toast = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(toast);
  lv_obj_add_style(toast, &S_.dialog, 0);
  lv_obj_set_style_pad_all(toast, 10, 0);
  lv_obj_set_style_border_width(toast, 2, 0);
  lv_obj_set_style_border_color(toast, bad ? tc(C_DANGER) : tc(C_ACC), 0);
  lv_obj_set_size(toast, SCREEN_W - 40, LV_SIZE_CONTENT);
  // main screen: over the lower part of the chart (the channel controls stay free); else at the bottom
  lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, tCurrent() == SCR_MAIN ? -(SCREEN_H - TOAST_MAIN_BOTTOM) : -8);
  lv_obj_set_scrollable(toast, false);
  lv_obj_set_clickable(toast, true);
  lv_obj_add_event_cb(toast, [](lv_event_t*) { toastGone(nullptr); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = tLabel(toast, &tfont_14, text);
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
  if (bad) lv_obj_add_style(l, &S_.danger, 0);
  toastTimer = lv_timer_create(toastGone, 4500, nullptr);
  lv_timer_set_repeat_count(toastTimer, 1);
}
