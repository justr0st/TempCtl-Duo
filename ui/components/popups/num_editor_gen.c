/**
 * @file num_editor_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "num_editor_gen.h"
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

lv_obj_t * num_editor_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "num_editor_#");
        lv_obj_set_width(lv_obj_0, lv_pct(100));
        lv_obj_set_height(lv_obj_0, lv_pct(100));
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_popup, 0);
        lv_obj_t * num_title = lv_label_create(lv_obj_0);
        lv_obj_set_name(num_title, "num_title");
        lv_obj_set_x(num_title, 0);
        lv_obj_set_y(num_title, 0);
        lv_obj_set_width(num_title, 312);
        lv_label_set_long_mode(num_title, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(num_title, "Step 4 ramp time (0 = jump)");

        lv_obj_t * num_ta = lv_textarea_create(lv_obj_0);
        lv_obj_set_name(num_ta, "num_ta");
        lv_obj_set_x(num_ta, 0);
        lv_obj_set_y(num_ta, 24);
        lv_obj_set_width(num_ta, 160);
        lv_textarea_set_one_line(num_ta, true);
        lv_textarea_set_text(num_ta, "90");

        lv_obj_t * num_units = lv_buttonmatrix_create(lv_obj_0);
        lv_obj_set_name(num_units, "num_units");
        lv_obj_set_x(num_units, 172);
        lv_obj_set_y(num_units, 20);
        lv_obj_set_width(num_units, 136);
        lv_obj_set_height(num_units, 44);
        static const char *num_units_map_0[] = {"min", "s", NULL};
        lv_buttonmatrix_set_map(num_units, num_units_map_0);
        static const lv_buttonmatrix_ctrl_t num_units_ctrl_map_0[] = {LV_BUTTONMATRIX_CTRL_CHECKABLE | LV_BUTTONMATRIX_CTRL_CHECKED, LV_BUTTONMATRIX_CTRL_CHECKABLE};
        lv_buttonmatrix_set_ctrl_map(num_units, num_units_ctrl_map_0);
        lv_buttonmatrix_set_one_checked(num_units, true);

        lv_obj_t * num_kb = lv_keyboard_create(lv_obj_0);
        lv_obj_set_name(num_kb, "num_kb");
        lv_obj_set_width(num_kb, 312);
        lv_obj_set_height(num_kb, 150);
        lv_obj_set_align(num_kb, LV_ALIGN_BOTTOM_MID);
        lv_keyboard_set_mode(num_kb, LV_KEYBOARD_MODE_NUMBER);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

