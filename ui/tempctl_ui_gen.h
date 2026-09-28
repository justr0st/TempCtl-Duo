/**
 * @file tempctl_ui_gen.h
 */

#ifndef LVGL_PRO_TEMPCTL_UI_GEN_H
#define LVGL_PRO_TEMPCTL_UI_GEN_H

#ifndef UI_SUBJECT_STRING_LENGTH
#define UI_SUBJECT_STRING_LENGTH 256
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
    #include "lvgl_private.h"
#else
    #include "lvgl/lvgl.h"
    #include "lvgl/lvgl_private.h"
#endif

#if defined(LV_USE_XML) && LV_USE_XML
    #include "lv_xml/lv_xml.h"
#endif



/* Prototypes for target functions, needed by responsive const definitions */

void tempctl_ui_set_target(uint32_t target);
uint32_t tempctl_ui_get_target(void);
bool tempctl_ui_check_target(uint32_t target);

/*********************
 *      DEFINES
 *********************/

#define TEMPCTL_UI_TARGET_UNDEFINED  (0 << 1)
#define TEMPCTL_UI_TARGET_CYD        (1 << 1)
#define TEMPCTL_UI_TARGET_ALL        0x0FFFFFFF

/* By default compile for all targets, allowing to switch to any targets at runtime */
#ifndef TEMPCTL_UI_COMPILE_TARGET
#define TEMPCTL_UI_COMPILE_TARGET TEMPCTL_UI_TARGET_ALL
#endif

#define TEMPCTL_UI_CHECK_COMPILE_TARGET(target) (TEMPCTL_UI_COMPILE_TARGET & (target) ? 1 : 0)

/**
 * Screen background
 */
#define COLOR_BG lv_color_hex(0x15171A)
/**
 * Inputs, tables, chart background
 */
#define COLOR_PANEL lv_color_hex(0x24272D)
/**
 * Panel borders
 */
#define COLOR_BORDER lv_color_hex(0x3A3F47)
/**
 * Primary text
 */
#define COLOR_TEXT lv_color_hex(0xF2F4F7)
/**
 * Secondary text and captions
 */
#define COLOR_MUTED lv_color_hex(0x9AA3AF)
/**
 * Theme primary color: buttons, selection
 */
#define COLOR_ACCENT lv_color_hex(0x2196F3)
/**
 * Theme secondary color
 */
#define COLOR_ACCENT2 lv_color_hex(0xFF5722)
/**
 * Selected step in the program table
 */
#define COLOR_ROW_SELECTED lv_color_hex(0x1565C0)
/**
 * Stop button
 */
#define COLOR_DANGER lv_color_hex(0xF44336)
/**
 * Process value (temperature)
 */
#define COLOR_PV lv_color_hex(0xFF5722)
/**
 * Setpoint
 */
#define COLOR_SP lv_color_hex(0x03A9F4)
/**
 * Program curve
 */
#define COLOR_PLAN lv_color_hex(0x90A4AE)
/**
 * Link OK
 */
#define COLOR_OK lv_color_hex(0x4CAF50)
/**
 * Warnings / unsaved
 */
#define COLOR_WARN lv_color_hex(0xFFC107)
#define DEFAULT_LIGHT_BG lv_color_hex(0xF3F4F6)
#define DEFAULT_LIGHT_PANEL lv_color_hex(0xFFFFFF)
#define DEFAULT_LIGHT_BORDER lv_color_hex(0xD0D5DC)
#define DEFAULT_LIGHT_TEXT lv_color_hex(0x1F2328)
#define DEFAULT_LIGHT_MUTED lv_color_hex(0x5F6B7A)
#define DEFAULT_LIGHT_ACCENT lv_color_hex(0x1E88E5)
#define DEFAULT_LIGHT_ACCENT2 lv_color_hex(0xF4511E)
#define DEFAULT_LIGHT_ROW_SELECTED lv_color_hex(0xBBDEFB)
#define DEFAULT_LIGHT_DANGER lv_color_hex(0xE53935)
#define DEFAULT_LIGHT_PV lv_color_hex(0xE64A19)
#define DEFAULT_LIGHT_SP lv_color_hex(0x0288D1)
#define DEFAULT_LIGHT_PLAN lv_color_hex(0x78909C)
#define DEFAULT_LIGHT_OK lv_color_hex(0x43A047)
#define DEFAULT_LIGHT_WARN lv_color_hex(0xC77700)
#define DRACULA_DARK_BG lv_color_hex(0x282A36)
#define DRACULA_DARK_PANEL lv_color_hex(0x343746)
#define DRACULA_DARK_BORDER lv_color_hex(0x44475A)
#define DRACULA_DARK_TEXT lv_color_hex(0xF8F8F2)
#define DRACULA_DARK_MUTED lv_color_hex(0x8F97BD)
#define DRACULA_DARK_ACCENT lv_color_hex(0xD1449B)
#define DRACULA_DARK_ACCENT2 lv_color_hex(0xBD93F9)
#define DRACULA_DARK_ROW_SELECTED lv_color_hex(0x6B2E57)
#define DRACULA_DARK_DANGER lv_color_hex(0xFF5555)
#define DRACULA_DARK_PV lv_color_hex(0xFFB86C)
#define DRACULA_DARK_SP lv_color_hex(0x8BE9FD)
#define DRACULA_DARK_PLAN lv_color_hex(0x6272A4)
#define DRACULA_DARK_OK lv_color_hex(0x50FA7B)
#define DRACULA_DARK_WARN lv_color_hex(0xF1FA8C)
#define DRACULA_LIGHT_BG lv_color_hex(0xFFFBEB)
#define DRACULA_LIGHT_PANEL lv_color_hex(0xF5F1DF)
#define DRACULA_LIGHT_BORDER lv_color_hex(0xCFCFDE)
#define DRACULA_LIGHT_TEXT lv_color_hex(0x1F1F1F)
#define DRACULA_LIGHT_MUTED lv_color_hex(0x6C664B)
#define DRACULA_LIGHT_ACCENT lv_color_hex(0xA3144D)
#define DRACULA_LIGHT_ACCENT2 lv_color_hex(0x644AC9)
#define DRACULA_LIGHT_ROW_SELECTED lv_color_hex(0xF2D4E0)
#define DRACULA_LIGHT_DANGER lv_color_hex(0xCB3A2A)
#define DRACULA_LIGHT_PV lv_color_hex(0xA34D14)
#define DRACULA_LIGHT_SP lv_color_hex(0x036A96)
#define DRACULA_LIGHT_PLAN lv_color_hex(0x8E8A76)
#define DRACULA_LIGHT_OK lv_color_hex(0x14710A)
#define DRACULA_LIGHT_WARN lv_color_hex(0x846E15)
#define SOLARIZED_DARK_BG lv_color_hex(0x002B36)
#define SOLARIZED_DARK_PANEL lv_color_hex(0x073642)
#define SOLARIZED_DARK_BORDER lv_color_hex(0x586E75)
#define SOLARIZED_DARK_TEXT lv_color_hex(0xEEE8D5)
#define SOLARIZED_DARK_MUTED lv_color_hex(0x839496)
#define SOLARIZED_DARK_ACCENT lv_color_hex(0x6C71C4)
#define SOLARIZED_DARK_ACCENT2 lv_color_hex(0xCB4B16)
#define SOLARIZED_DARK_ROW_SELECTED lv_color_hex(0x3F4480)
#define SOLARIZED_DARK_DANGER lv_color_hex(0xDC322F)
#define SOLARIZED_DARK_PV lv_color_hex(0xCB4B16)
#define SOLARIZED_DARK_SP lv_color_hex(0x2AA198)
#define SOLARIZED_DARK_PLAN lv_color_hex(0x657B83)
#define SOLARIZED_DARK_OK lv_color_hex(0x859900)
#define SOLARIZED_DARK_WARN lv_color_hex(0xB58900)
#define SOLARIZED_LIGHT_BG lv_color_hex(0xFDF6E3)
#define SOLARIZED_LIGHT_PANEL lv_color_hex(0xEEE8D5)
#define SOLARIZED_LIGHT_BORDER lv_color_hex(0xD6CFBA)
#define SOLARIZED_LIGHT_TEXT lv_color_hex(0x073642)
#define SOLARIZED_LIGHT_MUTED lv_color_hex(0x657B83)
#define SOLARIZED_LIGHT_ACCENT lv_color_hex(0x6C71C4)
#define SOLARIZED_LIGHT_ACCENT2 lv_color_hex(0xCB4B16)
#define SOLARIZED_LIGHT_ROW_SELECTED lv_color_hex(0xD9DAF2)
#define SOLARIZED_LIGHT_DANGER lv_color_hex(0xDC322F)
#define SOLARIZED_LIGHT_PV lv_color_hex(0xCB4B16)
#define SOLARIZED_LIGHT_SP lv_color_hex(0x2AA198)
#define SOLARIZED_LIGHT_PLAN lv_color_hex(0x93A1A1)
#define SOLARIZED_LIGHT_OK lv_color_hex(0x859900)
#define SOLARIZED_LIGHT_WARN lv_color_hex(0xB58900)
#define GRUVBOX_DARK_BG lv_color_hex(0x282828)
#define GRUVBOX_DARK_PANEL lv_color_hex(0x3C3836)
#define GRUVBOX_DARK_BORDER lv_color_hex(0x504945)
#define GRUVBOX_DARK_TEXT lv_color_hex(0xEBDBB2)
#define GRUVBOX_DARK_MUTED lv_color_hex(0xA89984)
#define GRUVBOX_DARK_ACCENT lv_color_hex(0xD65D0E)
#define GRUVBOX_DARK_ACCENT2 lv_color_hex(0xD65D0E)
#define GRUVBOX_DARK_ROW_SELECTED lv_color_hex(0x7C3A0E)
#define GRUVBOX_DARK_DANGER lv_color_hex(0xCC241D)
#define GRUVBOX_DARK_PV lv_color_hex(0xFE8019)
#define GRUVBOX_DARK_SP lv_color_hex(0x83A598)
#define GRUVBOX_DARK_PLAN lv_color_hex(0x928374)
#define GRUVBOX_DARK_OK lv_color_hex(0xB8BB26)
#define GRUVBOX_DARK_WARN lv_color_hex(0xFABD2F)
#define GRUVBOX_LIGHT_BG lv_color_hex(0xFBF1C7)
#define GRUVBOX_LIGHT_PANEL lv_color_hex(0xF2E5BC)
#define GRUVBOX_LIGHT_BORDER lv_color_hex(0xD5C4A1)
#define GRUVBOX_LIGHT_TEXT lv_color_hex(0x3C3836)
#define GRUVBOX_LIGHT_MUTED lv_color_hex(0x7C6F64)
#define GRUVBOX_LIGHT_ACCENT lv_color_hex(0xAF3A03)
#define GRUVBOX_LIGHT_ACCENT2 lv_color_hex(0xAF3A03)
#define GRUVBOX_LIGHT_ROW_SELECTED lv_color_hex(0xF0D2B0)
#define GRUVBOX_LIGHT_DANGER lv_color_hex(0x9D0006)
#define GRUVBOX_LIGHT_PV lv_color_hex(0xAF3A03)
#define GRUVBOX_LIGHT_SP lv_color_hex(0x076678)
#define GRUVBOX_LIGHT_PLAN lv_color_hex(0x928374)
#define GRUVBOX_LIGHT_OK lv_color_hex(0x79740E)
#define GRUVBOX_LIGHT_WARN lv_color_hex(0xB57614)
#define CATPPUCCIN_DARK_BG lv_color_hex(0x1E1E2E)
#define CATPPUCCIN_DARK_PANEL lv_color_hex(0x313244)
#define CATPPUCCIN_DARK_BORDER lv_color_hex(0x45475A)
#define CATPPUCCIN_DARK_TEXT lv_color_hex(0xCDD6F4)
#define CATPPUCCIN_DARK_MUTED lv_color_hex(0xA6ADC8)
#define CATPPUCCIN_DARK_ACCENT lv_color_hex(0x8839EF)
#define CATPPUCCIN_DARK_ACCENT2 lv_color_hex(0xFAB387)
#define CATPPUCCIN_DARK_ROW_SELECTED lv_color_hex(0x4A3F6B)
#define CATPPUCCIN_DARK_DANGER lv_color_hex(0xF38BA8)
#define CATPPUCCIN_DARK_PV lv_color_hex(0xFAB387)
#define CATPPUCCIN_DARK_SP lv_color_hex(0x89DCEB)
#define CATPPUCCIN_DARK_PLAN lv_color_hex(0x9399B2)
#define CATPPUCCIN_DARK_OK lv_color_hex(0xA6E3A1)
#define CATPPUCCIN_DARK_WARN lv_color_hex(0xF9E2AF)
#define CATPPUCCIN_LIGHT_BG lv_color_hex(0xEFF1F5)
#define CATPPUCCIN_LIGHT_PANEL lv_color_hex(0xE6E9EF)
#define CATPPUCCIN_LIGHT_BORDER lv_color_hex(0xCCD0DA)
#define CATPPUCCIN_LIGHT_TEXT lv_color_hex(0x4C4F69)
#define CATPPUCCIN_LIGHT_MUTED lv_color_hex(0x6C6F85)
#define CATPPUCCIN_LIGHT_ACCENT lv_color_hex(0x8839EF)
#define CATPPUCCIN_LIGHT_ACCENT2 lv_color_hex(0xFE640B)
#define CATPPUCCIN_LIGHT_ROW_SELECTED lv_color_hex(0xE1D2FA)
#define CATPPUCCIN_LIGHT_DANGER lv_color_hex(0xD20F39)
#define CATPPUCCIN_LIGHT_PV lv_color_hex(0xFE640B)
#define CATPPUCCIN_LIGHT_SP lv_color_hex(0x04A5E5)
#define CATPPUCCIN_LIGHT_PLAN lv_color_hex(0x8C8FA1)
#define CATPPUCCIN_LIGHT_OK lv_color_hex(0x40A02B)
#define CATPPUCCIN_LIGHT_WARN lv_color_hex(0xDF8E1D)
#define ONE_DARK_BG lv_color_hex(0x282C34)
#define ONE_DARK_PANEL lv_color_hex(0x31363F)
#define ONE_DARK_BORDER lv_color_hex(0x3E4451)
#define ONE_DARK_TEXT lv_color_hex(0xDCDFE4)
#define ONE_DARK_MUTED lv_color_hex(0x8B919D)
#define ONE_DARK_ACCENT lv_color_hex(0x50A14F)
#define ONE_DARK_ACCENT2 lv_color_hex(0xD19A66)
#define ONE_DARK_ROW_SELECTED lv_color_hex(0x2F5A36)
#define ONE_DARK_DANGER lv_color_hex(0xE06C75)
#define ONE_DARK_PV lv_color_hex(0xD19A66)
#define ONE_DARK_SP lv_color_hex(0x56B6C2)
#define ONE_DARK_PLAN lv_color_hex(0x7F848E)
#define ONE_DARK_OK lv_color_hex(0x98C379)
#define ONE_DARK_WARN lv_color_hex(0xE5C07B)
#define ONE_LIGHT_BG lv_color_hex(0xFAFAFA)
#define ONE_LIGHT_PANEL lv_color_hex(0xF0F0F1)
#define ONE_LIGHT_BORDER lv_color_hex(0xDBDBDC)
#define ONE_LIGHT_TEXT lv_color_hex(0x383A42)
#define ONE_LIGHT_MUTED lv_color_hex(0x696C77)
#define ONE_LIGHT_ACCENT lv_color_hex(0x50A14F)
#define ONE_LIGHT_ACCENT2 lv_color_hex(0x986801)
#define ONE_LIGHT_ROW_SELECTED lv_color_hex(0xD3ECD2)
#define ONE_LIGHT_DANGER lv_color_hex(0xCA1243)
#define ONE_LIGHT_PV lv_color_hex(0xE45649)
#define ONE_LIGHT_SP lv_color_hex(0x0184BC)
#define ONE_LIGHT_PLAN lv_color_hex(0xA0A1A7)
#define ONE_LIGHT_OK lv_color_hex(0x50A14F)
#define ONE_LIGHT_WARN lv_color_hex(0xC18401)
#define SPACE_XS 2
#define SPACE_SM 4
#define SPACE_MD 8
#define TAB_BAR_HEIGHT 32
#define ROW_HEIGHT 34
#define THEME_DARK 1


#ifndef LV_XML_EVAL_STRING_BUF_SIZE
    #define LV_XML_EVAL_STRING_BUF_SIZE 256
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL VARIABLES
 **********************/

/*-------------------
 * Permanent screens
 *------------------*/

extern lv_obj_t * gui;

/*----------------
 * Global styles
 *----------------*/

extern lv_style_t style_screen;
extern lv_style_t style_plain;
extern lv_style_t style_page;
extern lv_style_t style_caption;
extern lv_style_t style_small;
extern lv_style_t style_panel;
extern lv_style_t style_tab_bar;
extern lv_style_t style_row;
extern lv_style_t style_danger;
extern lv_style_t style_text_pv;
extern lv_style_t style_text_sp;
extern lv_style_t style_text_plan;
extern lv_style_t style_text_warn;
extern lv_style_t style_text_accent;
extern lv_style_t style_border_accent;
extern lv_style_t style_bg_ok;
extern lv_style_t style_table_cell;
extern lv_style_t style_chart_line;
extern lv_style_t style_no_points;
extern lv_style_t style_scale_ticks;
extern lv_style_t style_scale_line;
extern lv_style_t style_link_error;
extern lv_style_t style_popup;
extern lv_style_t style_backdrop;
extern lv_style_t style_dialog;
extern lv_style_t style_list;
extern lv_style_t style_spinner;
extern lv_style_t style_list_item;

/*----------------
 * Fonts
 *----------------*/

/* Targets: any */
extern lv_font_t * font_12;
extern lv_font_t * font_14;
extern lv_font_t * font_20;
extern lv_font_t * font_24;
extern lv_font_t * font_32;
extern lv_font_t * font_48;
extern lv_font_t * fontM_12;
extern lv_font_t * fontM_14;
extern lv_font_t * fontM_20;
extern lv_font_t * fontM_24;
extern lv_font_t * fontM_32;
extern lv_font_t * fontM_48;
extern lv_font_t * font_icons_14;


/*----------------
 * Images
 *----------------*/



/*----------------
 * Subjects
 *----------------*/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/*----------------
 * Event Callbacks
 *----------------*/

/**
 * Initialize the component library
 */

void tempctl_ui_init_gen(const char * asset_path);

/**********************
 *      MACROS
 **********************/

/**********************
 *   POST INCLUDES
 **********************/

/*Include all the widgets, components and screens of this library*/
#include "components/chart_page_gen.h"
#include "components/controller_panel_gen.h"
#include "components/main_page_gen.h"
#include "components/popups/dialog_gen.h"
#include "components/popups/num_editor_gen.h"
#include "components/popups/text_editor_gen.h"
#include "components/popups/wifi_item_gen.h"
#include "components/popups/wifi_list_gen.h"
#include "components/program_page_gen.h"
#include "components/setting_row_gen.h"
#include "components/setup_page_gen.h"
#include "components/system_panel_gen.h"
#include "screens/gui_gen.h"

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LVGL_PRO_TEMPCTL_UI_GEN_H*/