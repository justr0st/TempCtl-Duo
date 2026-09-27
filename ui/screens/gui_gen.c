/**
 * @file gui_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "gui_gen.h"
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

lv_obj_t * gui_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");

    static bool screen_created = false;
    if (screen_created) {
        LV_LOG_WARN("`gui` is already initialized as a permanent screen. Returning with no change.");
        return gui;
    }


    lv_obj_t * the_root = NULL;

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        gui = lv_obj_create(NULL);
        lv_obj_t * lv_obj_0 = gui;
        lv_obj_set_name_static(lv_obj_0, "gui_#");

        lv_obj_add_style(lv_obj_0, &style_screen, 0);
        lv_obj_t * tabs = lv_tabview_create(lv_obj_0);
        lv_obj_set_name(tabs, "tabs");
        lv_obj_set_width(tabs, lv_pct(100));
        lv_obj_set_height(tabs, lv_pct(100));
        lv_tabview_set_tab_bar_position(tabs, LV_DIR_TOP);
        lv_obj_t * lv_tabview_tab_bar_0 = lv_tabview_get_tab_bar(tabs);
        lv_obj_set_height(lv_tabview_tab_bar_0, TAB_BAR_HEIGHT);
        lv_obj_add_style(lv_tabview_tab_bar_0, &style_tab_bar, 0);
        lv_obj_t * lv_tabview_tab_0 = lv_tabview_add_tab(tabs, "Main");
        lv_obj_add_style(lv_tabview_tab_0, &style_page, 0);
        main_page_create(lv_tabview_tab_0);

        lv_obj_t * lv_tabview_tab_1 = lv_tabview_add_tab(tabs, "Program");
        lv_obj_add_style(lv_tabview_tab_1, &style_page, 0);
        program_page_create(lv_tabview_tab_1);

        lv_obj_t * lv_tabview_tab_2 = lv_tabview_add_tab(tabs, "Chart");
        lv_obj_add_style(lv_tabview_tab_2, &style_page, 0);
        chart_page_create(lv_tabview_tab_2);

        lv_obj_t * lv_tabview_tab_3 = lv_tabview_add_tab(tabs, "Setup");
        lv_obj_add_style(lv_tabview_tab_3, &style_plain, 0);
        setup_page_create(lv_tabview_tab_3);

        the_root = lv_obj_0;
    }
    #endif

    if (the_root) screen_created = true;

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

