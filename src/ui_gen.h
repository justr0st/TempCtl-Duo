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

// Constants from ui/globals.xml
typedef enum { UI_COLOR_ACCENT, UI_COLOR_ACCENT2, UI_COLOR_ROW_SELECTED, UI_COLOR_MUTED } UiGenColor;
lv_color_t uiGenColor(UiGenColor c);
bool uiGenThemeDark(void);

#ifdef __cplusplus
}
#endif
