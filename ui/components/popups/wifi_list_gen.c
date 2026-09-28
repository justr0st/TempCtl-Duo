/**
 * @file wifi_list_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "wifi_list_gen.h"
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

lv_obj_t * wifi_list_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "wifi_list_#");
        lv_obj_set_width(lv_obj_0, lv_pct(100));
        lv_obj_set_height(lv_obj_0, lv_pct(100));
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_popup, 0);
        lv_obj_t * scan_title = lv_label_create(lv_obj_0);
        lv_obj_set_name(scan_title, "scan_title");
        lv_obj_set_x(scan_title, 0);
        lv_obj_set_y(scan_title, 0);
        lv_obj_set_width(scan_title, 312);
        lv_label_set_long_mode(scan_title, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(scan_title, "Pick a network");

        lv_obj_t * scan_list = lv_obj_create(lv_obj_0);
        lv_obj_set_name(scan_list, "scan_list");
        lv_obj_set_x(scan_list, 0);
        lv_obj_set_y(scan_list, 22);
        lv_obj_set_width(scan_list, 312);
        lv_obj_set_height(scan_list, 172);
        lv_obj_set_flex_flow(scan_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(scan_list, 0, 0);
        lv_obj_add_style(scan_list, &style_panel, 0);
        lv_obj_add_style(scan_list, &style_list, 0);
        wifi_item_create(scan_list, "Workshop");

        wifi_item_create(scan_list, "FRITZ!Box 7590");

        wifi_item_create(scan_list, "Guest");

        lv_obj_t * scan_spinner = lv_spinner_create(lv_obj_0);
        lv_obj_set_name(scan_spinner, "scan_spinner");
        lv_obj_set_x(scan_spinner, 132);
        lv_obj_set_y(scan_spinner, 84);
        lv_obj_set_width(scan_spinner, 48);
        lv_obj_set_height(scan_spinner, 48);
        lv_obj_add_style(scan_spinner, &style_spinner, 0);
        lv_obj_add_style(scan_spinner, &style_spinner, LV_PART_INDICATOR);

        lv_obj_t * scan_cancel = lv_button_create(lv_obj_0);
        lv_obj_set_name(scan_cancel, "scan_cancel");
        lv_obj_set_x(scan_cancel, 220);
        lv_obj_set_y(scan_cancel, 198);
        lv_obj_set_width(scan_cancel, 92);
        lv_obj_set_height(scan_cancel, 34);
        lv_obj_t * lv_label_0 = lv_label_create(scan_cancel);
        lv_obj_set_align(lv_label_0, LV_ALIGN_CENTER);
        lv_label_set_translation_tag(lv_label_0, "btn_cancel");

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

