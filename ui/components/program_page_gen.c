/**
 * @file program_page_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "program_page_gen.h"
#include "../tempctl_ui.h"

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

lv_obj_t * program_page_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "program_page_#");
        lv_obj_set_width(lv_obj_0, lv_pct(100));
        lv_obj_set_height(lv_obj_0, lv_pct(100));
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_plain, 0);
        lv_obj_t * prog_select = lv_dropdown_create(lv_obj_0);
        lv_obj_set_name(prog_select, "prog_select");
        lv_obj_set_x(prog_select, 0);
        lv_obj_set_y(prog_select, 0);
        lv_obj_set_width(prog_select, 196);
        lv_obj_set_height(prog_select, 32);
        lv_dropdown_set_options(prog_select, "1 Example");
        lv_obj_set_style_pad_all(prog_select, 7, 0);
        lv_obj_add_style(prog_select, &style_panel, 0);

        lv_obj_t * prog_rename = lv_button_create(lv_obj_0);
        lv_obj_set_name(prog_rename, "prog_rename");
        lv_obj_set_x(prog_rename, 200);
        lv_obj_set_y(prog_rename, 0);
        lv_obj_set_width(prog_rename, 116);
        lv_obj_set_height(prog_rename, 32);
        lv_obj_t * lv_label_0 = lv_label_create(prog_rename);
        lv_obj_set_align(lv_label_0, LV_ALIGN_CENTER);
        lv_label_set_translation_tag(lv_label_0, "btn_name");

        lv_obj_t * lv_label_1 = lv_label_create(lv_obj_0);
        lv_obj_set_x(lv_label_1, 5);
        lv_obj_set_y(lv_label_1, 36);
        lv_label_set_translation_tag(lv_label_1, "prog_col_no");
        lv_obj_add_style(lv_label_1, &style_caption, 0);

        lv_obj_t * lv_label_2 = lv_label_create(lv_obj_0);
        lv_obj_set_x(lv_label_2, 39);
        lv_obj_set_y(lv_label_2, 36);
        lv_label_set_translation_tag(lv_label_2, "prog_col_target");
        lv_obj_add_style(lv_label_2, &style_caption, 0);

        lv_obj_t * lv_label_3 = lv_label_create(lv_obj_0);
        lv_obj_set_x(lv_label_3, 133);
        lv_obj_set_y(lv_label_3, 36);
        lv_label_set_translation_tag(lv_label_3, "prog_col_ramp");
        lv_obj_add_style(lv_label_3, &style_caption, 0);

        lv_obj_t * lv_label_4 = lv_label_create(lv_obj_0);
        lv_obj_set_x(lv_label_4, 227);
        lv_obj_set_y(lv_label_4, 36);
        lv_label_set_translation_tag(lv_label_4, "prog_col_hold");
        lv_obj_add_style(lv_label_4, &style_caption, 0);

        lv_obj_t * prog_table = lv_table_create(lv_obj_0);
        lv_obj_set_name(prog_table, "prog_table");
        lv_obj_set_x(prog_table, 0);
        lv_obj_set_y(prog_table, 52);
        lv_obj_set_width(prog_table, 316);
        lv_obj_set_height(prog_table, 104);
        lv_table_set_column_count(prog_table, 4);
        lv_table_set_row_count(prog_table, 3);
        lv_obj_set_style_pad_all(prog_table, 0, 0);
        lv_obj_add_style(prog_table, &style_panel, 0);
        lv_obj_add_style(prog_table, &style_panel, LV_PART_ITEMS);
        lv_obj_add_style(prog_table, &style_table_cell, LV_PART_ITEMS);
        lv_table_set_column_width(prog_table, 0, 34);
        lv_table_set_column_width(prog_table, 1, 94);
        lv_table_set_column_width(prog_table, 2, 94);
        lv_table_set_column_width(prog_table, 3, 92);
        lv_table_set_cell_value(prog_table, 0, 0, "1");
        lv_table_set_cell_value(prog_table, 0, 1, "100.0");
        lv_table_set_cell_value(prog_table, 0, 2, "30 min");
        lv_table_set_cell_value(prog_table, 0, 3, "30 min");
        lv_table_set_cell_value(prog_table, 1, 0, "2");
        lv_table_set_cell_value(prog_table, 1, 1, "250.0");
        lv_table_set_cell_value(prog_table, 1, 2, "60 min");
        lv_table_set_cell_value(prog_table, 1, 3, "45 s");
        lv_table_set_cell_value(prog_table, 2, 0, "3");
        lv_table_set_cell_value(prog_table, 2, 1, "400.0");
        lv_table_set_cell_value(prog_table, 2, 2, "0");
        lv_table_set_cell_value(prog_table, 2, 3, "120 min");

        lv_obj_t * prog_info = lv_label_create(lv_obj_0);
        lv_obj_set_name(prog_info, "prog_info");
        lv_obj_set_x(prog_info, 2);
        lv_obj_set_y(prog_info, 158);
        lv_label_set_text(prog_info, "3/100 steps  total 4:45:45");
        lv_obj_add_style(prog_info, &style_small, 0);

        lv_obj_t * prog_unsaved = lv_label_create(lv_obj_0);
        lv_obj_set_name(prog_unsaved, "prog_unsaved");
        lv_obj_set_x(prog_unsaved, 200);
        lv_obj_set_y(prog_unsaved, 158);
        lv_label_set_text(prog_unsaved, "unsaved");
        lv_obj_set_style_text_color(prog_unsaved, COLOR_WARN, 0);
        lv_obj_add_style(prog_unsaved, &style_small, 0);

        lv_obj_t * prog_add = lv_button_create(lv_obj_0);
        lv_obj_set_name(prog_add, "prog_add");
        lv_obj_set_x(prog_add, 0);
        lv_obj_set_y(prog_add, 174);
        lv_obj_set_width(prog_add, 40);
        lv_obj_set_height(prog_add, 30);
        lv_obj_t * lv_label_5 = lv_label_create(prog_add);
        lv_obj_set_align(lv_label_5, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_5, "");
        lv_obj_set_style_text_font(lv_label_5, font_icons_14, 0);

        lv_obj_t * prog_del = lv_button_create(lv_obj_0);
        lv_obj_set_name(prog_del, "prog_del");
        lv_obj_set_x(prog_del, 44);
        lv_obj_set_y(prog_del, 174);
        lv_obj_set_width(prog_del, 40);
        lv_obj_set_height(prog_del, 30);
        lv_obj_t * lv_label_6 = lv_label_create(prog_del);
        lv_obj_set_align(lv_label_6, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_6, "");
        lv_obj_set_style_text_font(lv_label_6, font_icons_14, 0);

        lv_obj_t * prog_up = lv_button_create(lv_obj_0);
        lv_obj_set_name(prog_up, "prog_up");
        lv_obj_set_x(prog_up, 88);
        lv_obj_set_y(prog_up, 174);
        lv_obj_set_width(prog_up, 40);
        lv_obj_set_height(prog_up, 30);
        lv_obj_t * lv_label_7 = lv_label_create(prog_up);
        lv_obj_set_align(lv_label_7, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_7, "");
        lv_obj_set_style_text_font(lv_label_7, font_icons_14, 0);

        lv_obj_t * prog_down = lv_button_create(lv_obj_0);
        lv_obj_set_name(prog_down, "prog_down");
        lv_obj_set_x(prog_down, 132);
        lv_obj_set_y(prog_down, 174);
        lv_obj_set_width(prog_down, 40);
        lv_obj_set_height(prog_down, 30);
        lv_obj_t * lv_label_8 = lv_label_create(prog_down);
        lv_obj_set_align(lv_label_8, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_8, "");
        lv_obj_set_style_text_font(lv_label_8, font_icons_14, 0);

        lv_obj_t * prog_revert = lv_button_create(lv_obj_0);
        lv_obj_set_name(prog_revert, "prog_revert");
        lv_obj_set_x(prog_revert, 176);
        lv_obj_set_y(prog_revert, 174);
        lv_obj_set_width(prog_revert, 62);
        lv_obj_set_height(prog_revert, 30);
        lv_obj_t * lv_label_9 = lv_label_create(prog_revert);
        lv_obj_set_align(lv_label_9, LV_ALIGN_CENTER);
        lv_label_set_translation_tag(lv_label_9, "btn_revert");

        lv_obj_t * prog_save = lv_button_create(lv_obj_0);
        lv_obj_set_name(prog_save, "prog_save");
        lv_obj_set_x(prog_save, 242);
        lv_obj_set_y(prog_save, 174);
        lv_obj_set_width(prog_save, 74);
        lv_obj_set_height(prog_save, 30);
        lv_obj_t * lv_label_10 = lv_label_create(prog_save);
        lv_obj_set_align(lv_label_10, LV_ALIGN_CENTER);
        lv_label_set_translation_tag(lv_label_10, "btn_save");

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

