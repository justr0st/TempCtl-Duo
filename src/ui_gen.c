// Forwards the UI code exported by LVGL Pro to ui.cpp (see ui_gen.h).
#include "ui_gen.h"
#include "tempctl_ui.h"

// The exported fonts don't contain LVGL's symbols (dropdown arrows,
// LV_SYMBOL_WARNING, ...); they come from the built-in Montserrat fonts,
// chained as fallback. The exported font data is const (flash), so each font is
// copied to RAM with the fallback set, before tempctl_ui_init() picks it up.
// A font removed from ui/globals.xml has to be removed here as well.
#define UI_FONT_WITH_FALLBACK(name, fb)             \
  do {                                              \
    extern const lv_font_t name##_data;             \
    static lv_font_t name##_ram;                    \
    name##_ram = name##_data;                       \
    name##_ram.fallback = (fb);                     \
    name = &name##_ram;                             \
  } while (0)

static void applyStyleColors(void);
static void checkStyleColors(void);
static bool stylesReady = false;

lv_obj_t* uiGenInit(void) {
  UI_FONT_WITH_FALLBACK(font_12, &lv_font_montserrat_12);
  UI_FONT_WITH_FALLBACK(font_14, &lv_font_montserrat_14);
  UI_FONT_WITH_FALLBACK(font_20, &lv_font_montserrat_14);
  UI_FONT_WITH_FALLBACK(font_24, &lv_font_montserrat_14);
  UI_FONT_WITH_FALLBACK(font_32, &lv_font_montserrat_14);
  UI_FONT_WITH_FALLBACK(font_48, &lv_font_montserrat_14);
  tempctl_ui_init("");
  checkStyleColors();  // the styles still have the color_* values here
  stylesReady = true;
  uiGenColor(UI_COLOR_BG);  // loads the default palette if none was set
  applyStyleColors();
  return gui;
}

lv_obj_t* uiGenDialog(lv_obj_t* parent) { return dialog_create(parent); }
lv_obj_t* uiGenNumEditor(lv_obj_t* parent) { return num_editor_create(parent); }
lv_obj_t* uiGenTextEditor(lv_obj_t* parent) { return text_editor_create(parent); }
lv_obj_t* uiGenWifiList(lv_obj_t* parent) { return wifi_list_create(parent); }
lv_obj_t* uiGenWifiItem(lv_obj_t* parent, const char* ssid) { return wifi_item_create(parent, ssid); }

// ------------------------------------------------------------ palettes
// The colours are constants in ui/globals.xml, exported as macros:
// color_bg -> COLOR_BG (palette "Default", dark), dracula_dark_bg -> DRACULA_DARK_BG.
// PAL(P) collects the 14 roles of one palette/mode, in UiGenColor order.
#define PAL(P)                                                                        \
  {                                                                                   \
    P##_BG, P##_PANEL, P##_BORDER, P##_TEXT, P##_MUTED, P##_ACCENT, P##_ACCENT2,      \
        P##_ROW_SELECTED, P##_DANGER, P##_PV, P##_SP, P##_PLAN, P##_OK, P##_WARN      \
  }

typedef struct {
  lv_color_t c[UI_COLOR_COUNT];
} Palette;

// Order = dropdown order (keep the names in the same order).
static const char PALETTE_NAMES[] = "Default\nDracula\nSolarized\nGruvbox\nCatppuccin\nOne";
enum { PALETTE_COUNT = 6 };

// lv_color_hex() is a function, so the table can't be a static initialiser.
static void paletteLoad(int palette, bool dark, Palette* p) {
  switch (palette * 2 + (dark ? 1 : 0)) {
    case 1: *p = (Palette){PAL(COLOR)}; break;
    case 0: *p = (Palette){PAL(DEFAULT_LIGHT)}; break;
    case 3: *p = (Palette){PAL(DRACULA_DARK)}; break;
    case 2: *p = (Palette){PAL(DRACULA_LIGHT)}; break;
    case 5: *p = (Palette){PAL(SOLARIZED_DARK)}; break;
    case 4: *p = (Palette){PAL(SOLARIZED_LIGHT)}; break;
    case 7: *p = (Palette){PAL(GRUVBOX_DARK)}; break;
    case 6: *p = (Palette){PAL(GRUVBOX_LIGHT)}; break;
    case 9: *p = (Palette){PAL(CATPPUCCIN_DARK)}; break;
    case 8: *p = (Palette){PAL(CATPPUCCIN_LIGHT)}; break;
    case 11: *p = (Palette){PAL(ONE_DARK)}; break;
    case 10: *p = (Palette){PAL(ONE_LIGHT)}; break;
    default: *p = (Palette){PAL(COLOR)}; break;
  }
}

// Which colour of which style (ui/globals.xml) has which role. Every colour in
// a style needs a line here, otherwise it keeps its color_* value in all
// palettes (checkStyleColors() reports lines that don't match globals.xml).
typedef struct {
  lv_style_t* style;
  lv_style_prop_t prop;
  UiGenColor role;
} StyleColor;

static const StyleColor STYLE_COLORS[] = {
    {&style_screen, LV_STYLE_BG_COLOR, UI_COLOR_BG},
    {&style_screen, LV_STYLE_TEXT_COLOR, UI_COLOR_TEXT},
    {&style_page, LV_STYLE_BG_COLOR, UI_COLOR_BG},
    {&style_caption, LV_STYLE_TEXT_COLOR, UI_COLOR_MUTED},
    {&style_panel, LV_STYLE_BG_COLOR, UI_COLOR_PANEL},
    {&style_panel, LV_STYLE_BORDER_COLOR, UI_COLOR_BORDER},
    {&style_panel, LV_STYLE_TEXT_COLOR, UI_COLOR_TEXT},
    {&style_tab_bar, LV_STYLE_BG_COLOR, UI_COLOR_PANEL},
    {&style_tab_bar, LV_STYLE_TEXT_COLOR, UI_COLOR_TEXT},
    {&style_danger, LV_STYLE_BG_COLOR, UI_COLOR_DANGER},
    {&style_text_pv, LV_STYLE_TEXT_COLOR, UI_COLOR_PV},
    {&style_text_sp, LV_STYLE_TEXT_COLOR, UI_COLOR_SP},
    {&style_text_plan, LV_STYLE_TEXT_COLOR, UI_COLOR_PLAN},
    {&style_text_warn, LV_STYLE_TEXT_COLOR, UI_COLOR_WARN},
    {&style_text_accent, LV_STYLE_TEXT_COLOR, UI_COLOR_ACCENT},
    {&style_border_accent, LV_STYLE_BORDER_COLOR, UI_COLOR_ACCENT},
    {&style_bg_ok, LV_STYLE_BG_COLOR, UI_COLOR_OK},
    {&style_link_error, LV_STYLE_BG_COLOR, UI_COLOR_DANGER},
    {&style_popup, LV_STYLE_BG_COLOR, UI_COLOR_PANEL},
    {&style_popup, LV_STYLE_TEXT_COLOR, UI_COLOR_TEXT},
    {&style_dialog, LV_STYLE_BG_COLOR, UI_COLOR_PANEL},
    {&style_dialog, LV_STYLE_BORDER_COLOR, UI_COLOR_BORDER},
    {&style_dialog, LV_STYLE_TEXT_COLOR, UI_COLOR_TEXT},
    {&style_list_item, LV_STYLE_BG_COLOR, UI_COLOR_PANEL},
    {&style_list_item, LV_STYLE_BORDER_COLOR, UI_COLOR_BORDER},
    {&style_list_item, LV_STYLE_TEXT_COLOR, UI_COLOR_TEXT},
};
#define STYLE_COLOR_COUNT (sizeof(STYLE_COLORS) / sizeof(STYLE_COLORS[0]))

static Palette current;
static bool currentSet = false;

static void checkStyleColors(void) {
  Palette def;
  paletteLoad(0, true, &def);
  for (size_t i = 0; i < STYLE_COLOR_COUNT; ++i) {
    const StyleColor* s = &STYLE_COLORS[i];
    lv_style_value_t v;
    if (lv_style_get_prop(s->style, s->prop, &v) != LV_STYLE_RES_FOUND) {
      LV_LOG_WARN("palette: STYLE_COLORS[%u]: the style has no such colour (ui/globals.xml changed?)", (unsigned)i);
    } else if (!lv_color_eq(v.color, def.c[s->role])) {
      LV_LOG_WARN("palette: STYLE_COLORS[%u]: colour doesn't match its role in ui/globals.xml", (unsigned)i);
    }
  }
}

static void applyStyleColors(void) {
  for (size_t i = 0; i < STYLE_COLOR_COUNT; ++i) {
    lv_style_value_t v;
    v.color = current.c[STYLE_COLORS[i].role];
    lv_style_set_prop(STYLE_COLORS[i].style, STYLE_COLORS[i].prop, v);
  }
  lv_obj_report_style_change(NULL);  // all widgets using these styles
}

int uiGenPaletteCount(void) { return PALETTE_COUNT; }
const char* uiGenPaletteNames(void) { return PALETTE_NAMES; }

void uiGenSetPalette(int palette, bool dark) {
  if (palette < 0 || palette >= PALETTE_COUNT) palette = 0;
  paletteLoad(palette, dark, &current);
  currentSet = true;
  if (stylesReady) applyStyleColors();
}

lv_color_t uiGenColor(UiGenColor c) {
  if (!currentSet) uiGenSetPalette(0, THEME_DARK != 0);
  return (unsigned)c < UI_COLOR_COUNT ? current.c[c] : lv_color_white();
}

bool uiGenThemeDark(void) { return THEME_DARK != 0; }
