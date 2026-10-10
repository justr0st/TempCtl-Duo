// Touch UI programs (spec 9.3.3): the program list and the table editor.
//   Sequence: one table, one row per phase (type, both channels' target / ramp / hold, option).
//   Timeline: two tables side by side, one per channel (target / ramp / hold).
// Tap a value to change it (number pad), tap # or Type for the phase / step menu (type, wait,
// pause text, insert, move, delete). Outside a run the edits go to a draft: Undo, Save, Discard;
// Start asks to save first. During a run the running program is shown with its run edits;
// targets and times can be changed there (run edits, spec 9.3.4), the structure is locked.
#include "tui.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void backToMenu(lv_event_t*) { tShow(SCR_MENU); }

// ------------------------------------------------------------ program list
static lv_obj_t* plBody;
static uint32_t plGen = 0xFFFFFFFF;
static int plSel = -1;
static int edSlot = -1;
void progEditOpen(int slot);

static void onListRow(lv_event_t* e) { progEditOpen((int)(intptr_t)lv_event_get_user_data(e)); }

lv_obj_t* progListCreate() {
  plGen = 0xFFFFFFFF;  // (320x240: the screen is built again each time)
  TPage p = tPageCreate("Programs", backToMenu);
  plBody = p.body;
  lv_obj_set_style_pad_gap(plBody, 4, 0);
  return p.scr;
}

void progListShow() {
  Settings S;
  settingsCopy(S);
  if (plGen == programStoreGen() && plSel == S.selProgram) return;
  plGen = programStoreGen();
  plSel = S.selProgram;
  static ProgSummary sums[PROG_MAX];
  programSummaries(sums);
  lv_obj_clean(plBody);
  for (int i = 0; i < PROG_MAX; ++i) {
    lv_obj_t* b = tButton(plBody, "", onListRow, (void*)(intptr_t)i, LV_PCT(100), 44);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(b, 10, 0);
    char t[96], tt[16];
    lv_obj_t* num = lv_obj_get_child(b, 0);
    snprintf(t, sizeof(t), "%d", i + 1);
    lv_label_set_text(num, t);
    lv_obj_set_width(num, 24);
    lv_obj_add_style(num, &S_.muted, 0);
    const ProgSummary& s = sums[i];
#if TUI_SMALL
    // 320x240: name above, summary below
    lv_obj_t* col = tCol(b, -2);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_t* name = tLabel(col, &tfont_16b, s.used ? s.name : "(empty)");
    lv_obj_set_width(name, LV_PCT(100));
    tOneLine(name);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
    if (!s.used) lv_obj_add_style(name, &S_.muted, 0);
    if (s.used) {
      fmtTime(s.total, tt, sizeof(tt));
      snprintf(t, sizeof(t), "%s \xC2\xB7 %u %s \xC2\xB7 %s%s", s.type == PROG_TL ? "Timeline" : "Sequence", s.items,
               s.type == PROG_TL ? "steps" : "phases", tt, s.waits ? " + waits" : "");
      lv_obj_t* sub = tLabel(col, &tfont_12, t);
      lv_obj_add_style(sub, &S_.muted, 0);
    }
    if (false) {
#else
    lv_obj_t* name = tLabel(b, &tfont_16b, s.used ? s.name : "(empty)");
    lv_obj_set_flex_grow(name, 1);
    tOneLine(name);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
    if (!s.used) lv_obj_add_style(name, &S_.muted, 0);
    if (s.used) {
#endif
      fmtTime(s.total, tt, sizeof(tt));
      snprintf(t, sizeof(t), "%s \xC2\xB7 %u %s \xC2\xB7 %s%s", s.type == PROG_TL ? "Timeline" : "Sequence", s.items,
               s.type == PROG_TL ? "steps" : "phases", tt, s.waits ? " + waits" : "");
      lv_obj_t* sub = tLabel(b, &tfont_12, t);
      lv_obj_add_style(sub, &S_.muted, 0);
    }
    if (i == S.selProgram) {
      lv_obj_t* tag = tLabel(b, &tfont_12, "next run");
      lv_obj_add_style(tag, &S_.acc, 0);
      lv_obj_add_style(b, &S_.btnChecked, 0);
    }
  }
}

// ------------------------------------------------------------ editor state
// undo steps: 15 on the S3 (PSRAM); 2 on the CYD (each snapshot is a whole program, ~3.9 kB of the
// short internal RAM; freed again when the editor is left, progEditRelease())
static constexpr int UNDO_N = TUI_SMALL ? 3 : 16;
static DuoProgram* ed = nullptr;           // the draft (or the running copy during a run)
static DuoProgram* undoBuf[UNDO_N];        // snapshots: [0..undoTop), current = undoPos
static int undoPos = 0, undoTop = 0, savedPos = 0;
static bool edEmpty = false;               // the slot has no program
static bool edRun = false;                 // showing the running copy (run edits)
static uint32_t edLoadedGen = 0;

static lv_obj_t *edScr, *edName, *edInfo, *edUndo, *edSave, *edBody, *edHead[2], *edTab[2], *edAdd[2], *edEmptyBox;
static Settings ES;  // settings copy (channel names, limits)

static DuoProgram* allocProg() {
  void* p = heap_caps_malloc(sizeof(DuoProgram), MALLOC_CAP_SPIRAM);
  if (!p) p = malloc(sizeof(DuoProgram));
  if (p) memset(p, 0, sizeof(DuoProgram));
  return (DuoProgram*)p;
}

static bool edDirty() { return !edRun && !edEmpty && undoPos != savedPos; }

static void undoReset() {
  undoPos = undoTop = savedPos = 0;
  if (!undoBuf[0]) undoBuf[0] = allocProg();
  if (undoBuf[0]) *undoBuf[0] = *ed;
  undoTop = 1;
}

// call after every change of *ed
static void undoPush() {
  if (undoPos + 1 >= UNDO_N) {  // drop the oldest
    DuoProgram* first = undoBuf[0];
    memmove(undoBuf, undoBuf + 1, sizeof(undoBuf[0]) * (UNDO_N - 1));
    undoBuf[UNDO_N - 1] = first;
    --undoPos;
    --savedPos;
  }
  ++undoPos;
  if (!undoBuf[undoPos]) undoBuf[undoPos] = allocProg();
  if (undoBuf[undoPos]) *undoBuf[undoPos] = *ed;
  undoTop = undoPos + 1;
}

static void edRefresh();

static void onUndo(lv_event_t*) {
  if (undoPos <= 0 || !undoBuf[undoPos - 1]) return;
  --undoPos;
  *ed = *undoBuf[undoPos];
  edRefresh();
}

// ------------------------------------------------------------ cells
static constexpr int SEQ_COLS = 9;
static const int SEQ_W[SEQ_COLS] = {30, 58, 50, 52, 52, 50, 52, 52, 66};
static const int TL_W[4] = {28, 62, 66, 66};
#if TUI_SMALL
// 320x240: Sequence = one line per phase (# | type | values | option), a tap opens the phase menu;
// Timeline = one table for the channel chosen with the switch above it
static constexpr int SEQ_COLS_S = 4;
static const int SEQ_W_S[SEQ_COLS_S] = {24, 50, 190, 46};
static const int TL_W_S[4] = {30, 84, 98, 98};
static int tlCh = 0;  // Timeline: the channel shown
#endif
static constexpr uint32_t MUTED = LV_TABLE_CELL_CTRL_CUSTOM_1, CURRENT = LV_TABLE_CELL_CTRL_CUSTOM_2,
                          ACCENT = LV_TABLE_CELL_CTRL_CUSTOM_3, LEFT = LV_TABLE_CELL_CTRL_CUSTOM_4;

static void fmtSec(uint32_t s, char* b, size_t n) { fmtTime((float)s, b, n); }

static void cell(lv_obj_t* t, int r, int c, const char* v, uint32_t ctrl = 0) {
  lv_table_set_cell_value(t, r, c, v);
  lv_table_clear_cell_ctrl(t, r, c, (lv_table_cell_ctrl_t)(MUTED | CURRENT | ACCENT | LEFT));
  if (ctrl) lv_table_set_cell_ctrl(t, r, c, (lv_table_cell_ctrl_t)ctrl);
}

static void seg(lv_obj_t* t, int r, int c0, const Seg& s, bool rampHold, uint32_t cur) {
  char b[16];
  snprintf(b, sizeof(b), "%.0f", s.t);
  cell(t, r, c0, b, cur);
  if (rampHold) {
    fmtSec(s.r, b, sizeof(b));
    cell(t, r, c0 + 1, b, cur);
    fmtSec(s.h, b, sizeof(b));
    cell(t, r, c0 + 2, b, cur);
  } else {
    cell(t, r, c0 + 1, "\xE2\x80\x94", MUTED | cur);
    cell(t, r, c0 + 2, "\xE2\x80\x94", MUTED | cur);
  }
}

#if !TUI_SMALL
static void none3(lv_obj_t* t, int r, int c0, uint32_t cur, const char* first = "\xE2\x80\x94") {
  cell(t, r, c0, first, MUTED | cur);
  cell(t, r, c0 + 1, "\xE2\x80\x94", MUTED | cur);
  cell(t, r, c0 + 2, "\xE2\x80\x94", MUTED | cur);
}
#endif

static int curRow(int ch) {  // the phase / step running now (-1 none)
  if (!edRun) return -1;
  LiveData L;
  liveCopy(L);
  if (!(L.state == RunState::Running || L.state == RunState::Paused)) return -1;
  return ed->type == PROG_SEQ ? L.seq.idx : L.lane[ch].idx;
}

#if TUI_SMALL
static void fillSeq() {
  lv_obj_t* t = edTab[0];
  const int n = ed->nPh;
  lv_table_set_row_count(t, n ? n : 1);
  const int cur = curRow(0);
  static const char* const KIND[] = {"Bottom", "Top", "Both", "Pause"};
  for (int i = 0; i < n; ++i) {
    const SeqPhase& f = ed->ph[i];
    const uint32_t c = i == cur ? CURRENT : 0;
    char b[64], r1[12], h1[12];
    snprintf(b, sizeof(b), "%d", i + 1);
    cell(t, i, 0, b, MUTED | c);
    cell(t, i, 1, f.kind < 4 ? KIND[f.kind] : "?", c);
    auto seg3 = [&](const Seg& g, char* out, size_t m) {
      fmtSec(g.r, r1, sizeof(r1));
      fmtSec(g.h, h1, sizeof(h1));
      snprintf(out, m, "%.0f\xC2\xB0 %s / %s", g.t, r1, h1);
    };
    switch (f.kind) {
      case PH_B: seg3(f.b, b, sizeof(b)); break;
      case PH_T: seg3(f.t, b, sizeof(b)); break;
      case PH_BT: {
        char tt[40];
        seg3(f.t, tt, sizeof(tt));
        if (f.bIdle) snprintf(b, sizeof(b), "B off \xC2\xB7 T %s", tt);
        else snprintf(b, sizeof(b), "B %.0f\xC2\xB0 \xC2\xB7 T %s", f.b.t, tt);
        break;
      }
      default: {
        const char* tx = progText(*ed, f.text);
        snprintf(b, sizeof(b), "%.30s", *tx ? tx : (f.msg ? "Action required" : "Pause"));
        break;
      }
    }
    cell(t, i, 2, b, (f.kind == PH_P ? MUTED : 0) | LEFT | c);
    const char* opt = (f.kind == PH_B || f.kind == PH_BT) && f.wait ? "Wait" : f.kind == PH_P && f.msg ? "Action" : "";
    cell(t, i, 3, opt, ACCENT | c);
  }
  if (!n)
    for (int k = 0; k < SEQ_COLS_S; ++k) cell(t, 0, k, k == 2 ? "no phases - tap to add" : "", MUTED | LEFT);
}
#else
static void fillSeq() {
  lv_obj_t* t = edTab[0];
  const int n = ed->nPh;
  lv_table_set_row_count(t, n ? n : 1);
  const int cur = curRow(0);
  static const char* const KIND[] = {"Bottom", "Top", "Both", "Pause"};
  for (int i = 0; i < n; ++i) {
    const SeqPhase& f = ed->ph[i];
    const uint32_t c = i == cur ? CURRENT : 0;
    char b[24];
    snprintf(b, sizeof(b), "%d", i + 1);
    cell(t, i, 0, b, MUTED | c);
    cell(t, i, 1, f.kind < 4 ? KIND[f.kind] : "?", c);
    switch (f.kind) {
      case PH_B:
        seg(t, i, 2, f.b, true, c);
        none3(t, i, 5, c, "keeps");
        break;
      case PH_T:
        none3(t, i, 2, c, "keeps");
        seg(t, i, 5, f.t, true, c);
        break;
      case PH_BT:
        if (f.bIdle) none3(t, i, 2, c, "off");
        else seg(t, i, 2, f.b, false, c);
        seg(t, i, 5, f.t, true, c);
        break;
      default: {
        const char* tx = progText(*ed, f.text);
        snprintf(b, sizeof(b), "%.20s", *tx ? tx : (f.msg ? "Action required" : "Pause"));
        cell(t, i, 2, b, MUTED | c);
        lv_table_set_cell_ctrl(t, i, 2, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
        for (int k = 3; k < 8; ++k) {
          cell(t, i, k, "", c);
          if (k < 7) lv_table_set_cell_ctrl(t, i, k, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
        }
        break;
      }
    }
    if (f.kind != PH_P)
      for (int k = 2; k < 8; ++k) lv_table_clear_cell_ctrl(t, i, k, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
    const char* opt = (f.kind == PH_B || f.kind == PH_BT) && f.wait ? "Wait" : f.kind == PH_P && f.msg ? "Action" : "";
    cell(t, i, 8, opt, ACCENT | c);
  }
  if (!n) {
    for (int k = 0; k < SEQ_COLS; ++k) cell(t, 0, k, k == 1 ? "no phases" : "", MUTED);
  }
}
#endif

static void fillTl(int ch) {
  lv_obj_t* t = edTab[TUI_SMALL ? 0 : ch];
  const int n = ed->nSt[ch];
  lv_table_set_row_count(t, n ? n : 1);
  const int cur = curRow(ch);
  for (int i = 0; i < n; ++i) {
    const TlStep& s = ed->st[ch][i];
    const uint32_t c = i == cur ? CURRENT : 0;
    char b[24];
    snprintf(b, sizeof(b), "%d", i + 1);
    cell(t, i, 0, b, MUTED | c);
    for (int k = 1; k < 4; ++k) lv_table_clear_cell_ctrl(t, i, k, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
    if (s.kind == ST_TARGET) {
      seg(t, i, 1, s.s, true, c);
    } else if (s.kind == ST_IDLE) {
      cell(t, i, 1, "off", MUTED | c);
      cell(t, i, 2, "\xE2\x80\x94", MUTED | c);
      fmtSec(s.s.h, b, sizeof(b));
      cell(t, i, 3, b, c);
    } else {
      const char* tx = progText(*ed, s.text);
      snprintf(b, sizeof(b), "%.18s", *tx ? tx : (s.msg ? "Action required" : "Pause"));
      cell(t, i, 1, b, ACCENT | c);
      lv_table_set_cell_ctrl(t, i, 1, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
      lv_table_set_cell_ctrl(t, i, 2, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
      cell(t, i, 2, "", c);
      cell(t, i, 3, "", c);
    }
  }
  if (!n) {
    for (int k = 0; k < 4; ++k) cell(t, 0, k, k == 1 ? "no steps" : "", MUTED);
  }
}

// cell colours: muted / accent text, the running phase / step highlighted
static void tableDraw(lv_event_t* e) {
  lv_draw_task_t* task = lv_event_get_draw_task(e);
  lv_draw_dsc_base_t* base = (lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(task);
  if (!base || base->part != LV_PART_ITEMS) return;
  lv_obj_t* t = (lv_obj_t*)lv_event_get_current_target(e);
  const uint32_t r = base->id1, c = base->id2;
  if (lv_draw_label_dsc_t* ld = lv_draw_task_get_label_dsc(task)) {
    if (lv_table_has_cell_ctrl(t, r, c, (lv_table_cell_ctrl_t)LEFT)) ld->align = LV_TEXT_ALIGN_LEFT;
    if (lv_table_has_cell_ctrl(t, r, c, (lv_table_cell_ctrl_t)MUTED)) ld->color = tc(C_MUTED);
    else if (lv_table_has_cell_ctrl(t, r, c, (lv_table_cell_ctrl_t)ACCENT)) ld->color = tc(C_ACC);
  }
  if (lv_draw_fill_dsc_t* fd = lv_draw_task_get_fill_dsc(task)) {
    if (lv_table_has_cell_ctrl(t, r, c, (lv_table_cell_ctrl_t)CURRENT)) {
      fd->color = tc(C_ACC);
      fd->opa = LV_OPA_20;
    }
  }
}

static lv_obj_t* makeTable(lv_obj_t* parent, int cols, const int* w) {
  lv_obj_t* t = lv_table_create(parent);
  lv_table_set_column_count(t, cols);
  int sum = 0;
  for (int i = 0; i < cols; ++i) {
    lv_table_set_column_width(t, i, w[i]);
    sum += w[i];
  }
  lv_obj_remove_style_all(t);
  lv_obj_add_style(t, &S_.cell, LV_PART_ITEMS);
  lv_obj_add_style(t, &S_.btnPressed, LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_size(t, sum, LV_SIZE_CONTENT);
  lv_obj_set_send_draw_task_events(t, true);
  lv_obj_add_event_cb(t, tableDraw, LV_EVENT_DRAW_TASK_ADDED, nullptr);
  return t;
}

static lv_obj_t* makeHeader(lv_obj_t* parent, int cols, const int* w) {
  lv_obj_t* h = makeTable(parent, cols, w);
  lv_obj_add_style(h, &S_.muted, LV_PART_ITEMS);
  lv_obj_set_style_text_font(h, &tfont_12, LV_PART_ITEMS);
  lv_obj_set_style_pad_ver(h, 2, LV_PART_ITEMS);
  lv_obj_set_clickable(h, false);
  return h;
}

// ------------------------------------------------------------ editing
static int cellCh, cellIdx, cellField;  // field 0 target, 1 ramp, 2 hold

static void editMsg(const char* m) { tToast(m, true); }

static void onCellValue(float v, int) {
  if (edRun) {  // run edits go to the control task (it checks what is allowed)
    static const uint8_t SUB[] = {EDIT_TARGET, EDIT_RAMP, EDIT_HOLD};
    sendCmd(CMD_RUN_EDIT, cellCh, v, cellIdx, SUB[cellField]);
    return;
  }
  Seg* s = nullptr;
  if (ed->type == PROG_TL) {
    TlStep& st = ed->st[cellCh][cellIdx];
    s = &st.s;
  } else {
    SeqPhase& f = ed->ph[cellIdx];
    s = cellCh == CH_BOTTOM ? &f.b : &f.t;
  }
  if (cellField == 0) s->t = v;
  else if (cellField == 1) s->r = (uint32_t)v;
  else s->h = (uint32_t)v;
  undoPush();
  edRefresh();
}

static void editField(int ch, int idx, int field, float cur) {
  cellCh = ch;
  cellIdx = idx;
  cellField = field;
  char title[64];
  static const char* const F[] = {"target", "ramp time", "hold time"};
  const char* name = ES.ch[ch].name;
  if (ed->type == PROG_SEQ) snprintf(title, sizeof(title), "P%d %s %s", idx + 1, name, F[field]);
  else snprintf(title, sizeof(title), "%s step %d %s", name, idx + 1, F[field]);
  if (field == 0) tNumPad(title, cur, ES.ch[ch].spMin, ES.ch[ch].spMax, 0, "\xC2\xB0" "C", onCellValue, 0);
  else tNumPad(title, cur, 0, PROG_MAX_SEC, 0, "s", onCellValue, 0);
}

// --- row menu
enum RowAct : uint8_t {
  A_KIND_B, A_KIND_T, A_KIND_BT, A_KIND_P, A_KIND_TARGET, A_KIND_IDLE, A_KIND_PAUSE, A_WAIT, A_BIDLE, A_MSG, A_TEXT,
  A_INS_ABOVE, A_INS_BELOW, A_UP, A_DOWN, A_DEL,
  A_FB_T, A_FB_R, A_FB_H, A_FT_T, A_FT_R, A_FT_H  // 320x240: edit a value (bottom / top: target, ramp, hold)
};
static uint8_t rowActs[24];
static int rowN, rowCh, rowIdx;

static int count() { return ed->type == PROG_SEQ ? ed->nPh : ed->nSt[rowCh]; }

static void newPhase(SeqPhase& f) {
  memset(&f, 0, sizeof(f));
  f.kind = PH_B;
  f.text = NO_TEXT;
  f.b = {150, 60, 60};
  f.t = {150, 60, 30};
}
static void newStep(TlStep& s) {
  memset(&s, 0, sizeof(s));
  s.kind = ST_TARGET;
  s.text = NO_TEXT;
  s.s = {150, 60, 60};
}

static void insertAt(int i) {
  if (ed->type == PROG_SEQ) {
    if (ed->nPh >= PROG_MAX_PHASES) return editMsg("A program has at most 100 phases.");
    memmove(&ed->ph[i + 1], &ed->ph[i], sizeof(SeqPhase) * (ed->nPh - i));
    newPhase(ed->ph[i]);
    ++ed->nPh;
  } else {
    TlStep* L = ed->st[rowCh];
    if (ed->nSt[rowCh] >= PROG_MAX_STEPS) return editMsg("A channel has at most 100 steps.");
    memmove(&L[i + 1], &L[i], sizeof(TlStep) * (ed->nSt[rowCh] - i));
    newStep(L[i]);
    ++ed->nSt[rowCh];
  }
}

static void onPauseText(const char* t, int) {
  uint8_t idx;
  if (!progAddText(*ed, t, idx)) return editMsg("Too many different pause texts in this program.");
  if (ed->type == PROG_SEQ) ed->ph[rowIdx].text = idx;
  else ed->st[rowCh][rowIdx].text = idx;
  undoPush();
  edRefresh();
}

static void onRowAct(int i, int) {
  if (i < 0 || i >= rowN) return;
  const uint8_t a = rowActs[i];
  const int n = count();
  SeqPhase* f = ed->type == PROG_SEQ ? &ed->ph[rowIdx] : nullptr;
  TlStep* s = ed->type == PROG_TL ? &ed->st[rowCh][rowIdx] : nullptr;
  if (a >= A_FB_T) {  // a value: the number entry (it does the undo step)
    const int ch = ed->type == PROG_TL ? rowCh : a >= A_FT_T ? CH_TOP : CH_BOTTOM, field = (a - A_FB_T) % 3;
    const Seg* g = progSeg(*ed, ch, rowIdx);
    if (!g && ed->type == PROG_TL) g = &ed->st[ch][rowIdx].s;  // an "off" step: its time
    if (g) editField(ch, rowIdx, field, field == 0 ? g->t : field == 1 ? g->r : g->h);
    return;
  }
  switch (a) {
    case A_KIND_B: case A_KIND_T: case A_KIND_BT: case A_KIND_P:
      f->kind = a - A_KIND_B;
      if (f->kind == PH_BT) f->wait = 1;
      if (f->kind == PH_P) f->wait = 0;
      break;
    case A_KIND_TARGET: s->kind = ST_TARGET; break;
    case A_KIND_IDLE: s->kind = ST_IDLE; break;
    case A_KIND_PAUSE: s->kind = ST_PAUSE; break;
    case A_WAIT: f->wait = !f->wait; break;
    case A_BIDLE: f->bIdle = !f->bIdle; break;
    case A_MSG:
      if (f) f->msg = !f->msg;
      else s->msg = !s->msg;
      break;
    case A_TEXT: {
      const char* cur = progText(*ed, f ? f->text : s->text);
      tKeyboard("Pause text (shown with the pause)", cur, PROG_TEXT_LEN - 1, false, false, onPauseText, 0);
      return;
    }
    case A_INS_ABOVE: insertAt(rowIdx); break;
    case A_INS_BELOW: insertAt(rowIdx + 1); break;
    case A_UP: case A_DOWN: {
      const int j = rowIdx + (a == A_UP ? -1 : 1);
      if (j < 0 || j >= n) return;
      if (f) {
        SeqPhase x = ed->ph[rowIdx];
        ed->ph[rowIdx] = ed->ph[j];
        ed->ph[j] = x;
      } else {
        TlStep x = ed->st[rowCh][rowIdx];
        ed->st[rowCh][rowIdx] = ed->st[rowCh][j];
        ed->st[rowCh][j] = x;
      }
      break;
    }
    case A_DEL:
      if (f) {
        memmove(&ed->ph[rowIdx], &ed->ph[rowIdx + 1], sizeof(SeqPhase) * (n - rowIdx - 1));
        --ed->nPh;
      } else {
        memmove(&ed->st[rowCh][rowIdx], &ed->st[rowCh][rowIdx + 1], sizeof(TlStep) * (n - rowIdx - 1));
        --ed->nSt[rowCh];
      }
      break;
  }
  undoPush();
  edRefresh();
}

static void rowMenu(int ch, int idx) {
  if (edRun && !TUI_SMALL) return editMsg("During a run only targets and times can be changed.");
  rowCh = ch;
  rowIdx = idx;
  rowN = 0;
  static char items[900];
  items[0] = 0;
  auto add = [&](uint8_t a, const char* t) {
    if (rowN) strlcat(items, "\n", sizeof(items));
    strlcat(items, t, sizeof(items));
    rowActs[rowN++] = a;
  };
  char b[64];
  const int n = count();
#if TUI_SMALL
  // the values first (the table shows them in one line)
  auto values = [&](int vch, uint8_t a0, bool rampHold, bool target = true) {
    const Seg* g = progSeg(*ed, vch, idx);
    if (!g && ed->type == PROG_TL) g = &ed->st[vch][idx].s;  // an "off" step: its time
    if (!g) return;
    char t1[12], t2[12];
    const char* nm = ES.ch[vch].name;
    if (target) {
      snprintf(b, sizeof(b), "%s target: %.0f \xC2\xB0" "C", nm, g->t);
      add(a0, b);
    }
    if (rampHold) {
      fmtSec(g->r, t1, sizeof(t1));
      fmtSec(g->h, t2, sizeof(t2));
      if (target) {
        snprintf(b, sizeof(b), "%s ramp: %s", nm, t1);
        add(a0 + 1, b);
      }
      snprintf(b, sizeof(b), "%s %s: %s", nm, target ? "hold" : "off for", t2);
      add(a0 + 2, b);
    }
  };
  if (ed->type == PROG_SEQ) {
    const SeqPhase& f = ed->ph[idx];
    if (f.kind == PH_B) values(CH_BOTTOM, A_FB_T, true);
    if (f.kind == PH_BT && !f.bIdle) values(CH_BOTTOM, A_FB_T, false);
    if (f.kind == PH_T || f.kind == PH_BT) values(CH_TOP, A_FT_T, true);
  } else {
    const TlStep& st = ed->st[ch][idx];
    if (st.kind == ST_TARGET) values(ch, A_FB_T, true);
    if (st.kind == ST_IDLE) values(ch, A_FB_T, true, false);
  }
  if (edRun) {
    if (!rowN) return editMsg("During a run only targets and times can be changed.");
    snprintf(b, sizeof(b), ed->type == PROG_SEQ ? "Phase %d (running copy)" : "Step %d (running copy)", idx + 1);
    tChoice(b, items, -1, onRowAct, 0);
    return;
  }
#endif
  if (ed->type == PROG_SEQ) {
    const SeqPhase& f = ed->ph[idx];
    snprintf(b, sizeof(b), "Type: %s (ramp, hold)", ES.ch[0].name);
    if (f.kind != PH_B) add(A_KIND_B, b);
    snprintf(b, sizeof(b), "Type: %s (ramp, hold)", ES.ch[1].name);
    if (f.kind != PH_T) add(A_KIND_T, b);
    if (f.kind != PH_BT) add(A_KIND_BT, "Type: Both (bottom jump + top ramp, hold)");
    if (f.kind != PH_P) add(A_KIND_P, "Type: Pause (wait for Continue)");
    if (f.kind == PH_B || f.kind == PH_BT) {
      snprintf(b, sizeof(b), "Wait for %s: %s", ES.ch[0].name, f.wait ? "on \xE2\x80\x94 turn off" : "off \xE2\x80\x94 turn on");
      add(A_WAIT, b);
    }
    if (f.kind == PH_BT) {
      snprintf(b, sizeof(b), "%s: %s", ES.ch[0].name, f.bIdle ? "off \xE2\x80\x94 give it a target" : "jump target \xE2\x80\x94 switch off");
      add(A_BIDLE, b);
    }
    if (f.kind == PH_P) {
      add(A_MSG, f.msg ? "\"Action required\": on \xE2\x80\x94 turn off" : "\"Action required\": off \xE2\x80\x94 turn on");
      add(A_TEXT, "Pause text \xE2\x80\xA6");
    }
  } else {
    const TlStep& s = ed->st[ch][idx];
    if (s.kind != ST_TARGET) add(A_KIND_TARGET, "Type: target (ramp, hold)");
    if (s.kind != ST_IDLE) add(A_KIND_IDLE, "Type: off (outputs off for a time)");
    if (s.kind != ST_PAUSE) add(A_KIND_PAUSE, "Type: pause (wait for Continue)");
    if (s.kind == ST_PAUSE) {
      add(A_MSG, s.msg ? "\"Action required\": on \xE2\x80\x94 turn off" : "\"Action required\": off \xE2\x80\x94 turn on");
      add(A_TEXT, "Pause text \xE2\x80\xA6");
    }
  }
  add(A_INS_ABOVE, "Insert above");
  add(A_INS_BELOW, "Insert below");
  if (idx > 0) add(A_UP, "Move up");
  if (idx + 1 < n) add(A_DOWN, "Move down");
  add(A_DEL, ed->type == PROG_SEQ ? "Delete phase" : "Delete step");
  if (ed->type == PROG_SEQ) snprintf(b, sizeof(b), "Phase %d", idx + 1);
  else snprintf(b, sizeof(b), "%s step %d", ES.ch[ch].name, idx + 1);
  tChoice(b, items, -1, onRowAct, 0);
}

static void onTable(lv_event_t* e) {
  lv_obj_t* t = (lv_obj_t*)lv_event_get_current_target(e);
  uint32_t r, c;
  lv_table_get_selected_cell(t, &r, &c);
  if (r == LV_TABLE_CELL_NONE || c == LV_TABLE_CELL_NONE) return;
#if TUI_SMALL
  const int ch = ed->type == PROG_TL ? tlCh : 0;
#else
  const int ch = t == edTab[1] ? 1 : 0;
#endif
  rowCh = ch;
  const int n = count();
  if (n == 0) {  // the "no phases / steps" row: add the first one
    if (!edRun) {
      insertAt(0);
      undoPush();
      edRefresh();
    }
    return;
  }
  if ((int)r >= n) return;
#if TUI_SMALL
  if (ed->type == PROG_SEQ) return rowMenu(0, r);  // one line per phase: its menu has the values
#endif
  if (ed->type == PROG_SEQ) {
    const SeqPhase& f = ed->ph[r];
    if (c <= 1 || c == 8) return rowMenu(0, r);
    const int cch = c >= 5 ? CH_TOP : CH_BOTTOM, field = (c - 2) % 3;
    const Seg* s = progSeg(*ed, cch, r);
    if (f.kind == PH_P) return rowMenu(0, r);
    if (!s || (cch == CH_BOTTOM && f.kind == PH_BT && field > 0)) return;
    editField(cch, r, field, field == 0 ? s->t : field == 1 ? s->r : s->h);
  } else {
    rowCh = ch;
    const TlStep& s = ed->st[ch][r];
    if (c == 0 || s.kind == ST_PAUSE) return rowMenu(ch, r);
    if (s.kind == ST_IDLE) {
      if (c == 3) editField(ch, r, 2, s.s.h);
      else rowMenu(ch, r);
      return;
    }
    const int field = c - 1;
    editField(ch, r, field, field == 0 ? s.s.t : field == 1 ? s.s.r : s.s.h);
  }
}

static void onAdd(lv_event_t* e) {
  if (edRun) return;
  rowCh = (int)(intptr_t)lv_event_get_user_data(e);
#if TUI_SMALL
  if (ed->type == PROG_TL) rowCh = tlCh;
#endif
  insertAt(count());
  undoPush();
  edRefresh();
  lv_obj_scroll_to_y(edBody, LV_COORD_MAX, LV_ANIM_ON);
}

// ------------------------------------------------------------ save / start / leave
static bool edSaveNow() {
  char err[128];
  const float lo[2] = {ES.ch[0].spMin, ES.ch[1].spMin}, hi[2] = {ES.ch[0].spMax, ES.ch[1].spMax};
  const char* names[2] = {ES.ch[0].name, ES.ch[1].name};
  if (!ed->name[0]) {
    editMsg("The program needs a name.");
    return false;
  }
  if (!progCheckLimits(*ed, lo, hi, err, sizeof(err), names) || !programSave(edSlot, *ed, err, sizeof(err))) {
    editMsg(err);
    return false;
  }
  savedPos = undoPos;
  edLoadedGen = programStoreGen();
  return true;
}

static void onSave(lv_event_t*) {
  if (edRun || edEmpty) return;
  if (edSaveNow()) tToast("Program saved.");
  edRefresh();
}

static void onLeaveAnswer(int b, int) {
  if (b == 3 && !edSaveNow()) return;
  if (b == 2) return;  // cancel
  tShow(SCR_PROGRAMS);
}

static void onEdBack(lv_event_t*) {
  if (edDirty()) {
    tDialog("Unsaved changes", "Save the changes to this program?", "Discard", "Cancel", "Save", onLeaveAnswer, 0);
    return;
  }
  tShow(SCR_PROGRAMS);
}

static void onRename(const char* t, int) {
  if (!*t) return editMsg("The program needs a name.");
  strlcpy(ed->name, t, sizeof(ed->name));
  undoPush();
  edRefresh();
}

static void onStartAnswer(int b, int) {
  if (b == 3) {
    if (!edSaveNow()) return;
    selectProgram(edSlot);
    sendCmd(CMD_START, edSlot);
  } else if (b == 2) {
    startDraft(edSlot, *ed);
  } else {
    return;
  }
  tShow(SCR_MAIN);
}

static void onDeleteAnswer(int b, int) {
  if (b != 2) return;
  programDelete(edSlot);
  tShow(SCR_PROGRAMS);
}

static void edLoad();

static void onMore(int i, int) {
  LiveData L;
  liveCopy(L);
  const bool active = L.state == RunState::Running || L.state == RunState::Paused;
  switch (i) {
    case 0:  // start
      if (active) return editMsg("A program is running.");
      if (progItems(*ed) == 0) return editMsg("The program has no phases / steps.");
      if (edDirty()) {
        tDialog("Start the program", "It has unsaved changes.", "Cancel", "Start without saving", "Save and start",
                onStartAnswer, 0);
        return;
      }
      selectProgram(edSlot);
      sendCmd(CMD_START, edSlot);
      tShow(SCR_MAIN);
      return;
    case 1:  // select
      selectProgram(edSlot);
      tToast("Selected for the next run.");
      return;
    case 2: tKeyboard("Program name", ed->name, PROG_NAME_LEN - 1, false, false, onRename, 0); return;
    case 3:  // discard
      edLoad();
      return;
    case 4:
      tDialog("Delete the program?", "The slot becomes empty. This can't be undone.", "Cancel", "Delete", "",
              onDeleteAnswer, 0, true);
      return;
  }
}

static void onMoreBtn(lv_event_t*) {
  if (edRun) return editMsg("This is the running program: only targets and times can be changed now.");
  if (edEmpty) return;
  tChoice("Program", "Start now\nSelect for the next run\nRename\nDiscard changes\nDelete program", -1, onMore, 0);
}

static void onNew(lv_event_t* e) {
  const int type = (int)(intptr_t)lv_event_get_user_data(e);
  char name[24];
  snprintf(name, sizeof(name), "Program %d", edSlot + 1);
  progClear(*ed, type, name);
  if (type == PROG_SEQ) {
    newPhase(ed->ph[0]);
    ed->nPh = 1;
  } else {
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      newStep(ed->st[ch][0]);
      ed->nSt[ch] = 1;
    }
  }
  edEmpty = false;
  undoReset();
  savedPos = -1;  // new: not saved yet
  edRefresh();
}

// ------------------------------------------------------------ layout
static lv_obj_t* edCreate() {
  edScr = lv_obj_create(nullptr);
  lv_obj_add_style(edScr, &S_.scr, 0);
  lv_obj_set_scrollable(edScr, false);
  lv_obj_t* hdr = tRow(edScr, TUI_SMALL ? 4 : 6);
  lv_obj_set_size(hdr, SCREEN_W, T_HDR);
  lv_obj_set_style_pad_hor(hdr, TUI_SMALL ? 3 : 4, 0);
  constexpr int HB = T_HDR - 8, NW = TUI_SMALL ? 118 : 230;
  tIconButton(hdr, LV_SYMBOL_LEFT, onEdBack, nullptr, TUI_SMALL ? 36 : 44, HB);
  lv_obj_t* col = tCol(hdr, TUI_SMALL ? -2 : 0);
  lv_obj_set_flex_grow(col, 1);
  edName = tLabel(col, TUI_SMALL ? &tfont_14 : &tfont_16b, "");
  lv_obj_set_width(edName, NW);
  tOneLine(edName);
  edInfo = tLabel(col, &tfont_12, "");
  lv_obj_add_style(edInfo, &S_.muted, 0);
  lv_obj_set_width(edInfo, NW);
  tOneLine(edInfo);
  edUndo = tButton(hdr, "Undo", onUndo, nullptr, TUI_SMALL ? 50 : 58, HB);
  edSave = tButton(hdr, "Save", onSave, nullptr, TUI_SMALL ? 50 : 58, HB);
  tIconButton(hdr, LV_SYMBOL_BARS, onMoreBtn, nullptr, TUI_SMALL ? 36 : 40, HB);

  edBody = lv_obj_create(edScr);
  lv_obj_remove_style_all(edBody);
  lv_obj_set_pos(edBody, 0, T_HDR);
  lv_obj_set_size(edBody, SCREEN_W, SCREEN_H - T_HDR);
  lv_obj_set_style_pad_hor(edBody, TUI_SMALL ? 4 : 6, 0);
  lv_obj_set_style_pad_bottom(edBody, 6, 0);
  lv_obj_set_scroll_dir(edBody, LV_DIR_VER);
  return edScr;
}

// (re)builds the tables for the program's type
static uint8_t builtType = 0xFF;
#if TUI_SMALL
static lv_obj_t* edTlBtn[2];
static void edRefresh();
static void onTlCh(lv_event_t* e) {
  tlCh = (int)(intptr_t)lv_event_get_user_data(e);
  edRefresh();
  lv_obj_scroll_to_y(edBody, 0, LV_ANIM_OFF);
}
// 320x240: the body is a column (switch | header | table | add button)
static void edBuild() {
  if (builtType == ed->type && !edEmpty) return;
  lv_obj_clean(edBody);
  edTab[0] = edTab[1] = edHead[0] = edHead[1] = edAdd[0] = edAdd[1] = edEmptyBox = edTlBtn[0] = edTlBtn[1] = nullptr;
  builtType = edEmpty ? 0xFE : ed->type;
  lv_obj_set_flex_flow(edBody, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(edBody, 0, 0);
  if (edEmpty) {
    edEmptyBox = tCol(edBody, 8);
    lv_obj_set_width(edEmptyBox, LV_PCT(100));
    lv_obj_set_style_pad_top(edEmptyBox, 16, 0);
    lv_obj_set_flex_align(edEmptyBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    tLabel(edEmptyBox, &tfont_16b, "This slot is empty.");
    lv_obj_t* row = tRow(edEmptyBox, 8);
    lv_obj_set_width(row, LV_SIZE_CONTENT);
    tButton(row, "New sequence", onNew, (void*)(intptr_t)PROG_SEQ, 140, 40);
    tButton(row, "New timeline", onNew, (void*)(intptr_t)PROG_TL, 140, 40);
    lv_obj_t* h = tLabel(edEmptyBox, &tfont_12, "Sequence: phases one after another. Timeline: one step list per channel, started together.");
    lv_obj_add_style(h, &S_.muted, 0);
    lv_obj_set_width(h, 290);
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
    return;
  }
  if (ed->type == PROG_SEQ) {
    edHead[0] = makeHeader(edBody, SEQ_COLS_S, SEQ_W_S);
    edTab[0] = makeTable(edBody, SEQ_COLS_S, SEQ_W_S);
    lv_obj_add_event_cb(edTab[0], onTable, LV_EVENT_VALUE_CHANGED, nullptr);
    edAdd[0] = tButton(edBody, "Add phase", onAdd, (void*)(intptr_t)0, 130, 34);
  } else {
    lv_obj_t* sw = tRow(edBody, 4);
    lv_obj_set_style_pad_bottom(sw, 4, 0);
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      edTlBtn[ch] = tButton(sw, "", onTlCh, (void*)(intptr_t)ch, (SCREEN_W - 12) / 2, 30);
      tOneLine(lv_obj_get_child(edTlBtn[ch], 0));
      lv_obj_set_width(lv_obj_get_child(edTlBtn[ch], 0), (SCREEN_W - 12) / 2 - 12);
    }
    edHead[0] = makeHeader(edBody, 4, TL_W_S);
    edTab[0] = makeTable(edBody, 4, TL_W_S);
    lv_obj_add_event_cb(edTab[0], onTable, LV_EVENT_VALUE_CHANGED, nullptr);
    edAdd[0] = tButton(edBody, "Add step", onAdd, (void*)(intptr_t)0, 130, 34);
  }
  lv_obj_set_style_margin_top(edAdd[0], 6, 0);
}
#else
static void edBuild() {
  if (builtType == ed->type && !edEmpty) return;
  lv_obj_clean(edBody);
  edTab[0] = edTab[1] = edHead[0] = edHead[1] = edAdd[0] = edAdd[1] = edEmptyBox = nullptr;
  builtType = edEmpty ? 0xFE : ed->type;
  if (edEmpty) {
    edEmptyBox = tCol(edBody, 10);
    lv_obj_set_width(edEmptyBox, LV_PCT(100));
    lv_obj_set_style_pad_top(edEmptyBox, 30, 0);
    lv_obj_set_flex_align(edEmptyBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    tLabel(edEmptyBox, &tfont_16b, "This slot is empty.");
    lv_obj_t* row = tRow(edEmptyBox, 10);
    lv_obj_set_width(row, LV_SIZE_CONTENT);
    tButton(row, "New sequence", onNew, (void*)(intptr_t)PROG_SEQ, 170, 44);
    tButton(row, "New timeline", onNew, (void*)(intptr_t)PROG_TL, 170, 44);
    lv_obj_t* h = tLabel(edEmptyBox, &tfont_12, "Sequence: phases one after another. Timeline: one step list per channel, started together.");
    lv_obj_add_style(h, &S_.muted, 0);
    lv_obj_set_width(h, 360);
    lv_label_set_long_mode(h, LV_LABEL_LONG_MODE_WRAP);
    return;
  }
  if (ed->type == PROG_SEQ) {
    edHead[0] = makeHeader(edBody, SEQ_COLS, SEQ_W);
    lv_table_set_row_count(edHead[0], 2);
    edTab[0] = makeTable(edBody, SEQ_COLS, SEQ_W);
    lv_obj_set_pos(edTab[0], 0, 34);
    lv_obj_add_event_cb(edTab[0], onTable, LV_EVENT_VALUE_CHANGED, nullptr);
    edAdd[0] = tButton(edBody, "Add phase", onAdd, (void*)(intptr_t)0, 140, 38);
  } else {
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      edHead[ch] = makeHeader(edBody, 4, TL_W);
      lv_table_set_row_count(edHead[ch], 2);
      lv_obj_set_pos(edHead[ch], ch * 238, 0);
      edTab[ch] = makeTable(edBody, 4, TL_W);
      lv_obj_set_pos(edTab[ch], ch * 238, 34);
      lv_obj_add_event_cb(edTab[ch], onTable, LV_EVENT_VALUE_CHANGED, nullptr);
      edAdd[ch] = tButton(edBody, "Add step", onAdd, (void*)(intptr_t)ch, 110, 38);
    }
  }
}
#endif

static void edRefresh() {
  edBuild();
  char b[96], t[16];
  tSetText(edName, edEmpty ? "(empty slot)" : ed->name);
  if (edEmpty) {
    snprintf(b, sizeof(b), "Slot %d", edSlot + 1);
  } else {
    fmtTime(programPreview(*ed, nullptr, nullptr, nullptr), t, sizeof(t));
    snprintf(b, sizeof(b), "%d \xC2\xB7 %s \xC2\xB7 %s%s%s", edSlot + 1, ed->type == PROG_TL ? "Timeline" : "Sequence", t,
             edRun ? " \xC2\xB7 running copy" : "", edDirty() ? " \xC2\xB7 unsaved" : "");
  }
  tSetText(edInfo, b);
  tSetEnabled(edUndo, !edRun && !edEmpty && undoPos > 0);
  tSetEnabled(edSave, edDirty());
  if (edDirty()) lv_obj_add_style(edSave, &S_.btnPri, 0);
  else lv_obj_remove_style(edSave, &S_.btnPri, 0);
  if (edEmpty) return;
#if TUI_SMALL
  if (ed->type == PROG_SEQ) {
    static const char* const H[SEQ_COLS_S] = {"#", "Type", "Target  ramp / hold", ""};
    for (int c = 0; c < SEQ_COLS_S; ++c) lv_table_set_cell_value(edHead[0], 0, c, H[c]);
    fillSeq();
  } else {
    static const char* const H[4] = {"#", "\xC2\xB0" "C", "Ramp", "Hold"};
    for (int c = 0; c < 4; ++c) lv_table_set_cell_value(edHead[0], 0, c, H[c]);
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      char b2[48];
      snprintf(b2, sizeof(b2), "%s \xC2\xB7 %u", ES.ch[ch].name, (unsigned)ed->nSt[ch]);
      tButtonText(edTlBtn[ch], b2);
      tSetChecked(edTlBtn[ch], ch == tlCh);
    }
    fillTl(tlCh);
  }
  tSetHidden(edAdd[0], edRun);
  return;
#endif
  if (ed->type == PROG_SEQ) {
    lv_obj_t* h = edHead[0];
    static const char* const H1[SEQ_COLS] = {"#", "Type", "", "", "", "", "", "", "Option"};
    static const char* const H2[SEQ_COLS] = {"", "", "\xC2\xB0" "C", "Ramp", "Hold", "\xC2\xB0" "C", "Ramp", "Hold", ""};
    for (int c = 0; c < SEQ_COLS; ++c) {
      lv_table_set_cell_value(h, 0, c, c == 2 ? ES.ch[0].name : c == 5 ? ES.ch[1].name : H1[c]);
      lv_table_set_cell_value(h, 1, c, H2[c]);
    }
    for (int c : {2, 3, 5, 6}) lv_table_set_cell_ctrl(h, 0, c, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
    fillSeq();
    lv_obj_update_layout(edTab[0]);
    lv_obj_set_pos(edAdd[0], 0, 34 + lv_obj_get_height(edTab[0]) + 6);
    tSetHidden(edAdd[0], edRun);
  } else {
    int32_t maxH = 0;
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      lv_obj_t* h = edHead[ch];
      lv_table_set_cell_value(h, 0, 0, ES.ch[ch].name);
      for (int c = 1; c < 4; ++c) lv_table_set_cell_value(h, 0, c, "");
      for (int c = 0; c < 3; ++c) lv_table_set_cell_ctrl(h, 0, c, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
      static const char* const H2[4] = {"#", "\xC2\xB0" "C", "Ramp", "Hold"};
      for (int c = 0; c < 4; ++c) lv_table_set_cell_value(h, 1, c, H2[c]);
      fillTl(ch);
      lv_obj_update_layout(edTab[ch]);
      maxH = LV_MAX(maxH, lv_obj_get_height(edTab[ch]));
    }
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      lv_obj_set_pos(edAdd[ch], ch * 238, 34 + maxH + 6);
      tSetHidden(edAdd[ch], edRun);
    }
  }
}

static void edLoad() {
  if (!ed) ed = allocProg();
  if (!ed) return;
  settingsCopy(ES);
  LiveData L;
  liveCopy(L);
  const bool active = L.state == RunState::Running || L.state == RunState::Paused;
  edRun = active && L.runProg == edSlot;
  char err[96];
  memset(ed, 0, sizeof(DuoProgram));
  if (edRun) edRun = runProgramCopy(*ed);
  edEmpty = false;
  if (!edRun && !programLoad(edSlot, *ed, err, sizeof(err))) {
    edEmpty = true;
    progClear(*ed, PROG_SEQ, "");
  }
  edLoadedGen = programStoreGen();
  undoReset();
  builtType = 0xFF;
  edRefresh();
  lv_obj_scroll_to_y(edBody, 0, LV_ANIM_OFF);
}

void progEditOpen(int slot) {
  edSlot = slot;
  tShow(SCR_PROGEDIT);
  edLoad();
}

// shown without progEditOpen() (320x240: built again after it was left): load the program again
void progEditShow() {
  if (ed) return;
  if (edSlot < 0) edSlot = 0;
  edLoad();
}

lv_obj_t* progEditCreate() {
  builtType = 0xFF;  // (320x240: the screen is built again each time)
  return edCreate();
}

void progEditRelease() {
  free(ed);
  ed = nullptr;
  for (DuoProgram*& u : undoBuf) {
    free(u);
    u = nullptr;
  }
  undoPos = undoTop = savedPos = 0;
}

// every 250 ms while the editor is shown: the running copy changes with run edits and time
void progEditUpdate() {
  static uint32_t at = 0;
  if (!edRun || tModalOpen() || (at && lv_tick_elaps(at) < 1000)) return;
  at = tTickNow();
  LiveData L;
  liveCopy(L);
  if (!(L.state == RunState::Running || L.state == RunState::Paused)) {  // the run ended
    edLoad();
    return;
  }
  runProgramCopy(*ed);
  edRefresh();
}
