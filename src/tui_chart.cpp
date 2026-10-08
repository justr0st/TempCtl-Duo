// Touch UI chart: both channels drawn by hand (LV_EVENT_DRAW_MAIN), like the web chart.
//   Program view: during a run the run's time axis (setpoints and temperatures so far, the forecast
//   dashed, a "now" line); otherwise the selected program's plan.
//   Live view: the live history over the chosen period (display.liveMin), 0 .. highest value.
#include "tui.h"
#ifdef UI_V2
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <esp_heap_caps.h>

// last snapshot (~6 kB): in PSRAM where there is some (internal RAM is the scarce one)
static ChartData& chartStore() {
#ifdef BOARD_HAS_PSRAM
  void* p = heap_caps_malloc(sizeof(ChartData), MALLOC_CAP_SPIRAM);
#else
  void* p = nullptr;
#endif
  if (!p) p = malloc(sizeof(ChartData));
  memset(p, 0, sizeof(ChartData));
  return *(ChartData*)p;
}
static ChartData& cd = chartStore();
#ifdef CHART_LOHI
#define LO(ch) ((ch) ? cd.loT : cd.lo)
#define HI(ch) ((ch) ? cd.hiT : cd.hi)
#else
#define LO(ch) nullptr
#define HI(ch) nullptr
#endif
static bool cdLive = false;  // the snapshot is shown as the live view
static bool cdValid = false;

static constexpr int PAD_L = 30, PAD_R = 8, PAD_T = 10, PAD_B = 16;

static float niceTop(float mx, float& step) {
  const float top = fmaxf(fmaxf(mx * 1.06f, mx + 2), 10);
  static const float S[] = {1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000};
  step = 1000;
  for (float s : S)
    if (top / s <= 4.0f) {
      step = s;
      break;
    }
  return ceilf(top / step) * step;
}

static float tickStep(float span, int px) {
  static const float S[] = {5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 10800, 14400, 21600, 43200};
  for (float s : S)
    if (px / (span / s) >= 64) return s;
  return 86400;
}

static void fmtTick(float t, char* out, size_t n) {
  const bool neg = t < -0.5f;
  const float a = fabsf(t);
  char b[16];
  if (a >= 3600 && fmodf(a, 3600) < 0.5f) snprintf(b, sizeof(b), "%d h", (int)(a / 3600 + 0.5f));
  else fmtTime(a, b, sizeof(b));
  snprintf(out, n, "%s%s", neg ? "\xE2\x88\x92" : "", b);  // U+2212 minus
}

struct Plot {
  lv_layer_t* layer;
  int32_t x0, y0, w, h;  // plot area (screen coordinates)
  float t0, t1, ymax;
  float X(float t) const { return x0 + w * (t - t0) / (t1 - t0); }
  float Y(float v) const { return y0 + h - h * v / ymax; }
};

static void line(const Plot& P, float x1, float y1, float x2, float y2, lv_color_t c, int width, int dash = 0,
                 lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = c;
  d.width = width;
  d.opa = opa;
  d.round_start = d.round_end = width > 1;
  if (dash) {
    d.dash_width = dash;
    d.dash_gap = dash;
  }
  d.p1.x = x1;
  d.p1.y = y1;
  d.p2.x = x2;
  d.p2.y = y2;
  lv_draw_line(P.layer, &d);
}

static void text(const Plot& P, int32_t x, int32_t y, int32_t w, const char* s, lv_color_t c, lv_text_align_t al) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.color = c;
  d.font = &tfont_12;
  d.align = al;
  d.text = s;
  d.text_local = 1;
  lv_area_t a = {x, y, x + w - 1, y + 14};
  lv_draw_label(P.layer, &d, &a);
}

// One series: points i (time tOf(i)) from `from` on; gaps where NaN. The chart has fewer pixels
// than points, so the points are gathered per pixel column: the line enters a column at its first
// value, a vertical stroke covers its lowest..highest value (lo/hi: the extremes within each point,
// optional) and the line leaves at its last value. Every point counts, so the curve stays the same
// while the window moves (skipping points made peaks and noise change shape from second to second).
// dash > 0: dashes of `dash` px with gaps of 2/3 of that, continued along the whole line.
template <typename F>
static void series(const Plot& P, const Tenth16* v, int from, F tOf, lv_color_t c, int width, int dash,
                   lv_opa_t opa = LV_OPA_COVER, const Tenth16* lo = nullptr, const Tenth16* hi = nullptr) {
  float phase = 0;  // position in the dash pattern (px)
  const float on = dash, period = dash + dash * 2 / 3.0f;
  auto seg = [&](float x1, float y1, float x2, float y2) {
    if (!dash) return line(P, x1, y1, x2, y2, c, width, 0, opa);
    const float len = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    if (len < 0.01f) return;
    float d = 0;
    while (d < len) {
      const float inPat = fmodf(phase + d, period);
      const bool drawing = inPat < on;
      const float step = fminf(len - d, drawing ? on - inPat : period - inPat);
      if (drawing) {
        const float a = d / len, b = (d + step) / len;
        line(P, x1 + (x2 - x1) * a, y1 + (y2 - y1) * a, x1 + (x2 - x1) * b, y1 + (y2 - y1) * b, c, width, 0, opa);
      }
      d += step;
    }
    phase = fmodf(phase + len, period);
  };
  // current column: x (of its first point), pixel index, y of first / last, top / bottom (screen y)
  bool open = false, single = false;
  float cx = 0, cFirst = 0, cLast = 0, cTop = 0, cBot = 0;
  int32_t cCol = 0;
  auto flush = [&]() {
    if (!open) return;
    if (cBot - cTop >= 1) seg(cx, cTop, cx, cBot);
    else if (single) seg(cx - 0.5f, cFirst, cx + 0.5f, cFirst);  // lone point: a dot
  };
  for (int i = from; i < CHART_N; ++i) {
    const float val = v[i];
    float x = 0;
    if (!isnan(val)) x = P.X(tOf(i));
    if (isnan(val) || x < P.x0 - 1 || x > P.x0 + P.w + 1) {  // gap
      flush();
      open = false;
      continue;
    }
    const float y = P.Y(val);
    float top = y, bot = y;
    if (hi && !isnan((float)hi[i])) top = fminf(top, P.Y(hi[i]));
    if (lo && !isnan((float)lo[i])) bot = fmaxf(bot, P.Y(lo[i]));
    const int32_t col = (int32_t)lroundf(x);
    if (open && col == cCol) {
      cLast = y;
      cTop = fminf(cTop, top);
      cBot = fmaxf(cBot, bot);
      continue;
    }
    const bool joined = open;
    if (joined) single = false;  // connected: no dot needed
    flush();
    if (joined) seg(cx, cLast, x, y);
    single = !joined;
    open = true;
    cx = x;
    cCol = col;
    cFirst = cLast = y;
    cTop = top;
    cBot = bot;
  }
  flush();
}

// Area under a series down to the axis (live / run SP): one 1 px column per pixel, 20 % opacity
template <typename F>
static void area(const Plot& P, const Tenth16* v, F tOf, lv_color_t c) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = c;
  d.bg_opa = LV_OPA_20;
  const int32_t base = P.y0 + P.h;
  for (int i = 0; i + 1 < CHART_N; ++i) {
    const float a = v[i], b = v[i + 1];
    if (isnan(a) || isnan(b)) continue;
    const float x1 = P.X(tOf(i)), x2 = P.X(tOf(i + 1));
    int32_t c1 = (int32_t)ceilf(fmaxf(x1, (float)P.x0)), c2 = (int32_t)ceilf(fminf(x2, (float)(P.x0 + P.w)));
    for (int32_t x = c1; x < c2; ++x) {  // columns [x1, x2): each column once
      const float y = P.Y(a + (b - a) * (x - x1) / (x2 - x1));
      lv_area_t r = {x, (int32_t)lroundf(y), x, base - 1};
      if (r.y1 < base) lv_draw_rect(P.layer, &d, &r);
    }
  }
}

static void drawChart(lv_event_t* e) {
  if (!cdValid) return;
  lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  Plot P;
  P.layer = lv_event_get_layer(e);
  P.x0 = a.x1 + PAD_L;
  P.y0 = a.y1 + PAD_T;
  P.w = lv_area_get_width(&a) - PAD_L - PAD_R;
  P.h = lv_area_get_height(&a) - PAD_T - PAD_B;
  const bool live = cdLive;
  const bool run = !live && cd.run;
  // x axis
  if (live) {
    P.t0 = -cd.span;
    P.t1 = 0;
  } else if (run) {
    P.t0 = 0;
    P.t1 = cd.span;
  } else {
    P.t0 = 0;
    P.t1 = cd.planSpan > 1 ? cd.planSpan : 60;
  }
  // y range: 0 .. the highest value shown
  float mx = 0;
  auto see = [&](const Tenth16* s) {
    for (int i = 0; i < CHART_N; ++i) {
      const float v = s[i];
      if (!isnan(v) && v > mx) mx = v;
    }
  };
  if (live) {  // only what is in view (a period shorter than the points shows the newest ones)
    const float bin = cd.binSec > 0 ? cd.binSec : 1;
    for (int i = 0; i < CHART_N; ++i) {
      if (-(float)(CHART_N - 1 - i) * bin < P.t0) continue;
#ifdef CHART_LOHI
      for (const Tenth16* s : {cd.pv, cd.sp, cd.pvT, cd.spT, cd.hi, cd.hiT}) {
#else
      for (const Tenth16* s : {cd.pv, cd.sp, cd.pvT, cd.spT}) {
#endif
        const float v = s[i];
        if (!isnan(v) && v > mx) mx = v;
      }
    }
  } else if (run) {
    see(cd.pv), see(cd.sp), see(cd.pvT), see(cd.spT);
#ifdef CHART_LOHI
    see(cd.hi), see(cd.hiT);
#endif
  }
  if (!live) see(cd.plan), see(cd.planT);
  float ystep;
  P.ymax = niceTop(mx, ystep);
  // grid + labels
  const lv_color_t cl = tc(C_LINE), cm = tc(C_MUTED);
  char b[24];
  for (float v = 0; v <= P.ymax + 1e-3f; v += ystep) {
    const float y = P.Y(v);
    line(P, P.x0, y, P.x0 + P.w, y, cl, 1);
    if (v == 0) continue;  // "0" would touch the first time label
    snprintf(b, sizeof(b), "%g", v);
    text(P, a.x1, (int32_t)y - 7, PAD_L - 4, b, cm, LV_TEXT_ALIGN_RIGHT);
  }
  const float ts = tickStep(P.t1 - P.t0, P.w);
  for (float t = ceilf(P.t0 / ts) * ts; t <= P.t1 + 1e-3f; t += ts) {
    fmtTick(t, b, sizeof(b));
    int32_t x = (int32_t)P.X(t) - 30;  // centred under the tick, kept inside the panel
    if (x + 60 > a.x2 - 2) x = a.x2 - 62;
    if (x < a.x1 + 2) x = a.x1 + 2;
    text(P, x, P.y0 + P.h + 2, 60, b, cm,
         x == a.x2 - 62 ? LV_TEXT_ALIGN_RIGHT : x == a.x1 + 2 ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER);
  }
  const float bin = cd.binSec > 0 ? cd.binSec : 1;
  if (live) {
    auto tOf = [&](int i) { return -(float)(CHART_N - 1 - i) * bin; };
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      area(P, ch ? cd.spT : cd.sp, tOf, tcSp(ch));
      series(P, ch ? cd.spT : cd.sp, 0, tOf, tcSp(ch), 2, 0, LV_OPA_50);
      series(P, ch ? cd.pvT : cd.pv, 0, tOf, tcPv(ch), 3, 0, LV_OPA_COVER, LO(ch), HI(ch));
    }
    return;
  }
  if (run) {
    auto tOf = [&](int i) { return (i + 0.5f) * bin; };
    int now = (int)(cd.elapsed / bin);
    if (now < 0) now = 0;
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) series(P, ch ? cd.planT : cd.plan, now, tOf, tcSp(ch), 2, 6, LV_OPA_50);
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
      area(P, ch ? cd.spT : cd.sp, tOf, tcSp(ch));
      series(P, ch ? cd.spT : cd.sp, 0, tOf, tcSp(ch), 2, 0, LV_OPA_50);
      series(P, ch ? cd.pvT : cd.pv, 0, tOf, tcPv(ch), 3, 0, LV_OPA_COVER, LO(ch), HI(ch));
    }
    const float xn = P.X(cd.elapsed);
    line(P, xn, P.y0, xn, P.y0 + P.h, tc(C_TEXT), 1);
    if (cd.planTotal > 0 && cd.planTotal < cd.span) {
      const float xe = P.X(cd.planTotal);
      line(P, xe, P.y0, xe, P.y0 + P.h, tc(C_MUTED), 1, 3);
    }
    return;
  }
  // preview of the selected program
  const float pb = cd.planSpan / CHART_N;
  auto tOf = [&](int i) { return (i + 0.5f) * pb; };
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) series(P, ch ? cd.planT : cd.plan, 0, tOf, tcSp(ch), 2, 6, LV_OPA_50);
  if (cd.planTotal > 0) {
    const float xe = P.X(cd.planTotal);
    line(P, xe, P.y0, xe, P.y0 + P.h, tc(C_MUTED), 1, 3);
  }
}

lv_obj_t* chartCreate(lv_obj_t* parent) {  // no tile: the chart sits on the screen background
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_scrollable(c, false);
  lv_obj_add_event_cb(c, drawChart, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_set_clickable(c, true);
  return c;
}

void chartUpdate(lv_obj_t* chart, bool live) {
  chartSnapshot(cd, live);
  cdLive = live;
  cdValid = true;
  lv_obj_invalidate(chart);
}

// for the header caption
const ChartData& chartData() { return cd; }
#endif
