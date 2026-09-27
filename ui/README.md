# TempCtl touch UI (LVGL Pro project)

Open this folder in LVGL Pro Editor. Target: 320x240, LVGL 9.5.0 (the firmware
runs on LVGL 9.6).

After editing, run **Export C code** in the editor: the firmware compiles the
exported `*_gen.c/.h` files (this folder is a PlatformIO library, see
`library.json`), not the XML. Commit the XML and the exported files together.
Keep the widget `name`s (the firmware finds widgets and attaches its touch
handlers by name); see section 4a of the main README for the rules.

Pages are in `components/`, the popups (number / text entry, dialog, Wi-Fi list)
in `components/popups/`.
All texts are in `translations.xml`, colours and theme settings in `globals.xml`.

Fonts in `fonts/` (Montserrat, Font Awesome 5 Free) are converted by the export
into `fonts/*_data.c`; licenses in `fonts/licenses/`.
