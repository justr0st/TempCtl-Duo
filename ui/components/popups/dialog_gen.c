/**
 * @file dialog_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "dialog_gen.h"
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

lv_obj_t * dialog_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "dialog_#");
        lv_obj_set_width(lv_obj_0, lv_pct(100));
        lv_obj_set_height(lv_obj_0, lv_pct(100));
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_backdrop, 0);
        lv_obj_t * dlg_panel = lv_obj_create(lv_obj_0);
        lv_obj_set_name(dlg_panel, "dlg_panel");
        lv_obj_set_width(dlg_panel, 290);
        lv_obj_set_height(dlg_panel, LV_SIZE_CONTENT);
        lv_obj_set_align(dlg_panel, LV_ALIGN_CENTER);
        lv_obj_set_flex_flow(dlg_panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flag(dlg_panel, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_pad_row(dlg_panel, 10, 0);
        lv_obj_add_style(dlg_panel, &style_dialog, 0);
        lv_obj_t * lv_obj_1 = lv_obj_create(dlg_panel);
        lv_obj_set_width(lv_obj_1, lv_pct(100));
        lv_obj_set_height(lv_obj_1, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(lv_obj_1, LV_FLEX_FLOW_ROW);
        lv_obj_set_flag(lv_obj_1, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_flex_cross_place(lv_obj_1, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_add_style(lv_obj_1, &style_plain, 0);
        lv_obj_t * dlg_title = lv_label_create(lv_obj_1);
        lv_obj_set_name(dlg_title, "dlg_title");
        lv_obj_set_flex_grow(dlg_title, 1);
        lv_label_set_long_mode(dlg_title, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(dlg_title, "Stop program?");
        lv_obj_set_style_text_font(dlg_title, font_20, 0);

        lv_obj_t * dlg_close = lv_button_create(lv_obj_1);
        lv_obj_set_name(dlg_close, "dlg_close");
        lv_obj_set_width(dlg_close, 36);
        lv_obj_set_height(dlg_close, 32);
        lv_obj_t * lv_label_0 = lv_label_create(dlg_close);
        lv_obj_set_align(lv_label_0, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_0, "");
        lv_obj_set_style_text_font(lv_label_0, font_icons_14, 0);

        lv_obj_t * dlg_text = lv_label_create(dlg_panel);
        lv_obj_set_name(dlg_text, "dlg_text");
        lv_obj_set_width(dlg_text, lv_pct(100));
        lv_label_set_text(dlg_text, "The end action from Setup > Controller is applied.");

        lv_obj_t * dlg_footer = lv_obj_create(dlg_panel);
        lv_obj_set_name(dlg_footer, "dlg_footer");
        lv_obj_set_width(dlg_footer, lv_pct(100));
        lv_obj_set_height(dlg_footer, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(dlg_footer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flag(dlg_footer, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_flex_main_place(dlg_footer, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
        lv_obj_add_style(dlg_footer, &style_plain, 0);
        lv_obj_t * dlg_btn1 = lv_button_create(dlg_footer);
        lv_obj_set_name(dlg_btn1, "dlg_btn1");
        lv_obj_set_width(dlg_btn1, 80);
        lv_obj_set_height(dlg_btn1, 36);
        lv_obj_t * lv_label_1 = lv_label_create(dlg_btn1);
        lv_obj_set_align(lv_label_1, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_1, "Stop");

        lv_obj_t * dlg_btn2 = lv_button_create(dlg_footer);
        lv_obj_set_name(dlg_btn2, "dlg_btn2");
        lv_obj_set_width(dlg_btn2, 80);
        lv_obj_set_height(dlg_btn2, 36);
        lv_obj_t * lv_label_2 = lv_label_create(dlg_btn2);
        lv_obj_set_align(lv_label_2, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_2, "Cancel");

        lv_obj_t * dlg_btn3 = lv_button_create(dlg_footer);
        lv_obj_set_name(dlg_btn3, "dlg_btn3");
        lv_obj_set_width(dlg_btn3, 80);
        lv_obj_set_height(dlg_btn3, 36);
        lv_obj_t * lv_label_3 = lv_label_create(dlg_btn3);
        lv_obj_set_align(lv_label_3, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_3, "Other");

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

