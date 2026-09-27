/**
 * @file controller_panel_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "controller_panel_gen.h"
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

lv_obj_t * controller_panel_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "controller_panel_#");
        lv_obj_set_width(lv_obj_0, lv_pct(100));
        lv_obj_set_height(lv_obj_0, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(lv_obj_0, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_pad_row(lv_obj_0, 2, 0);

        lv_obj_add_style(lv_obj_0, &style_plain, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_label_set_translation_tag(lv_label_0, "ctl_serial");
        lv_obj_set_style_pad_top(lv_label_0, 6, 0);
        lv_obj_add_style(lv_label_0, &style_caption, 0);

        lv_obj_t * setting_row_0 = setting_row_create(lv_obj_0, "ctl_type");
        lv_obj_t * ctl_profile = lv_dropdown_create(setting_row_0);
        lv_obj_set_name(ctl_profile, "ctl_profile");
        lv_obj_set_width(ctl_profile, 170);
        lv_dropdown_set_options(ctl_profile, "PMA KS 4x-1\nGeneric / LCTC");
        lv_obj_add_style(ctl_profile, &style_panel, 0);

        lv_obj_t * setting_row_1 = setting_row_create(lv_obj_0, "ctl_slave");
        lv_obj_t * ctl_slave = lv_button_create(setting_row_1);
        lv_obj_set_name(ctl_slave, "ctl_slave");
        lv_obj_set_width(ctl_slave, 80);
        lv_obj_set_height(ctl_slave, 32);
        lv_obj_t * ctl_slave_text = lv_label_create(ctl_slave);
        lv_obj_set_name(ctl_slave_text, "ctl_slave_text");
        lv_obj_set_align(ctl_slave_text, LV_ALIGN_CENTER);
        lv_label_set_text(ctl_slave_text, "1");

        lv_obj_t * setting_row_2 = setting_row_create(lv_obj_0, "ctl_baud");
        lv_obj_t * ctl_baud = lv_dropdown_create(setting_row_2);
        lv_obj_set_name(ctl_baud, "ctl_baud");
        lv_obj_set_width(ctl_baud, 110);
        lv_dropdown_set_options(ctl_baud, "2400\n4800\n9600\n19200\n38400");
        lv_dropdown_set_selected(ctl_baud, 2);
        lv_obj_add_style(ctl_baud, &style_panel, 0);

        lv_obj_t * setting_row_3 = setting_row_create(lv_obj_0, "ctl_format");
        lv_obj_t * ctl_format = lv_dropdown_create(setting_row_3);
        lv_obj_set_name(ctl_format, "ctl_format");
        lv_obj_set_width(ctl_format, 110);
        lv_dropdown_set_options(ctl_format, "8N1\n8E1\n8O1\n8N2");
        lv_dropdown_set_selected(ctl_format, 1);
        lv_obj_add_style(ctl_format, &style_panel, 0);

        lv_obj_t * lv_label_1 = lv_label_create(lv_obj_0);
        lv_label_set_translation_tag(lv_label_1, "ctl_behaviour");
        lv_obj_set_style_pad_top(lv_label_1, 6, 0);
        lv_obj_add_style(lv_label_1, &style_caption, 0);

        lv_obj_t * setting_row_4 = setting_row_create(lv_obj_0, "ctl_ramp");
        lv_obj_t * ctl_ramp = lv_dropdown_create(setting_row_4);
        lv_obj_set_name(ctl_ramp, "ctl_ramp");
        lv_obj_set_width(ctl_ramp, 170);
        lv_dropdown_set_options(ctl_ramp, "ESP32 steps\nController r.SP");
        lv_dropdown_set_selected(ctl_ramp, 1);
        lv_obj_add_style(ctl_ramp, &style_panel, 0);

        lv_obj_t * setting_row_5 = setting_row_create(lv_obj_0, "ctl_soak");
        lv_obj_t * ctl_soak = lv_switch_create(setting_row_5);
        lv_obj_set_name(ctl_soak, "ctl_soak");

        lv_obj_t * setting_row_6 = setting_row_create(lv_obj_0, "ctl_band");
        lv_obj_t * ctl_band = lv_button_create(setting_row_6);
        lv_obj_set_name(ctl_band, "ctl_band");
        lv_obj_set_width(ctl_band, 80);
        lv_obj_set_height(ctl_band, 32);
        lv_obj_t * ctl_band_text = lv_label_create(ctl_band);
        lv_obj_set_name(ctl_band_text, "ctl_band_text");
        lv_obj_set_align(ctl_band_text, LV_ALIGN_CENTER);
        lv_label_set_text(ctl_band_text, "5.0");

        lv_obj_t * setting_row_7 = setting_row_create(lv_obj_0, "ctl_lock");
        lv_obj_t * ctl_lock = lv_switch_create(setting_row_7);
        lv_obj_set_name(ctl_lock, "ctl_lock");

        lv_obj_t * setting_row_8 = setting_row_create(lv_obj_0, "ctl_end");
        lv_obj_t * ctl_end = lv_dropdown_create(setting_row_8);
        lv_obj_set_name(ctl_end, "ctl_end");
        lv_obj_set_width(ctl_end, 170);
        lv_dropdown_set_options(ctl_end, "Keep setpoint\nSet end SP\nOutputs off");
        lv_obj_add_style(ctl_end, &style_panel, 0);

        lv_obj_t * ctl_info = lv_label_create(lv_obj_0);
        lv_obj_set_name(ctl_info, "ctl_info");
        lv_obj_set_width(ctl_info, lv_pct(100));
        lv_label_set_text(ctl_info, "Registers (edit on the web page): SP 11372,\nramp r.SP 11295, outputs off 1086");
        lv_obj_add_style(ctl_info, &style_small, 0);

        lv_obj_t * setting_row_9 = setting_row_create(lv_obj_0, "blank");
        lv_obj_t * ctl_test = lv_button_create(setting_row_9);
        lv_obj_set_name(ctl_test, "ctl_test");
        lv_obj_set_width(ctl_test, 130);
        lv_obj_set_height(ctl_test, 36);
        lv_obj_t * lv_label_2 = lv_label_create(ctl_test);
        lv_obj_set_align(lv_label_2, LV_ALIGN_CENTER);
        lv_label_set_translation_tag(lv_label_2, "ctl_test");

        lv_obj_t * ctl_save = lv_button_create(setting_row_9);
        lv_obj_set_name(ctl_save, "ctl_save");
        lv_obj_set_width(ctl_save, 110);
        lv_obj_set_height(ctl_save, 36);
        lv_obj_t * lv_label_3 = lv_label_create(ctl_save);
        lv_obj_set_align(lv_label_3, LV_ALIGN_CENTER);
        lv_label_set_translation_tag(lv_label_3, "btn_save");

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

