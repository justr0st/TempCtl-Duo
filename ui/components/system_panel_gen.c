/**
 * @file system_panel_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "system_panel_gen.h"
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

lv_obj_t * system_panel_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "system_panel_#");
        lv_obj_set_width(lv_obj_0, lv_pct(100));
        lv_obj_set_height(lv_obj_0, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(lv_obj_0, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_pad_row(lv_obj_0, 2, 0);

        lv_obj_add_style(lv_obj_0, &style_plain, 0);
        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_label_set_translation_tag(lv_label_0, "sys_display");
        lv_obj_set_style_pad_top(lv_label_0, 6, 0);
        lv_obj_add_style(lv_label_0, &style_caption, 0);

        lv_obj_t * setting_row_0 = setting_row_create(lv_obj_0, "sys_rotate");
        lv_obj_t * sys_rotate = lv_switch_create(setting_row_0);
        lv_obj_set_name(sys_rotate, "sys_rotate");

        lv_obj_t * setting_row_1 = setting_row_create(lv_obj_0, "sys_brightness");
        lv_obj_set_style_pad_right(setting_row_1, 14, 0);
        lv_obj_t * sys_bright = lv_slider_create(setting_row_1);
        lv_obj_set_name(sys_bright, "sys_bright");
        lv_obj_set_width(sys_bright, 140);
        lv_slider_set_min_value(sys_bright, 10);
        lv_slider_set_max_value(sys_bright, 255);
        lv_slider_set_value(sys_bright, 200, false);

        lv_obj_t * setting_row_2 = setting_row_create(lv_obj_0, "sys_theme");
        lv_obj_t * sys_theme = lv_dropdown_create(setting_row_2);
        lv_obj_set_name(sys_theme, "sys_theme");
        lv_obj_set_width(sys_theme, 140);
        lv_dropdown_set_options(sys_theme, "Default\nDracula\nSolarized\nGruvbox\nCatppuccin\nOne");
        lv_obj_add_style(sys_theme, &style_panel, 0);

        lv_obj_t * setting_row_3 = setting_row_create(lv_obj_0, "sys_dark");
        lv_obj_t * sys_dark = lv_switch_create(setting_row_3);
        lv_obj_set_name(sys_dark, "sys_dark");

        lv_obj_t * lv_label_1 = lv_label_create(lv_obj_0);
        lv_label_set_translation_tag(lv_label_1, "sys_wifi");
        lv_obj_set_style_pad_top(lv_label_1, 6, 0);
        lv_obj_add_style(lv_label_1, &style_caption, 0);

        lv_obj_t * setting_row_4 = setting_row_create(lv_obj_0, "sys_network");
        lv_obj_t * lv_obj_1 = lv_obj_create(setting_row_4);
        lv_obj_set_width(lv_obj_1, 196);
        lv_obj_set_height(lv_obj_1, LV_SIZE_CONTENT);
        lv_obj_set_flag(lv_obj_1, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_add_style(lv_obj_1, &style_plain, 0);
        lv_obj_t * sys_ssid = lv_button_create(lv_obj_1);
        lv_obj_set_name(sys_ssid, "sys_ssid");
        lv_obj_set_x(sys_ssid, 0);
        lv_obj_set_y(sys_ssid, 0);
        lv_obj_set_width(sys_ssid, 140);
        lv_obj_set_height(sys_ssid, ROW_HEIGHT);
        lv_obj_t * sys_ssid_text = lv_label_create(sys_ssid);
        lv_obj_set_name(sys_ssid_text, "sys_ssid_text");
        lv_obj_set_align(sys_ssid_text, LV_ALIGN_CENTER);
        lv_obj_set_width(sys_ssid_text, 132);
        lv_label_set_long_mode(sys_ssid_text, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(sys_ssid_text, "(none: AP only)");

        lv_obj_t * sys_scan = lv_button_create(lv_obj_1);
        lv_obj_set_name(sys_scan, "sys_scan");
        lv_obj_set_x(sys_scan, 144);
        lv_obj_set_y(sys_scan, 0);
        lv_obj_set_width(sys_scan, 50);
        lv_obj_set_height(sys_scan, ROW_HEIGHT);
        lv_obj_t * lv_label_2 = lv_label_create(sys_scan);
        lv_obj_set_align(lv_label_2, LV_ALIGN_CENTER);
        lv_label_set_text(lv_label_2, "");
        lv_obj_set_style_text_font(lv_label_2, font_icons_14, 0);

        lv_obj_t * setting_row_5 = setting_row_create(lv_obj_0, "sys_password");
        lv_obj_t * sys_pass = lv_button_create(setting_row_5);
        lv_obj_set_name(sys_pass, "sys_pass");
        lv_obj_set_width(sys_pass, 140);
        lv_obj_set_height(sys_pass, ROW_HEIGHT);
        lv_obj_t * sys_pass_text = lv_label_create(sys_pass);
        lv_obj_set_name(sys_pass_text, "sys_pass_text");
        lv_obj_set_align(sys_pass_text, LV_ALIGN_CENTER);
        lv_label_set_text(sys_pass_text, "(none)");

        lv_obj_t * setting_row_6 = setting_row_create(lv_obj_0, "sys_ap_password");
        lv_obj_t * sys_ap_pass = lv_button_create(setting_row_6);
        lv_obj_set_name(sys_ap_pass, "sys_ap_pass");
        lv_obj_set_width(sys_ap_pass, 140);
        lv_obj_set_height(sys_ap_pass, ROW_HEIGHT);
        lv_obj_t * sys_ap_pass_text = lv_label_create(sys_ap_pass);
        lv_obj_set_name(sys_ap_pass_text, "sys_ap_pass_text");
        lv_obj_set_align(sys_ap_pass_text, LV_ALIGN_CENTER);
        lv_label_set_text(sys_ap_pass_text, "********");

        lv_obj_t * setting_row_7 = setting_row_create(lv_obj_0, "blank");
        lv_obj_t * sys_connect = lv_button_create(setting_row_7);
        lv_obj_set_name(sys_connect, "sys_connect");
        lv_obj_set_width(sys_connect, 200);
        lv_obj_set_height(sys_connect, 36);
        lv_obj_t * lv_label_3 = lv_label_create(sys_connect);
        lv_obj_set_align(lv_label_3, LV_ALIGN_CENTER);
        lv_label_set_translation_tag(lv_label_3, "sys_connect");

        lv_obj_t * sys_info = lv_label_create(lv_obj_0);
        lv_obj_set_name(sys_info, "sys_info");
        lv_obj_set_width(sys_info, lv_pct(100));
        lv_label_set_text(sys_info, "Wi-Fi Workshop  192.168.1.57\nhttp://tempctl-duo.local   firmware 1.0.0");
        lv_obj_add_style(sys_info, &style_small, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

