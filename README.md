# TempCtl Duo: two-channel ramp/soak temperature programmer (ESP32-S3 / ESP32-CYD + Modbus RTU)

> **Work in progress (version 1.0.22).** Modbus RTU (RS485 controllers) is a **beta** feature: built
> in, but not tested with a real controller yet; local thermocouple + SSR channels are the tested path.
> TempCtl Duo is being built from TempCtl 1.2 (bottom + top heater
> of a BGA rework station, see `claude/tempctl-duo-spec.md`). Done: **steps 1–5** — hardware bring-up
> (the ESP32-S3 board, env `s3`, and the CYD, envs `cyd-1xlocal-1x485`, `cyd-2xlocal`, `cyd-2xlocal-2x485`,
> build from the same sources), two channels (Modbus / local / off), local thermocouple + SSR channels,
> PID + autotune, and the two program types (Sequence, Timeline) with one run engine for both channels.
> Step 7 (new web page, section 5a): Status (both channels, run control, chart with forecast, program
> editor with chart handles, event log), Programs, Channels, System and Tools pages; the TempCtl 1.2 page
> is gone. Still to come in step 7: "Run interrupted by a reset". The touch screens (TempCtl 1.2, step 6
> comes later) show the **bottom channel**; programs are **read-only** there. On the S3's 480×320 screen
> the 320×240 TempCtl screens run in the top left corner, next to a bring-up panel.

Firmware for the ESP32-2432S028R ("Cheap Yellow Display") that drives a **PMA KS 40-1 / 41-1 / 42-1**
(or another Modbus RTU temperature controller) over RS485. It stores **up to 20 named programs** for both
heaters (Sequence or Timeline, see "Programs" below) and can also hold a **manual setpoint** per channel.
Main use: the bottom and top heater of a **BGA rework station** (reflow / desolder profiles); on the first
start three example programs are created (Lead-free BGA, Leaded BGA, Lead-free BGA as a Timeline).

- LVGL 9.6 touch GUI, **both boards: the new touch UI** (step 6, `src/tui_*.cpp`, section 4a; S3 480×320 in
  1.0.19, CYD 320×240 in 1.0.21), same look
  as the web page (Barlow font, palette, accent and channel colours):
  - **Main screen**: header with the program (tap: choose the program for the next run), phase / steps,
    state, run time and remaining time, Start / Pause / Continue, Stop, Next phase; the chart of both
    channels (tap: program / live view); one tile per channel with PV, SP, output, what the channel does,
    a target stepper (− value +, tap the value for the number pad; during a run it changes the current or
    kept target, otherwise the manual setpoint), the manual on / off button (filled in the channel colour
    while on), "+ Hold" (extend the running hold),
    Acknowledge for a fault. While a channel's outputs are on (run, manual, autotune) its tile gets a 4 px
    border in the channel colour and a 20 % tint of it. Overlays: "Action required" (Continue / Stop), "Run interrupted by a reset"
    (Resume / Later / Dismiss; the red "Interrupted" badge opens it again), "Save the run's changes?",
    messages at the bottom.
  - **Menu**: Programs (list; editor: Sequence as one table, Timeline as two tables side by side; tap a
    value to change it, tap # / Type for type, wait, pause text, insert, move, delete; Undo, Save, Start,
    Rename, Discard, Delete; during a run the running program with run edits of targets and times),
    Channels (name, type, limits, end action, Modbus controller, thermocouple, PID + autotune, runaway
    protection, RS485 bus; register addresses and the two-point calibration on the web page), Network
    (Wi-Fi with scan and password, AP password, host name, fixed IP), Display (brightness, dark mode,
    accent colour: six presets or any colour (hue / saturation / brightness), upside down, live chart
    period), System (polling interval, "bottom reached" band, info,
    restart), Event log.
  - **CYD (320×240)**: the same screens as a compact set (section 4a).
- Web interface (section 5a): Status, Programs, Channels (incl. controller parameters, full register
  map, calibration, PID autotune), System (Wi-Fi, display, restart) and Tools (register read/write, a
  setpoint-address finder, backup) pages; CSV log download
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

### Programs (TempCtl Duo, spec 9.3)

- **One program drives both channels.** Up to 20 programs, name up to 31 characters, stored as JSON files
  `/duo/prog0.json` … `/duo/prog19.json` on LittleFS (safe save: temp file + rename). Two types:
  - **Sequence**: up to 100 phases, one after another. `B` (bottom: target, ramp, hold), `T` (top: target,
    ramp, hold), `BT` (both: the bottom setpoint jumps to its target or goes Idle = outputs off, the top
    ramps and holds), `P` (pause: waits for Continue, optional "Action required" and a text up to 40
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
  A fault on any channel stops the run.
- **Seconds:** times are stored in seconds. With *Controller ramps (r.SP)* the KS 4x-1 gradient is per
  minute, so a short ramp only uses the controller gradient if it fits the register (see above); otherwise
  the ESP32 steps the setpoint itself.

### Manual setpoint

- *Main* has a manual setpoint and an on/off switch. **On:** the ESP32 switches the controller outputs on and
  writes the setpoint (again whenever you change it). **Off:** the outputs are switched off (`C.Off` = 1). If
  no outputs-off register is configured, *off* only stops the ESP32 from writing; the controller keeps its
  last setpoint.
- Manual mode is always off after a restart; the last value is remembered.
- Pause freezes program time (and the controller ramps).
- End/stop action: keep setpoint, set an end setpoint, or switch controller outputs off (`C.Off`).
  The original `r.SP` is restored afterwards.
- If a channel of the program doesn't answer for 30 s (configurable) the program pauses.
- **Run interrupted by a reset** (watchdog, crash, brown-out, power loss …): the board starts with the
  program's channels off (local: SSR off; Modbus: outputs-off register, if set — without it the controller
  keeps its last setpoint) and the web page shows "Run interrupted by a reset" with the reason, program,
  phase / steps and run time. Never an automatic resume: **Resume** (Sequence: the interrupted phase from
  its beginning, ramps from the measured temperatures, channels the phase doesn't start get their kept
  target at once; Timeline: on at the interrupted run time, ramps from the measured temperatures in the
  time they had left, holds with their remaining time) or **Dismiss** (outputs stay off, the controller
  gradient and front panel are restored). Starting another program also ends the offer. The running copy
  (with run edits) is in `/duo/run.json`; the position in RTC memory (exact, survives resets) and in
  `/duo/runpos.json` (every phase / step change and every minute, for a power loss). The touch screen
  shows the message; its Resume button comes with the new touch UI (step 6).

## 2. Hardware

Two supported boards, selected by the PlatformIO environment; all pins are in `src/board_s3.h` /
`src/board_cyd.h`.

### ESP32-S3 board (env `s3`, main target)

| Part | Notes |
|---|---|
| ESP32-S3-DevKitC-1 **v1.0 N16R8** | 16 MB flash, 8 MB octal PSRAM. On-board WS2812 on GPIO48 (a v1.1 board has it on GPIO38: swap 38 ↔ 48 in `board_s3.h`) |
| 4.0" SPI module **MSP4031** | ST7796S 320×480 (used as 480×320 landscape), FT6336U capacitive touch (I²C 0x38). VCC 5 V recommended |
| **3.3 V** RS485 transceiver | on UART1 |

Wiring picture: `claude/wiring/s3-wiring.png` (display on J1, RS485 / thermocouples / SSR drivers on J3).

| Function | GPIO | | Function | GPIO |
|---|---|---|---|---|
| LCD SCK / SDI (MOSI) / SDO (MISO) | 12 / 11 / 13 | | RS485 TX → DI | 1 |
| LCD_CS / LCD_RS (D/C) / LCD_RST | 10 / 9 / 14 | | RS485 RX ← RO | 2 |
| LED (backlight, PWM) | 8 | | RS485 DE + /RE | 42 |
| SD_CS (held high, SD not used) | 18 | | Thermocouple SCK / SO (later) | 41 / 40 |
| CTP_SDA / CTP_SCL | 16 / 15 | | TC CS bottom / top (later) | 39 / 38 |
| CTP_RST / CTP_INT | 17 / 7 | | SSR bottom / top (later) | 21 / 47 |

Flash and watch the log through the USB port labelled **"UART"**. The native **"USB"** port is the serial
console (section 4b; later also the Web Serial page): open it with any terminal at 115200 baud, opening it
never resets the board.

Status LED (WS2812): green = idle, blue = running, amber blinking = paused / waiting, red blinking = fault
(no answer from a channel, latched fault), purple = access point mode.

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
4. On the web page's *Channels* page set each channel's type; for a Modbus controller check slave
   address, baud rate and parity match the controller (`ConF/othr`), then check that the channel shows
   its process value (*Status* page or the reading at the top of the channel's settings).

TempCtl Duo keeps its settings in `/duo/settings.json` and doesn't read TempCtl's: Wi-Fi, controller and
touch settings start from defaults. Defaults: bottom = Modbus (address 1), top = Off (`cyd-2xlocal`: both
local); change them on the web page's *Channels* page. After switching from the TempCtl build delete the `.pio` folder once.

Touch calibration defaults (200…3700 / 240…3800) suit most CYDs. If touches land off, adjust them with
`POST /api/config {"display":{"touchCal":[minX,maxX,minY,maxY]}}` and restart.

## 4a. Touch UI

**`src/tui_*.cpp`** — screens written in LVGL C code (decided with Rost, step 6), one code for both screen
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
AddressSanitizer); `BOARD=cyd ./build.sh` does the same for the 320×240 set (`uihost_cyd`, `shots_cyd/`),
`python3 sheet.py [cyd] [names]` converts the shots and puts them on one sheet, `MEM=1 BOARD=cyd ./build.sh`
+ `./uihost_mem mem` prints the heap used per screen.

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

## 4b. Serial console

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
| `get [b\|t\|bus\|run\|wifi\|display\|<key prefix>]` | settings as `key=value` lines, ready to be used with `set` |
| `set <key>=<value> …` | up to 8 settings at once, the keys of the web API (`b.` / `t.` = the channels). Stricter than touch / web: a value out of range is an error and then nothing is changed; a value the device adjusts because of another setting is reported. Saved at once |
| `factory reset yes` | all settings to their defaults, Wi-Fi included (programs are kept), then a restart; not while a program runs |
| `touch cal` | CYD: calibrate the resistive touch screen (tap 4 crosses; any key cancels) |
| `reboot` | restart the device (refused while a program runs, like on touch / web) |
| `screen restart` | set only the display and the touch controller up again (S3: reset pins + I2C bus freed; CYD: software reset + SPI) and redraw; prints whether the touch controller answers |
| `screen check` | what the automatic screen check sees now: display status read back, touch controller, number of automatic restarts |
| Up / Down keys | the last 20 commands (lines with `pw=` and password entries are not kept) |
| `api on` · `api off` | machine mode for the Web Serial page (JSON lines, below) |

**api mode** (`api on`, spec 7b): one JSON object per line, for the Web Serial page. Requests
`{"id":7,"m":"GET","p":"/api/live"}` or `{"id":8,"m":"POST","p":"/api/manual","b":{...}}` (`m`: GET,
POST, PING) go through the same handlers as the HTTP API (section 5), with the same paths, query
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

## 5a. Web page

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
- Status page: run panel (program, Start / Pause / Continue / Stop / Next phase, state, run time,
  remaining time), chart (both channels: temperatures, setpoints, state lanes, phase lines with times,
  pauses, the forecast of the rest of the run dashed; zoom − / + / Fit, Ctrl + wheel, drag, pinch;
  "Live" shows the live history over a chosen period, see below), channel cards (PV, SP, output, what the channel does, target
  −5 / −1 / +1 / +5 / Set and Extend hold during a run, manual switch + setpoint otherwise, fault +
  Acknowledge; outputs on: 4 px border and 20 % tint in the channel colour, like the touch tiles), the "Action required" overlay, "Save the run's changes?" after a run with run edits, and
  the event log. Channel colours come from the device's accent colour (spec 6); dark mode switch in the
  sidebar (follows the system until switched; remembered by the browser; colours fade over).
- Program editor (under the channel cards): the selected program as tables (Sequence: one row per
  phase, cards on narrow screens; Timeline: two tables side by side) and on the chart (drag squares up /
  down = target, diamonds = ramp end, circles = hold end; whole seconds and °C). Outside a run edits go
  to a draft in the browser (Save / Discard / Undo / Redo, Ctrl+Z / Ctrl+Y; the program list marks
  unsaved programs; drafts are lost when the page is reloaded). Empty slot: New sequence / New timeline.
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
- Refresh: the page asks for new values every polling interval (Channels page; 250 ms … 1 s) and draws
  the newest temperature at the end of the chart; the chart history comes every 2 s.
- Pages (sidebar; addresses `#/programs`, `#/channels`, `#/system`, `#/tools`, so a page can be
  bookmarked; all on the one page file, the drafts stay while switching pages):
  - **Programs**: the 20 slots with type, channels, phases / steps and time; Open (in the Status page
    editor), Duplicate (into the first empty slot), Export (the program's JSON file), Delete; empty
    slots: New sequence / New timeline / Import (a program JSON file).
  - **Channels**: polling interval, link-loss pause, "bottom reached" band, RS485 bus; per channel (tabs):
    name, type, end action, setpoint limits; Modbus: address, controller type (+ load register defaults),
    setpoint ramps, front lock, register map, PMA parameters and actions; local: converter, calibration
    (with a two-point helper), PID, SSR window, max. output / temperature, PID autotune; runaway limits
    of the selected type. Changed fields are marked; Save sends only the changed values
    (`POST /api/config`) and lists values the device had to adjust to the allowed range.
  - **System** (tiles fill the free space): device info, restart, restart of the screen only (display
    and touch controller set up again, the device keeps running), Wi-Fi (with network scan, show / hide
    password, optional fixed IP address), touch display (light / dark switch, brightness slider;
    applied at once, no Save button — the screen follows the slider while it is dragged) (accent colour:
    a preset or "Custom" with the browser's colour picker; theme, brightness, rotation, screen check),
    **Log**: everything the device logs, colour-coded — errors (red), warnings (amber), program runs
    (accent), events, technical lines of the serial log ([wifi], [lcd], [mem] …, grey); filters per kind,
    search, download as text. The events of earlier starts come from flash (each start is a section);
    times are the real time when the device knows it (NTP on Wi-Fi, or the page sets the clock), else
    the time since the start. The device keeps about 7000 lines on the S3 (256 kB PSRAM), about 50 on
    the CYD (4 kB); the last 300 events (not the technical lines) survive restarts (`/duo/events.txt`,
    saved at most every 30 s and before a requested restart).
  - **Tools**: Modbus tools for a Modbus channel (find the setpoint address, read registers, write one
    register); backup / restore of the settings and of all programs as JSON files.

## 5b. The page over USB (Web Serial) and the local app

The same page also works **without Wi-Fi, over the USB cable** (spec 7b): it then talks to the device
through the serial `api` mode (section 4b) instead of HTTP — same screens, same functions. This needs
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
  copy `data-launcher`; without a mark (GitHub Pages, planned) the page shows the connect screen.

## 5. Web API

TempCtl Duo: two channels. The flat fields of `/api/status` (TempCtl 1.2) show the
**bottom channel**; everything else covers both (`ch` = 0 bottom, 1 top).

**Local channels** (type `local`, step 3): thermocouple converter MAX31855 or MAX6675 (`local.sensor`) read
every 250 ms, calibration `true = raw * calGain + calOffset`, PID (`kp`, `ki`, `kd`, or from the autotune: relay test around a
test temperature, Tyreus–Luyben rule) and a time-proportional SSR output (`windowSec`, default 1 s; `maxOut` %). Safety: SSR pins low from
the first line of `setup()`, SSR off when the task stops refreshing it (1.5 s), and latched faults
(outputs off, run stopped) for no reading (3 bad reads while on), above `maxTemp` and the runaway checks.
Modbus channels get the same runaway checks with the controller's output (Ypid); a fault switches the
controller's outputs off (C.Off). `/api/status` → `channels[].fault`, `POST /api/ack` clears it.

| Method | Path | Body / query |
|---|---|---|
| GET | `/api/live` | web page, polled every second: both channels (pv, sp, out, outOn (outputs on), state, target, kept, canExtend, manual, fault, autotune, …), run (state, el, rem, phase / steps, progPause, actionRequired, pauseText, edited, msg; `intr` while a run interrupted by a reset can be resumed: reason, exact, el, i / st / left per channel), `sel`, `accent`, `progGen`, `cfgGen` (changes with every settings save), `logSeq`, `sysSeq` (newest system log line), `boot` (start number), `time` (device clock, UTC s; 0 = unknown) |
| GET | `/api/chart?v=run` | current / last run: 300 bins of `pv`, `sp` and state lanes `st` per channel, past events `ev` [t, type, ch, idx], forecast `fc` (`seg` [t0, t1, v0, v1, src, flags] per channel, `ev`; times from now) |
| GET | `/api/chart?v=live` | the live period (`display.liveMin`) in 300 points of `pv` / `sp` / `dlo` / `dhi` per channel; `bin` s per point, `span`, `sh` (the points before the newest end `sh` s later than whole bins before now), `have` / `cap` (s recorded / s the history holds) |
| GET | `/api/log?since=n` | event log entries after n: [seq, uptime s, run time s or null, text, kind, epoch] (kind 1 event, 2 run, 3 warning, 4 error; epoch UTC s, 0 = clock unknown) |
| GET | `/api/syslog?since=n` | system log lines after n (about 12 kB per answer, `more`: ask again): [seq, uptime s, epoch, kind (0 technical … 4 error), text]; also `first`, `boot` |
| GET | `/api/eventlog.txt` | the events saved in flash, all starts: lines `boot \t epoch \t uptime \t kind \t text` |
| POST | `/api/time` | `{"t":<UTC s>}`: sets the device clock if it has no NTP time (the page does it; for the log) |
| GET | `/api/status` | live values (bottom), manual setpoint, selected program; `channels`: both channels (name, type, ok, err, pv, sp, out, mode, manual); `run`: state, `type` (`seq`/`tl`), `seq` (phase `idx`, `stage`, `el`, `dur`), `waiting`, `progPause`, `actionRequired`, `pauseText`, `elapsed`, `remaining`, `edited`, `channels[]` (inProgram, idle, sp, target, moving, Timeline `lane`) |
| GET/POST | `/api/config` | the whole settings document (= `/duo/settings.json` without passwords) + `limits` (`rs485`, `localMax` of this board variant). POST changes only the keys given, e.g. `{"channels":[null,{"type":"modbus","modbus":{"addr":2}}]}`; `"modbus":{"loadDefaults":true}` loads the register map of the channel's controller type; errors come back as `{"ok":false,"err":"channels[1].type: one of …"}` |
| GET | `/api/log.csv` | last program run (or, without one, the live history) as CSV, both channels |
| GET | `/api/history.csv` | the whole live history, streamed: time_s (before now), uptime_s, then per channel pv, min, max, sp; `?pv=3&sp=3` = channels (bit 0 bottom, bit 1 top) whose PV / SP columns are included (default all; rows without any value are left out) |
| GET | `/api/programs` | all 20 slots: `used`, `name`, `type`, `steps` (phases / steps), `total` (s), `uses` [bottom, top], `waits`; `sel` |
| GET | `/api/program?i=0` | `used`, `program`: the program JSON (spec 9.3.5) |
| POST | `/api/program` | `{"i":0,"program":{…}}` save (validated, setpoint limits checked) or `{"i":0,"delete":true}` |
| GET | `/api/run` | `run` (as in `/api/status`) + `program`: the running copy with its run edits |
| POST | `/api/run` | `{"cmd":"start","i":0,"program":{…}}` (start slot 0 with this unsaved content), `{"cmd":"extend","ch":0,"sec":30}`, `{"cmd":"hold","ch":1,"idx":4,"sec":45}`, `{"cmd":"ramp","ch":1,"idx":5,"sec":40}`, `{"cmd":"target","ch":0,"idx":-1,"t":170}` (`idx` = phase / step, -1 = current / kept), `{"cmd":"save"}`, `"next"`, `"continue"`, `"recover"` (resume the run interrupted by a reset), `"dismiss"` (dismiss it); the result shows in `run.msg` |
| POST | `/api/select` | `{"i":index}` select the program for Main / Chart |
| POST | `/api/manual` | `{"sp":150}` and/or `{"on":true}`, optional `"ch":1` (top; default 0 = bottom) |
| POST | `/api/autotune` | local channels: `{"ch":1,"cmd":"start","sp":200}`, `"stop"`, `"accept"` (proposed kp/ki/kd → settings), `"discard"`; progress and result in `/api/status` → `channels[].autotune` |
| POST | `/api/ack` | `{"ch":0}` acknowledge a channel fault (runaway, sensor, over temperature); refused while the cause is still there. The outputs stay off until switched on again |
| POST | `/api/cmd` | `{"cmd":"start"|"pause"|"resume"|"stop"|"next"|"continue", "prog":index}` (`skip` = `next`) |
| POST | `/api/wifi` | `{"ssid","pass","apPass"}`, reconnects without restart |
| GET | `/api/wifiscan` | `?start=1` starts a network scan; then `{"busy":true}` until `{"networks":[…]}` (strongest first) |
| POST | `/api/brightness` | `{"v":128}` touch screen brightness while the web slider moves (not saved) |
| POST | `/api/restart` | restarts the device (refused while a program runs) |
| POST | `/api/screen/restart` | sets the display and the touch controller up again (reset + init), redraws; the device keeps running (result in the serial log) |
| GET | `/api/mb/read?addr=&n=&fn=&ch=` | raw registers (`ch`: channel whose controller address is used, default 0; also in the POST tools below) |
| POST | `/api/mb/write` | `{"addr","value"}` raw |
| POST | `/api/mb/findsp` | `{"from","to"}` |
| GET/POST | `/api/params` | PMA parameters (`?ch=` / `"ch"`) |
| POST | `/api/action` | `{"i":index}` PMA actions (self-tuning, outputs off, …) |

The web UI has no login. Keep it on a trusted network.

## 6. Project layout

```
platformio.ini            environments s3, cyd-1xlocal-1x485, cyd-2xlocal, cyd-2xlocal-2x485; libraries
include/lv_conf.h         LVGL 9.6 configuration
src/version.h             product name, version, host / AP name
src/config.h              selects the board header, chart size
src/board_s3.h, board_cyd.h  pins, display, touch and LED per board
src/lcd.*                 display: LVGL's ST7796 / ILI9341 driver on ESP-IDF spi_master with DMA
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
src/tui*.cpp, tui.h, ui.h touch UI of both boards (480×320 S3, 320×240 CYD), section 4a
src/fonts/                touch UI fonts (tools/lv_font_gen.py from tools/fonts/*.ttf)
src/web.*                 HTTP server, JSON API
web/, tools/web_embed.py  web page + fonts → src/web_assets.h (generated before each build)
tools/build_site.py       the same page as static files (device URL layout), for the launcher / GitHub Pages
tools/launcher/           local app: Go launcher (build.sh → dist/tempctl-duo-<version>.exe), section 5b
test/ui_host/             touch UI preview on a PC (screenshots, random-tap test)
test/console_host/        serial console on a PC (scripted commands -> output)
src/console.*             serial console (section 4b), LogSerial (log lines that don't break typed input)
test/host_test.cpp        g++ -std=c++17 -Isrc -I<ArduinoJson>/src test/host_test.cpp src/duo_program.cpp src/run_engine.cpp src/settings.cpp src/tc_sensor.cpp src/pid.cpp src/runaway.cpp src/autotune.cpp && ./a.out
```

## 7. Safety

A software programmer must never be the only protection against overheating. Fit an independent
over-temperature limiter (PMA recommends the TB 40-1 in the KS 4x-1 manual) and use the controller's own
limit alarms and `SP.Hi` setpoint limit. Set *Highest allowed setpoint* on *Controller* to match `SP.Hi`.

Status: the program engine, settings, PID / autotune, runaway check and Modbus framing pass host unit
tests; the firmware (Arduino-ESP32 2.0.17, `espressif32@6.9.0`, LVGL 9.6) runs on the S3 and the CYD
with local channels. The Modbus channel is beta: it has not been tested with a controller yet.
