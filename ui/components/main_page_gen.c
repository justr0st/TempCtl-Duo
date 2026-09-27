/**
 * @file main_page_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "main_page_gen.h"
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

lv_obj_t * main_page_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "main_page_#");
        lv_obj_set_width(lv_obj_0, lv_pct(100));
        lv_obj_set_height(lv_obj_0, lv_pct(100));
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_plain, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_obj_set_x(lv_label_0, 2);
        lv_obj_set_y(lv_label_0, 0);
        lv_label_set_translation_tag(lv_label_0, "main_pv_caption");
        lv_obj_add_style(lv_label_0, &style_caption, 0);

        lv_obj_t * main_pv = lv_label_create(lv_obj_0);
        lv_obj_set_name(main_pv, "main_pv");
        lv_obj_set_x(main_pv, 0);
        lv_obj_set_y(main_pv, 12);
        lv_label_set_text(main_pv, "153.8");
        lv_obj_set_style_text_font(main_pv, font_48, 0);
        lv_obj_set_style_text_color(main_pv, COLOR_PV, 0);

        lv_obj_t * main_sp = lv_label_create(lv_obj_0);
        lv_obj_set_name(main_sp, "main_sp");
        lv_obj_set_x(main_sp, 2);
        lv_obj_set_y(main_sp, 62);
        lv_label_set_text(main_sp, "SP 150.0");
        lv_obj_set_style_text_font(main_sp, font_24, 0);
        lv_obj_set_style_text_color(main_sp, COLOR_SP, 0);

        lv_obj_t * main_link = lv_obj_create(lv_obj_0);
        lv_obj_set_name(main_link, "main_link");
        lv_obj_set_x(main_link, 296);
        lv_obj_set_y(main_link, 2);
        lv_obj_set_width(main_link, 12);
        lv_obj_set_height(main_link, 12);
        lv_obj_set_flag(main_link, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_radius(main_link, 6, 0);
        lv_obj_set_style_border_width(main_link, 0, 0);
        lv_obj_set_style_bg_color(main_link, COLOR_OK, 0);
        lv_obj_set_style_bg_opa(main_link, (255 * 100 / 100), 0);
        lv_obj_add_style(main_link, &style_link_error, LV_STATE_USER_1);

        lv_obj_t * main_state = lv_label_create(lv_obj_0);
        lv_obj_set_name(main_state, "main_state");
        lv_obj_set_x(main_state, 176);
        lv_obj_set_y(main_state, 0);
        lv_label_set_text(main_state, "Idle");
        lv_obj_set_style_text_font(main_state, font_20, 0);

        lv_obj_t * main_phase = lv_label_create(lv_obj_0);
        lv_obj_set_name(main_phase, "main_phase");
        lv_obj_set_x(main_phase, 176);
        lv_obj_set_y(main_phase, 24);
        lv_label_set_text(main_phase, "Ramp 36:40 left");
        lv_obj_add_style(main_phase, &style_small, 0);

        lv_obj_t * main_total = lv_label_create(lv_obj_0);
        lv_obj_set_name(main_total, "main_total");
        lv_obj_set_x(main_total, 176);
        lv_obj_set_y(main_total, 38);
        lv_obj_set_width(main_total, 140);
        lv_label_set_long_mode(main_total, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(main_total, "Example, 10:18:05");
        lv_obj_add_style(main_total, &style_small, 0);

        lv_obj_t * main_out = lv_label_create(lv_obj_0);
        lv_obj_set_name(main_out, "main_out");
        lv_obj_set_x(main_out, 176);
        lv_obj_set_y(main_out, 52);
        lv_label_set_text(main_out, "Out 37%");
        lv_obj_add_style(main_out, &style_small, 0);

        lv_obj_t * main_mode = lv_label_create(lv_obj_0);
        lv_obj_set_name(main_mode, "main_mode");
        lv_obj_set_x(main_mode, 236);
        lv_obj_set_y(main_mode, 52);
        lv_label_set_text(main_mode, "Automatic");
        lv_obj_add_style(main_mode, &style_small, 0);

        lv_obj_t * main_target = lv_label_create(lv_obj_0);
        lv_obj_set_name(main_target, "main_target");
        lv_obj_set_x(main_target, 176);
        lv_obj_set_y(main_target, 68);
        lv_label_set_text(main_target, "");
        lv_obj_add_style(main_target, &style_small, 0);

        lv_obj_t * lv_label_1 = lv_label_create(lv_obj_0);
        lv_obj_set_x(lv_label_1, 156);
        lv_obj_set_y(lv_label_1, 101);
        lv_label_set_translation_tag(lv_label_1, "main_manual_sp");

        lv_obj_t * main_man_sp = lv_button_create(lv_obj_0);
        lv_obj_set_name(main_man_sp, "main_man_sp");
        lv_obj_set_x(main_man_sp, 0);
        lv_obj_set_y(main_man_sp, 92);
        lv_obj_set_width(main_man_sp, 92);
        lv_obj_set_height(main_man_sp, ROW_HEIGHT);
        lv_obj_set_style_bg_opa(main_man_sp, 100, 0);
        lv_obj_set_style_border_color(main_man_sp, COLOR_ACCENT, 0);
        lv_obj_set_style_border_width(main_man_sp, 2, 0);
        lv_obj_t * main_man_sp_text = lv_label_create(main_man_sp);
        lv_obj_set_name(main_man_sp_text, "main_man_sp_text");
        lv_obj_set_align(main_man_sp_text, LV_ALIGN_CENTER);
        lv_label_set_text(main_man_sp_text, "150.0");
        lv_obj_set_style_text_font(main_man_sp_text, font_32, 0);
        lv_obj_set_style_text_color(main_man_sp_text, COLOR_ACCENT, 0);

        lv_obj_t * main_man_sw = lv_switch_create(lv_obj_0);
        lv_obj_set_name(main_man_sw, "main_man_sw");
        lv_obj_set_x(main_man_sw, 96);
        lv_obj_set_y(main_man_sw, 92);
        lv_obj_set_width(main_man_sw, 54);
        lv_obj_set_height(main_man_sw, 32);

        lv_obj_t * main_prog = lv_dropdown_create(lv_obj_0);
        lv_obj_set_name(main_prog, "main_prog");
        lv_obj_set_x(main_prog, 0);
        lv_obj_set_y(main_prog, 132);
        lv_obj_set_width(main_prog, 140);
        lv_obj_set_height(main_prog, 36);
        lv_dropdown_set_options(main_prog, "1 Example");
        lv_obj_set_style_pad_all(main_prog, 8, 0);
        lv_obj_add_style(main_prog, &style_panel, 0);

        lv_obj_t * main_start = lv_button_create(lv_obj_0);
        lv_obj_set_name(main_start, "main_start");
        lv_obj_set_x(main_start, 144);
        lv_obj_set_y(main_start, 132);
        lv_obj_set_width(main_start, 46);
        lv_obj_set_height(main_start, 36);
        lv_obj_t * lv_label_2 = lv_label_create(main_start);
        lv_obj_set_align(lv_label_2, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_2, "");
        lv_obj_set_style_text_font(lv_label_2, font_icons_14, 0);

        lv_obj_t * main_pause = lv_button_create(lv_obj_0);
        lv_obj_set_name(main_pause, "main_pause");
        lv_obj_set_x(main_pause, 194);
        lv_obj_set_y(main_pause, 132);
        lv_obj_set_width(main_pause, 38);
        lv_obj_set_height(main_pause, 36);
        lv_obj_t * lv_label_3 = lv_label_create(main_pause);
        lv_obj_set_align(lv_label_3, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_3, "");
        lv_obj_set_style_text_font(lv_label_3, font_icons_14, 0);

        lv_obj_t * main_stop = lv_button_create(lv_obj_0);
        lv_obj_set_name(main_stop, "main_stop");
        lv_obj_set_x(main_stop, 236);
        lv_obj_set_y(main_stop, 132);
        lv_obj_set_width(main_stop, 38);
        lv_obj_set_height(main_stop, 36);
        lv_obj_add_style(main_stop, &style_danger, 0);
        lv_obj_t * lv_label_4 = lv_label_create(main_stop);
        lv_obj_set_align(lv_label_4, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_4, "");
        lv_obj_set_style_text_font(lv_label_4, font_icons_14, 0);

        lv_obj_t * main_skip = lv_button_create(lv_obj_0);
        lv_obj_set_name(main_skip, "main_skip");
        lv_obj_set_x(main_skip, 278);
        lv_obj_set_y(main_skip, 132);
        lv_obj_set_width(main_skip, 38);
        lv_obj_set_height(main_skip, 36);
        lv_obj_t * lv_label_5 = lv_label_create(main_skip);
        lv_obj_set_align(lv_label_5, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_5, "");
        lv_obj_set_style_text_font(lv_label_5, font_icons_14, 0);

        lv_obj_t * main_progress = lv_bar_create(lv_obj_0);
        lv_obj_set_name(main_progress, "main_progress");
        lv_obj_set_x(main_progress, 2);
        lv_obj_set_y(main_progress, 174);
        lv_obj_set_width(main_progress, 312);
        lv_obj_set_height(main_progress, 6);
        lv_bar_set_min_value(main_progress, 0);
        lv_bar_set_max_value(main_progress, 1000);
        lv_bar_set_value(main_progress, 380, false);

        lv_obj_t * main_msg = lv_label_create(lv_obj_0);
        lv_obj_set_name(main_msg, "main_msg");
        lv_obj_set_x(main_msg, 2);
        lv_obj_set_y(main_msg, 184);
        lv_obj_set_width(main_msg, 312);
        lv_label_set_long_mode(main_msg, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(main_msg, "Program started");
        lv_obj_add_style(main_msg, &style_small, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

