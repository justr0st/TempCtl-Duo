# TempCtl touch UI (LVGL Pro project)

Open this folder in LVGL Pro Editor. Target: 320x240, LVGL 9.4.0.

The firmware embeds these XML files at build time (`scripts/embed_ui_xml.py`) and
creates the screen with LVGL's XML loader, so after editing just build and upload
with PlatformIO. Keep the widget `name`s (the firmware finds widgets and attaches
its touch handlers by name); see section 4a of the main README for the rules.

Fonts in `fonts/` (Montserrat, Font Awesome 5 Free) are only used by the editor
preview; licenses in `fonts/licenses/`. On the device the built-in LVGL
Montserrat fonts are used.
