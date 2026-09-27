/**
 * @file setup_page_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "setup_page_gen.h"
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

lv_obj_t * setup_page_create(lv_obj_t * parent)
{
    LV_TRACE_OBJ_CREATE("begin");


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(parent);
        lv_obj_set_name_static(lv_obj_0, "setup_page_#");
        lv_obj_set_width(lv_obj_0, lv_pct(100));
        lv_obj_set_height(lv_obj_0, lv_pct(100));
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);

        lv_obj_add_style(lv_obj_0, &style_plain, 0);
        lv_obj_t * setup_tabs = lv_tabview_create(lv_obj_0);
        lv_obj_set_name(setup_tabs, "setup_tabs");
        lv_obj_set_width(setup_tabs, lv_pct(100));
        lv_obj_set_height(setup_tabs, lv_pct(100));
        lv_tabview_set_tab_bar_position(setup_tabs, LV_DIR_TOP);
        lv_obj_t * lv_tabview_tab_bar_0 = lv_tabview_get_tab_bar(setup_tabs);
        lv_obj_set_height(lv_tabview_tab_bar_0, 28);
        lv_obj_add_style(lv_tabview_tab_bar_0, &style_tab_bar, 0);
        lv_obj_t * lv_tabview_tab_0 = lv_tabview_add_tab(setup_tabs, "System");
        lv_obj_add_style(lv_tabview_tab_0, &style_page, 0);
        system_panel_create(lv_tabview_tab_0);

        lv_obj_t * lv_tabview_tab_1 = lv_tabview_add_tab(setup_tabs, "Controller");
        lv_obj_add_style(lv_tabview_tab_1, &style_page, 0);
        controller_panel_create(lv_tabview_tab_1);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

