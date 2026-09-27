/**
 * @file tempctl_ui_gen.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "tempctl_ui_gen.h"

#if defined(LV_USE_XML) && LV_USE_XML
#endif /* LV_USE_XML */

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void check_font(lv_font_t ** font, const char * name);

/**********************
 *  STATIC VARIABLES
 **********************/

static uint32_t tempctl_ui_target = TEMPCTL_UI_TARGET_ALL;

/*----------------
 * Translations
 *----------------*/

#ifndef LV_EDITOR_PREVIEW
    static const char * translation_languages[] = {"en", NULL};
    static const char * translation_tags[] = {"blank", "btn_save", "btn_cancel", "btn_stop", "btn_discard", "btn_revert", "btn_name", "time_min", "time_s", "main_pv_caption", "main_manual_sp", "main_on_off", "st_idle", "st_manual", "st_finished", "st_stopped", "st_step", "st_paused", "ph_ramp", "ph_hold", "ph_soak_wait", "man_written", "man_writing", "run_left", "prog_total", "sp_value", "step_target", "sp_reg_unset", "out_value", "pv_sensor", "stop_title", "stop_text", "ed_manual_sp", "prog_col_no", "prog_col_target", "prog_col_ramp", "prog_col_hold", "prog_empty", "tbl_no_steps", "tbl_tap_plus", "prog_info", "prog_unsaved", "ed_step_target", "ed_step_ramp", "ed_step_hold", "ed_prog_name", "not_saved_title", "not_saved_text", "saved_title", "saved_next_run", "unsaved_title", "unsaved_text", "chart_pv", "chart_sp", "chart_program", "chart_dashed", "chart_run_info", "chart_idle_info", "sys_display", "sys_rotate", "sys_brightness", "sys_wifi", "sys_network", "sys_password", "sys_ap_password", "sys_connect", "sys_info", "wifi_none", "pass_none", "ed_ssid", "ed_pass", "ed_ap_pass", "too_short_title", "too_short_text", "scan_running", "scan_pick", "scan_none", "scan_failed", "wifi_title", "wifi_connecting", "wifi_ap_only", "wifi_connected", "wifi_failed", "ctl_serial", "ctl_type", "ctl_slave", "ctl_baud", "ctl_format", "ctl_behaviour", "ctl_ramp", "ctl_soak", "ctl_band", "ctl_lock", "ctl_end", "ctl_test", "ctl_regs", "reg_not_set", "ed_slave", "ed_band", "ctl_saved_text", "link_title", "link_no_pv", "link_ok", "link_fail", NULL};
    static const char * translation_texts[] = {
        " ", /* blank */
        "Save", /* btn_save */
        "Cancel", /* btn_cancel */
        "Stop", /* btn_stop */
        "Discard", /* btn_discard */
        "Revert", /* btn_revert */
        "Name", /* btn_name */
        "%1 min", /* time_min */
        "%1 s", /* time_s */
        "Process value (C)", /* main_pv_caption */
        "Manual SP on/off", /* main_manual_sp */
        "on / off", /* main_on_off */
        "Idle", /* st_idle */
        "Manual", /* st_manual */
        "Finished", /* st_finished */
        "Stopped", /* st_stopped */
        "Step %1/%2", /* st_step */
        "Paused %1/%2", /* st_paused */
        "Ramp  %1 left", /* ph_ramp */
        "Hold  %1 left", /* ph_hold */
        "Hold: waiting for PV", /* ph_soak_wait */
        "setpoint written", /* man_written */
        "writing...", /* man_writing */
        "%1: %2 left", /* run_left */
        "%1, %2", /* prog_total */
        "SP %1", /* sp_value */
        "Step target %1", /* step_target */
        "SP register not set!", /* sp_reg_unset */
        "Out %1%", /* out_value */
        "Sensor", /* pv_sensor */
        "Stop program?", /* stop_title */
        "The end action from Setup > Controller is applied.", /* stop_text */
        "Manual setpoint (C), %1..%2", /* ed_manual_sp */
        "No", /* prog_col_no */
        "Target C", /* prog_col_target */
        "Ramp", /* prog_col_ramp */
        "Hold", /* prog_col_hold */
        "(empty)", /* prog_empty */
        "no steps", /* tbl_no_steps */
        "tap +", /* tbl_tap_plus */
        "%1/%2 steps  total %3", /* prog_info */
        "unsaved", /* prog_unsaved */
        "Step %1 target temperature (C)", /* ed_step_target */
        "Step %1 ramp time (0 = jump)", /* ed_step_ramp */
        "Step %1 hold time", /* ed_step_hold */
        "Name of program %1", /* ed_prog_name */
        "Not saved", /* not_saved_title */
        "Writing the program to flash failed.", /* not_saved_text */
        "Saved", /* saved_title */
        "Changes apply to the next run.", /* saved_next_run */
        "Unsaved changes", /* unsaved_title */
        "\"%1\" has unsaved changes.", /* unsaved_text */
        "PV", /* chart_pv */
        "SP", /* chart_sp */
        "Program", /* chart_program */
        "(dashed = not active)", /* chart_dashed */
        "%1  %2 / %3", /* chart_run_info */
        "Last 5 min. Preview: %1 (%2 over full width)", /* chart_idle_info */
        "Display", /* sys_display */
        "Rotate 180 deg", /* sys_rotate */
        "Brightness", /* sys_brightness */
        "Wi-Fi", /* sys_wifi */
        "Network", /* sys_network */
        "Password", /* sys_password */
        "AP password", /* sys_ap_password */
        "Save + connect", /* sys_connect */
        "%1\nhttp://%2.local   firmware %3", /* sys_info */
        "(none: AP only)", /* wifi_none */
        "(none)", /* pass_none */
        "Wi-Fi network name (SSID)", /* ed_ssid */
        "Wi-Fi password", /* ed_pass */
        "Access point password (8+ characters)", /* ed_ap_pass */
        "Too short", /* too_short_title */
        "The access point password needs at least 8 characters.", /* too_short_text */
        "Scanning for networks...", /* scan_running */
        "Pick a network", /* scan_pick */
        "No networks found", /* scan_none */
        "Scan failed - Wi-Fi busy, try again", /* scan_failed */
        "Wi-Fi", /* wifi_title */
        "Connecting to \"%1\"...", /* wifi_connecting */
        "Saved. Running as access point only.", /* wifi_ap_only */
        "Connected to %1\nIP %2", /* wifi_connected */
        "Could not join \"%1\". Check the name and password. The access point stays available.", /* wifi_failed */
        "Serial link", /* ctl_serial */
        "Type", /* ctl_type */
        "Slave address", /* ctl_slave */
        "Baud rate", /* ctl_baud */
        "Format", /* ctl_format */
        "Program behaviour", /* ctl_behaviour */
        "Setpoint ramp", /* ctl_ramp */
        "Guaranteed soak", /* ctl_soak */
        "Soak band +/-", /* ctl_band */
        "Lock front panel in run", /* ctl_lock */
        "At end", /* ctl_end */
        "Test link", /* ctl_test */
        "Registers (edit on the web page): SP %1,\nramp r.SP %2, outputs off %3", /* ctl_regs */
        "not set", /* reg_not_set */
        "Modbus slave address (1-247)", /* ed_slave */
        "Soak band +/- (C)", /* ed_band */
        "Controller settings stored. The serial link was reopened.", /* ctl_saved_text */
        "Link test", /* link_title */
        "The PV register address is not set.", /* link_no_pv */
        "Controller answered.\nPV register %1 = %2 (raw)", /* link_ok */
        "No valid answer.\n%1\nSave settings first if you changed them.", /* link_fail */
    };
#endif

/**********************
 *  GLOBAL VARIABLES
 **********************/

/*--------------------
 *  Permanent screens
 *-------------------*/

lv_obj_t * gui = NULL;

/*----------------
 * Fonts
 *----------------*/

lv_font_t * font_12;
extern lv_font_t font_12_data;
lv_font_t * font_14;
extern lv_font_t font_14_data;
lv_font_t * font_20;
extern lv_font_t font_20_data;
lv_font_t * font_24;
extern lv_font_t font_24_data;
lv_font_t * font_32;
extern lv_font_t font_32_data;
lv_font_t * font_48;
extern lv_font_t font_48_data;
lv_font_t * font_icons_14;
extern lv_font_t font_icons_14_data;

/*----------------
 * Images
 *----------------*/



/*----------------
 * Global styles
 *----------------*/

lv_style_t style_screen;
lv_style_t style_plain;
lv_style_t style_page;
lv_style_t style_caption;
lv_style_t style_small;
lv_style_t style_panel;
lv_style_t style_tab_bar;
lv_style_t style_row;
lv_style_t style_danger;
lv_style_t style_table_cell;
lv_style_t style_chart_line;
lv_style_t style_no_points;
lv_style_t style_scale_ticks;
lv_style_t style_scale_line;
lv_style_t style_link_error;
lv_style_t style_popup;
lv_style_t style_backdrop;
lv_style_t style_dialog;
lv_style_t style_list;
lv_style_t style_list_item;

/*----------------
 * Subjects
 *----------------*/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void tempctl_ui_init_gen(const char * asset_path)
{

    /*----------------
     * Fonts
     *----------------*/

    /* Targets: any */

    #if TEMPCTL_UI_CHECK_COMPILE_TARGET(TEMPCTL_UI_TARGET_ALL)
    if (tempctl_ui_check_target(TEMPCTL_UI_TARGET_ALL)) {
        if (!font_12) {
            /* font_12 */
            /* get font 'font_12' from a C array */
            font_12 = &font_12_data;

        }
        if (!font_14) {
            /* font_14 */
            /* get font 'font_14' from a C array */
            font_14 = &font_14_data;

        }
        if (!font_20) {
            /* font_20 */
            /* get font 'font_20' from a C array */
            font_20 = &font_20_data;

        }
        if (!font_24) {
            /* font_24 */
            /* get font 'font_24' from a C array */
            font_24 = &font_24_data;

        }
        if (!font_32) {
            /* font_32 */
            /* get font 'font_32' from a C array */
            font_32 = &font_32_data;

        }
        if (!font_48) {
            /* font_48 */
            /* get font 'font_48' from a C array */
            font_48 = &font_48_data;

        }
        if (!font_icons_14) {
            /* font_icons_14 */
            /* get font 'font_icons_14' from a C array */
            font_icons_14 = &font_icons_14_data;

        }
    }
    #endif

    /*----------------
     * Images
     *----------------*/



    /*----------------
     * Global styles
     *----------------*/

    static bool style_inited = false;

    if (!style_inited) {
        /*Init all styles*/
        lv_style_init(&style_screen);
        lv_style_init(&style_plain);
        lv_style_init(&style_page);
        lv_style_init(&style_caption);
        lv_style_init(&style_small);
        lv_style_init(&style_panel);
        lv_style_init(&style_tab_bar);
        lv_style_init(&style_row);
        lv_style_init(&style_danger);
        lv_style_init(&style_table_cell);
        lv_style_init(&style_chart_line);
        lv_style_init(&style_no_points);
        lv_style_init(&style_scale_ticks);
        lv_style_init(&style_scale_line);
        lv_style_init(&style_link_error);
        lv_style_init(&style_popup);
        lv_style_init(&style_backdrop);
        lv_style_init(&style_dialog);
        lv_style_init(&style_list);
        lv_style_init(&style_list_item);

        lv_style_set_bg_color(&style_screen, COLOR_BG);
        lv_style_set_bg_opa(&style_screen, (255 * 100 / 100));
        lv_style_set_text_color(&style_screen, COLOR_TEXT);
        lv_style_set_text_font(&style_screen, font_14);
        lv_style_set_bg_opa(&style_plain, 0);
        lv_style_set_border_width(&style_plain, 0);
        lv_style_set_pad_all(&style_plain, 0);
        lv_style_set_radius(&style_plain, 0);
        lv_style_set_bg_color(&style_page, COLOR_BG);
        lv_style_set_bg_opa(&style_page, (255 * 100 / 100));
        lv_style_set_border_width(&style_page, 0);
        lv_style_set_radius(&style_page, 0);
        lv_style_set_pad_all(&style_page, SPACE_SM);
        lv_style_set_text_color(&style_caption, COLOR_MUTED);
        lv_style_set_text_font(&style_caption, font_12);
        lv_style_set_text_font(&style_small, font_12);
        lv_style_set_bg_color(&style_panel, COLOR_PANEL);
        lv_style_set_bg_opa(&style_panel, (255 * 100 / 100));
        lv_style_set_border_color(&style_panel, COLOR_BORDER);
        lv_style_set_border_width(&style_panel, 1);
        lv_style_set_text_color(&style_panel, COLOR_TEXT);
        lv_style_set_bg_color(&style_tab_bar, COLOR_PANEL);
        lv_style_set_bg_opa(&style_tab_bar, (255 * 100 / 100));
        lv_style_set_text_color(&style_tab_bar, COLOR_TEXT);
        lv_style_set_bg_opa(&style_row, 0);
        lv_style_set_border_width(&style_row, 0);
        lv_style_set_pad_all(&style_row, SPACE_XS);
        lv_style_set_radius(&style_row, 0);
        lv_style_set_bg_color(&style_danger, COLOR_DANGER);
        lv_style_set_pad_top(&style_table_cell, 3);
        lv_style_set_pad_bottom(&style_table_cell, 3);
        lv_style_set_pad_left(&style_table_cell, 4);
        lv_style_set_pad_right(&style_table_cell, 4);
        lv_style_set_line_width(&style_chart_line, 2);
        lv_style_set_width(&style_no_points, 0);
        lv_style_set_height(&style_no_points, 0);
        lv_style_set_length(&style_scale_ticks, 4);
        lv_style_set_line_opa(&style_scale_line, 0);
        lv_style_set_bg_color(&style_link_error, COLOR_DANGER);
        lv_style_set_bg_color(&style_popup, COLOR_PANEL);
        lv_style_set_bg_opa(&style_popup, (255 * 100 / 100));
        lv_style_set_border_width(&style_popup, 0);
        lv_style_set_radius(&style_popup, 0);
        lv_style_set_pad_all(&style_popup, SPACE_SM);
        lv_style_set_text_color(&style_popup, COLOR_TEXT);
        lv_style_set_bg_color(&style_backdrop, lv_color_hex(0x000000));
        lv_style_set_bg_opa(&style_backdrop, (255 * 50 / 100));
        lv_style_set_border_width(&style_backdrop, 0);
        lv_style_set_radius(&style_backdrop, 0);
        lv_style_set_pad_all(&style_backdrop, 0);
        lv_style_set_bg_color(&style_dialog, COLOR_PANEL);
        lv_style_set_bg_opa(&style_dialog, (255 * 100 / 100));
        lv_style_set_border_color(&style_dialog, COLOR_BORDER);
        lv_style_set_border_width(&style_dialog, 1);
        lv_style_set_radius(&style_dialog, 8);
        lv_style_set_pad_all(&style_dialog, 10);
        lv_style_set_text_color(&style_dialog, COLOR_TEXT);
        lv_style_set_pad_all(&style_list, 0);
        lv_style_set_radius(&style_list, 4);
        lv_style_set_bg_color(&style_list_item, COLOR_PANEL);
        lv_style_set_bg_opa(&style_list_item, (255 * 100 / 100));
        lv_style_set_radius(&style_list_item, 0);
        lv_style_set_shadow_width(&style_list_item, 0);
        lv_style_set_border_color(&style_list_item, COLOR_BORDER);
        lv_style_set_border_width(&style_list_item, 1);
        lv_style_set_border_side(&style_list_item, LV_BORDER_SIDE_BOTTOM);
        lv_style_set_text_color(&style_list_item, COLOR_TEXT);
        lv_style_set_pad_hor(&style_list_item, 10);

        style_inited = true;
    }

    /*----------------
     * Subjects
     *----------------*/
    /*----------------
     * Translations
     *----------------*/

    #ifndef LV_EDITOR_PREVIEW
        lv_translation_add_static(translation_languages, translation_tags, translation_texts);
        lv_translation_set_language(translation_languages[0]);
    #endif

#if defined(LV_USE_XML) && LV_USE_XML
    /* Register widgets */

    /* Check all fonts / default if needed. This prevents fonts that are used in one target but
       defined in another from causing assertion failures during rendering of the Preview. */
    check_font(&font_12, "font_12");
    check_font(&font_14, "font_14");
    check_font(&font_20, "font_20");
    check_font(&font_24, "font_24");
    check_font(&font_32, "font_32");
    check_font(&font_48, "font_48");
    check_font(&font_icons_14, "font_icons_14");

    /* Register fonts */
    lv_xml_register_font(NULL, "font_12", font_12);
    lv_xml_register_font(NULL, "font_14", font_14);
    lv_xml_register_font(NULL, "font_20", font_20);
    lv_xml_register_font(NULL, "font_24", font_24);
    lv_xml_register_font(NULL, "font_32", font_32);
    lv_xml_register_font(NULL, "font_48", font_48);
    lv_xml_register_font(NULL, "font_icons_14", font_icons_14);

    /* Register subjects */

    /* Register callbacks */
#endif

    /* Register all the global assets so that they won't be created again when globals.xml is parsed.
     * While running in the editor skip this step to update the preview when the XML changes */
#if defined(LV_USE_XML) && LV_USE_XML && !defined(LV_EDITOR_PREVIEW)
    /* Register images */
#endif

#if !defined(LV_USE_XML) || LV_USE_XML == 0
    /*--------------------
     *  Permanent screens
     *-------------------*/
    /* If XML is enabled it's assumed that the permanent screens are created
     * manually from XML using lv_xml_create() */
    gui_create();
#endif
}

void tempctl_ui_set_target(uint32_t target)
{
    tempctl_ui_target = target;
}

uint32_t tempctl_ui_get_target(void)
{
    return tempctl_ui_target;
}

bool tempctl_ui_check_target(uint32_t target)
{
    return (tempctl_ui_target & target) ? true : false;
}

/* Callbacks */

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void check_font(lv_font_t ** font, const char * name)
{
    if (!(*font)) {
        *font = (lv_font_t *)LV_FONT_DEFAULT;
        LV_LOG_WARN("font `%s` was not set. Using `LV_FONT_DEFAULT` instead", name);
    }
}