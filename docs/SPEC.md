# TempCtl Duo — specification and working notes

Status: 2026-09-28. Written at the end of the conversation that built TempCtl 1.2 so that work on
**TempCtl Duo** can continue in a new conversation. Read this file first, then `README.md` (hardware,
wiring, PMA register background, build, web API) and `ui/README.md`.

## 0. Isolation rules (read first)

TempCtl Duo is a copy of TempCtl (made 2026-09-28 from TempCtl commit `47995c9`, first own commit
`7c15cd0` "Start TempCtl Duo as a copy of TempCtl"). The original must stay untouched.

| | TempCtl (original — do not touch) | TempCtl Duo (work here) |
|---|---|---|
| Local clone on Rost's PC | `D:\Share\GitHub\TempCtl` | `D:\Share\GitHub\TempCtl-Duo` |
| In the Cowork device VM | `$HOME/mnt/Share/GitHub/TempCtl` | `$HOME/mnt/Share/GitHub/TempCtl-Duo` |
| GitHub | `justr0st/TempCtl` | `justr0st/TempCtl-Duo` (origin is set; Rost creates the empty repo and pushes) |
| Claude project notes | `claude/tempctl-firmware-notes.md` (original's history) | this file (+ copy `claude/tempctl-duo-spec.md` in the project) |

- Only read, edit, build-check and commit inside `TempCtl-Duo`. Never write to `TempCtl`, never commit
  there, never change its remote.
- Before any file transfer, check the target path contains `TempCtl-Duo`.
- Temporary transfer archives go to `D:\Share\claude-xfer-duo-*.tgz` and are deleted afterwards.
- Firmware name/version: `src/config.h` still says `FW_VERSION "1.2.0"` and the device calls itself
  "TempCtl" (web page title, AP name `TempCtl-XXXX`, host name `tempctl`, LVGL Pro project name
  `tempctl_ui`). Renaming these is a Duo task (see 9), only when Rost asks — a board flashed with Duo
  should be distinguishable from one running TempCtl.

## 1. Working with Rost (user preferences and environment)

- User: Rost (GitHub `justr0st`, commit email `50486502+justr0st@users.noreply.github.com`).
- **No computer use / screen control** ("stop using computer screen"). Rost flashes the board, runs the
  LVGL Pro Editor ("Export C code") and pushes to GitHub himself. The in-app browser may be used for
  looking at web pages (e.g. design references).
- **Serial port (COM6) is not reachable** from Claude: ask Rost for the serial monitor output.
  `platformio.ini` has `monitor_filters = esp32_exception_decoder`, so his monitor prints decoded
  backtraces. His build ELF: `D:\Share\GitHub\TempCtl-Duo\.pio\build\cyd\firmware.elf`.
- **GitHub is not reachable** from the device VM (proxy 403) and the cloud container has no
  credentials: never try to enter credentials; tell Rost to push.
- Commit only when Rost asks. Commit messages: mention only the most significant changes. Leave
  `.vscode/settings.json` (his local editor settings) out unless he says otherwise. `upload_port` /
  `monitor_port = COM6` are in `platformio.ini` and committed (Rost did that himself).
- Answers short and direct; Rost usually tests on hardware and reports back ("works", a log, a photo).
- Ask before large design decisions (AskUserQuestion); otherwise make the reasonable choice and say so.

### Tools and file handling
- Rost's clone is reached through the Cowork device VM (`device_bash`, folder `D:\Share` mounted at
  `$HOME/mnt/Share`). Delete permission for `D:\Share` had to be granted per session.
- The working tree is **CRLF**, the repository stores LF. Git in the VM:
  - read-only commands with `GIT_OPTIONAL_LOCKS=0`;
  - everything else with
    `git -c core.autocrlf=true -c user.name=justr0st -c user.email=50486502+justr0st@users.noreply.github.com`
    (without autocrlf every file looks modified);
  - commit trailer: `Co-Authored-By: Claude …` and `Claude-Session: …` as given by the session.
- Small edits: python read-modify-write in `device_bash`, keeping CRLF (`open(p, newline='')`).
- Bigger changes: work in the cloud container on an LF snapshot (tar on the device into `D:\Share`,
  `device_stage_files`, extract, `sed -i 's/\r$//'`), then send back a tgz of CRLF files
  (`sed 's/$/\r/'`) via `/mnt/user-data/outputs/<new dir>/` + `device_commit_files` to `D:\Share`,
  extract in the VM, verify the md5 of the CR-stripped files, delete the tgz. Before overwriting,
  check that the device files still have the md5 of the snapshot (Rost edits XML in the editor).
- Don't `pkill -f`/`pgrep -f` with a pattern that also matches the running bash command (it kills the
  shell, exit 144).

## 2. What the device is

- ESP32-2432S028R "Cheap Yellow Display" (ESP32, ILI9341 320×240 on HSPI, XPT2046 touch on its own
  SPI, RGB LED, backlight GPIO21) + RS485 transceiver on UART2 (RX 35, TX 22, DE/RE 27).
- Talks Modbus RTU to a **PMA KS 40-1 / 41-1 / 42-1** process controller (or a generic Modbus RTU
  controller with a user-supplied register map). The ESP32 is a ramp/soak **programmer**: it writes the
  controller's setpoint over time; the PMA does the PID control.
- **Main use: the heater of a BGA rework station** (reflow / desolder profiles). Example program on the
  first start: "GPU desolder (LeadFree)" — 160 °C (ramp 60 s, hold 30 s), 190 °C (40/40), 225 °C
  (30/20), 245 °C (20/40), total 4:40 (`programExample()` in `src/program_store.cpp`).
- Touch UI (LVGL 9.6) and a web UI (JSON API) with the same functions ("full parity").

## 3. Build and toolchain

- PlatformIO, `espressif32@6.9.0` (Arduino-ESP32 2.0.17), board `esp32dev`, `huge_app.csv`
  partitions, env `cyd`.
- Libraries: `lvgl/lvgl@9.6.0`, `symlink://ui` (the exported UI code as a library, `ui/library.json`),
  TFT_eSPI 2.5.43 (configured by build flags), XPT2046_Touchscreen (git), ArduinoJson 7.
- Build flags: `-D LV_CONF_INCLUDE_SIMPLE -D LV_LVGL_H_INCLUDE_SIMPLE -I include`,
  `-D DISABLE_ALL_LIBRARY_WARNINGS=1` (TFT_eSPI TOUCH_CS warning), TFT pins/frequencies.
- `include/lv_conf.h`: from the LVGL 9.6 template; CLIB malloc/string/sprintf, 30 ms refresh,
  `LV_USE_OBJ_NAME`, `LV_USE_TRANSLATION`, printf log, dark default theme without grow, Montserrat
  12 + 14, asserts on with `include/lv_assert_custom.h` (`LV_ASSERT_HANDLER abort();` instead of
  LVGL's silent `while(1)`).
- `src/main.cpp`: loop task stack 24 kB (`SET_LOOP_TASK_STACK_SIZE`), after setup
  `esp_task_wdt_init(15, true)` + `enableLoopWDT()` (a stuck loop restarts the board with a backtrace;
  the longest intended wait in the loop is a 5 s bus request from the touch UI).
- Measured on hardware (1.2 on LVGL 9.6): flash ≈ 1.36 MB (before the web redesign, which added ≈ 17 kB); free heap 249 kB before UI,
  158 kB after UI, 97 kB after net/web/control (largest block 51 kB); loop stack ≈ 3 kB used in setup.

## 4. Source layout

| File | Role |
|---|---|
| `src/config.h` | pins, FW_VERSION, chart size (CHART_N 300, 1 s live samples) |
| `src/modbus_frame.h`, `modbus_rtu.*` | RTU framing/CRC, half-duplex bus with DE pin |
| `src/controller.*` | register access via the RegMap, value flags (sensor errors, off value) |
| `src/program.*` | pure C++ ramp/soak engine (`ProgramEngine`), host-testable |
| `src/program_store.*` | LittleFS `/progN.bin` (N = 0..9), header magic "PRG1", name 24 bytes |
| `src/settings.*` | `Settings` struct in NVS (namespace `tempctl`, key `cfg`), **version 4**; v3 and v2 migrated |
| `src/app_state.*` | shared state (`gSettings`, `gLive`, `gHist`, `gPrograms`, lock `Guard`), control task on core 0 (Modbus + engine, command queue `sendCmd`, `busRequest` for Tools) |
| `src/net.*` | Wi-Fi STA with AP fallback after 20 s, mDNS, asynchronous scan for the touch UI |
| `src/web.*`, `web_page.h` | WebServer + JSON API; single-page UI in PROGMEM |
| `src/ui.*` | touch UI logic (LVGL), binds to the exported widgets by name |
| `src/ui_gen.c/.h` | C bridge to the exported UI code (see 6) |
| `ui/` | LVGL Pro Editor project (XML) + exported C (`*_gen.c/.h`, `tempctl_ui*.c/.h`, `fonts/*_data.c`) |
| `test/host_test.cpp` | host tests for CRC and the engine: `g++ -std=c++17 -I../src host_test.cpp ../src/program.cpp` |

Data model: 10 programs × 100 steps; step = target (°C, float), rampSec, holdSec (uint32, max
9999 min); name ≤ 23 chars (`PROG_NAME_LEN` 24 is part of the file header — changing it needs a file
migration). Run states Idle/Running/Paused/Finished/Aborted ("stopped" in the API), phases Ramp/Hold.

## 5. Behaviour (agreed decisions)

- **Program run**: the first ramp starts at the PV measured at start. Ramp mode "ESP32 writes setpoint
  steps" (min. step and min. seconds between writes, to spare the PMA EEPROM) or "controller ramps"
  (writes target + r.SP gradient). Optional guaranteed soak (hold time counts only inside ±band).
  End/stop action: keep SP, set end SP, or outputs off (C.Off). Front panel lock while running
  (L-R). Pause after link loss (s). Setpoint limits spMin/spMax.
- **Manual setpoint**: on/off switch. On = write SP and switch the outputs on (C.Off = 0); off =
  outputs off (C.Off = 1). Starting a program ends manual mode.
- **Chart**: idle = rolling 5-min window, PV scrolls right-to-left, the selected program is shown
  dashed as a preview on its own time scale across the full width. Running = fixed axis over program
  total + max(10 %, 30 s); plan and PV grow from the left. Solid = active, dashed = not active. Series
  order PV, SP, program (LVGL draws the first series last → PV on top).
- **Step times** are stored in seconds; editors offer min or s per value.
- **Web UI parity**: Status (Main), Programs, System, Controller, Tools on both UIs.
- PMA KS 4x-1 register defaults ("1dP" column, value = register/10, DP1 offset in `settings.cpp`):
  PV 13294, SP.EF 13293, Ypid 13295, St.Cntr 5100, St.Ala 250, SP 11372, SP.LO 11292, SP.Hi 11293,
  r.SP 11295, C.Off 1086, L-R 1080; read fn 3, write fn 6, off raw −32000.

## 6. Touch UI (LVGL 9.6 + LVGL Pro Editor)

- LVGL 9.5+ has no open-source XML loader; the UI is **C code exported by LVGL Pro Editor** from the
  XML in `ui/` ("Export C code", `project.xml` `lvgl_version="9.5.0"` — the editor offers at most 9.5;
  the export compiles fine with 9.6). XML and exported files are committed together. After XML changes
  Rost exports; simple string/number changes may be patched in both XML and `*_gen.c` by hand (a
  re-export produces the same).
- `ui.cpp` must **not include the generated headers** (they pull `lvgl_private.h`, whose FT81x macros
  like `CMD_START` clash with `CmdType`). `src/ui_gen.c` includes them and forwards: `uiGenInit()`
  (fonts + `tempctl_ui_init("")` + palette, returns the permanent screen `gui`), popup create wrappers
  (`uiGenDialog/NumEditor/TextEditor/WifiList/WifiItem`), colours/palettes. A new component used from
  C needs a wrapper there.
- Fonts: `font_12/14/20/24/32/48` (Roboto / Roboto Slab, ranges incl. German, Cyrillic and °), `fontM_*`
  (Montserrat Medium), `font_icons_14` (Font Awesome). Exported font data is const (flash), so
  `UI_FONT_WITH_FALLBACK` makes RAM copies with LVGL's Montserrat as fallback (symbols) before init.
- Texts: all in `ui/translations.xml` ("en" only), `translation_tag` in XML, `T(tag)` / `trf(buf, n,
  tag, a1, a2, a3)` with `%1..%3` in C. Never braces in XML attribute values (the editor treats `{…}` as
  an expression). An empty text must be `" "` (the export turns `""` into "missing" → the tag shows).
- Widgets are found **by name** (`main_*`, `prog_*`, `chart*`, `sys_*`, `ctl_*`, popup names
  `num_*`, `text_*`, `dlg_*`, `scan_title/scan_list/scan_spinner/scan_cancel`, `wifi_item`); event
  handlers are attached in C (`UI_EVENTS`), none in XML. Popups are created on `lv_layer_top()` each time
  they open.
- **Colour themes**: 6 palettes (Default, Dracula, Solarized, Gruvbox, Catppuccin, One) × dark/light,
  chosen in Setup › System (and on the web System page), stored in Settings v4
  (`themePalette`, `themeDark`; 0xFF = `theme_dark` from `globals.xml` at the first start).
  `color_*` in `ui/globals.xml` = Default dark = the editor preview; the others are constants
  `<palette>_<dark|light>_<role>`; 14 roles: bg panel border text muted accent accent2 row_selected
  danger pv sp plan ok warn. `src/ui_gen.c` builds the palettes (`PAL()`, `paletteLoad()`,
  `PALETTE_NAMES`) and recolours the styles listed in `STYLE_COLORS` (warning at boot if a line doesn't
  match `globals.xml`); `ui.cpp` `applyTheme()` re-inits the LVGL default theme and sets chart series and
  table colours. **Rule: widgets get colours only through named styles**, never `style_…_color` on the
  widget. Not palette-driven yet: LVGL theme greys (switch/slider tracks, keyboard keys, dropdown list).
- Wi-Fi scan: the list popup opens immediately with a spinner until `netScanResult()` is ready; Cancel
  works during the scan.
- LVGL Pro Editor gotchas: no `.woff` fonts (use `.ttf`); avoid `send_draw_task_events` / `update_mode`
  in XML (table draw events are set in C); scale numbers + labels in one component abort the preview →
  `label_show="false"` in XML, switched on in C; a component with a 100 % root needs
  `<previews><preview width height/>`; local style props with part selectors are ignored → named styles
  with `selector=`; `#` at the start of a text is read as a constant; the editor reformats files it saves.
- Known harmless logs: two "Deprecated: lv_obj_set_flag/remove_flag" warnings at boot (the 9.5-style
  export), "LittleFS … progN.bin does not exist" (LittleFS.exists() in core 2.0.x).

## 7. Web UI

- `src/web_page.h` (≈ 54 kB, one PROGMEM raw string `R"HTML(…)HTML"` — must not contain `)HTML"`).
  Self-contained (works in AP mode): inline CSS/JS/SVG icons, system fonts, always dark.
- Look (after MoldControl by servomold / HMI Project, iF Design 2025): dark panels, pale cyan controls
  (`--acc #9FD9D5`), yellow for the active step (`--hi #F3C335`); sidebar with the page tiles in one
  column (always visible ≥ 860 px, slide-in panel with a menu button on phones) and link status, network
  info, clock and log download in its footer; top bar with title and jump buttons to the cards.
- Status page: chart with step boxes above it (active phase yellow + time marker), run bar (program
  select, Start/Resume, Pause, Next phase, Stop) under the chart, facts row whose **state cell is
  coloured by state** (running green, paused yellow, finished cyan, stopped red, manual orange), right
  column PV / SP / output (level meter) / **manual setpoint tile (stepper + switch, dark orange while
  manual is on)**. In-page `ask()` dialogs instead of `confirm()`; boolean settings are switches.
- API (`src/web.cpp`): GET `/`, `/api/status`, `/api/history`, `/api/log.csv`, `/api/programs`,
  `/api/program?i=`, `/api/settings`, `/api/mb/read`, `/api/params`; POST `/api/program`, `/api/select`,
  `/api/manual`, `/api/cmd` (start/pause/resume/stop/skip), `/api/settings`, `/api/wifi`,
  `/api/mb/write`, `/api/mb/findsp`, `/api/params`, `/api/action`. Settings JSON includes
  `themePalette`, `themeDark`, `themes[]`.

## 8. Verification approach (cloud container)

Rebuild these in `/tmp` if the container was reset (they were not kept in the repo):
- ESP32 compile check: xtensa-esp32-elf gcc 8.4 (esp-2021r2-patch5) + arduino-esp32 2.0.17 + LVGL 9.6
  (`git clone --depth 1 --branch v9.6.0 https://github.com/lvgl/lvgl` works; curl to GitHub/registries
  was blocked) + TFT_eSPI + ArduinoJson; compile the changed `.cpp` with the project flags, C files with
  xtensa gcc directly; link everything for the flash size.
- Host simulator: LVGL 9.6 built for the host with the project's `lv_conf.h` and shims for Arduino,
  TFT_eSPI, Preferences etc.; compiles `ui/*.c`, `src/ui_gen.c`, `src/ui.cpp`, `program.cpp`,
  `settings.cpp` + stubs; drives touches/tabs and writes PPM screenshots of all screens; ASan/UBSan
  build. For heap checks use `GLIBC_TUNABLES=glibc.malloc.tcache_count=0` (tcache makes mallinfo look
  like a leak). Lesson: host tests get zeroed stack memory — uninitialised-variable bugs only show on
  hardware.
- Web UI: headless Chromium (Playwright, preinstalled) against a small Python mock of the JSON API
  (states running / paused / manual / no link), desktop 1366×820 and phone 390×844, check for page
  errors, look at the screenshots; `node --check` on the extracted script.
- Host tests: `test/host_test.cpp` (see 4).

## 9. TempCtl Duo — goals (to be defined with Rost)

Not specified yet. The name suggests two channels (e.g. top and bottom heater of the BGA rework
station, two PMA controllers on the same RS485 bus with different slave addresses), but **ask Rost what
"Duo" means before designing**. Questions to settle first:
1. Two controllers (two slave addresses / register maps) or one controller with two loops?
2. Programs: one program per heater, or one program with two setpoint curves (top/bottom) per step?
3. Start/stop together? Synchronised steps? Separate manual setpoints?
4. Screen layout on 320×240 (two PV/SP readouts, two chart curves + plans), web UI layout.
5. Settings/program file format changes (Settings v5, program file v2) and migration from TempCtl data,
   or a clean start (new NVS namespace / new file names so a board can be switched between firmwares).
6. Identity: product name "TempCtl Duo" on screen, web title, AP name, host name, FW_VERSION.

Keep the TempCtl conventions (full web/touch parity, colours via named styles, texts in
`translations.xml`, widgets bound by name, exported UI code committed) unless Rost decides otherwise.

## 10. Open items carried over from TempCtl

- Sporadic freeze once seen on the Wi-Fi "Scanning…" screen (not reproduced). The assert handler and
  loop watchdog now turn a freeze into a restart with a backtrace; ask for the log if it happens.
  Suspect: Arduino WiFi scan timeout (`_scanTimeout` = 20 × 300 ms) → our retry calls
  `scanNetworks`/`scanDelete` while the old hardware scan finishes (race with `_scanDone` in the event
  task).
- LVGL issue text about the 9.4 globals parser crash was drafted for Rost to post (irrelevant on 9.6).
- Optional: palette-driven colours for LVGL theme parts (switch/slider tracks, keyboard, dropdown list).
- Optional: program names longer than 23 characters (needs a program file migration).
- Rost's board still has the old stored program 1 "Example" (the new example is only created when no
  program exists).
