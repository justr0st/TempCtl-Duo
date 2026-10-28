# TempCtl Duo: two-channel ramp/soak temperature programmer (ESP32-S3 / ESP32 CYD)

Firmware for a two-channel temperature programmer, made for the **bottom and top heater of a BGA rework
station** (reflow / desolder profiles). Version **1.0.62**. Two boards, built from the same sources:

- **ESP32-S3** with a 4" 480×320 capacitive touch screen (env `s3`, the main target)
- **ESP32-2432S028R "Cheap Yellow Display"** with a 2.8" 320×240 resistive touch screen (envs
  `cyd-1xlocal-1x485`, `cyd-2xlocal`, `cyd-2xlocal-2x485`, section 1)

Design notes and decisions: `claude/tempctl-duo-spec.md`.

**Wiki** — instructions with wiring diagrams and pictures of every screen: [`docs/wiki/Home.md`](docs/wiki/Home.md)
(on GitHub), also on the release page: <https://tempctl-duo.just-r0st.workers.dev/wiki/> (and linked from the
device's web page, *Help (wiki)*).

**Demo** — the web page with a simulated rework station (1100 W bottom, 450 W top heater, a GPU board), in any
browser: <https://tempctl-duo.just-r0st.workers.dev/demo/>. It runs the firmware's own control code, built for
WebAssembly (`demo/`, wiki page [Browser demo](docs/wiki/Demo.md)).

> **Status:** local thermocouple + SSR channels are the tested path (S3 and CYD). **Modbus RTU** (an RS485
> controller per channel) is a **beta** feature: built in, but not tested with a real controller yet.

- **Two channels** (bottom, top), each one of:
  - **local**: a MAX31855 / MAX6675 thermocouple converter and an SSR; the ESP32 does the PID (with
    autotune) and the protections (sections 8, 12);
  - **Modbus RTU** (beta): a controller such as the PMA KS 40-1 / 41-1 / 42-1 does the PID, the ESP32
    feeds it setpoints (section 9);
  - **off**.
- **Up to 20 named programs** that drive both channels (Sequence or Timeline, section 8), plus a **manual
  setpoint** per channel. On the first start three example programs are created (Lead-free BGA, Leaded
  BGA, Lead-free BGA as a Timeline).
- LVGL 9.6 **touch UI** on both boards (`src/tui_*.cpp`, section 3), same look as the web page (Barlow
  font, palette, accent and channel colours):
  - **Main screen**: header with the program (tap: choose the program for the next run), phase / steps,
    state, run time and remaining time, Start / Pause / Continue, Stop, Next phase; the chart of both
    channels (tap: program / live view); one tile per channel with PV, SP, output, what the channel does,
    a target stepper (− value +, tap the value for the number pad; during a run it changes the current or
    kept target, otherwise the manual setpoint), the manual on / off button (filled in the channel colour
    while on), "+ Hold" (extend the running hold), Acknowledge for a fault. While a channel's outputs are
    on (run, manual, autotune) its tile gets a 4 px border in the channel colour and a 20 % tint of it.
    Overlays: "Action required" (Continue / Stop), "Run interrupted by a reset" (Resume / Later / Dismiss;
    the red "Interrupted" badge opens it again), "Save the run's changes?", messages at the bottom.
  - **Menu**: Programs (list; editor: Sequence as one table, Timeline as two tables side by side; tap a
    value to change it, tap # / Type for type, wait, pause text, insert, move, delete; Undo, Save, Start,
    Rename, Discard, Delete; during a run the running program with run edits of targets and times),
    Channels (name, type, limits, end action, Modbus controller, thermocouple, PID + autotune, runaway
    protection, heating while off, frozen reading, converter temperature, RS485 bus; register addresses
    and the two-point calibration on the web page), Network (Wi-Fi with scan and password, AP password,
    host name, fixed IP), Display (brightness, dark mode, accent colour: six presets or any colour, upside
    down, live chart period, screen check, SPI clock, frame rate meter), System (polling interval,
    "bottom reached" band, time zone, S3: online update, firmware update and backup / restore from the SD
    card; manual mode timeout, E-stop input, SSR supply relay, alarm sound, chip temperature warning,
    info, restart), Event log.
  - **CYD (320×240)**: the same screens as a compact set (section 3).
- **Web page** (section 5): Dashboard, Programs, Channels (incl. controller parameters, register map,
  calibration, PID autotune, protection), Log (system log, SD run logs), Settings (device, firmware
  update, Wi-Fi, safety, display, clock) and Tools (Modbus register tools, backup) pages; CSV export and a
  CSV replay page.
- The same page **over USB** (Web Serial), in the desktop app or on the hosted page (section 6), and a
  **serial console** (section 4).
- **Releases** (section 7): every push to `main` builds all firmware images and the desktop app and
  publishes them on `https://tempctl-duo.just-r0st.workers.dev`; the web page checks for updates and
  installs them on every board, the S3 also fetches them itself over HTTPS.
- Firmware **protections** (runaway, heating while off, frozen reading, E-stop, SSR supply relay, …,
  section 12).

## 1. Hardware

Two supported boards, selected by the PlatformIO environment; all pins are in `src/board_s3.h` /
`src/board_cyd.h`.

### ESP32-S3 board (env `s3`, main target)

| Part | Notes |
|---|---|
| ESP32-S3-DevKitC-1 **v1.0 N16R8** | 16 MB flash, 8 MB octal PSRAM. On-board WS2812 on GPIO48 (a v1.1 board has it on GPIO38: swap 38 ↔ 48 in `board_s3.h`) |
| 4.0" SPI module **MSP4031** | ST7796S 320×480 (used as 480×320 landscape), FT6336U capacitive touch (I²C 0x38). VCC 5 V recommended |
| MAX31855 (or MAX6675) thermocouple converters, SSRs | local channels |
| **3.3 V** RS485 transceiver | on UART1 (Modbus channels, beta) |

Wiring diagrams per part (display, thermocouples, SSRs + relay, E-stop / buzzer, RS485): wiki page
[ESP32-S3 board](docs/wiki/Hardware-S3.md). All in one picture: `claude/wiring/s3-wiring.png` (display on J1, RS485 / thermocouples / SSR drivers on J3,
relay / E-stop / buzzer on J1-4/5/6). Schematic of the whole device: `hardware/s3-schematic/`
(KiCad 7 project + PDF / PNG, A3).

| Function | GPIO | | Function | GPIO |
|---|---|---|---|---|
| LCD SCK / SDI (MOSI) / SDO (MISO) | 12 / 11 / 13 | | RS485 TX → DI | 1 |
| LCD_CS / LCD_RS (D/C) / LCD_RST | 10 / 9 / 14 | | RS485 RX ← RO | 2 |
| LED (backlight, PWM) | 8 | | RS485 DE + /RE | 42 |
| SD_CS (SD card: run logs) | 18 | | Thermocouple SCK / SO | 41 / 40 |
| CTP_SDA / CTP_SCL | 16 / 15 | | TC CS bottom / top | 39 / 38 |
| CTP_RST / CTP_INT | 17 / 7 | | SSR bottom / top | 21 / 47 |
| | | | SSR supply relay (+5 V common of both SSRs) | 4 |
| | | | E-stop input (NC contact to GND) | 5 |
| | | | Alarm buzzer (2.7 kHz square wave) | 6 |
| | | | TC CS monitor channel (optional, third converter) | 3 |

Monitor channel (1.0.60): an optional third MAX31855 / MAX6675 on the shared SCK / SO with its CS on GPIO 3
(J1-13), measured only: no output, no alarm. Web: a third Channels tab (accent colour), a chart line in the
accent colour with the reading in the legend; touch: the reading at the bottom right of the chart and a
*Monitor* tab; CSV / SD logs: `monitor_pv` (+ `_min`, `_max` in the history). Settings `monitor.*` (`on`,
`name`, `sensor`, `calOffset`, `calGain`, `readMs` default 1000, `median`, `medianN`); boards without it
(CYD) keep it off. Its history costs a 4th word per live slot (24 h: 1.38 MB instead of 1 MB PSRAM)
and its own every-reading ring (1 MB).

SSR supply relay: an **active-high DC solid-state relay** (input 3–32 V DC, at most 20 mA at 3.3 V;
output at least 0.5 A DC). Input + = GPIO4, input − = GND (driven directly by the pin, no module power);
output: 5 V (J1-21) → OUT+, OUT− → the "+" inputs of both SSRs. GPIO4 is driven low (off) from the first
line of `setup()`. The relay covers a stuck SSR input or driver transistor; an SSR whose output is shorted
keeps heating — that needs a contactor in the heater supply (and the independent limiter of section 12).
Setting `safety.relay` (web Settings › Safety, touch Menu › System, serial; default on): off only when no relay is
fitted and the SSR "+" inputs get 5 V directly — GPIO4 then stays low.

Flash and watch the log through the USB port labelled **"UART"**. The native **"USB"** port is the serial
console (section 4) and the Web Serial page's USB link (section 6): open it with any terminal at
115200 baud, opening it never resets the board.

Status LED (WS2812): green = idle, blue = running, amber blinking = paused / waiting, red blinking = fault
(no answer from a channel, latched fault), purple = access point mode.

Bring-up checks: if the colours look inverted set `LCD_INVERT 1`; if touches land in the wrong place change
`TOUCH_SWAP_XY` / `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` in `board_s3.h`.

### ESP32-2432S028R "CYD" (envs `cyd-1xlocal-1x485`, `cyd-2xlocal`, `cyd-2xlocal-2x485`)

The CYD has too few free pins for RS485 plus two local channels, so it has three wirings, one env each
(diagrams on the wiki page [CYD board](docs/wiki/Hardware-CYD.md), older pictures in `claude/wiring/`, KiCad
schematics in `hardware/cyd-schematic/`, sheets A / B / C):

| Env | Channels | Wiring |
|---|---|---|
| `cyd-1xlocal-1x485` | RS485 (one or two Modbus controllers) + one local channel (bottom or top) | RS485: IO22 → DI (P3-2), IO35 ← RO (P3-3), IO27 → DE+/RE (CN1-2). Local: thermocouple SCK IO23, SO IO19, CS IO5; SSR IO18 (SD slot pins) — `cyd-wiring-a.png` |
| `cyd-2xlocal` | two local channels, no RS485 | Thermocouples SCK IO23, SO IO19, CS bottom IO5, CS top IO27 (CN1-2); SSR bottom IO18, SSR top IO22 (P3-2) — `cyd-wiring-b.png` |
| `cyd-2xlocal-2x485` | RS485 + two local channels, each channel local or Modbus. **On-board RGB LED removed** | RS485 as in `cyd-1xlocal-1x485`. Thermocouples SCK IO23, SO IO19, CS bottom IO5, CS top IO4 (LED pad R, 10 kΩ pull-up to 3V3); SSR bottom IO18, SSR top IO16 (LED pad G); status LED = external WS2812 on IO17 (LED pad B, powered from 3V3) — `cyd-wiring-c.png` |


| Part | Notes |
|---|---|
| ESP32-2432S028R (CYD) | 2.8" 320×240 ILI9341 + resistive touch (the ST7789 variant with 2 USB ports is not supported yet) |
| **3.3 V** RS485 transceiver module | MAX3485 / SP3485 based, with DE and RE pins. **Don't use a 5 V MAX485 module**: its RO output would drive 5 V into the ESP32 |
| Optional: isolated transceiver | ADM2587E / ADM2483 based module if the ground potentials differ. The KS 4x-1 bus port is already isolated, so a plain transceiver is usually fine |
| Twisted pair, screened cable | e.g. LiYCY 2×2×0.25; screen grounded at one end |
| 5 V USB supply for the CYD | |

The KS 4x-1 must be the version with the **Modbus option** (terminals 12–17 fitted, `othr/bAud`, `Addr` visible).

### RS485 wiring (Modbus, beta)

```
CYD CN1 (JST 1.25, 4 pin)         RS485 module (3.3 V)          PMA KS 40-1
  3V3 ------------------------------ VCC
  GND ------------------------------ GND ------------------------ 13 RGND (optional, via 100 Ω)
  IO22 ----------------------------- DI
  IO27 ----------------------------- DE + RE (bridge them)
CYD P3
  IO35 ----------------------------- RO
                                     A (+) ---------------------- 17 DATA A
                                     B (−) ---------------------- 15 DATA B
```

Check the silkscreen for the CN1/P3 pin order; it differs between batches. For an auto-direction module,
set `PIN_RS485_DE` to `-1` in `src/board_cyd.h` / `src/board_s3.h`.

- **Polarity:** PMA's "Data A/B" naming is reversed relative to EIA-485, but in practice PMA DATA A goes to the
  module's A(+). If there is no answer, swapping A and B is harmless.
- **Termination:** PMA recommends *no* 120 Ω terminators for fewer than 8 devices at ≤ 9600 baud (up to 1000 m).
  Many cheap modules have a 120 Ω resistor fitted; remove it if communication is flaky on a short line, and make
  sure the module has fail-safe bias resistors.
- CYD status LED: the on-board RGB LED (external WS2812 in `cyd-2xlocal-2x485`) with the same colours as the S3 (green = idle, blue = running,
  amber blinking = paused / waiting, red blinking = fault, purple = access point mode).
- Free CYD pins: only IO22 (P3-2 and CN1-3), IO27 (CN1-2, 10 k pull-up) and IO35 (P3-3, input only) are
  on connectors. The SD slot pins (IO5, IO18, IO19, IO23) are reached with a micro-SD breakout adapter
  (no SD card in the slot). IO6–IO11 are the module's flash and not usable.
- CYD alarm sound: a small 8 Ω speaker (0.5–2 W) or a passive piezo on the speaker connector P4 (IO26
  drives the on-board SC8002B amplifier; bridged output, SPK− is not ground); IO35 can't drive a buzzer (input
  only). No E-stop input and no chip temperature on the CYD (no free pin / sensor).
- CYD touch calibration: the defaults (200…3700 / 240…3800) suit most CYDs. If touches land off: serial
  `touch cal` (tap 4 crosses), or `POST /api/config {"display":{"touchCal":[minX,maxX,minY,maxY]}}` and
  restart.

## 2. Build and flash

1. Install VS Code + PlatformIO.
2. Open this folder, build and upload environment `s3` (default), `cyd-1xlocal-1x485`, `cyd-2xlocal` or `cyd-2xlocal-2x485`
   (`pio run -e s3 -t upload`).
   Check `upload_port` / `monitor_port` in `platformio.ini`. Without PlatformIO: the release page
   (section 7, Downloads) has a full image per board (`tempctl-duo-<version>-<env>-full.bin`: bootloader,
   partition table and firmware); write it at address 0x0, e.g.
   `esptool.py write_flash 0x0 tempctl-duo-1.0.55-s3-full.bin`.
3. First start: no Wi-Fi configured → the device opens access point `TempCtl-Duo-XXXX`, password `tempctl123`.
   Either set Wi-Fi on the touch screen (*Menu › Network*: network name (scan), password), over serial
   (`wifi scan`, `wifi connect`), or browse to `http://192.168.4.1` and use *Settings › Wi-Fi*.
   Afterwards: `http://tempctl-duo.local` or the address shown in *Menu › System* (info).
4. Set each channel's type (web *Channels* page or touch *Menu › Channels*): local — converter type,
   then check the reading; Modbus — check that slave address, baud rate and parity match the controller
   (`ConF/othr`), then that the channel shows its process value (Dashboard, or the reading at the top of
   the channel's settings). Local channels: run the PID autotune once (section 8).

Settings are kept in `/duo/settings.json` (LittleFS). Channel defaults: boards with RS485 (`s3`,
`cyd-1xlocal-1x485`, `cyd-2xlocal-2x485`): bottom = Modbus (address 1), top = Off; `cyd-2xlocal`: both
local.

### Firmware update without USB (OTA)

- **Web page** (any board, over Wi-Fi): *Settings › Device › Firmware update*: choose the `firmware.bin` of
  this board (PlatformIO: `.pio/build/<env>/firmware.bin`, e.g. `.pio/build/s3/firmware.bin`) and *Install*.
  The page checks the file first (ESP image of the right chip, TempCtl Duo id string with the same env:
  an image for another board / env is refused, also by the device), uploads it (about a minute), the
  device writes it into its other firmware slot and restarts.
- **SD card** (S3): copy `firmware.bin` into the card's root folder, then *Settings › Device › Check the SD
  card › Install from the SD card* (or touch *Menu › System › Firmware update*, serial `fw sd` / `fw sd install
  yes`). The check runs in the SD task, in the background (the page shows "Reading firmware.bin… n s", touch
  a message and then the dialog, serial prints the result when it is there). Images since 1.0.43 carry their
  id string right after the ESP-IDF app description (section `.rodata_custom_desc`, file offset 288), so the
  check reads only the first 16 kB (well under a second); for older images it reads the whole file (about
  30 s on the S3: the card shares the SPI bus with the display, which is slow meanwhile). Log:
  `[sd] firmware.bin checked: 1855 kB in 0.2 s`. After the install the file is
  renamed `firmware.done`.
- **Release page** (section 7): every push to `main` publishes all firmware images on
  `https://tempctl-duo.just-r0st.workers.dev`. *Settings › Device › Firmware update › Check for updates*
  (also checked by itself when Settings opens, at most every 10 min) shows whether a newer release exists;
  *Install* lets the browser download this board's image and upload it as above (the device also checks its
  MD5). Works on every board, as long as the computer or phone has internet access. The **S3** can also fetch
  it itself over HTTPS (Wi-Fi with internet): touch *Menu › System › OTA update*, serial `fw net` /
  `fw net install yes` (root certificates: `src/ca_roots.h`, `tools/gen_ca_roots.py`). The CYD leaves this to
  the web page: an HTTPS session needs about 70 kB of RAM; the S3 takes it from its PSRAM (mbedTLS allocates
  there while the download runs), the CYD has none.
- Not while a program runs or an output is on. The touch screen shows the progress; don't switch off.
- **Rollback**: the new firmware runs "on probation" and confirms itself after 30 s of normal running (loop
  and control task alive). A crash or reset before that makes the bootloader start the previous firmware
  again, which logs "Firmware update to … failed: it did not start correctly". Events: "Firmware update
  started", "Firmware … written", "Firmware updated from … to …", "Firmware … confirmed".
- **CYD: once over USB first.** OTA needs two firmware slots: the CYD envs now use `partitions_cyd_ota.csv`
  (2 × 1.875 MB firmware + 192 kB file system, instead of `huge_app.csv`). The first USB upload with this
  table **erases the file system**: back up settings and programs before (web page, *Tools › Backup*),
  restore them afterwards. Wi-Fi settings are in that backup too, so the device starts as an access point
  until they are restored. The CYD firmware is about 1.83 MB (93 % of a slot); the page and icons are
  embedded without the web fonts on the CYD (the S3 only gets those), which keeps it in the slot.
- Serial: `fw` (version, board, slot), `fw sd`, `fw sd install yes`, `fw net`, `fw net install yes` (S3).

### Backup on the SD card (S3)

Settings and all programs in one file in the card's `backup` folder, named by the device's local date and time
(`backup/2026-10-14_153012.json`; before the clock is set `start<boot>_<n>.json`). Made by hand: web *Tools ›
Backup › SD card › Back up to SD card*, touch *Menu › System › Backup to SD card*, serial `backup sd`. Restore: the web
and touch list the backups newest first; pick one, then settings and programs, settings only or programs only
(with a confirmation). Settings: all are replaced, the stored Wi-Fi passwords stay (they are not in the backup);
changed network settings are used after a restart. Programs: the slots the backup holds are replaced, the
other slots stay. Not while a program runs. The web page also downloads and deletes backups, and its file
restore (*Restore settings…* / *Restore programs…*) takes a backup file too. The file:
`{"fmt":1,"kind":"tempctl-duo-backup","fw","board","host","time","settings":{…},"programs":[{"i":0,"program":{…}},…]}`.

## 3. Touch UI

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

### Display speed (setting "Display SPI clock", `display.spiMhz`, 20 / 40 / 80 MHz, default 40)

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

### Automatic screen check (setting "Screen check", `display.screenCheck`, on by default)

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
  stays on the panel (seen all over Rost's 4" panel, most in the lower left corner). Up to 2 such reads (LVGL
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

## 4. Serial console

Commands over USB serial, **115200 baud** (S3: the port labelled **"USB"**, the log stays on "UART"; CYD:
its one USB port, shared with the log). The help is printed at the start (S3: when a terminal opens the
port) and with `help`. One command per line; commands and keys are not case-sensitive; parameters are
`key=value`, a value with spaces in quotes (`s="My Net"`). Typed characters are echoed, Backspace works
(PlatformIO's monitor or any terminal); on the CYD a log line that arrives while you type appears on its
own line and your typed text is shown again. Errors start with `error:`.

`help` lists the commands in sections (status, manual control, programs and runs, autotune, Wi-Fi,
settings, device); `help <section>` prints one of them (`help wifi`), `help settings` all settings with
their ranges and an example per section.

| Command | |
|---|---|
| `status` · `watch` · `info` · `log [n]` | per channel PV, SP, output and what it does, run state, Wi-Fi · the same as one line per second (any key stops) · firmware, uptime, memory, network · the last n events |
| `manual b\|t on [sp=<C>]` · `manual b\|t sp=<C>` · `manual b\|t off` · `ack b\|t` | manual setpoint (b = bottom, t = top); not for a channel of the running program · acknowledge a channel fault |
| `programs` · `program n=<n>` · `show` | list, select for the next run, print the selected / running program (no editing over serial) |
| `start` `pause` `resume` `stop` `next` `continue` | run control |
| `extend b\|t +<s>` · `target b\|t <C>` | extend the running hold, change the current / kept target |
| `resume run` · `dismiss run` | after "Run interrupted by a reset" |
| `save` · `discard` | the changes made during the last run |
| `tune b\|t start temp=<C>` · `tune b\|t stop` · `tune b\|t accept\|discard` · `tune b\|t` | autotune of a local channel (test temperature 50 … max − 20 °C, not during a run) · stop · take over / drop the values found · its state |
| `wifi scan` · `wifi connect n=<number> [pw=…]` · `wifi connect s=<SSID> [pw=…]` | join a network; without `pw=` the password is asked for (hidden; empty = open network). The result: `connected, IP …` (then saved) or `failed: wrong password / network not found / timeout` (the previous network is used again) |
| `wifi ap [pw=…]` · `wifi status` · `wifi forget` | start the access point (`TempCtl-Duo-XXXX`, password 8–32 characters, saved), Wi-Fi state, forget the network (access point only) |
| `wifi off` · `wifi on` | Wi-Fi completely off / on (setting `wifi.enabled`, default on; also touch *Network* and web *Settings › Wi-Fi*): off = radio off, no station, no access point, no scan, no mDNS; the touch screen and USB keep working |
| `get [b\|t\|bus\|run\|wifi\|display\|<key prefix>]` | settings as `key=value` lines, ready to be used with `set` |
| `set <key>=<value> …` | up to 8 settings at once, the keys of the web API (`b.` / `t.` = the channels). Stricter than touch / web: a value out of range is an error and then nothing is changed; a value the device adjusts because of another setting is reported. Saved at once |
| `factory reset yes` | all settings to their defaults, Wi-Fi included (programs are kept), then a restart; not while a program runs |
| `touch cal` | CYD: calibrate the resistive touch screen (tap 4 crosses; any key cancels) |
| `reboot` | restart the device (refused while a program runs, like on touch / web) |
| `screen restart` | set only the display and the touch controller up again (S3: reset pins + I2C bus freed; CYD: software reset + SPI) and redraw; prints whether the touch controller answers |
| `screen check` | what the automatic screen check sees now: display status read back, touch controller, number of automatic restarts |
| `perf on` · `perf off` · `perf` | frame rate meter: a box at the bottom right of the touch screen and a `[perf]` line every second (see "Display speed") |
| `perf simd on` · `perf simd off` | S3: the SIMD fills / copies (src/simd) on or off, to compare (on again after a restart) |
| Up / Down keys | the last 20 commands (lines with `pw=` and password entries are not kept) |
| `backup sd` · `backup sd list` · `backup sd restore n=<n> [settings\|programs] [yes]` · `backup sd delete n=<n> yes` | S3: settings + all programs to a new file on the SD card · the backups, newest first, numbered · without `yes`: what backup n holds; with `yes`: restore both parts or one · delete one |
| `api on` · `api off` | machine mode for the Web Serial page (JSON lines, below) |

**api mode** (`api on`, spec 7b): one JSON object per line, for the Web Serial page. Requests
`{"id":7,"m":"GET","p":"/api/live"}` or `{"id":8,"m":"POST","p":"/api/manual","b":{...}}` (`m`: GET,
POST, PING) go through the same handlers as the HTTP API (section 10), with the same paths, query
parameters and JSON. Answers: `{"id":7,"code":200,"body":{...}}`; text (CSV) in pieces
`{"id":9,"code":200,"type":"text/csv","chunk":"..."}` … `{"id":9,"end":true}`. Also `{"t":"hello",
"api":1,"fw":…,"board":…,"variant":…,"host":…}` after `api on`, `{"t":"bye","why":…}` at the end, and
on the CYD (shared port) the log as `{"t":"log","m":"..."}`. `api off`, or no request for 15 s, returns
to the console. Output is queued and written as the port takes it (the loop never waits for the
port); a request line may be up to 16 kB (a whole program). Try it in a terminal: `api on`, then
`{"id":1,"p":"/api/live"}`. `help api` shows a short description on the device.

Passwords are never shown (also not a `pw=` typed on the command line: shown as `*`). Run events
(phases, messages, faults, the end) are printed as they happen; "Action required" as a highlighted block,
repeated every 30 s. The PC test `test/console_host` types a script of commands and shows the output.

## 5. Web page

- Look (1.0.40): frosted glass — an accent-tinted gradient backdrop, translucent blurred sidebar and cards,
  pill buttons, channel colour strips, folder tabs on the Channels page, a title bar that fades in while
  scrolling; a channel card turns into its channel colour while the output is on. Browsers that ask for
  reduced transparency get it without the blur. Logo: `web/icons/`
  (from `logo/TempCtl.svg`).
- Chart: starts in the **Live** view (touch screen too); a run, opening or choosing a program switch to the
  program view. **Hover** (mouse; a finger on the live chart) shows a dotted line, a dot on each curve and a
  box with the time and the values (live: clock time and time before now). The live chart's time marks
  count back from now and stay put, with vertical grid lines.
- **CSV replay** page (`#/replay`): a CSV of the device - Export CSV (live history or last run), "Device
  history", "Last run", an SD run log (Log page › Replay) or any file opened / dropped - as a chart: wheel /
  two fingers zoom, drag moves, Shift + drag zooms to a range, double click shows all, hover as above, the
  legend switches curves; Crop to view + Save CSV write only the rows of that range (same columns).
- Autotune on the Dashboard and in the sidebar: Stop (running), Clear (failed / stopped), Review (done); a
  finished autotune opens a dialog with the current and the new PID values: Accept, Edit… (then Save these
  values), Dismiss, Later.
- Source: `web/index.html` (one file: HTML, CSS, JS) and the Barlow / Barlow Semi Condensed fonts in
  `web/fonts/` (woff2 subsets: Latin, Latin-1, Latin Extended-A and a few symbols; SIL Open Font License,
  `web/fonts/OFL.txt`). The page works without internet (AP mode): everything comes from the device.
- `tools/web_embed.py` gzips the page and writes `src/web_assets.h` (generated, not in git). PlatformIO
  runs it before every build (`extra_scripts` in `platformio.ini`); by hand: `python tools/web_embed.py`.
  The page is served gzipped (≈ 40 kB), the fonts (≈ 90 kB) with a one-year browser cache.
  Boards without PSRAM (CYD) serve the page without the web fonts (system fonts; RAM is short there).
- Icons: `web/icons/` (`logo.svg` = favicon and sidebar logo, `favicon.ico` 16/32/48 px,
  `apple-touch-icon.png` 180 px), made from the logo in `logo/` (SVG cleaned up for size, PNGs rendered
  from it); served at `/favicon.svg`, `/favicon.ico`, `/apple-touch-icon.png` (≈ 10 kB in all).
- **Dashboard** (was "Status"): run panel (program, Start / Pause / Continue / Stop / Next phase, state, run time,
  remaining time), chart (both channels: temperatures, setpoints, state lanes, phase lines with times,
  pauses, the forecast of the rest of the run dashed; zoom − / + / Fit, Ctrl + wheel, drag, pinch;
  "Live" shows the live history over a chosen period, see below), channel cards (PV, SP, output, what the channel does, target
  −5 / −1 / +1 / +5 / Set and Extend hold during a run, manual switch + setpoint otherwise, fault +
  Acknowledge; outputs on: 4 px border and 20 % tint in the channel colour, like the touch tiles; a fault
  waiting for Acknowledge: a blinking red warning badge on the card's top edge, also on the touch tiles),
  the "Action required" overlay, "Save the run's changes?" after a run with run edits, and the event
  log. After a run the chart shows it with **Export CSV** (the run's PV / setpoint / plan) and **Back to
  preview**. The jump buttons in the title bar (Run, Channels, Program, Log) appear only when the page is
  one column wide. Channel colours come from the device's accent colour (spec 6); dark mode switch in the
  sidebar (follows the system until switched; remembered by the browser; colours fade over).
- Sidebar, on every page: the pages, then the run (program, state and time, Start / Pause / Continue /
  Resume and Stop) and both channels (PV and what they do, manual on / off), the SSR supply relay
  (S3), then the connection, address, clock and dark mode — sections apart with a thin line.
- Program editor (under the channel cards): the selected program as tables (Sequence: one row per
  phase, cards on narrow screens; Timeline: two tables side by side) and on the chart (drag squares up /
  down = target, diamonds = ramp end, circles = hold end; whole seconds and °C). Outside a run edits go
  to a draft in the browser (Save / Discard / Undo / Redo, Ctrl+Z / Ctrl+Y; the program list marks
  unsaved programs; drafts are lost when the page is reloaded). Empty slot: New sequence / New timeline.
  **Convert to Timeline** (Sequence, outside a run): the same temperatures and times as a Timeline — a
  channel that keeps its target gets its hold lengthened, a Both phase's bottom jump becomes a ramp-0
  step, a Pause phase a pause step; "wait for bottom" is dropped (the warning lists the phases). It asks
  whether to replace the program (a draft: Undo brings the Sequence back until it is saved) or to create
  a new program in the first empty slot. A Timeline can't be turned back into a Sequence.
  Start with unsaved changes asks: Save and start / Start without saving / Cancel. During a run the same
  tables and handles edit the running copy (targets, later ramps, holds; finished phases and the running
  ramp are locked) and the changes are sent to the device at once.
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
- Pages (sidebar; addresses `#/programs`, `#/channels`, `#/log`, `#/settings` (old `#/system` too), `#/tools`, so a page can be
  bookmarked; all on the one page file, the drafts stay while switching pages):
  - **Programs**: the 20 slots with type, channels, phases / steps and time; Open (in the Dashboard
    editor), Duplicate (into the first empty slot), Export (the program's JSON file), Delete; empty
    slots: New sequence / New timeline / Import (a program JSON file).
  - **Channels**: "All channels" tile: polling interval, link-loss pause, "bottom reached" band. Below it the channel tile with one tab
    per channel (name, colour dot, type), its subsections apart with a thin line:
    name, type, end action, setpoint limits; Modbus: address, controller type (+ load register defaults),
    setpoint ramps, front lock, RS485 bus (beta; one bus, shown in the tile of each Modbus channel),
    register map, PMA parameters and actions; local: converter, calibration (with a two-point helper),
    PID, SSR window, max. output / temperature and the PID autotune; Protection: runaway limits, heating
    while off, frozen reading (of the selected type) and, local, converter too hot. Changed fields are marked; Save sends only the changed values
    (`POST /api/config`) and lists values the device had to adjust to the allowed range.
  - **Log**: the SD run logs (S3, below) and the system log (below).
  - **Settings** (tiles fill the free space; jump buttons Device / Wi-Fi / Safety / Display / Clock in the
    title bar when it is one column): device info, restart, restart of the screen only (display
    and touch controller set up again, the device keeps running), firmware update (file, SD card, Check
    for updates, section 2), Wi-Fi (with network scan, show / hide password, optional fixed IP address),
    touch display (applied at once, no Save button: light / dark, brightness slider — the screen follows
    it while it is dragged —, accent colour: a preset or "Custom" with the browser's colour picker,
    rotation, screen check, SPI clock),
    safety (section 12.1: manual-mode timeout, E-stop input, alarm sound with a test beep, chip
    temperature warning, with the E-stop contact and chip temperature live), the clock (time zone, below).
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
    **Clock** (Settings page): the time zone for local times (SD run logs): set from the browser the first time the
    page is opened (its zone if the device's list has it, else its current offset as `UTC+hh:mm`),
    changeable here, on the touch screen (Menu › System › Time zone: region, then place) and over serial
    (`set time.zone=Europe/Berlin`, `help time` lists the zones). Summer time follows the zone's rules.
  - **Tools**: Modbus tools for a Modbus channel (find the setpoint address, read registers, write one
    register); backup / restore of the settings and of all programs as JSON files; S3: backups on the SD
    card (back up, list, restore all / settings / programs, download, delete; section 2).

## 6. The page over USB (Web Serial) and the local app

The same page also works **without Wi-Fi, over the USB cable** (spec 7b): it then talks to the device
through the serial `api` mode (section 4) instead of HTTP — same screens, same functions. This needs
**Chrome or Edge** on a computer (the Web Serial API), and the page must come from `localhost` or https,
so it is not opened from the device but from the **local app**:

- **`tempctl-duo-<version>.exe`** — one portable file, nothing to install. It serves the page on
  `http://localhost:8765` (fixed port: the browser remembers the USB permission for it) and opens it
  in Chrome or Edge as an app window; a second start only opens another window; it ends by itself
  30 s after the last window was closed. Built with `tools/launcher/build.sh` (Go 1.21+; `all` also
  builds macOS and Linux versions). Windows may warn about an unknown publisher the first time.
- The page starts with a **connect screen**: **USB cable** → "Connect…" → choose the port (S3: the port
  of the "USB" connector; CYD: its only port). The page sends `api on`, checks the API version from the
  device's hello and then works as usual (`Connected over USB` at the bottom left, **Disconnect USB**
  next to it). Or **Wi-Fi**: the local app lists the devices it finds on the network (click one), or
  enter the address (`tempctl-duo.local` or the IP) → the device's own page.
- **Device search** (local app): mDNS queries for `_tempctl._tcp` (announced since 1.0.27 with fw /
  board / api) and `_http._tcp` (older firmware) on every network of the computer, each answer checked
  by reading its `/api/live`; the list refreshes every 3 s while the connect screen is shown. Windows
  asks once whether the app may use the network ("Allow access", private networks) — without it the
  list stays empty and the address field still works.
- **Tabs** (like Windows Terminal): one device per tab — "+" opens a new tab with the connect screen,
  × (or a middle click) closes one; the icon shows how it is connected (USB / Wi-Fi), the dot whether
  the device answers. A device opened over Wi-Fi shows **Disconnect** at the bottom left (back to the
  connect screen in that tab). The tabs come back at the next start (USB tabs connect again to their
  port). The window is `shell.html` (served at `/`), the page in each tab `/app?tab=<n>`.
- Close a serial monitor (PlatformIO, Arduino) first: only one program can use the port.
- The connection comes back by itself: when the page is opened again (the last port is remembered
  until "Disconnect USB") and when the cable is plugged in again or the device restarted. A lost
  connection never touches a running program; after 15 s without the page the device returns to the
  human console. CSV downloads work over USB too.
- The device's own copy of the page is marked (`data-device`, by `tools/web_embed.py`), the launcher's
  copy `data-launcher`; without a mark (the hosted page) the page shows the connect screen.
- **Hosted page** (Cloudflare Workers static assets, no install at all): `https://tempctl-duo.just-r0st.workers.dev`,
  Chrome / Edge. The same connect screen: USB (Web Serial) or Wi-Fi = open the device's own `http://` page (an
  https page may not call the device's http API itself). No device search and no tabs (those need the local
  app). Below them: **Downloads** of the newest release (desktop app for Windows, macOS, Linux; firmware of
  every board), section 7; the local app shows the same list and says when a newer app exists.

## 7. Releases (GitHub Actions → Cloudflare)

Every push to `main` (pushes that change only documentation are skipped; *Actions › Release › Run workflow*
starts one by hand) runs `.github/workflows/release.yml`:

1. PlatformIO builds the four envs; per env the update image (`firmware.bin`) and a full image for the
   first USB flash (`esptool merge_bin`: bootloader, partition table, boot_app0, firmware; write it at
   address 0x0).
2. `demo/build.sh` with the wasi-sdk: the browser demo (the firmware's control code as WebAssembly, `demo/README.md`).
   `tools/launcher/build.sh all`: the desktop app for Windows, macOS (Apple silicon, Intel; zipped) and
   Linux (tar.gz).
3. `tools/build_site.py dist --web` (the page, `_headers`, the wiki under `/wiki/`, the demo under `/demo/`), then `tools/release.py dist art`: the files go to
   `dist/dl/` with the version in their names, plus `dist/dl/manifest.json` (version, date, commit; per board
   url, size, MD5, SHA-256; the app files). It stops if an image does not carry its env and this version.
4. `npx wrangler deploy --tag v<version> --message "<first line of the commit>"` (`wrangler.jsonc`, assets
   from `dist/`): Cloudflare's deployment list shows the version and what changed (`release_local.sh`:
   "<version> local build from <commit>"). The files are also kept 14 days as a run
   artifact.

Setup, once: GitHub › repository › *Settings › Secrets and variables › Actions*: `CLOUDFLARE_API_TOKEN` (a
Cloudflare API token from the template "Edit Cloudflare Workers") and `CLOUDFLARE_ACCOUNT_ID`; in Cloudflare
*Workers & Pages › tempctl-duo › Settings › Build*: disconnect the Git repository (Workers Builds would
otherwise deploy the page without the release files). `/dl/*` is served with `Access-Control-Allow-Origin: *`
(the device's own page reads it) and `Cache-Control: no-cache`. A private repository has 2000 free Actions
minutes a month; one release takes about 10–15.

**Without commit / push** (a test release from the PC, same steps, your own Cloudflare login): in the
PlatformIO terminal, Git Bash: `bash tools/release_local.sh` (needs Node.js for `npx wrangler`, the first time
`npx wrangler login`; Go for the desktop app, else `--no-app`; `--dry` builds `dist/` only).

## 8. Channels, programs and runs

### Local channels

Type `local`: thermocouple converter MAX31855 or MAX6675 (`local.sensor`) read every `readMs` (100 … 1000 ms,
default 250; MAX6675 from 250 ms), optionally through a median filter over the last `medianN` readings
(`median`, 3 / 5 / 7 / 9, default on, 3; the PID, the protections and the autotune all see this filtered,
calibrated value), calibration `true = raw * calGain + calOffset`, PID (`kp`, `ki`, `kd`, or from the autotune
(time limit `tuneMaxMin`, default 70 min): relay test around a test temperature, Tyreus–Luyben rule, or with
`tuneFast` the same Kp / Td with Ti = 0.7 Pu (faster, a little overshoot; switch in the autotune section); started
from the touch Channels screen, the web Channels page or serial `tune`) and a time-proportional SSR output
(`windowSec`, default 1 s; `maxOut` %). Safety: SSR pins low from the first line of `setup()`, SSR off when
the task stops refreshing it (1.5 s), and latched faults (outputs off, run stopped) for no reading (3 bad
reads while on), above `maxTemp` and the runaway checks. Modbus channels get the same runaway checks with the
controller's output (Ypid); a fault switches the controller's outputs off (C.Off). `/api/status` →
`channels[].fault`, `POST /api/ack` clears it.

### Programs (spec 9.3)

- **One program drives both channels.** Up to 20 programs, name up to 31 characters, stored as JSON files
  `/duo/prog0.json` … `/duo/prog19.json` on LittleFS (safe save: temp file + rename). Two types:
  - **Sequence**: up to 100 phases, one after another. `B` (bottom: target, ramp, hold), `T` (top: target,
    ramp, hold), `BT` (both: the bottom setpoint jumps to its target or goes Idle = outputs off, the top
    ramps and holds; or the top goes Idle and the bottom ramps and holds; or both Idle: both outputs off for
    the bottom's hold time, JSON `"b":{"idle":true,"h":…},"t":{"idle":true}`), `P` (pause: waits for Continue, optional "Action required" and a text up to 40
    characters). `B` and `BT` phases can **wait for the bottom** to reach its target (± the "bottom reached"
    band, default 5 °C) before the hold (`B`) or the top ramp (`BT`) starts. A channel keeps its last
    target while other phases run.
  - **Timeline**: two independent step lists (up to 100 steps each) started together: target steps
    (target, ramp, hold), `idle` steps (outputs off for a time) and `pause` steps (the whole run pauses).
    A channel that finishes earlier keeps its last target until the other one is done.
- The first ramp of a channel, and the first ramp after Idle, starts at its measured temperature; later
  ramps start at the previous target. A ramp of 0 is a jump. A channel without phases/steps in the program
  stays under manual control during the run; the program's channels start with their outputs off until
  they get a target.
- **Next phase** (Sequence): ramp → hold (setpoint at the target, a wait is skipped), wait → hold / top ramp,
  hold → next phase; in a pause it continues. Timelines have no Next phase (only Continue in a pause).
- **Run edits** (`POST /api/run`): extend or change a hold (not below the time already held; down to now
  ends it), change later ramp times, change targets (current, later, or the target a channel keeps). Slopes
  are kept and times adapt; in a Timeline a timing change of one channel shifts the other one through its
  holds. The edits go to the running copy; `{"cmd":"save"}` stores them into the program.
- End: each channel of the program applies its own end action (keep setpoint / end setpoint / outputs off).
  With "keep" or "end setpoint" the channel goes on in manual mode with that setpoint (its manual button
  is on and switches the outputs off).
- Resume after a reset: a ramp starts at the measured temperature with its planned speed (from the
  previous target to its own in its ramp time); Sequence: the phase's ramp takes as long as that needs;
  Timeline: the hold after the ramp takes up the difference, so later steps keep their run time.
- **Seconds:** times are stored in seconds. With *Controller ramps (r.SP)* the KS 4x-1 gradient is per
  minute, so a short ramp only uses the controller gradient if it fits the register; otherwise
  the ESP32 steps the setpoint itself (section 9).

### Manual setpoint

- Each channel has a manual setpoint and an on / off switch (touch tile, web Dashboard card and sidebar,
  serial `manual`); not for a channel of the running program. **On:** local: the PID drives the SSR to
  the setpoint; Modbus: the ESP32 switches the controller outputs on and writes the setpoint (again
  whenever it changes). **Off:** local: SSR off; Modbus: outputs off (`C.Off` = 1); without an
  outputs-off register *off* only stops the writes and the controller keeps its last setpoint.
- Manual mode is always off after a restart; the last value is remembered. Optional timeout: section 12.1.

### Pause, end, faults, reset

- Pause freezes program time (Modbus: the controller ramps too).
- End / stop action per channel: keep setpoint, set an end setpoint, or outputs off. Modbus: the original
  `r.SP` is restored afterwards.
- If a channel of the program doesn't answer for 30 s (link-loss pause, Channels page › All channels) the
  program pauses. A fault on any channel stops the run.
- **Run interrupted by a reset** (watchdog, crash, brown-out, power loss …): the board starts with the
  program's channels off (local: SSR off; Modbus: outputs-off register, if set — without it the controller
  keeps its last setpoint), and the touch screen and the web page show "Run interrupted by a reset" with
  the reason, program, phase / steps and run time. Never an automatic resume: **Resume** (Sequence: the
  interrupted phase from its beginning, ramps from the measured temperatures, channels the phase doesn't
  start get their kept target at once; Timeline: on at the interrupted run time, ramps from the measured
  temperatures in the time they had left, holds with their remaining time), **Later** (touch: closes the
  overlay, the red "Interrupted" badge opens it again) or **Dismiss** (outputs stay off, the controller
  gradient and front panel are restored). Starting another program also ends the offer. Serial:
  `resume run` / `dismiss run`. The running copy (with run edits) is in `/duo/run.json`; the position in RTC
  memory (exact, survives resets) and in `/duo/runpos.json` (every phase / step change and every minute,
  for a power loss).

## 9. Modbus controllers (beta)

A Modbus channel leaves the PID to the controller; the ESP32 writes its setpoints, reads PV, setpoint
and output, and can switch its outputs off. Wiring: section 1. Set it up on the web page's *Channels*
page (controller type PMA KS 4x-1 or Generic Modbus RTU, address, RS485 bus, register map).

### What the PMA interface description tells us

| Item | Value (KS 4x-1) |
|---|---|
| Physical | RS485 2-wire or RS422 4-wire, galvanically isolated **option** on the controller |
| Terminals (RS485) | 17 = DATA A, 15 = DATA B, 13 = RGND |
| Protocol | Modbus RTU, slave only, address 1…247 |
| Serial | 2400/4800/9600/19200 baud (menu), 8 data bits, even/odd/none parity. **Factory: 9600, even parity, address 1** |
| Function codes | 03/04 read (identical), 06 write single, 16 write multiple, 08 diagnostics |
| Address format | top 3 bits select format: `base` = integer, `base + 8192` = integer with 1 decimal ("1dP"), plus 2dP/3dP/float areas |
| Special raw values | −31000 sensor fault, −32000 switched off, −32500 undefined, −32768 out of range |

Registers used by default (1dP = value × 10):

| Purpose | Name | Address |
|---|---|---|
| Process value | Cntr/Signal `In.1` | 13294 |
| Effective setpoint | Cntr/Signal `SP.EF` | 13293 |
| Output % | Cntr/Signal `Ypid` | 13295 |
| Controller status | `St.Cntr` (bit 0 heating, bits 12–15 mode) | 5100 |
| Alarm status | `St.Ala` | 250 |
| Outputs off | LOGI/Signal `C.Off` | 1086 |
| Front panel lock | LOGI/Signal `L-R` | 1080 |
| Setpoint (interface) | SEtP/Signal `SP` | 11372 |
| Setpoint limits | SEtP/PArA `SP.LO` / `SP.Hi` | 11292 / 11293 |
| Setpoint gradient | SEtP/PArA `r.SP` (per minute, 0.01…9999 or off) | 11295 |

**Why the ESP32 runs the program:** the KS 4x-1's built-in programmer (ProG, addresses 14292…14299) has only
**4 segments** (`SP.01…SP.04`, `Pt.01…Pt.04`), so longer profiles have to be driven from outside by writing `SP`.

**EEPROM:** the interface description says the `SP` value in RAM is updated immediately, while storing it to
EEPROM is timed (about one write per half hour). Frequent setpoint writes over Modbus are therefore safe on
the KS 4x-1. This is controller-specific; cheaper controllers may write every change to EEPROM.

**Setpoint limits:** the controller clamps setpoints to `SP.LO…SP.Hi` (factory 0…900). Before starting, the
firmware reads both and refuses to start if an enabled step's target lies outside them.

### Setpoint ramp modes

- **Controller ramps (r.SP)**, default: per segment the ESP32 writes the start value, the gradient
  (|ΔT| / remaining minutes) and the target. About 4 writes per phase; the controller ramps smoothly.
  Ramps slower than 0.1 °/min (the 1dP resolution) or faster than the register can hold (3000 °/min at 1dP)
  fall back to host stepping for that segment.
- **ESP32 writes setpoint steps**: writes a new setpoint when it has moved by *minimum step* (default 0.5)
  and at least *minimum seconds* (default 5) have passed. Works with any controller.

### Other controllers, e.g. the LCTC M226-TE

**Electrically and at protocol level: yes.** It is advertised with RS485 Modbus RTU, so the same transceiver and
the same Modbus master in this firmware can talk to it (it almost certainly uses FC 03 and 06).

**Register-wise: no, and I could not find its register map.** LCTC's TE-M226 product page and catalog don't publish
a Modbus table. PMA addresses (13294 etc.) will not work on it. To use it:

1. Get the communication manual from the seller/LCTC (address list for PV and SV, scaling, serial format).
2. On the web *Channels* page set the channel's controller type to *Generic Modbus RTU*, enter
   baud / parity / address as set on the controller.
3. Enter the PV and SV addresses (*Tools → Find the setpoint address* helps once PV/SV reads work) and the divisor (1 or 10). If you only know the front-panel values, use
   *Tools → Read registers* over 0…60 and look for the PV and SV numbers (e.g. SV 148 appears as 148 or 1480).
4. Use *ESP32 writes setpoint steps* (it probably has no gradient register) and keep *minimum step* ≥ 1 and
   *minimum seconds* ≥ 30 to spare its EEPROM.

PMA-specific extras (status word, self-tuning, outputs off, parameter page) won't apply unless you map equivalents.

## 10. Web API

The flat fields of `/api/status` (kept from TempCtl 1.2) show the **bottom channel**; everything else
covers both (`ch` = 0 bottom, 1 top). The serial console's api mode (section 4) reaches the same paths.

**Request checks** (no password, but other web pages must not reach the device): every **POST needs the
header `X-TempCtl: 1`**, otherwise 403. A browser only sends such a header cross-site after a CORS check,
which the device never answers, so a script on some other website open on a PC in the workshop can't
switch anything. Own scripts add it, e.g.
`curl -H "X-TempCtl: 1" -H "Content-Type: application/json" -d '{"sp":150}' http://tempctl-duo.local/api/manual`
(PowerShell: `-Headers @{"X-TempCtl"="1"}`). The **`Host` header** must be the device: an IP address, its
host name, or the host name with a local suffix (`.local`, `.lan`, `.home`, `.fritz.box`, … see
`src/host_check.h`); other names get 403 (DNS rebinding), each refused name is logged once. The serial
console's api path has no such checks.

| Method | Path | Body / query |
|---|---|---|
| GET | `/api/live` | web page, polled every second: both channels (pv, sp, out, outOn (outputs on), state, target, kept, canExtend, manual, fault, autotune, …), run (state, el, rem, phase / steps, progPause, actionRequired, pauseText, edited, msg; `intr` while a run interrupted by a reset can be resumed: reason, exact, el, i / st / left per channel), `sel`, `accent`, `progGen`, `cfgGen` (changes with every settings save), `logSeq`, `sysSeq` (newest system log line), `boot` (start number), `time` (device clock, UTC s; 0 = unknown), `tz` (time zone, "" = not set), `sd` (S3: card, logging, file, err); `mon` while the monitor channel is on: name, pv, raw, cold, err; `?a0=&a1=[&a2=]` (device millis, as `/api/raw`): also `raw` [[ms, pv in 0.1 °C or null, …], …] = the PV readings after them, up to 32 per channel (a2: a third array, the monitor's; more: `rawMore`, the page then uses `/api/raw`) |
| GET | `/api/chart?v=run` | current / last run: 300 bins of `pv`, `sp` and state lanes `st` per channel, past events `ev` [t, type, ch, idx], forecast `fc` (`seg` [t0, t1, v0, v1, src, flags] per channel, `ev`; times from now) |
| GET | `/api/chart?v=live` | the live period (`display.liveMin`) in 300 points of `pv` / `sp` / `dlo` / `dhi` per channel; `bin` s per point, `span`, `sh` (the points before the newest end `sh` s later than whole bins before now), `have` / `cap` (s recorded / s the history holds), `ms` (device millis), `raw` (readings kept per channel; run view: also `ra` / `rn`, run time against millis). Both views: while the monitor channel is on, `pv` / `dlo` / `dhi` have a third row (its PV) and `mon` its name |
| GET | `/api/raw?a0=&a1=[&a2=]&win=&max=` | every PV reading of both channels after `a0` / `a1` (device millis), at most `win` s old, `max` (8192) per channel. Binary, little-endian: uint32 now, n0, n1, 1; then 8-byte records (with `a2` on a board with the monitor channel: uint32 now, n0, n1, 2, n2, then n0 + n1 + n2 records) uint32 millis, int16 PV in 0.1 °C (−32768 = no reading). Wi-Fi only. The web chart draws these; S3 keeps 131 072 per channel (2 MB PSRAM: 36 h at 1 s polling, 9 h at 250 ms), CYD 600 (10 min at 1 s) |
| GET | `/api/log?since=n` | event log entries after n: [seq, uptime s, run time s or null, text, kind, epoch] (kind 1 event, 2 run, 3 warning, 4 error; epoch UTC s, 0 = clock unknown) |
| GET | `/api/syslog?since=n` | system log lines after n (about 12 kB per answer, `more`: ask again): [seq, uptime s, epoch, kind (0 technical … 4 error), text]; also `first`, `boot` |
| GET | `/api/sd` | SD run logs (S3): `slot`, `card`, `logging`, `file`, `err`, `totalKb`, `freeKb`, `files` [[name, bytes, mtime], …] newest first |
| GET | `/api/sd/file?name=` | one run log (CSV; up to 4 MB through the web) |
| POST | `/api/sd/delete` | `{"name":…}` deletes a run log (not the one being written) |
| GET | `/api/backup` | SD backups (S3): `slot`, `card`, `err`, `totalKb`, `freeKb`, `files` [[name, bytes, mtime], …] newest first |
| POST | `/api/backup` | `{}` writes a new backup → `{"ok":true,"name":…}` |
| GET | `/api/backup/info?name=` | what a backup holds: `time`, `fw`, `settings`, `programs` (count), `slots` ("1, 2, 5"), `mask` (bit k = slot k) |
| POST | `/api/backup/restore` | `{"name":…,"settings":true,"programs":true}` → `{"ok":true,"msg":…}` (or `err`; not during a run) |
| POST | `/api/backup/delete` | `{"name":…}` deletes a backup |
| GET | `/api/backup/file?name=` | the backup file (JSON) |
| GET | `/api/eventlog.txt` | the events saved in flash, all starts: lines `boot \t epoch \t uptime \t kind \t text` |
| POST | `/api/time` | `{"t":<UTC s>}`: sets the device clock if it has no NTP time (the page does it; for the log); `{"zone":…, "fixed":"UTC+02:00"}`: the time zone, only while none is set |
| GET | `/api/timezones` | `{"zones":[…]}`: the names `time.zone` takes (besides `UTC+hh:mm`) |
| GET | `/api/status` | live values (bottom), manual setpoint, selected program; `channels`: both channels (name, type, ok, err, pv, sp, out, mode, manual); `run`: state, `type` (`seq`/`tl`), `seq` (phase `idx`, `stage`, `el`, `dur`), `waiting`, `progPause`, `actionRequired`, `pauseText`, `elapsed`, `remaining`, `edited`, `channels[]` (inProgram, idle, sp, target, moving, Timeline `lane`) |
| GET/POST | `/api/config` | the whole settings document (= `/duo/settings.json` without passwords) + `limits` (`rs485`, `localMax`, `monitor` of this board variant). POST changes only the keys given, e.g. `{"channels":[null,{"type":"modbus","modbus":{"addr":2}}]}`; `"modbus":{"loadDefaults":true}` loads the register map of the channel's controller type; errors come back as `{"ok":false,"err":"channels[1].type: one of …"}` |
| GET | `/api/log.csv` | last program run (or, without one, the live history) as CSV, both channels (+ `monitor_pv` while the monitor channel is on) |
| GET | `/api/history.csv` | the whole live history, streamed: time_s (before now), uptime_s, then per channel pv, min, max, sp (the monitor channel: pv, min, max); `?pv=3&sp=3` = channels (bit 0 bottom, bit 1 top, pv bit 2 the monitor) whose PV / SP columns are included (default all; rows without any value are left out) |
| GET | `/api/programs` | all 20 slots: `used`, `name`, `type`, `steps` (phases / steps), `total` (s), `uses` [bottom, top], `waits`; `sel` |
| GET | `/api/program?i=0` | `used`, `program`: the program JSON (spec 9.3.5) |
| POST | `/api/program` | `{"i":0,"program":{…}}` save (validated, setpoint limits checked) or `{"i":0,"delete":true}` |
| GET | `/api/run` | `run` (as in `/api/status`) + `program`: the running copy with its run edits |
| POST | `/api/run` | `{"cmd":"start","i":0,"program":{…}}` (start slot 0 with this unsaved content), `{"cmd":"extend","ch":0,"sec":30}`, `{"cmd":"hold","ch":1,"idx":4,"sec":45}`, `{"cmd":"ramp","ch":1,"idx":5,"sec":40}`, `{"cmd":"target","ch":0,"idx":-1,"t":170}` (`idx` = phase / step, -1 = current / kept), `{"cmd":"save"}`, `"next"`, `"continue"`, `"recover"` (resume the run interrupted by a reset), `"dismiss"` (dismiss it); the result shows in `run.msg` |
| POST | `/api/select` | `{"i":index}` select the program for the next run (Dashboard, touch) |
| POST | `/api/manual` | `{"sp":150}` and/or `{"on":true}`, optional `"ch":1` (top; default 0 = bottom) |
| POST | `/api/autotune` | local channels: `{"ch":1,"cmd":"start","sp":200}`, `"stop"`, `"accept"` (proposed kp/ki/kd → settings), `"discard"`; progress and result in `/api/status` → `channels[].autotune` |
| POST | `/api/ack` | `{"ch":0}` acknowledge a channel fault (runaway, sensor, over temperature, heating while off, frozen reading, converter too hot); refused while the cause is still there. The outputs stay off until switched on again |
| GET | `/api/ota` | firmware update: `board`, `version`, `slot`, `slotKb` (0 = no second slot, `why`), `canStart` / `why`, `probation`, `sd` (SD slot), `status` {`phase` idle / running / done / failed, `source`, `done`, `total`, `version`, `msg`}; `/api/live` → `ota` while one runs or the new firmware is on probation |
| POST | `/api/ota?size=<bytes>[&md5=<hex>]` | multipart upload of a `firmware.bin` (field `fw`, Wi-Fi only): written while it arrives (with `md5`: checked at the end), `{"ok":true,"version"}` or `{"err"}`; the device restarts 2 s later |
| GET / POST | `/api/ota/sd` | S3: `?start=1` starts the check of the `firmware.bin` in the SD card's root; then `{"busy":true}` until its result (`found`, `sizeKb`, `board`, `version`, `problem`, or `err`) / POST: install it (progress in `/api/live` → `ota`) |
| POST | `/api/beep` | `{}` one short test beep (Settings › Safety). `/api/live` → `safety`: `estop`, `contact` (open / closed, S3), `chipC`, `chipHot`, `muted`; `/api/config` → `limits.estop / sound / chipTemp` |
| POST | `/api/cmd` | `{"cmd":"start"|"pause"|"resume"|"stop"|"next"|"continue", "prog":index}` (`skip` = `next`) |
| POST | `/api/wifi` | `{"ssid","pass","apPass"}`, reconnects without restart |
| GET | `/api/wifiscan` | `?start=1` starts a network scan; then `{"busy":true}` until `{"networks":[…]}` (strongest first) |
| POST | `/api/brightness` | `{"v":128}` touch screen brightness while the web slider moves (not saved) |
| POST | `/api/restart` | restarts the device (refused while a program runs) |
| POST | `/api/screen/restart` | sets the display and the touch controller up again (reset + init), redraws; the device keeps running (result in the serial log) |
| GET | `/api/mb/read?addr=&n=&fn=&ch=` | raw registers (`ch`: channel whose controller address is used, default 0; also in the POST tools below) |
| POST | `/api/mb/write` | `{"addr","value"}` raw |
| POST | `/api/mb/findsp` | `{"from","to"}`: starts the scan in the background (at most 20 s) → `{"busy":true}` |
| GET | `/api/mb/findsp` | `{"busy":true}` while it runs, then `ok`, `err`, `reference`, `candidates`, `stoppedAt` (the time limit stopped it there: scan on from the next address); `{"none":true}`: none started |
| GET/POST | `/api/params` | PMA parameters (`?ch=` / `"ch"`). GET reads them in the background: `{"busy":true,"done","total"}` until all are read (ask again), then `supported`, `params`, `actions` |
| POST | `/api/action` | `{"i":index}` PMA actions (self-tuning, outputs off, …) |

The web UI has no login. Keep it on a trusted network.

## 11. Project layout

```
platformio.ini            environments s3, cyd-1xlocal-1x485, cyd-2xlocal, cyd-2xlocal-2x485; libraries
partitions_cyd_ota.csv    CYD flash layout: two firmware slots (OTA) + 192 kB file system
include/lv_conf.h         LVGL 9.6 configuration (only what differs from LVGL's defaults)
include/lv_blend_esp32s3.h, src/simd/  S3: Espressif's SIMD fills / copies for LVGL (Apache-2.0, esp-bsp)
include/lv_assert_custom.h  LVGL assert: backtrace + restart instead of a frozen screen
src/main.cpp              start-up, loop (S3 loop stack 16 kB, a warning in the log below 2 kB free)
src/version.h             product name, version, host / AP name
src/config.h              selects the board header, chart size
src/board_s3.h, board_cyd.h  pins, display, touch and LED per board
src/lcd.*                 display: LVGL's ST7796 / ILI9341 driver on ESP-IDF spi_master with DMA, frame rate meter
src/lv_mem_psram.c        S3: LVGL's memory in PSRAM
src/touch.h, touch_ft6336.cpp, touch_xpt2046.cpp  touch drivers (own FT6336U driver / XPT2046_Touchscreen)
src/status_led.*          status LED (WS2812 / single LED)
src/duo_program.*         programs (Sequence / Timeline): model, JSON, limits, examples (pure C++, unit-tested)
src/run_engine.*          one run engine for both types and channels, run edits, plan forecast (pure C++, unit-tested)
src/program_store.*       20 programs as JSON files on LittleFS
src/modbus_frame.h        Modbus RTU framing + CRC (pure C++, unit-tested)
src/modbus_rtu.*          half-duplex Modbus master (UART1 on the S3, UART2 on the CYD)
src/controller.*          register access of one controller on the shared bus, PMA parameter/action tables
src/settings.*            settings of both channels: defaults, validation, JSON (pure C++, unit-tested)
src/settings_store.*      /duo/settings.json on LittleFS (safe save)
src/channel.*             channel interface: Modbus backend (+ runaway check), local, off
src/local_ctrl.*          local channels: task reading the thermocouples, PID, SSR time proportioning, safety
src/tc_sensor.*, pid.*, runaway.*, autotune.*  MAX31855/MAX6675 decoding, PID, runaway check, relay autotune
                          (pure C++, unit-tested)
src/app_state.*           control task: polling both channels, run engine, manual setpoint per channel, chart history
src/raw_hist.h            every PV reading for the web chart (pure C++, unit-tested)
src/syslog.*              system log (RAM ring) + events saved to flash
src/crumbs.*              breadcrumbs for watchdog resets and crashes
src/net.*                 Wi-Fi, access point, mDNS
src/tui*.cpp, tui.h, ui.h touch UI of both boards (480×320 S3, 320×240 CYD), section 3
src/fonts/                touch UI fonts (tools/lv_font_gen.py from tools/fonts/*.ttf)
src/web.*, host_check.h    HTTP server, JSON API, request checks
src/safety.*              E-stop input, alarm sound, user activity, chip temperature (section 12.1)
src/ota.*, ota_image.*    firmware update (web upload, SD card), image check (pure C++, unit-tested), rollback
src/psram.h               PSRAM_VAR: large static variables in PSRAM on the S3 (plain statics on the CYD)
src/ota_net.*, release.*  release page: manifest (pure C++, unit-tested), S3 download over HTTPS
src/ca_roots.h            root certificates for it (tools/gen_ca_roots.py)
src/sdlog.*               SD card (S3): run logs, firmware.bin, backup files
src/backup.*              backup of settings + programs on the SD card (S3), restore
hardware/s3-schematic/    KiCad schematic of the S3 device (+ PDF / PNG)
hardware/cyd-schematic/   KiCad schematics of the three CYD wirings (+ PDF / PNG)
hardware/gen/             generator of both (Python 3: python3 make.py ..)
web/, tools/web_embed.py  web page + fonts → src/web_assets.h (generated before each build)
tools/build_site.py       the same page as static files (device URL layout), for the launcher / the hosted page
wrangler.jsonc            the hosted page: Cloudflare Workers static assets (dist/, built by build_site.py --web)
tools/release.py          release files + dl/manifest.json for the hosted page (section 7)
tools/release_local.sh    the same release from a PC, without commit / push
docs/wiki/                the wiki (Markdown, _Sidebar.md = page list; img/: wiring SVGs, touch PNGs, web WebPs)
tools/wiki_site.py        the wiki as HTML pages for the hosted page (/wiki/, called by build_site.py --web)
tools/wiki/               the wiki pictures: make_shots.sh (all), wiring.py, touch_png.py, shots_web.js +
                          mock/server.py (demo device), demo_run.cpp (runs from the firmware's run engine)
demo/                     browser demo: the firmware's control code + simulated hardware (sim.cpp) as WebAssembly
                          (build.sh), demo.js connects the page to it; README.md
.github/workflows/        release.yml: builds firmware + desktop app, deploys page and files to Cloudflare
tools/launcher/           local app: Go launcher (build.sh → dist/tempctl-duo-<version>.exe), section 6
test/ui_host/             touch UI preview on a PC (screenshots, random-tap test)
test/console_host/        serial console on a PC (scripted commands -> output)
src/console.*             serial console (section 4), LogSerial (log lines that don't break typed input)
test/host_test.cpp        g++ -std=c++17 -Wall -Wextra -Isrc -I<ArduinoJson>/src test/host_test.cpp src/duo_program.cpp src/run_engine.cpp src/settings.cpp src/tc_sensor.cpp src/pid.cpp src/runaway.cpp src/autotune.cpp src/ota_image.cpp src/release.cpp && ./a.out
```

## 12. Safety

A software programmer must never be the only protection against overheating. Fit an independent
over-temperature limiter (PMA recommends the TB 40-1 in the KS 4x-1 manual) and use the controller's own
limit alarms and `SP.Hi` setpoint limit. Set each channel's *Highest setpoint* (Channels page) to match the
controller's `SP.Hi` (Modbus); a local channel's is at most its max. temperature − 10 °C.

### 12.1 Protections in the firmware (spec 9.2a)

Per channel (Channels page › Protection, touch Menu › Channels, `b.local.runaway.*` / `b.modbus.runaway.*` /
`b.local.cj*`); a tripped check switches the outputs off (local: also the SSR supply relay, when used), stops a run
and waits for Acknowledge:

| Check | Default | |
|---|---|---|
| Runaway: heating failed / left the setpoint band | on | output ≥ 80 % for 300 s (top 120 s) with < 5 °C rise; ±15 °C for 60 s after the SP was reached |
| Heating while off (stuck SSR / controller) | on, 10 °C in 60 s | outputs off but the PV rises; from 2 min after off, not while the other channel heats (its heat reaches the sensor). Modbus: only after an "outputs off" was written |
| Reading frozen | on, 120 s | outputs on, the unfiltered reading did not change at all |
| Converter too hot (local, MAX31855) | on, 70 °C | cold-junction temperature; outputs can't be switched on above it |
| Over temperature, sensor fault | always | as before |

General (Settings › Safety, touch Menu › System, `safety.*`):

| Setting | Default | |
|---|---|---|
| Manual mode timeout (`manualIdle`, `manualIdleMin`) | off, 30 min | manual mode ends after this long without a touch, a web action or a serial command; message + beeps 1 min before |
| E-stop input (`estop`, S3 GPIO5) | off | NC contact to GND, pull-up; open or a broken wire: SSRs and relay off at once (SSR timer / local task), run stopped, manual and autotune off, Modbus outputs off; nothing can be switched on while it is open |
| SSR supply relay (`relay`, S3 GPIO4) | on | active-high DC SSR in the +5 V of both SSR inputs: on while a local channel's outputs are on or autotune runs, off when none is and at once on any local fault or E-stop (event "SSR supply relay off: …"). Off: no relay fitted, GPIO4 stays low (with a relay fitted the heaters then get no power) |
| Alarm sound (`sound`, S3 GPIO6, CYD speaker P4) | on | fault / E-stop: 3 long beeps every 3 s; "Action required" and the timeout warning: 2 short beeps every 10 s; run finished: 3 short beeps. Any touch / web action / serial command silences the current alarm |
| Chip temperature warning (`chipWarn`, `chipMaxC`, S3) | on, 80 °C | read every 30 s; a warning event above the limit (5 °C hysteresis) |

Start warning: a channel that already is within 10 °C of (or above) its first target when a program
starts gets a warning message (the run starts anyway).

## Status (1.0.55)

The program engine, settings, PID / autotune, runaway check, Modbus framing, image check and release
manifest pass host unit tests (`test/host_test.cpp`); the touch UI and the console have PC hosts
(`test/ui_host`, `test/console_host`). The firmware (Arduino-ESP32 2.0.17, `espressif32@6.9.0`, LVGL 9.6)
runs on the S3 and the CYD with local channels. S3 while running: about 145–158 kB of internal RAM free
(large tables, LVGL's memory, the histories and logs are in PSRAM, `src/psram.h`). The Modbus channel is
beta: it has not been tested with a controller yet; the macOS / Linux desktop apps are built but untested.
