/**
 * @file wifi_item_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "wifi_item_gen.h"
#include "../../tempctl_ui.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/***********************
 *  STATIC VARIABLES
 **********************/

/***********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * wifi_item_create(lv_obj_t * parent, const char * ssid)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        lv_obj_t * lv_button_0 = lv_button_create(parent);
        lv_obj_set_name_static(lv_button_0, "wifi_item_#");
        lv_obj_set_width(lv_button_0, lv_pct(100));
        lv_obj_set_height(lv_button_0, 36);
        lv_obj_set_flex_flow(lv_button_0, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_cross_place(lv_button_0, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_column(lv_button_0, 10, 0);

        lv_obj_add_style(lv_button_0, &style_list_item, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_button_0);
        lv_label_set_text(lv_label_0, "");
        lv_obj_set_style_text_font(lv_label_0, font_icons_14, 0);

        lv_obj_t * wifi_ssid = lv_label_create(lv_button_0);
        lv_obj_set_name(wifi_ssid, "wifi_ssid");
        lv_obj_set_flex_grow(wifi_ssid, 1);
        lv_label_set_long_mode(wifi_ssid, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(wifi_ssid, ssid);

        the_root = lv_button_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

