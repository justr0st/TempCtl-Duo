/**
 * @file chart_page_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "chart_page_gen.h"
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

lv_obj_t * chart_page_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "chart_page_#");
        lv_obj_set_width(lv_obj_0, lv_pct(100));
        lv_obj_set_height(lv_obj_0, lv_pct(100));
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_plain, 0);
        lv_obj_t * chart_yscale = lv_scale_create(lv_obj_0);
        lv_obj_set_name(chart_yscale, "chart_yscale");
        lv_obj_set_x(chart_yscale, 0);
        lv_obj_set_y(chart_yscale, 4);
        lv_obj_set_width(chart_yscale, 34);
        lv_obj_set_height(chart_yscale, 138);
        lv_scale_set_mode(chart_yscale, LV_SCALE_MODE_VERTICAL_LEFT);
        lv_scale_set_min_value(chart_yscale, 0);
        lv_scale_set_max_value(chart_yscale, 600);
        lv_scale_set_total_tick_count(chart_yscale, 6);
        lv_scale_set_major_tick_every(chart_yscale, 1);
        lv_scale_set_label_show(chart_yscale, false);
        lv_obj_set_style_text_font(chart_yscale, font_12, 0);
        lv_obj_add_style(chart_yscale, &style_scale_ticks, LV_PART_INDICATOR);
        lv_obj_add_style(chart_yscale, &style_scale_line, 0);

        lv_obj_t * chart = lv_chart_create(lv_obj_0);
        lv_obj_set_name(chart, "chart");
        lv_obj_set_x(chart, 36);
        lv_obj_set_y(chart, 0);
        lv_obj_set_width(chart, 262);
        lv_obj_set_height(chart, 146);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart, 16);
        lv_chart_set_hor_div_line_count(chart, 5);
        lv_chart_set_ver_div_line_count(chart, 6);
        lv_obj_set_style_pad_all(chart, 4, 0);
        lv_obj_add_style(chart, &style_panel, 0);
        lv_obj_add_style(chart, &style_chart_line, LV_PART_ITEMS);
        lv_obj_add_style(chart, &style_no_points, LV_PART_INDICATOR);
        lv_chart_set_axis_min_value(chart, LV_CHART_AXIS_PRIMARY_Y, 0);
        lv_chart_set_axis_max_value(chart, LV_CHART_AXIS_PRIMARY_Y, 6000);
        lv_chart_series_t * lv_chart_series_0 = lv_chart_add_series(chart, lv_color_hex(0xFF5722), LV_CHART_AXIS_PRIMARY_Y);
        static const int32_t chart_values_0[] = {220, 600, 1000, 1450, 1900, 2400, 2480, 2500, 2510, 2490, 3000, 3500, 3950, 3990, 4010, 4000};
        lv_chart_set_series_values(chart, lv_chart_series_0, chart_values_0, 16);
        lv_chart_series_t * lv_chart_series_1 = lv_chart_add_series(chart, lv_color_hex(0x03A9F4), LV_CHART_AXIS_PRIMARY_Y);
        static const int32_t chart_values_1[] = {200, 650, 1050, 1500, 1950, 2500, 2500, 2500, 2500, 2500, 3000, 3500, 4000, 4000, 4000, 4000};
        lv_chart_set_series_values(chart, lv_chart_series_1, chart_values_1, 16);
        lv_chart_series_t * lv_chart_series_2 = lv_chart_add_series(chart, lv_color_hex(0x90A4AE), LV_CHART_AXIS_PRIMARY_Y);
        static const int32_t chart_values_2[] = {1000, 1500, 2000, 2500, 2500, 2500, 4000, 4000, 4000, 4000, 5000, 5500, 6000, 5500, 5000, 4500};
        lv_chart_set_series_values(chart, lv_chart_series_2, chart_values_2, 16);

        lv_obj_t * chart_xscale = lv_scale_create(lv_obj_0);
        lv_obj_set_name(chart_xscale, "chart_xscale");
        lv_obj_set_x(chart_xscale, 40);
        lv_obj_set_y(chart_xscale, 146);
        lv_obj_set_width(chart_xscale, 254);
        lv_obj_set_height(chart_xscale, 20);
        lv_scale_set_mode(chart_xscale, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
        lv_scale_set_min_value(chart_xscale, 0);
        lv_scale_set_max_value(chart_xscale, 5);
        lv_scale_set_total_tick_count(chart_xscale, 6);
        lv_scale_set_major_tick_every(chart_xscale, 1);
        lv_scale_set_label_show(chart_xscale, false);
        lv_obj_set_style_text_font(chart_xscale, font_12, 0);
        lv_obj_add_style(chart_xscale, &style_scale_ticks, LV_PART_INDICATOR);
        lv_obj_add_style(chart_xscale, &style_scale_line, 0);

        lv_obj_t * lv_label_0 = lv_label_create(lv_obj_0);
        lv_obj_set_x(lv_label_0, 2);
        lv_obj_set_y(lv_label_0, 170);
        lv_label_set_translation_tag(lv_label_0, "chart_pv");
        lv_obj_set_style_text_color(lv_label_0, COLOR_PV, 0);
        lv_obj_add_style(lv_label_0, &style_small, 0);

        lv_obj_t * lv_label_1 = lv_label_create(lv_obj_0);
        lv_obj_set_x(lv_label_1, 30);
        lv_obj_set_y(lv_label_1, 170);
        lv_label_set_translation_tag(lv_label_1, "chart_sp");
        lv_obj_set_style_text_color(lv_label_1, COLOR_SP, 0);
        lv_obj_add_style(lv_label_1, &style_small, 0);

        lv_obj_t * lv_label_2 = lv_label_create(lv_obj_0);
        lv_obj_set_x(lv_label_2, 58);
        lv_obj_set_y(lv_label_2, 170);
        lv_label_set_translation_tag(lv_label_2, "chart_program");
        lv_obj_set_style_text_color(lv_label_2, COLOR_PLAN, 0);
        lv_obj_add_style(lv_label_2, &style_small, 0);

        lv_obj_t * lv_label_3 = lv_label_create(lv_obj_0);
        lv_obj_set_x(lv_label_3, 120);
        lv_obj_set_y(lv_label_3, 170);
        lv_label_set_translation_tag(lv_label_3, "chart_dashed");
        lv_obj_add_style(lv_label_3, &style_caption, 0);

        lv_obj_t * chart_info = lv_label_create(lv_obj_0);
        lv_obj_set_name(chart_info, "chart_info");
        lv_obj_set_x(chart_info, 2);
        lv_obj_set_y(chart_info, 186);
        lv_obj_set_width(chart_info, 312);
        lv_label_set_long_mode(chart_info, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(chart_info, "Last 5 min. Preview: Example (10:18:05)");
        lv_obj_add_style(chart_info, &style_small, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

