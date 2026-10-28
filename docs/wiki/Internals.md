# How it works inside

Notes for anyone changing the firmware or the web page: how the touch UI is built and drawn, the screen check,
where things are stored. What the device does for its user is on the other pages; the design decisions with
their reasons are in `claude/tempctl-duo-spec.md`.

## Touch UI

**`src/tui_*.cpp`** — screens written in LVGL C code, one code for both screen
sizes: 480×320 (S3) and a compact 320×240 set (CYD, `TUI_SMALL` in `tui.h`): there the main screen has one
SP button per channel (tap: number pad) instead of the − value + stepper and the SP readout, the run buttons
and the menu in a bottom bar; the Sequence editor shows one line per phase (tap: the phase menu with its
values, type and row actions) and the Timeline editor one table with a channel switch above it. The CYD
builds only the main screen once; the other screens are built when opened and freed when left (its
internal RAM is short), the editor keeps 2 undo steps there (15 on the S3). `tui.cpp` (display +
touch glue, screen switching, the 250 ms update), `tui_theme.cpp` (the web page's palette, accent-derived
channel colours, named styles), `tui_widgets.cpp` (buttons, pages, number pad, keyboard, dialogs, choice
lists, messages), `tui_main.cpp`, `tui_chart.cpp`, `tui_menu.cpp` (menu, log, display, network, system),
`tui_prog.cpp`, `tui_chan.cpp`. Fonts: Barlow / Barlow Semi Condensed (as on the web page) in
`src/fonts/tfont_*.c`, made by `tools/lv_font_gen.py` from `tools/fonts/*.ttf` (OFL); LVGL's Montserrat
supplies the symbols. **Preview without a board**: `test/ui_host/build.sh` builds the screens for the PC
and renders screenshots of the main states (and `./uihost monkey` taps around at random under
AddressSanitizer, `./uihost stepper` checks the tile's + / - while the device takes a value over late, `./uihost chan` the Channels screen); `BOARD=cyd ./build.sh` does the same for the 320×240 set (`uihost_cyd`, `shots_cyd/`),
`python3 sheet.py [cyd] [names]` converts the shots and puts them on one sheet, `MEM=1 BOARD=cyd ./build.sh`
+ `./uihost_mem mem` prints the heap used per screen (`./uihost_mem memleak <n> x`: what one screen holds,
by caller; `./uihost_cyd lowmem`: a page is not opened without enough free RAM).

**RAM on the CYD**: only the main screen stays; another page is built when opened, after the one being left
was deleted. A page is built only with the RAM it took last time (first time: 40 kB) + 12 kB free; otherwise
the message "Not enough free memory for this page" (serial: `[mem] <page> page: N bytes (M free)` after each
build). Labels, rows, columns, button and switch sizes use shared styles (no per-object local style); the
Channels page's value buttons are single labels (`tValueButton`), and on the CYD its rows have no object of
their own (label and control in the page body, a flex wrap): about 39 kB instead of 51 kB.

**Slide off to cancel** (both boards): a press released away from the widget it started on (more than 10 px
outside it, CYD 6 px) does nothing — no click, no switch toggle; a held stepper stops repeating while the
finger is outside. Sliders and the keyboard / number keys behave as before. (`slideOffCancels` in tui.cpp:
an event handler on the touch input resets it on such a release, before the widget sees it.)

### Drawing and display speed

Setting "Display SPI clock" (`display.spiMhz`, 20 / 40 / 80 MHz, default 40).

- LVGL draws in 2 buffers of 20 lines (S3; CYD one of 10) in internal DMA RAM (RGB565, bytes swapped for the
  panel just before sending) and sends each part by DMA while it draws the next. S3: the chart is drawn once
  per change into a picture in PSRAM (grid and labels by LVGL, the curves by its own anti-aliased line
  drawing, `tui_chart.cpp`; about 10–15 ms, once a second) and only copied to the screen (the CYD draws it
  directly). Drawing on both
  CPU cores (LVGL draw threads) was tried and made busy screens slower: LVGL draws in the loop task. S3: solid
  fills and RGB565 copies use Espressif's ESP32-S3 SIMD assembly (src/simd/, Apache-2.0, see its README). A full screen is 300 kB on the
  S3: about 61 ms at 40 MHz, 31 ms at 80 MHz (only changed areas are drawn and sent).
- **80 MHz** needs short, clean wires (S3: GPIO 10–13, the SPI bus's IO-MUX pins; CYD: 12–15). Wrong pixels or a
  garbled picture: back to 40 MHz on the web page (Settings › Display) or serially (`set display.spiMhz=40`).
  The setting takes effect at once (touch: Menu › Display › Display SPI clock).
- **Frame rate meter** (touch Menu › Display › Frame rate meter, serial `perf on`, not saved): per second the
  redraws (FPS), the LVGL loop's CPU load, the average frame time split into drawing and waiting for the SPI
  transfer, how busy the SPI was and the data rate, e.g. `[perf] 24 FPS  LVGL CPU 61 %, frame 31 ms (draw 19,
  wait 12), SPI 40 MHz busy 52 %, 410 kB/s`. Much "wait": the SPI is the limit (a higher clock helps); much
  "draw": the CPU is. Measured on the S3 (1.0.54): busy screens (nearly the whole screen redrawn) reach 13 FPS at
  40 MHz and 14–15 FPS at 80 MHz with the CPU at 95 % and almost no waiting: the drawing is the limit; 80 MHz
  halves the SPI load (40 → 80 MHz: 70 % → 38 % busy).

### Automatic screen check

Setting "Screen check" (`display.screenCheck`, on by default).

Every second the firmware checks the screen hardware and restarts a part that hangs, without restarting
the device; each automatic restart is an event in the log ("Screen restarted automatically: …").

- **Display** (both boards): its state is read back over the display's MISO line (S3 GPIO13, CYD
  GPIO12): registers 0Ah power mode, 0Bh MADCTL (orientation), 0Ch pixel format and 0Dh image mode,
  compared with the values read right after the start (incl. the booster bit that keeps the panel
  driven, inversion, all-pixels on / off). A panel reset by interference wakes up asleep / with its
  defaults (white or garbled screen) → set up again. Whether the state can be read at all is decided
  right after the start (`[lcd] status: …` in the log); if it does not read back as set up, this part
  of the check stays off. A restart by hand (web, `screen restart`) is an event too, with what the check
  saw just before (`regs 9C 28 55 00` = the four registers; `set …` = the values they should have).
- **Touch, S3 (FT6336)**: its chip id is read; no answer twice in a row → reset pin, I2C bus freed,
  set up again. The CYD's XPT2046 cannot be asked.
- **Touch stuck** (both): a press that does not move at all for a minute → touch controller restarted
  (3 tries, then left as it is until it is released).
- At most one automatic restart per 30 s; after 3 that did not help it stops until a check passes.
  Not covered: pixels garbled while the controller's state is fine, a resistive touch that simply stops
  reacting, a hanging UI program (that is the loop watchdog's job: device restart).
- Test: the serial command `screen check test` (not in the help) switches the display off and puts it
  to sleep; within a few seconds the check should restart it (event "Screen restarted automatically").
- **Short touch gaps (S3)**: the FT6336 sometimes reports no touch point for a read or two while the finger
  stays on the panel (seen all over a 4" panel, most in the lower left corner). Up to 2 such reads (LVGL
  reads every 30 ms, so up to ~60 ms) keep the press at its last point, so a held button (stepper
  acceleration) is not let go; a "lift-up" from the controller releases at once, a longer gap after the 2
  reads. `screen check` (serial) prints the statistics: reads, I2C read problems, gaps bridged (1 / 2 reads),
  longer gaps (release + new press within 150 ms, by reason) and the INT line state; once a minute the log
  gets `[touch] last minute: n short gaps in held touches bridged (...), n longer (...); last at x .. y ..`
  when there were some.

**S3 touch INT line** (CTP_INT, GPIO7, `TOUCH_USE_INT` in touch_ft6336.cpp): the FT6336 holds it low while
the panel is touched, so the touch is read over I2C only then (and on every read while pressed, so a release
is seen at once — no added delay). Every 25th read (~0.75 s) is done anyway: a touch found while the line is
high (3 times) switches back to reading every time (`[touch] INT line stays high while touched ...`);
`[touch] INT line works` after the first touch it saw.

## Web page

- Look (1.0.40): frosted glass — an accent-tinted gradient backdrop, translucent blurred sidebar and cards,
  pill buttons, channel colour strips, folder tabs on the Channels page, a title bar that fades in while
  scrolling; a channel card turns into its channel colour while the output is on. Browsers that ask for
  reduced transparency get it without the blur. Logo: `web/icons/`
  (from `logo/TempCtl.svg`).
- Source: `web/index.html` (one file: HTML, CSS, JS) and the Barlow / Barlow Semi Condensed fonts in
  `web/fonts/` (woff2 subsets: Latin, Latin-1, Latin Extended-A and a few symbols; SIL Open Font License,
  `web/fonts/OFL.txt`). The page works without internet (AP mode): everything comes from the device.
- `tools/web_embed.py` gzips the page and writes `src/web_assets.h` (generated, not in git). PlatformIO
  runs it before every build (`extra_scripts` in `platformio.ini`); by hand: `python tools/web_embed.py`.
  The page is served gzipped (≈ 40 kB), the fonts (≈ 90 kB) with a one-year browser cache.
  Boards without PSRAM (CYD) serve the page without the web fonts (system fonts; RAM is short there).
- The device's own copy of the page is marked (`data-device`, by `tools/web_embed.py`), the desktop app's copy
  `data-launcher`, the demo's `data-demo`; without a mark (the hosted page) the page shows the connect screen.
- Icons: `web/icons/` (`logo.svg` = favicon and sidebar logo, `favicon.ico` 16/32/48 px,
  `apple-touch-icon.png` 180 px), made from the logo in `logo/` (SVG cleaned up for size, PNGs rendered
  from it); served at `/favicon.svg`, `/favicon.ico`, `/apple-touch-icon.png` (≈ 10 kB in all).
- Chart PV lines show every reading: each chart point (live: period / 300, run: run span / 300) keeps its
  lowest and highest reading, and the line passes through both, so short peaks and dips stay visible.
- Live history: the device keeps every second of both channels in RAM (last, lowest and highest reading
  and setpoint; lost on restart): 24 h on the S3 (1 MB of PSRAM), on the CYD what fits into the IRAM-only
  heap (about 30 min; the boot log says `[hist] live history N min`). The Live chart's Period (1 min …
  24 h, up to what the history holds, so 30 min on the CYD; saved as `display.liveMin`, also used by the touch screen) picks how much of it is
  shown; its vertical axis always runs from 0 °C to the highest value in view. Setpoint lines are drawn
  at half strength with the area below them lightly filled. The legend switches the PV
  and SP of each channel on / off (chart, scale and export; the browser remembers it). Export CSV
  downloads the whole history of the switched-on series, one row per second ("Download live history" in
  the sidebar: all series). A setpoint is recorded only while it is active: in a run, with the manual
  setpoint on, or kept / set by a run's end action (Modbus; local channels: while their output is on).
- Refresh: the page asks for new values every polling interval (Channels page; at most every 250 ms, at least
  every 1 s) and draws
  the newest temperature at the end of the chart; the chart history comes every 2 s.

### System log and run logs

**System log** (Log page): everything the device logs, colour-coded — errors (red), warnings (amber), program runs
(accent), events, technical lines of the serial log ([wifi], [lcd], [mem] …, grey); filters per kind,
search, download as text. The events of earlier starts come from flash (each start is a section);
times are the real time when the device knows it (NTP on Wi-Fi, or the page sets the clock), else
the time since the start. The device keeps about 7000 lines on the S3 (256 kB PSRAM), about 50 on
the CYD (4 kB); the last 300 events (not the technical lines) survive restarts (`/duo/events.txt`,
saved at most every 30 s and before a requested restart).

**Run logs (SD card)** (Log page), S3 only: one CSV file per program run on the display's micro-SD card
(FAT32, folder `runs`, name = start time (local) + program, or start / run number when the clock is
not set): a row every second — `time` (local, `2026-10-13 01:05:35`), `run_s, where` (phase / steps),
per channel `pv, sp` (active setpoint), `prog` (program setpoint), `out` (%), `state`, then `monitor_pv`
(the monitor channel; empty while off) — and a row for
each event during the run (`kind, event`). Written every 30 s and at the end of the run (a failed
write is tried again every 5 s; nothing is lost while the rows fit into 192 kB); list with Download /
Delete on the page, or read the card in a PC. Without a card the run is simply not logged. All card
work runs in its own task; the card shares the SPI bus with the display: it is mounted only while it
is written or read, and the screen check skips that second.

## Storage

| File (LittleFS) | |
|---|---|
| `/duo/settings.json` | all settings (safe save: temp file + rename) |
| `/duo/prog0.json` … `/duo/prog19.json` | the 20 programs (JSON, [program files](Programs.md#program-files)) |
| `/duo/events.txt` | the last 300 events, kept over restarts (saved at most every 30 s and before a requested restart) |
| `/duo/run.json` | the running copy of the program, with its run edits |
| `/duo/runpos.json` | the run position for "Run interrupted by a reset" after a power loss (every phase / step change and every minute; after other resets the exact position comes from RTC memory) |

SD card backup file (S3, folder `backup`):
`{"fmt":1,"kind":"tempctl-duo-backup","fw","board","host","time","settings":{…},"programs":[{"i":0,"program":{…}},…]}`.

## Firmware image check and rollback

- **Image check** (web upload, SD card, release page): the image must be an ESP image of the right chip with
  the TempCtl Duo id string of the same env (an image for another board / env is refused, also by the
  device). Images since 1.0.43 carry the id string right after the ESP-IDF app description (section
  `.rodata_custom_desc`, file offset 288), so the SD card check reads only the first 16 kB (well under a
  second); for older images it reads the whole file (about 30 s on the S3: the card shares the SPI bus with
  the display, which is slow meanwhile). Log: `[sd] firmware.bin checked: 1855 kB in 0.2 s`. After the
  install the file is renamed `firmware.done`.
- **HTTPS on the S3**: the release page download needs about 70 kB of RAM for the TLS session; mbedTLS
  allocates it in PSRAM while the download runs (root certificates: `src/ca_roots.h`,
  `tools/gen_ca_roots.py`). The CYD has no PSRAM and leaves this to the web page.

- **Rollback**: the new firmware runs "on probation" and confirms itself after 30 s of normal running (loop
  and control task alive). A crash or reset before that makes the bootloader start the previous firmware
  again, which logs "Firmware update to … failed: it did not start correctly". Events: "Firmware update
  started", "Firmware … written", "Firmware updated from … to …", "Firmware … confirmed".
- **CYD: once over USB first.** OTA needs two firmware slots: the CYD envs now use `partitions_cyd_ota.csv`
  (2 × 1.875 MB firmware + 192 kB file system, instead of `huge_app.csv`). The first USB upload with this
  table **erases the file system**: back up settings and programs before (web page, *Tools › Backup*),
  restore them afterwards. Wi-Fi settings are in that backup too, so the device starts as an access point
  until they are restored. The CYD firmware is about 1.85 MB (94 % of a slot); the page and icons are
  embedded without the web fonts on the CYD (the S3 only gets those), which keeps it in the slot.

## Status (1.0.64)

The program engine, settings, PID / autotune, runaway check, Modbus framing, image check and release
manifest pass host unit tests (`test/host_test.cpp`); the touch UI and the console have PC hosts
(`test/ui_host`, `test/console_host`). The firmware (Arduino-ESP32 2.0.17, `espressif32@6.9.0`, LVGL 9.6)
runs on the S3 and the CYD with local channels. S3 while running: about 145–158 kB of internal RAM free
(large tables, LVGL's memory, the histories and logs are in PSRAM, `src/psram.h`). The Modbus channel is
beta: it has not been tested with a controller yet; the macOS / Linux desktop apps are built but untested.
