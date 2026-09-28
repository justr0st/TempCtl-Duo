/**
 * @file text_editor_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "text_editor_gen.h"
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

lv_obj_t * text_editor_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "text_editor_#");
        lv_obj_set_width(lv_obj_0, lv_pct(100));
        lv_obj_set_height(lv_obj_0, lv_pct(100));
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_popup, 0);
        lv_obj_t * text_title = lv_label_create(lv_obj_0);
        lv_obj_set_name(text_title, "text_title");
        lv_obj_set_x(text_title, 0);
        lv_obj_set_y(text_title, 0);
        lv_obj_set_width(text_title, 312);
        lv_label_set_long_mode(text_title, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(text_title, "Name of program 1");

        lv_obj_t * text_ta = lv_textarea_create(lv_obj_0);
        lv_obj_set_name(text_ta, "text_ta");
        lv_obj_set_x(text_ta, 0);
        lv_obj_set_y(text_ta, 20);
        lv_obj_set_width(text_ta, 312);
        lv_textarea_set_one_line(text_ta, true);
        lv_textarea_set_text(text_ta, "GPU desolder (LeadFree)");

        lv_obj_t * text_kb = lv_keyboard_create(lv_obj_0);
        lv_obj_set_name(text_kb, "text_kb");
        lv_obj_set_width(text_kb, 312);
        lv_obj_set_height(text_kb, 166);
        lv_obj_set_align(text_kb, LV_ALIGN_BOTTOM_MID);
        lv_keyboard_set_mode(text_kb, LV_KEYBOARD_MODE_TEXT_LOWER);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

