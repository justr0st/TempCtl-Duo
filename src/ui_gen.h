// Access to the UI code exported by LVGL Pro (ui/, see ui/library.json).
//
// The generated headers pull in LVGL's private headers, whose macros clash with
// names in this firmware (e.g. CMD_START from the FT81x driver), so ui.cpp does
// not include them. src/ui_gen.c does, and forwards what ui.cpp needs.
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Styles, fonts, translations (ui/translations.xml) and the permanent screen
// (ui/screens/gui.xml). Returns the screen or NULL.
lv_obj_t* uiGenInit(void);

// Popups (ui/components/popups), created on `parent`
lv_obj_t* uiGenDialog(lv_obj_t* parent);
lv_obj_t* uiGenNumEditor(lv_obj_t* parent);
lv_obj_t* uiGenTextEditor(lv_obj_t* parent);
lv_obj_t* uiGenWifiList(lv_obj_t* parent);
lv_obj_t* uiGenWifiItem(lv_obj_t* parent, const char* ssid);

// Colour roles: the color_* constants in ui/globals.xml and every palette
// there have these 14 colours.
typedef enum {
  UI_COLOR_BG,
  UI_COLOR_PANEL,
  UI_COLOR_BORDER,
  UI_COLOR_TEXT,
  UI_COLOR_MUTED,
  UI_COLOR_ACCENT,
  UI_COLOR_ACCENT2,
  UI_COLOR_ROW_SELECTED,
  UI_COLOR_DANGER,
  UI_COLOR_PV,
  UI_COLOR_SP,
  UI_COLOR_PLAN,
  UI_COLOR_OK,
  UI_COLOR_WARN,
  UI_COLOR_COUNT
} UiGenColor;

// Colour of the current palette
lv_color_t uiGenColor(UiGenColor c);

// Palettes (ui/globals.xml): count and names ("Default\nNord\n...", dropdown
// options). uiGenSetPalette() selects one (out of range -> 0) and, once
// uiGenInit() has run, recolours the styles from ui/globals.xml and refreshes
// all widgets. The LVGL theme, chart series and custom-drawn colours are up to
// the caller (ui.cpp, applyTheme()).
int uiGenPaletteCount(void);
const char* uiGenPaletteNames(void);
void uiGenSetPalette(int palette, bool dark);

// Dark mode at the first start (theme_dark in ui/globals.xml)
bool uiGenThemeDark(void);

#ifdef __cplusplus
}
#endif
