// Touch UI chart: both channels drawn by hand (LV_EVENT_DRAW_MAIN), like the web chart.
//   Program view: during a run the run's time axis (setpoints and temperatures so far, the forecast
//   dashed, a "now" line); otherwise the selected program's plan.
//   Live view: the live history over the chosen period (display.liveMin), 0 .. highest value.
#include "tui.h"
#include "lcd.h"
#ifdef ESP_PLATFORM
#include <esp_timer.h>
static inline uint32_t nowUs() { return (uint32_t)esp_timer_get_time(); }
#else
#include <chrono>
static inline uint32_t nowUs() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#endif
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
#ifdef BOARD_HAS_PSRAM
#define CHART_CACHE 1  // the chart drawn once into a picture in PSRAM (chartUpdate)
#else
#define CHART_CACHE 0
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

// ---- drawing mode: 0 everything through LVGL draw tasks (directly on the screen: CYD); with the chart
// picture (S3) two passes: 1 = grid + labels through LVGL into the picture, 2 = the data (areas, curves, the
// "now" / plan-end lines) by the own raster below, straight into the picture's pixels. LVGL made thousands of
// small draw tasks of the curves (a line with round ends per piece, a 1 px rectangle per area column) and
// rendered them into PSRAM column by column: ~0.8 s of CPU per second on a long live chart (Rost, 1.0.54).
static int rmode = 0;

#if CHART_CACHE
// The raster: per curve the vertical extent of its centre line in each pixel column (all its pieces
// together), then one span per column, widened by the line width, with anti-aliased ends; blended in
// RGB565. Areas row by row (sequential in memory: PSRAM-friendly).
static uint16_t* rpx = nullptr;  // the picture's pixels, rStride pixels per row
static int32_t rStride = 0, rW = 0, rH = 0;
static float sLo[SCREEN_W], sHi[SCREEN_W];  // centre line extent per column (lo > hi: nothing)
static bool inSeries = false;               // line() pieces go into the spans of the curve being drawn

static inline void blendPx(int32_t x, int32_t y, uint16_t c, uint32_t a) {  // a 0..255
  if (x < 0 || y < 0 || x >= rW || y >= rH || !a) return;
  uint16_t& d = rpx[y * rStride + x];
  if (a >= 255) {
    d = c;
    return;
  }
  const uint32_t al = (a + 4) >> 3;  // 0..32
  const uint32_t f = (c | ((uint32_t)c << 16)) & 0x07E0F81Fu, b = (d | ((uint32_t)d << 16)) & 0x07E0F81Fu;
  const uint32_t r = ((((f - b) * al) >> 5) + b) & 0x07E0F81Fu;
  d = (uint16_t)(r | (r >> 16));
}

static void spansReset() {
  for (int32_t i = 0; i < rW && i < SCREEN_W; ++i) sLo[i] = 1e9f, sHi[i] = -1e9f;
}

// the centre line from (x1, y1) to (x2, y2) into the columns whose centres it passes
static void spanAdd(float x1, float y1, float x2, float y2) {
  if (x1 > x2) {
    float t = x1;
    x1 = x2, x2 = t;
    t = y1;
    y1 = y2, y2 = t;
  }
  const int32_t c1 = (int32_t)floorf(x1 + 0.5f), c2 = (int32_t)floorf(x2 + 0.5f);
  for (int32_t c = c1 < 0 ? 0 : c1; c <= c2 && c < rW && c < SCREEN_W; ++c) {
    float ya = y1, yb = y2;
    if (x2 - x1 > 1e-3f) {  // the part of the piece within this column
      const float xa = fmaxf(x1, c - 0.5f), xb = fminf(x2, c + 0.5f);
      ya = y1 + (y2 - y1) * (xa - x1) / (x2 - x1);
      yb = y1 + (y2 - y1) * (xb - x1) / (x2 - x1);
    }
    sLo[c] = fminf(sLo[c], fminf(ya, yb));
    sHi[c] = fmaxf(sHi[c], fmaxf(ya, yb));
  }
}

// the spans as a line of `width` px: columns widened by the neighbours' spans (steep parts keep their
// width), rows by half the width; pixel r covers [r, r + 1), the centre of row y is y + 0.5
static void spansStroke(lv_color_t col, int width, lv_opa_t opa) {
  const uint16_t c = lv_color_to_u16(col);
  const int d0 = -(width - 1) / 2, d1 = width / 2;  // 3: -1..1, 2: 0..1, 1: 0
  const float e = width * 0.5f;
  for (int32_t x = 0; x < rW && x < SCREEN_W; ++x) {
    float lo = 1e9f, hi = -1e9f;
    for (int k = d0; k <= d1; ++k) {
      const int32_t s = x - k;
      if (s < 0 || s >= rW || s >= SCREEN_W || sLo[s] > sHi[s]) continue;
      lo = fminf(lo, sLo[s]);
      hi = fmaxf(hi, sHi[s]);
    }
    if (lo > hi) continue;
    const float A = lo - e + 0.5f, B = hi + e + 0.5f;
    for (int32_t y = (int32_t)floorf(A); y < (int32_t)ceilf(B); ++y) {
      const float cov = fminf(B, (float)(y + 1)) - fmaxf(A, (float)y);
      if (cov > 0) blendPx(x, y, c, (uint32_t)(opa * fminf(cov, 1.0f) + 0.5f));
    }
  }
}
#endif

static void line(const Plot& P, float x1, float y1, float x2, float y2, lv_color_t c, int width, int dash = 0,
                 lv_opa_t opa = LV_OPA_COVER) {
#if CHART_CACHE
  if (rmode == 2) {
    if (inSeries) return spanAdd(x1, y1, x2, y2);  // (stroked with the whole curve)
    if (!dash) {
      spansReset();
      spanAdd(x1, y1, x2, y2);
      return spansStroke(c, width, opa);
    }
    // dashes of `dash` px with gaps of `dash` px (as LVGL's dash_width / dash_gap), each stroked by itself
    // (a column holds one span: the dashes of a vertical line would merge)
    const float len = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    for (float d = 0; d < len; d += 2.0f * dash) {
      const float a = d / len, b = fminf(d + dash, len) / len;
      spansReset();
      spanAdd(x1 + (x2 - x1) * a, y1 + (y2 - y1) * a, x1 + (x2 - x1) * b, y1 + (y2 - y1) * b);
      spansStroke(c, width, opa);
    }
    return;
  }
#endif
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
#if CHART_CACHE
  if (rmode == 2) {
    spansReset();
    inSeries = true;
  }
#endif
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
#if CHART_CACHE
  if (rmode == 2) {
    inSeries = false;
    spansStroke(c, width, opa);
  }
#endif
}

// Area under a series down to the axis (live / run SP): one 1 px column per pixel, 20 % opacity
template <typename F>
static void area(const Plot& P, const Tenth16* v, F tOf, lv_color_t c) {
#if CHART_CACHE
  if (rmode == 2) {  // the top of each column (as below), then row by row
    static int16_t top[SCREEN_W];
    for (int32_t x = 0; x < rW && x < SCREEN_W; ++x) top[x] = INT16_MAX;
    const int32_t base = P.y0 + P.h;
    int32_t ymin = base;
    for (int i = 0; i + 1 < CHART_N; ++i) {
      const float a = v[i], b = v[i + 1];
      if (isnan(a) || isnan(b)) continue;
      const float x1 = P.X(tOf(i)), x2 = P.X(tOf(i + 1));
      int32_t c1 = (int32_t)ceilf(fmaxf(x1, (float)P.x0)), c2 = (int32_t)ceilf(fminf(x2, (float)(P.x0 + P.w)));
      for (int32_t x = c1; x < c2; ++x) {
        if (x < 0 || x >= rW || x >= SCREEN_W) continue;
        int32_t y = (int32_t)lroundf(P.Y(a + (b - a) * (x - x1) / (x2 - x1)));
        if (y < 0) y = 0;
        top[x] = (int16_t)y;
        if (y < ymin) ymin = y;
      }
    }
    const uint16_t c16 = lv_color_to_u16(c);
    for (int32_t y = ymin; y < base && y < rH; ++y)
      for (int32_t x = 0; x < rW && x < SCREEN_W; ++x)
        if (y >= top[x]) blendPx(x, y, c16, LV_OPA_20);
    return;
  }
#endif
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

// the whole chart into a layer: a = the chart's area in that layer's coordinates
static void render(lv_layer_t* layer, const lv_area_t& a) {
  Plot P;
  P.layer = layer;
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
  if (rmode != 2) {
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
  }
  if (rmode == 1) return;  // (the data: pass 2)
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

#if CHART_CACHE
// The chart as a picture (S3): drawn once into an RGB565 buffer in PSRAM when its data, size or colours
// change (chartUpdate, at most once a second), and on the screen only copied from there. Drawn directly,
// the hundreds of lines and area columns were drawn again for every 20-line strip of the display that
// the chart touches (partial rendering: ~7 times per refresh) and for every redraw of anything on top of
// it. The buffer is the canvas's; the chart sits on the screen background, so it is filled with that.
static lv_draw_buf_t* cbuf = nullptr;
static lv_obj_t* cObj = nullptr;  // the canvas showing cbuf (a new one after the main screen was rebuilt)
static uint32_t cHash = 0;        // of what cbuf shows

static uint32_t fnv(const void* p, size_t n, uint32_t h = 2166136261u) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
  return h;
}

lv_obj_t* chartCreate(lv_obj_t* parent) {  // no tile: the chart sits on the screen background
  lv_obj_t* c = lv_canvas_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_scrollable(c, false);
  lv_obj_set_clickable(c, true);
  return c;
}

void chartUpdate(lv_obj_t* chart, bool live) {
  chartSnapshot(cd, live);
  cdLive = live;
  cdValid = true;
  lv_obj_update_layout(chart);
  const int32_t w = lv_obj_get_width(chart), h = lv_obj_get_height(chart);
  if (w <= 0 || h <= 0) return;
  if (!cbuf || (int32_t)cbuf->header.w != w || (int32_t)cbuf->header.h != h) {
    if (cbuf) {
      if (cObj && cObj != chart && lv_obj_is_valid(cObj)) lv_canvas_set_draw_buf(cObj, nullptr);
      lv_draw_buf_destroy(cbuf);
    }
    cbuf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    cObj = nullptr;
    if (!cbuf) return;  // (no memory: the chart stays empty)
  }
  if (cObj != chart) {
    lv_canvas_set_draw_buf(chart, cbuf);
    lv_obj_set_size(chart, w, h);  // (the image widget would size itself to the buffer: the same size)
    cObj = chart;
    cHash = 0;
  }
  // what is drawn: the snapshot, the view, the size and the theme's colours
  uint32_t k = fnv(&cd, sizeof(cd));
  const uint32_t extra[] = {(uint32_t)live, (uint32_t)w, (uint32_t)h, lv_color_to_u32(tc(C_BG)), lv_color_to_u32(tc(C_LINE)),
                            lv_color_to_u32(tc(C_MUTED)), lv_color_to_u32(tc(C_TEXT)), lv_color_to_u32(tcPv(0)),
                            lv_color_to_u32(tcPv(1)), lv_color_to_u32(tcSp(0)), lv_color_to_u32(tcSp(1))};
  k = fnv(extra, sizeof(extra), k);
  if (k == cHash) return;  // unchanged: nothing to draw, nothing to send
  cHash = k;
  const uint32_t t0 = nowUs();
  lv_canvas_fill_bg(chart, tc(C_BG), LV_OPA_COVER);
  const lv_area_t a = {0, 0, w - 1, h - 1};
  lv_layer_t layer;  // pass 1: grid + labels through LVGL
  lv_canvas_init_layer(chart, &layer);
  rmode = 1;
  render(&layer, a);
  lv_canvas_finish_layer(chart, &layer);  // (draws and waits for it)
  rpx = (uint16_t*)cbuf->data;  // pass 2: the data, own raster
  rStride = cbuf->header.stride / 2;
  rW = w;
  rH = h;
  rmode = 2;
  render(nullptr, a);
  rmode = 0;
  lv_obj_invalidate(chart);
  lcdPerfChart(nowUs() - t0);  // (frame rate meter)
}

#else  // CYD: no PSRAM for the picture (124 kB on the S3): drawn directly

static void drawChart(lv_event_t* e) {
  if (!cdValid) return;
  lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  render(lv_event_get_layer(e), a);
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
#endif

// for the header caption
const ChartData& chartData() { return cd; }

#if CHART_CACHE && !defined(ESP_PLATFORM)
// UI host only (./uihost chartbench): the chart picture drawn n times, all through LVGL draw tasks
// (the 1.0.54 way) and with the own raster for the data: microseconds per drawing
#include <chrono>
void chartBench(int n, double& lvglUs, double& rasterUs) {
  lv_obj_t* chart = cObj;
  if (!chart || !cbuf) return;
  const lv_area_t a = {0, 0, (int32_t)cbuf->header.w - 1, (int32_t)cbuf->header.h - 1};
  for (int mode = 0; mode < 2; ++mode) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
      lv_canvas_fill_bg(chart, tc(C_BG), LV_OPA_COVER);
      lv_layer_t layer;
      lv_canvas_init_layer(chart, &layer);
      rmode = mode == 0 ? 0 : 1;
      render(&layer, a);
      lv_canvas_finish_layer(chart, &layer);
      if (mode == 1) {
        rpx = (uint16_t*)cbuf->data;
        rStride = cbuf->header.stride / 2;
        rW = a.x2 + 1;
        rH = a.y2 + 1;
        rmode = 2;
        render(nullptr, a);
      }
      rmode = 0;
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / n;
    (mode == 0 ? lvglUs : rasterUs) = us;
  }
}
#endif
