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