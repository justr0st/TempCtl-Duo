// Touch UI theme: the web page's palette (spec 6) and the named styles.
#include "tui.h"
#include <math.h>

TStyles S_;
static lv_color_t col[C_COUNT];
static bool dark_ = true;
static uint32_t gen_ = 0;

lv_color_t tc(TColor c) { return col[c]; }
lv_color_t tcPv(int ch) { return col[ch ? C_PV1 : C_PV0]; }
lv_color_t tcSp(int ch) { return col[ch ? C_SP1 : C_SP0]; }
bool themeDark() { return dark_; }
uint32_t themeGen() { return gen_; }

// ---- colour maths (the same as the web page: applyPalette())
struct Rgb {
  float r, g, b;
};
static Rgb hex(uint32_t h) { return {((h >> 16) & 255) / 255.0f, ((h >> 8) & 255) / 255.0f, (h & 255) / 255.0f}; }
static lv_color_t toLv(const Rgb& c) {
  auto q = [](float v) { return (uint8_t)lroundf(fmaxf(0, fminf(1, v)) * 255); };
  return lv_color_make(q(c.r), q(c.g), q(c.b));
}
static void rgb2hsl(const Rgb& c, float& h, float& s, float& l) {
  const float mx = fmaxf(c.r, fmaxf(c.g, c.b)), mn = fminf(c.r, fminf(c.g, c.b));
  l = (mx + mn) / 2;
  h = s = 0;
  if (mx != mn) {
    const float d = mx - mn;
    s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    h = mx == c.r ? (c.g - c.b) / d + (c.g < c.b ? 6 : 0) : mx == c.g ? (c.b - c.r) / d + 2 : (c.r - c.g) / d + 4;
    h *= 60;
  }
}
static Rgb hsl2rgb(float h, float s, float l) {
  auto f = [&](float n) {
    const float k = fmodf(n + h / 30, 12), a = s * fminf(l, 1 - l);
    return l - a * fmaxf(-1, fminf(fminf(k - 3, 9 - k), 1));
  };
  return {f(0), f(8), f(4)};
}
static float lum(const Rgb& c) {
  auto f = [](float v) { return v <= 0.03928f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f); };
  return 0.2126f * f(c.r) + 0.7152f * f(c.g) + 0.0722f * f(c.b);
}
static float contrast(const Rgb& a, const Rgb& b) {
  const float x = lum(a), y = lum(b);
  return (fmaxf(x, y) + 0.05f) / (fminf(x, y) + 0.05f);
}
// keep hue and saturation, move the lightness until the contrast against bg is reached
static Rgb fitC(float h, float s, float& l, const Rgb& bg, float minC, bool dark) {
  for (int i = 0; i < 120; ++i) {
    const Rgb c = hsl2rgb(h, s, l);
    if (contrast(c, bg) >= minC || l <= 0 || l >= 1) return c;
    l = fmaxf(0, fminf(1, l + (dark ? 0.01f : -0.01f)));
  }
  return hsl2rgb(h, s, l);
}

static void computePalette(uint32_t accent, bool dark) {
  // neutrals: web/index.html :root (light) and [data-theme=dark]
  static const uint32_t L[] = {0xEDF0F1, 0xFFFFFF, 0xF4F6F7, 0xD3DADD, 0x172024, 0x5B6970, 0, 0, 0xA87400, 0xFFF3D1, 0xB3261E, 0x2E7D32, 0xB85C00};
  static const uint32_t D[] = {0x11161A, 0x192024, 0x141A1E, 0x2B353B, 0xE3E9EB, 0x8D9BA2, 0, 0, 0xF3C335, 0x3A3217, 0xF2867E, 0x7BC47F, 0xF0A050};
  const uint32_t* N = dark ? D : L;
  for (int i = 0; i <= C_MANUAL; ++i) col[i] = lv_color_hex(N[i]);
  const Rgb bg = hex(N[C_PANEL]);
  float h, s, l;
  rgb2hsl(hex(accent), h, s, l);
  for (int ch = 0; ch < 2; ++ch) {
    const float hh = fmodf(h + (ch ? 150 : -150) + 360, 360);
    float lp = l;
    const Rgb pv = fitC(hh, s, lp, bg, 4.5f, dark);
    float ls = fminf(1, lp + 0.12f);
    const Rgb sp = fitC(hh, s, ls, bg, 3, dark);
    col[ch ? C_PV1 : C_PV0] = toLv(pv);
    col[ch ? C_SP1 : C_SP0] = toLv(sp);
  }
  float la = l;
  const Rgb a = fitC(h, s, la, bg, 3, dark);
  col[C_ACC] = toLv(a);
  col[C_ACC_INK] = contrast(a, {1, 1, 1}) >= contrast(a, {0.06f, 0.08f, 0.09f}) ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x0F1417);
}

static void initStyles() {
  static bool done = false;
  if (done) return;
  done = true;
  lv_style_t* all[] = {&S_.scr, &S_.panel, &S_.sunk, &S_.btn, &S_.btnPressed, &S_.btnPri, &S_.btnDanger, &S_.btnDis,
                       &S_.btnChecked, &S_.muted, &S_.small, &S_.title, &S_.danger, &S_.amber, &S_.ok, &S_.acc, &S_.ring, &S_.cell, &S_.btnOn[0], &S_.btnOn[1], &S_.tileOn[0], &S_.tileOn[1],
                       &S_.pv[0], &S_.pv[1], &S_.sp[0], &S_.sp[1], &S_.barBg, &S_.bar[0], &S_.bar[1], &S_.overlay,
                       &S_.dialog, &S_.listRow, &S_.sep};
  for (lv_style_t* st : all) lv_style_init(st);
  // geometry and fonts (colours: recolour())
  lv_style_set_text_font(&S_.scr, &tfont_14);
  lv_style_set_bg_opa(&S_.scr, LV_OPA_COVER);
  lv_style_set_pad_all(&S_.scr, 0);

  lv_style_set_bg_opa(&S_.panel, LV_OPA_COVER);
  lv_style_set_border_width(&S_.panel, 1);
  lv_style_set_radius(&S_.panel, 8);
  lv_style_set_pad_all(&S_.panel, TUI_SMALL ? 6 : 8);

  lv_style_set_bg_opa(&S_.sunk, LV_OPA_COVER);
  lv_style_set_border_width(&S_.sunk, 1);
  lv_style_set_radius(&S_.sunk, 6);

  lv_style_set_bg_opa(&S_.btn, LV_OPA_COVER);
  lv_style_set_border_width(&S_.btn, 1);
  lv_style_set_radius(&S_.btn, 6);
  lv_style_set_pad_hor(&S_.btn, TUI_SMALL ? 6 : 10);
  lv_style_set_pad_ver(&S_.btn, 4);
  lv_style_set_text_font(&S_.btn, &tfont_14);
  lv_style_set_text_align(&S_.btn, LV_TEXT_ALIGN_CENTER);

  lv_style_set_bg_opa(&S_.btnPri, LV_OPA_COVER);
  lv_style_set_opa(&S_.btnDis, LV_OPA_40);

  lv_style_set_text_font(&S_.small, &tfont_12);
  lv_style_set_text_font(&S_.title, &tfont_16b);

  lv_style_set_bg_opa(&S_.barBg, LV_OPA_COVER);
  lv_style_set_radius(&S_.barBg, 3);
  lv_style_set_border_width(&S_.barBg, 1);
  for (int i = 0; i < 2; ++i) {
    // main screen tile of a channel with its outputs on: 4 px border (the padding 3 px less, so the
    // content stays in place)
    lv_style_set_border_width(&S_.tileOn[i], 4);
    lv_style_set_pad_all(&S_.tileOn[i], (TUI_SMALL ? 6 : 8) - 3);
    lv_style_set_bg_opa(&S_.bar[i], LV_OPA_COVER);
    lv_style_set_radius(&S_.bar[i], 3);
  }

  lv_style_set_bg_opa(&S_.overlay, LV_OPA_60);
  lv_style_set_bg_color(&S_.overlay, lv_color_black());
  lv_style_set_border_width(&S_.overlay, 0);
  lv_style_set_radius(&S_.overlay, 0);
  lv_style_set_pad_all(&S_.overlay, 0);

  lv_style_set_bg_opa(&S_.dialog, LV_OPA_COVER);
  lv_style_set_border_width(&S_.dialog, 1);
  lv_style_set_radius(&S_.dialog, 12);
  lv_style_set_pad_all(&S_.dialog, TUI_SMALL ? 10 : 14);

  lv_style_set_bg_opa(&S_.listRow, LV_OPA_COVER);
  lv_style_set_border_width(&S_.listRow, 1);
  lv_style_set_border_side(&S_.listRow, LV_BORDER_SIDE_BOTTOM);
  lv_style_set_radius(&S_.listRow, 0);
  lv_style_set_pad_hor(&S_.listRow, TUI_SMALL ? 8 : 10);
  lv_style_set_pad_ver(&S_.listRow, TUI_SMALL ? 7 : 8);

  lv_style_set_bg_opa(&S_.sep, LV_OPA_COVER);

  lv_style_set_bg_opa(&S_.cell, LV_OPA_COVER);
  lv_style_set_border_width(&S_.cell, 1);
  lv_style_set_border_side(&S_.cell, (lv_border_side_t)(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT));
  lv_style_set_pad_hor(&S_.cell, 4);
  lv_style_set_pad_ver(&S_.cell, TUI_SMALL ? 6 : 8);
  lv_style_set_text_font(&S_.cell, TUI_SMALL ? &tfont_12 : &tfont_14);
  lv_style_set_text_align(&S_.cell, LV_TEXT_ALIGN_CENTER);

  lv_style_set_outline_width(&S_.ring, 3);
  lv_style_set_outline_pad(&S_.ring, 2);
}

static void recolour() {
  lv_style_set_bg_color(&S_.scr, col[C_BG]);
  lv_style_set_text_color(&S_.scr, col[C_TEXT]);
  lv_style_set_bg_color(&S_.panel, col[C_PANEL]);
  lv_style_set_border_color(&S_.panel, col[C_LINE]);
  lv_style_set_bg_color(&S_.sunk, col[C_SUNK]);
  lv_style_set_border_color(&S_.sunk, col[C_LINE]);
  lv_style_set_bg_color(&S_.btn, col[C_SUNK]);
  lv_style_set_border_color(&S_.btn, col[C_LINE]);
  lv_style_set_text_color(&S_.btn, col[C_TEXT]);
  lv_style_set_bg_color(&S_.btnPressed, col[C_LINE]);
  lv_style_set_bg_color(&S_.btnPri, col[C_ACC]);
  lv_style_set_border_color(&S_.btnPri, col[C_ACC]);
  lv_style_set_text_color(&S_.btnPri, col[C_ACC_INK]);
  lv_style_set_text_color(&S_.btnDanger, col[C_DANGER]);
  lv_style_set_bg_color(&S_.btnChecked, col[C_PANEL]);
  lv_style_set_border_color(&S_.btnChecked, col[C_ACC]);
  lv_style_set_text_color(&S_.btnChecked, col[C_TEXT]);
  lv_style_set_text_color(&S_.muted, col[C_MUTED]);
  lv_style_set_text_color(&S_.danger, col[C_DANGER]);
  lv_style_set_text_color(&S_.amber, col[C_AMBER]);
  lv_style_set_text_color(&S_.ok, col[C_OK]);
  lv_style_set_text_color(&S_.acc, col[C_ACC]);
  for (int i = 0; i < 2; ++i) {
    // "on" button of a channel: filled with its PV colour, symbol in the panel colour (contrast:
    // the PV colours are fitted to 4.5:1 against the panel)
    lv_style_set_bg_color(&S_.btnOn[i], tcPv(i));
    lv_style_set_border_color(&S_.btnOn[i], tcPv(i));
    lv_style_set_text_color(&S_.btnOn[i], col[C_PANEL]);
    lv_style_set_text_color(&S_.pv[i], tcPv(i));
    lv_style_set_text_color(&S_.sp[i], tcSp(i));
    lv_style_set_bg_color(&S_.bar[i], tcPv(i));
    // tile with the outputs on: border in the channel colour, background 20 % of it over the panel
    lv_style_set_border_color(&S_.tileOn[i], tcPv(i));
    lv_style_set_bg_color(&S_.tileOn[i], lv_color_mix(tcPv(i), col[C_PANEL], LV_OPA_20));
  }
  lv_style_set_bg_color(&S_.barBg, col[C_SUNK]);
  lv_style_set_border_color(&S_.barBg, col[C_LINE]);
  lv_style_set_bg_color(&S_.dialog, col[C_PANEL]);
  lv_style_set_border_color(&S_.dialog, col[C_LINE]);
  lv_style_set_text_color(&S_.dialog, col[C_TEXT]);
  lv_style_set_bg_color(&S_.listRow, col[C_PANEL]);
  lv_style_set_border_color(&S_.listRow, col[C_LINE]);
  lv_style_set_bg_color(&S_.sep, col[C_LINE]);
  lv_style_set_outline_color(&S_.ring, col[C_TEXT]);
  lv_style_set_bg_color(&S_.cell, col[C_PANEL]);
  lv_style_set_border_color(&S_.cell, col[C_LINE]);
  lv_style_set_text_color(&S_.cell, col[C_TEXT]);
}

void themeApply(uint32_t accentRgb, bool dark) {
  initStyles();
  dark_ = dark;
  computePalette(accentRgb & 0xFFFFFF, dark);
  recolour();
  // LVGL's own widgets (switch, slider, keyboard, dropdown list) take the accent as primary colour
  lv_display_t* d = lv_display_get_default();
  if (d) {
    lv_theme_t* th = lv_theme_default_init(d, col[C_ACC], col[C_AMBER], dark, &tfont_14);
    lv_display_set_theme(d, th);
  }
  ++gen_;
  lv_obj_report_style_change(nullptr);  // every object that uses the named styles
}
