"""Patch LVGL 9.4.0 before it is compiled (PlatformIO pre script).

1. Crash fix: lv_xml_register_component_from_data() leaves the parser state
   uninitialised when it registers "globals" (only state.scope is set). If the
   stack happens to hold the value of LV_XML_PARSER_SECTION_API there, the
   <globals> element is parsed as an API <prop> without a name and
   strlen(NULL) crashes the board at start-up (Guru Meditation,
   LoadProhibited, in lv_strdup from process_prop_element). The patch zeroes
   the state first.
2. Warning fix: lv_xml_buttonmatrix_parser.c assigns the string literal "\\n"
   to a `char *` (-Wdiscarded-qualifiers). The pointer is only read (copied
   with lv_strdup), so a cast is enough. This is the only warning LVGL 9.4.0
   gives with the project's build flags.

The patches are applied to the downloaded library in .pio/libdeps and are
idempotent. If LVGL's code no longer matches, a warning is printed and the
file is left alone.

Can also be run by hand:  python scripts/patch_lvgl.py <path to the lvgl folder>
"""
import os
import sys

PATCHES = [
    {
        "file": os.path.join("src", "others", "xml", "lv_xml_component.c"),
        "mark": "/* TempCtl patch: zero the parser state (globals path) */",
        "old": "    lv_xml_parser_state_t state;\n    if(globals) {",
        "new": "    lv_xml_parser_state_t state;\n"
               "    lv_memzero(&state, sizeof(state)); /* TempCtl patch: zero the parser state (globals path) */\n"
               "    if(globals) {",
    },
    {
        "file": os.path.join("src", "others", "xml", "parsers", "lv_xml_buttonmatrix_parser.c"),
        "mark": "/* TempCtl patch: const cast */",
        "old": 'if(lv_streq("\\\\n", text_start)) text_start = "\\n";',
        "new": 'if(lv_streq("\\\\n", text_start)) text_start = (char *)"\\n"; /* TempCtl patch: const cast */',
    },
]


def apply(lvgl_dir, p):
    path = os.path.join(lvgl_dir, p["file"])
    if not os.path.isfile(path):
        print("patch_lvgl: %s not found (LVGL not installed yet?)" % path)
        return False
    with open(path, "r", encoding="utf-8", newline="") as f:
        src = f.read()
    if p["mark"] in src:
        return True
    nl = "\r\n" if "\r\n" in src else "\n"
    old, new = p["old"].replace("\n", nl), p["new"].replace("\n", nl)
    if old not in src:
        print("patch_lvgl: WARNING: code in %s has changed, patch not applied - "
              "check whether this LVGL version still needs it" % path)
        return False
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(src.replace(old, new, 1))
    print("patch_lvgl: patched %s" % path)
    return True


def patch(lvgl_dir):
    ok = True
    for p in PATCHES:
        ok = apply(lvgl_dir, p) and ok
    return ok


try:
    Import("env")  # noqa: F821  (PlatformIO/SCons)
    patch(os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "lvgl"))  # noqa: F821
except NameError:
    if len(sys.argv) != 2:
        sys.exit("usage: python scripts/patch_lvgl.py <lvgl folder>")
    sys.exit(0 if patch(sys.argv[1]) else 1)
