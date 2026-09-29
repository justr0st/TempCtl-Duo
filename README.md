# TempCtl Duo: two-channel ramp/soak temperature programmer (ESP32-S3 / ESP32-CYD + Modbus RTU)

> **Work in progress (version 1.0.0).** TempCtl Duo is being built from TempCtl 1.2 (bottom + top heater
> of a BGA rework station, see `claude/tempctl-duo-spec.md`). Done: **step 1, hardware bring-up** —
> the ESP32-S3 board (env `s3`) and the CYD (envs `cyd-1xlocal-1x485`, `cyd-2xlocal`, `cyd-2xlocal-2x485`) build from the same sources; the single-channel
> TempCtl 1.2 functions below still apply. On the S3's 480×320 screen the 320×240 TempCtl screens run in
> the top left corner, next to a bring-up panel (version, free memory, touch coordinates with a red dot
> at the touched point).

Firmware for the ESP32-2432S028R ("Cheap Yellow Display") that drives a **PMA KS 40-1 / 41-1 / 42-1**
(or another Modbus RTU temperature controller) over RS485. It stores **up to 10 named programs with up to
100 ramp/soak steps each** and can also hold a **manual setpoint**.
Main use: the heater of a **BGA rework station** (reflow / desolder profiles); on the first start
program 1 is an example lead-free GPU desolder profile ("GPU desolder (LeadFree)", 4 steps, 4:40).

- LVGL 9.6 touch GUI, designed as an **LVGL Pro Editor** project (`ui/`, see section 4a):
  - **Main**: live values, manual setpoint (numeric entry + on/off), program selection and Start / Pause / Stop / Skip
  - **Program**: pick one of 10 programs, rename it with the on-screen keyboard, add / remove / move steps
    (only configured steps are shown), times in minutes or seconds
  - **Chart**: updated every second. Idle: rolling 5-minute window (PV scrolls from right to left) plus a
    preview of the selected program on its own time scale. Running: fixed time axis covering the whole program
    plus spare time; the program curve and the PV trace grow from the left. Program and setpoint lines are
    solid while active and dashed while not active.
  - **Setup → System**: rotate the screen by 180°, brightness, colour theme and dark mode, Wi-Fi (scan,
    password, AP password)
  - **Setup → Controller**: controller type, serial link, ramp mode, soak, front-panel lock, end action
- Web interface with the same functions plus controller parameters (PID, limits, self-tuning), full register
  map, register read/write tools, a setpoint-address finder and CSV log download
- The controller keeps doing the PID control; the ESP32 only feeds it setpoints

## 1. What the PMA interface description tells us

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

The web *Main* page shows the number of setpoint writes in the current run.

### Program behaviour

- Up to 10 programs, each with a name (23 characters) and 1…100 steps. Stored on the LittleFS partition,
  one file per program; settings stay in NVS.
- Each step: target, ramp time (0 = jump), hold time. The first ramp starts at the PV measured at Start.
- **Seconds:** times are stored in seconds. In the editors each time can be entered in minutes or seconds
  (the table shows `30 min` or `45 s`). Second-resolution timing always works with *ESP32 writes setpoint
  steps* (set *minimum seconds between writes* low enough, e.g. 1). With *Controller ramps (r.SP)* the
  KS 4x-1 gradient is per minute, so a short ramp only uses the controller gradient if it fits the register
  (see above); otherwise the ESP32 steps the setpoint itself.
- Starting a program switches the controller outputs on (`C.Off` = 0) and ends manual mode.

### Manual setpoint

- *Main* has a manual setpoint and an on/off switch. **On:** the ESP32 switches the controller outputs on and
  writes the setpoint (again whenever you change it). **Off:** the outputs are switched off (`C.Off` = 1). If
  no outputs-off register is configured, *off* only stops the ESP32 from writing; the controller keeps its
  last setpoint.
- Manual mode is always off after a restart; the last value is remembered.
- *Guaranteed soak*: hold time only counts while PV is within ± band of the target.
- Pause freezes program time (and the controller ramp). Skip jumps to the next phase.
- End/stop action: keep setpoint, set an end setpoint, or switch controller outputs off (`C.Off`).
  The original `r.SP` is restored afterwards.
- If the controller doesn't answer for 30 s (configurable) the program pauses.
- If the ESP32 resets, the program stops; the controller keeps controlling at its last setpoint.

## 2. Hardware

Two supported boards, selected by the PlatformIO environment; all pins are in `src/board_s3.h` /
`src/board_cyd.h`.

### ESP32-S3 board (env `s3`, main target)

| Part | Notes |
|---|---|
| ESP32-S3-DevKitC-1 **v1.0 N16R8** | 16 MB flash, 8 MB octal PSRAM. On-board WS2812 on GPIO48 (a v1.1 board has it on GPIO38: swap 38 ↔ 48 in `board_s3.h`) |
| 4.0" SPI module **MSP4031** | ST7796S 320×480 (used as 480×320 landscape), FT6336U capacitive touch (I²C 0x38). VCC 5 V recommended |
| **3.3 V** RS485 transceiver | on UART1 |

| Function | GPIO | | Function | GPIO |
|---|---|---|---|---|
| LCD SCK / SDI (MOSI) / SDO (MISO) | 12 / 11 / 13 | | RS485 TX → DI | 1 |
| LCD_CS / LCD_RS (D/C) / LCD_RST | 10 / 9 / 14 | | RS485 RX ← RO | 2 |
| LED (backlight, PWM) | 8 | | RS485 DE + /RE | 42 |
| SD_CS (held high, SD not used) | 18 | | Thermocouple SCK / SO (later) | 41 / 40 |
| CTP_SDA / CTP_SCL | 16 / 15 | | TC CS bottom / top (later) | 39 / 38 |
| CTP_RST / CTP_INT | 17 / 7 | | SSR bottom / top (later) | 21 / 47 |

Flash and watch the log through the USB port labelled **"UART"**. The native "USB" port is kept for the
serial console / Web Serial page (later steps).

Status LED (WS2812): green = idle, blue = running, amber blinking = paused / waiting, red blinking = fault
(no answer from the controller), purple = access point mode.

Bring-up checks: if the colours look inverted set `LCD_INVERT 1`; if touches land in the wrong place change
`TOUCH_SWAP_XY` / `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` in `board_s3.h` (the bring-up panel shows the raw
touch values and the mapped point).

### ESP32-2432S028R "CYD" (envs `cyd-1xlocal-1x485`, `cyd-2xlocal`, `cyd-2xlocal-2x485`)

The CYD has too few free pins for RS485 plus two local channels, so it has three wirings, one env each
(pictures in `claude/wiring/`):

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

### Wiring

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

## 3. Can the LCTC M226-TE (from the screenshot) work the same way?

**Electrically and at protocol level: yes.** It is advertised with RS485 Modbus RTU, so the same transceiver and
the same Modbus master in this firmware can talk to it (it almost certainly uses FC 03 and 06).

**Register-wise: no, and I could not find its register map.** LCTC's TE-M226 product page and catalog don't publish
a Modbus table. PMA addresses (13294 etc.) will not work on it. To use it:

1. Get the communication manual from the seller/LCTC (address list for PV and SV, scaling, serial format).
2. Select *Generic Modbus RTU* on *Controller* (web), enter baud/parity/address as set on the controller.
3. Enter the PV and SV addresses (*Tools → Find the setpoint address* helps once PV/SV reads work) and the divisor (1 or 10). If you only know the front-panel values, use
   *Tools → Read registers* over 0…60 and look for the PV and SV numbers (e.g. SV 148 appears as 148 or 1480).
4. Use *ESP32 writes setpoint steps* (it probably has no gradient register) and keep *minimum step* ≥ 1 and
   *minimum seconds* ≥ 30 to spare its EEPROM.

PMA-specific extras (status word, self-tuning, outputs off, parameter page) won't apply unless you map equivalents.

## 4. Build and flash

1. Install VS Code + PlatformIO.
2. Open this folder, build and upload environment `s3` (default), `cyd-1xlocal-1x485`, `cyd-2xlocal` or `cyd-2xlocal-2x485`
   (`pio run -e s3 -t upload`).
   Check `upload_port` / `monitor_port` in `platformio.ini`.
3. First start: no Wi-Fi configured → the device opens access point `TempCtl-Duo-XXXX`, password `tempctl123`.
   Either set Wi-Fi on the display (*Setup → System*: scan, pick the network, enter the password, *Save +
   connect*) or browse to `http://192.168.4.1` and use the *System* page. Afterwards: `http://tempctl-duo.local`
   or the IP shown on *Setup → System*.
4. On *Controller*, check slave address, baud rate and parity match the controller (`ConF/othr`), then
   check that *Main* shows the process value.

TempCtl Duo keeps its settings in `/duo/settings.json` and doesn't read TempCtl's: Wi-Fi, controller and
touch settings start from defaults. Defaults: bottom = Modbus (address 1), top = Off (`cyd-2xlocal`: both
local); change the channels with `POST /api/config` until the new UIs have a Channels page. After switching from the TempCtl build delete the `.pio` folder once.

Touch calibration defaults (200…3700 / 240…3800) suit most CYDs. If touches land off, adjust `tsMinX…tsMaxY`
through `POST /api/settings` and restart.

## 4a. Editing the touch UI with LVGL Pro Editor

The screen layout lives in `ui/` as an [LVGL Pro](https://lvgl.io/pro) project (`project.xml`,
`globals.xml`, `translations.xml`, `components/`, `screens/`, target 320×240). Open the **`ui` folder**
(not the repository root) in LVGL Pro Editor to see and edit the screens.

How it gets into the firmware: LVGL Pro Editor **exports the UI as C code** (`*_gen.c/.h` next to each
XML file, `tempctl_ui_gen.c/.h`, `fonts/*_data.c`). PlatformIO compiles that code as the library
`ui/library.json` (`symlink://ui` in `platformio.ini`), and the firmware calls it through
`src/ui_gen.c`. So after editing: **save, run "Export C code" in the editor**, then build and upload with
PlatformIO. Commit the XML and the exported files together. The firmware runs on LVGL 9.6, which has no
XML loader any more; the editor project targets LVGL 9.5 (`project.xml`), whose exported code compiles
with 9.6 (the deprecation warnings it causes are switched off for the `ui` library).

| File | Contents |
|---|---|
| `ui/globals.xml` | colours (incl. theme colours and dark/light), spacing, fonts, shared styles |
| `ui/translations.xml` | all texts of the touch UI (labels, status texts, popup titles and messages) |
| `ui/screens/gui.xml` | the screen: the tab view with the four pages |
| `ui/components/main_page.xml` | Main: values, manual setpoint, program selection, run buttons |
| `ui/components/program_page.xml` | Program editor: selection, name, step table, step buttons |
| `ui/components/chart_page.xml` | chart, axes and legend |
| `ui/components/setup_page.xml`, `system_panel.xml`, `controller_panel.xml`, `setting_row.xml` | Setup |
| `ui/components/popups/num_editor.xml` | number / time entry (min or s) with keypad |
| `ui/components/popups/text_editor.xml` | text entry with on-screen keyboard (program names, Wi-Fi) |
| `ui/components/popups/dialog.xml` | message / question box (stop, unsaved changes, saved, Wi-Fi connecting, link test) |
| `ui/components/popups/wifi_list.xml`, `wifi_item.xml` | Wi-Fi scan result list |

Rules so the firmware keeps working:

- **Keep the `name` attributes** of widgets the firmware uses (e.g. `main_pv`, `main_start`,
  `prog_table`, `chart`). `src/ui.cpp` finds them with `lv_obj_find_by_name`, updates them and attaches
  the touch handlers to them (`UI_EVENTS` table). A missing name is reported on the serial monitor as
  `[ui] widget '…' not found`. The XML itself contains no `event_cb`s, so the editor preview has nothing
  unresolved.
- The chart must keep its three series in the order PV, SP, program.
- Dropdown options in *Controller* are matched by position (baud rates, formats, …).
- Fonts: the editor converts the fonts in `ui/globals.xml` (from `ui/fonts/*.ttf`) into C fonts with
  the characters in their `range`. LVGL's symbols (dropdown arrows, warning sign) come from the built-in
  Montserrat 12/14, chained as fallback in `src/ui_gen.c`; a font added to or removed from
  `globals.xml` needs its line there (`UI_FONT_WITH_FALLBACK`).
- Texts: static labels use `translation_tag="…"`, the firmware uses the same tags, all defined in
  `ui/translations.xml`. `%1`, `%2`, `%3` in a text are filled in by the firmware (numbers, names,
  times) and must stay in the text (not `{1}`: LVGL Pro reads a value in braces as an expression).
  An empty text must be a space (`en=" "`, see the `blank` tag): the export writes an empty text as
  "missing", and LVGL would show the tag instead. A second language: add its code to `languages` and
  an attribute per text; the exported code selects the first language. Tab names and dropdown options
  are plain text in the XML files.
- Colours: everything is in `ui/globals.xml`. `color_*` are 14 colour roles (background, panel,
  border, text, muted, accent, accent2, row_selected, danger, pv, sp, plan, ok, warn); they are the
  palette *Default* in dark mode and what the editor preview shows. The other palettes (Default
  light, Dracula, Solarized, Gruvbox, Catppuccin, One — each dark and light) are constants named
  `<palette>_<dark|light>_<role>`. *Setup → System* (and the web *System* page) selects palette and
  dark mode at runtime; `theme_dark` is only the mode at the first start. The export turns the
  constants into C macros (`COLOR_ACCENT`, `DRACULA_DARK_BG`, …); `src/ui_gen.c` builds the palettes
  from them (`PALETTE_NAMES`, `paletteLoad()`) and recolours the styles (`STYLE_COLORS`), and `src/ui.cpp`
  (`applyTheme`) the LVGL theme (accent, accent2, dark), the chart series and the program table
  highlight. Rules:
  - widgets get colours only through named styles in `globals.xml`, not with `style_…_color` on the
    widget (that colour would stay the same in every palette);
  - a style with a colour needs a line in `STYLE_COLORS` in `src/ui_gen.c` (the firmware logs a
    warning at start-up if a line no longer matches `globals.xml`);
  - a new palette: 14 constants in `globals.xml` for dark and 14 for light, its name in
    `PALETTE_NAMES` and two cases in `paletteLoad()` in `src/ui_gen.c`.
- Everything that changes at runtime (values, table rows, chart data) is done by the firmware; the XML
  only holds placeholder values for the preview.
- Popups are created with their exported create function each time they open (on LVGL's top layer).
  The dialog has three footer buttons and a close button; the firmware labels the ones it needs and
  hides the others. Keep the `num_units` buttons in the order min, s.
- A new component or screen is picked up by the build automatically; to use it from the firmware, add
  a wrapper for its `<name>_create()` function to `src/ui_gen.c` / `ui_gen.h`.

## 5. Web API

TempCtl Duo (step 2): two channels. Until the new web page and run engine arrive, the program engine, the
chart and the TempCtl 1.2 endpoints below work on the **bottom channel**; `channels` in `/api/status`,
`ch` in `/api/manual` and the bus tools, and `/api/config` cover both.

**Local channels** (type `local`, step 3): thermocouple converter MAX31855 or MAX6675 (`local.sensor`) read
every 250 ms, calibration `true = raw * calGain + calOffset`, PID (`kp`, `ki`, `kd`; autotune comes in step
4) and a time-proportional SSR output (`windowSec`, default 1 s; `maxOut` %). Safety: SSR pins low from
the first line of `setup()`, SSR off when the task stops refreshing it (1.5 s), and latched faults
(outputs off, run stopped) for no reading (3 bad reads while on), above `maxTemp` and the runaway checks.
Modbus channels get the same runaway checks with the controller's output (Ypid); a fault switches the
controller's outputs off (C.Off). `/api/status` → `channels[].fault`, `POST /api/ack` clears it.

| Method | Path | Body / query |
|---|---|---|
| GET | `/api/status` | live values (bottom), run state, manual setpoint, selected program; `channels`: both channels (name, type, ok, err, pv, sp, out, mode, manual) |
| GET/POST | `/api/config` | the whole settings document (= `/duo/settings.json` without passwords). POST changes only the keys given, e.g. `{"channels":[null,{"type":"modbus","modbus":{"addr":2}}]}`; errors come back as `{"ok":false,"err":"channels[1].type: one of …"}` |
| GET | `/api/history` | chart data (300 points): `pv`, `sp`, `plan`, `mode` = `live`/`run`, `planActive`, `spActive` |
| GET | `/api/log.csv` | last program run (or the live window) as CSV |
| GET | `/api/programs` | names, step counts and durations of all 10 programs, `sel` |
| GET/POST | `/api/program?i=0` | `{"i":0,"name":"Bisque","steps":[{"t":100,"r":1800,"h":600}, …]}` (r, h in seconds) |
| POST | `/api/select` | `{"i":index}` select the program for Main / Chart |
| POST | `/api/manual` | `{"sp":150}` and/or `{"on":true}`, optional `"ch":1` (top; default 0 = bottom) |
| POST | `/api/ack` | `{"ch":0}` acknowledge a channel fault (runaway, sensor, over temperature); refused while the cause is still there. The outputs stay off until switched on again |
| POST | `/api/cmd` | `{"cmd":"start"|"pause"|"resume"|"stop"|"skip", "prog":index}` |
| GET/POST | `/api/settings` | TempCtl 1.2 form (bottom channel + common); partial updates allowed; `{"profile":0,"loadDefaults":true}`, `{"rotate180":true}` |
| POST | `/api/wifi` | `{"ssid","pass","apPass"}`, reconnects without restart |
| GET | `/api/mb/read?addr=&n=&fn=&ch=` | raw registers (`ch`: channel whose controller address is used, default 0; also in the POST tools below) |
| POST | `/api/mb/write` | `{"addr","value"}` raw |
| POST | `/api/mb/findsp` | `{"from","to"}` |
| GET/POST | `/api/params` | PMA parameters |
| POST | `/api/action` | `{"i":index}` PMA actions (self-tuning, outputs off, …) |

The web UI has no login. Keep it on a trusted network.

## 6. Project layout

```
platformio.ini            environments s3, cyd-1xlocal-1x485, cyd-2xlocal, cyd-2xlocal-2x485; libraries
include/lv_conf.h         LVGL 9.6 configuration
ui/                       LVGL Pro Editor project: touch UI layout (XML) + exported C code (library.json)
src/version.h             product name, version, host / AP name
src/config.h              selects the board header, chart size
src/board_s3.h, board_cyd.h  pins, display, touch and LED per board
src/lcd.*                 display: LVGL's ST7796 / ILI9341 driver on ESP-IDF spi_master with DMA
src/touch.h, touch_ft6336.cpp, touch_xpt2046.cpp  touch drivers (own FT6336U driver / XPT2046_Touchscreen)
src/status_led.*          status LED (WS2812 / single LED)
src/program.*             ramp/soak engine + step editing helpers (pure C++, unit-tested)
src/program_store.*       10 programs on LittleFS
src/modbus_frame.h        Modbus RTU framing + CRC (pure C++, unit-tested)
src/modbus_rtu.*          half-duplex Modbus master (UART1 on the S3, UART2 on the CYD)
src/controller.*          register access of one controller on the shared bus, PMA parameter/action tables
src/settings.*            settings of both channels: defaults, validation, JSON (pure C++, unit-tested)
src/settings_store.*      /duo/settings.json on LittleFS (safe save)
src/channel.*             channel interface: Modbus backend (+ runaway check), local, off
src/local_ctrl.*          local channels: task reading the thermocouples, PID, SSR time proportioning, safety
src/tc_sensor.*, pid.*, runaway.*  MAX31855/MAX6675 decoding, PID, runaway check (pure C++, unit-tested)
src/app_state.*           control task: polling both channels, program (bottom), manual setpoint per channel, chart history
src/ui.*                  touch UI behaviour: event callbacks, dialogs, bring-up panel
src/ui_gen.*              bridge to the exported UI code (create functions, constants, font fallbacks)
src/web.*, web_page.h     HTTP server, JSON API, single-page web UI
test/host_test.cpp        g++ -std=c++17 -Isrc -I<ArduinoJson>/src test/host_test.cpp src/program.cpp src/settings.cpp src/tc_sensor.cpp src/pid.cpp src/runaway.cpp && ./a.out
```

## 7. Safety

A software programmer must never be the only protection against overheating. Fit an independent
over-temperature limiter (PMA recommends the TB 40-1 in the KS 4x-1 manual) and use the controller's own
limit alarms and `SP.Hi` setpoint limit. Set *Highest allowed setpoint* on *Controller* to match `SP.Hi`.

Status: the program engine and Modbus framing pass host unit tests. Firmware 1.2 compiles and links
against Arduino-ESP32 2.0.17 (the `espressif32@6.9.0` platform) with LVGL 9.6 and the UI exported by
LVGL Pro, and the touch screens were checked in an LVGL desktop simulator. The LVGL 9.4 / runtime-XML
version ran on a CYD; the 9.6 version has not been tested on hardware yet. Expect to adjust small details
(display variant, touch calibration, CN1 pin order) on first bring-up.
