// Forwards the UI code exported by LVGL Pro to ui.cpp (see ui_gen.h).
#include "ui_gen.h"
#include "tempctl_ui.h"

// The exported fonts only contain ASCII (0x20-0x7F). LVGL's symbols (dropdown
// arrows, LV_SYMBOL_WARNING, ...) come from the built-in Montserrat fonts,
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

lv_obj_t* uiGenInit(void) {
  UI_FONT_WITH_FALLBACK(font_12, &lv_font_montserrat_12);
  UI_FONT_WITH_FALLBACK(font_14, &lv_font_montserrat_14);
  UI_FONT_WITH_FALLBACK(font_20, &lv_font_montserrat_14);
  UI_FONT_WITH_FALLBACK(font_24, &lv_font_montserrat_14);
  UI_FONT_WITH_FALLBACK(font_32, &lv_font_montserrat_14);
  UI_FONT_WITH_FALLBACK(font_48, &lv_font_montserrat_14);
  tempctl_ui_init("");
  return gui;
}

lv_obj_t* uiGenDialog(lv_obj_t* parent) { return dialog_create(parent); }
lv_obj_t* uiGenNumEditor(lv_obj_t* parent) { return num_editor_create(parent); }
lv_obj_t* uiGenTextEditor(lv_obj_t* parent) { return text_editor_create(parent); }
lv_obj_t* uiGenWifiList(lv_obj_t* parent) { return wifi_list_create(parent); }
lv_obj_t* uiGenWifiItem(lv_obj_t* parent, const char* ssid) { return wifi_item_create(parent, ssid); }

lv_color_t uiGenColor(UiGenColor c) {
  switch (c) {
    case UI_COLOR_ACCENT: return COLOR_ACCENT;
    case UI_COLOR_ACCENT2: return COLOR_ACCENT2;
    case UI_COLOR_ROW_SELECTED: return COLOR_ROW_SELECTED;
    case UI_COLOR_MUTED: return COLOR_MUTED;
  }
  return lv_color_white();
}

bool uiGenThemeDark(void) { return THEME_DARK != 0; }
