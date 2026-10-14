# TempCtl Duo — specification and working notes

Status: 2026-09-28 (third revision — all design decisions made). The first revision was written at the
end of the conversation that built TempCtl 1.2. Revisions 2–3 add everything agreed with Rost in a
claude.ai chat: the Duo goals and program model (9), the hardware — ESP32-S3 + 4.0" ST7796 as main
target and the CYD as second device (2, 2a, 2b), colours (6), the Web-GUI based on the 9.3 simulator
(7, `claude/web-sim/`), serial console (7a) and the Web Serial page / local app (7b). Every open point
was decided one by one; what is left is only for later (GitHub Pages, SD logging, "Convert to
Timeline") and hardware checks during bring-up (9.10).
2026-09-30: **steps 1–4 of 9.10 implemented** (S3 bring-up, per-board headers, three CYD variants,
identity; channel interface, two Modbus channels on one bus, `/duo/settings.json`; local channels
with thermocouple, SSR, PID and safety; autotune). All four envs compile and link in the cloud container.
Commits (Rost pushes): `7c34d2e` 1.0.0 = steps 1–3, then 1.0.1 = step 4. State: 9.10.1–9.10.4. **The 4" display
and the PMA controller are still being delivered** (Rost): their checks are deferred until each part
arrives; meanwhile test what the bare S3 board and the CYD allow. Next: step 5 (program types
Sequence + Timeline, one run engine for both channels).

Read this file first, then `README.md` (TempCtl hardware, wiring, PMA register background, build, web
API) and `ui/README.md`. Sections 3–8 describe the **TempCtl 1.2 code base that Duo starts from**; the
parts that change for Duo are marked "Duo:".

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
  "TempCtl" (web page title, AP name `TempCtl-XXXX`, host name `tempctl`). **Identity and versioning
  are decided (9.8)**: "TempCtl Duo", host `tempctl-duo`, AP `TempCtl-Duo-XXXX`, version starts at
  **1.0.0** and the patch number goes up with every commit that is pushed.

## 1. Working with Rost (user preferences and environment)

- User: Rost (GitHub `justr0st`, commit email `50486502+justr0st@users.noreply.github.com`).
- **No computer use / screen control** ("stop using computer screen"). Rost flashes the board, runs the
  LVGL Pro Editor ("Export C code") and pushes to GitHub himself. The in-app browser may be used for
  looking at web pages (e.g. design references, datasheets).
- **Serial port is not reachable** from Claude: ask Rost for the serial monitor output.
  `platformio.ini` has `monitor_filters = esp32_exception_decoder`, so his monitor prints decoded
  backtraces. His build ELF: `D:\Share\GitHub\TempCtl-Duo\.pio\build\<env>\firmware.elf`
  (envs `s3`, `cyd-1xlocal-1x485`, `cyd-2xlocal`, `cyd-2xlocal-2x485`). **Flashing and monitoring go through the
  board's USB port labelled "UART"** (USB-to-UART bridge → UART0). The COM port changes with the new
  board (ask Rost; `upload_port`/`monitor_port` are in `platformio.ini`).
- **GitHub is not reachable** from the device VM (proxy 403) and the cloud container has no
  credentials: never try to enter credentials; tell Rost to push.
- Commit only when Rost asks. Commit messages: mention only the most significant changes. Leave
  `.vscode/settings.json` (his local editor settings) out unless he says otherwise.
- Answers short and direct; Rost usually tests on hardware and reports back ("works", a log, a photo).
- This document lives in the Claude project (`claude/tempctl-duo-spec.md`); **the repo keeps a copy at the same path,
  updated with every commit** (since 1.0.46; `docs/SPEC.md` only points to it). Old untracked files go to `.old/`
  (ignored).
- Ask before large design decisions (AskUserQuestion); otherwise make the reasonable choice and say so.

### Tools and file handling
- Rost's clone is reached through the Cowork device VM (`device_bash`, folder `D:\Share` mounted at
  `$HOME/mnt/Share`). Delete permission for `D:\Share` had to be granted per session.
  2026-09-29 session: only `D:\Share\GitHub\TempCtl-Duo` was connected (`$HOME/mnt/TempCtl-Duo`);
  files were staged directly (≤ 50 per `device_stage_files` call, no tgz) and written back one by one
  with `device_commit_files` (CRLF copies, `expectedMtimeMs` from the staging), then md5-checked.
- **Never run a git command in the VM without `GIT_OPTIONAL_LOCKS=0`** (read-only) — a plain
  `git status` left an empty `.git/index.lock` that the VM could not delete without delete permission
  (it would block Rost's next commit).
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

TempCtl (the original) runs on an ESP32-2432S028R "Cheap Yellow Display" (ESP32, ILI9341 320×240,
XPT2046 resistive touch, RS485 on UART2 RX 35 / TX 22 / DE+RE 27). **Duo supports two boards**
(Rost, 2026-09-28): the new **ESP32-S3 board (main target)** below, and the **CYD as a second supported
device** (section 2b). Main target:

| Part | Duo hardware |
|---|---|
| MCU board | **ESP32-S3-DevKitC-1 v1.0, N16R8** (ESP32-S3-WROOM-1-N16R8: 16 MB quad flash, 8 MB octal PSRAM). v1.0 = on-board WS2812 on **GPIO48** (v1.1 has it on GPIO38). Rost believes his board is v1.0; **work on the assumption WS2812 = GPIO48** until he has tested it with `neopixelWrite(48, 0, 0, 40)` (blue = v1.0). |
| Display | **4.0" capacitive SPI module MSP4031** (lcdwiki "4.0inch Capacitive SPI Module ST7796"): ST7796S, 320×480 (used as 480×320 landscape, confirmed), 4-wire SPI, on-board level shifters (3.3 V logic OK), VCC 5 V recommended (backlight ≈ 103 mA), micro-SD slot on the same SPI bus, 14-pin header. |
| Touch | **FT6336U** capacitive controller on the display module, I²C (address 0x38), with CTP_RST and CTP_INT. |
| Bus | RS485 transceiver on UART1, **3.3 V part** (MAX3485 / SP3485); a 5 V MAX485 module would put 5 V on the RX pin. |
| Local channels | Thermocouple amplifiers **MAX31855** (preferred: fault bits, cold-junction readout) **or MAX6675** (selectable per channel), SSRs driven from GPIO through an NPN/logic MOSFET from 5 V. |

- Duo controls **two heaters of a BGA rework station: bottom (preheater underneath) and top**.
- Each channel is either a **Modbus RTU controller** (PMA KS 40-1 / 41-1 / 42-1 or generic register map;
  the controller does the PID, the ESP32 is the ramp/soak programmer) or a **local channel**
  (thermocouple + SSR, PID on the ESP32). See 9.
- Touch UI (LVGL 9.6) and a web UI (JSON API) with the same functions ("full parity").
- Example program for the BGA use case: "GPU desolder (LeadFree)" — 160 °C (ramp 60 s, hold 30 s),
  190 °C (40/40), 225 °C (30/20), 245 °C (20/40), total 4:40 (`programExample()` in
  `src/program_store.cpp`). Duo: three two-channel examples (9.9).

## 2a. Pinout (ESP32-S3-DevKitC-1 v1.0 N16R8) — agreed 2026-09-28

Unusable / kept free:
- **GPIO 35, 36, 37**: used by the octal PSRAM on N16R8 (not available).
- **GPIO 19 / 20** (native USB D−/D+ — the **control port**, 7a/7b) and **43 / 44** (UART0,
  USB-to-UART bridge port — flashing and log): kept free so
  **both USB ports stay usable** (Rost's requirement).
- **Strapping pins 0, 3, 45, 46**: not used (GPIO45 high at boot selects 1.8 V flash voltage).
- GPIO 26–32: flash/PSRAM, not on the headers.

| Function | GPIO | Header | Notes |
|---|---|---|---|
| Display SCK | 12 | J1 | FSPI IO_MUX pins (10–13) → high SPI clock possible |
| Display SDI (MOSI) | 11 | J1 | |
| Display SDO (MISO) | 13 | J1 | shared with the SD card |
| LCD_CS | 10 | J1 | |
| LCD_RS (D/C) | 9 | J1 | |
| LCD_RST | 14 | J1 | |
| LED (backlight) | 8 | J1 | LEDC PWM, dimming |
| SD_CS | 18 | J1 | driven high (card deselected); SD use optional (logs) |
| CTP_SDA | 16 | J1 | I²C 400 kHz |
| CTP_SCL | 15 | J1 | |
| CTP_RST | 17 | J1 | |
| CTP_INT | 7 | J1 | optional (skip I²C reads when not touched) |
| RS485 TX → DI | 1 | J3 | UART1 |
| RS485 RX ← RO | 2 | J3 | |
| RS485 DE + /RE | 42 | J3 | |
| Thermocouple SCK | 41 | J3 | own slow SPI bus (not the display bus) |
| Thermocouple SO (MISO) | 40 | J3 | |
| TC CS bottom | 39 | J3 | 10 kΩ pull-up |
| TC CS top | 38 | J3 | 10 kΩ pull-up; **on a v1.1 board swap 38 ↔ 48** |
| SSR bottom | 21 | J3 | 10 kΩ pull-down (off during reset/flash) |
| SSR top | 47 | J3 | 10 kΩ pull-down |
| Status LED (on-board WS2812) | 48 | — | status colours, see below |
| SSR supply relay | 4 | J1 | +5 V common of both SSR inputs; active-high DC SSR driven directly (1.0.38) |
| E-stop input | 5 | J1 | NC contact to GND, internal pull-up; `safety.estop`, off by default (1.0.38) |
| Alarm buzzer | 6 | J1 | LEDC channel 6, 2.7 kHz; `safety.sound` (1.0.38) |

**Status LED (decided)**: green = idle · blue = running · amber blinking = waiting for bottom / program
pause / action required · red blinking = fault · purple = AP mode. Priority when several apply: fault >
amber > purple > blue > green. Low brightness (≈ 20 %). **CYD: the on-board RGB LED (external WS2812 on IO17 in `cyd-2xlocal-2x485`) with the same
colours** (Rost, 2026-09-29; replaces "blue LED only"): PWM on LEDC channels 1–3, active low.

Wiring picture: `claude/wiring/s3-wiring.png` (2026-09-30; display pins drawn in J1 order, VCC on 5 V;
1.0.38: relay / E-stop / buzzer on J1-4/5/6 as net labels, SSR "+" inputs on "5V relay"). Generator:
`/home/claude/wire/gen_s3.py` (cloud only; render with `node shot.js <svg>`).
**SSR supply relay (decided 2026-10-13, Rost): only an active-high DC solid-state relay driven directly by
GPIO4** (input + = GPIO4, − = GND; output 5 V → OUT+, OUT− → both SSR "+" inputs; input ≤ 20 mA at 3.3 V,
output ≥ 0.5 A DC). No relay modules, no level setting, no optional (DNP) parts: the relay-variant sheet
`s3-relay-wiring.png`, `relay.activeLow` (settings, touch, serial, web) and the RGND / E-stop RC options
were removed. GPIO4 is an output, low, from `localCtrlEarlyInit()`.
Setting `safety.relay` (decided 2026-10-13, Rost; default on): a switch in web Settings › Safety (the note
block on the Channels page was removed), touch System, serial `safety.relay`. Off = no relay fitted: GPIO4
stays low, no relay state in live / info, no "SSR supply relay off" events (`localCtrlRelayEnable`,
`localCtrlRelayUsed`).
Schematic (2026-10-13): `hardware/s3-schematic/` — KiCad 7 project (A3, one sheet: DevKitC-1 with J1/J3 pin
numbers, display, RS485 + PMA terminals (RGND not connected), 2 × MAX31855 + CS pull-ups, 2 × BC337 SSR
drivers + SSRs, K1 DC SSR supply relay, E-stop SW1, buzzer BZ1 with R7 100 Ω), net labels between the
blocks, PDF / PNG. Written by a Python generator (`hardware/gen/`: no KiCad in the cloud — hand-written KiCad 7
file format, checked by reading it back with `verify.py`; ERC to be run in KiCad by Rost).
CYD schematics (2026-10-13): `hardware/cyd-schematic/` — one KiCad project per env (`TempCtl-Duo-CYD-A/B/C`
= `cyd-1xlocal-1x485` / `cyd-2xlocal` / `cyd-2xlocal-2x485`), same generator (`hardware/gen/make.py`). The
CYD is one symbol with its connectors (pin numbers P3-n, CN1-n, SD-n via the micro-SD breakout, P4-n, P1-n,
LED-R/G/B pads on C); RS485 + PMA (A, C), MAX31855s (C: 10 k pull-up on the IO4 CS), BC337 SSR drivers, SSR
+ inputs on 5 V from P1-1 VIN, speaker LS1 on P4 (all), WS2812 status LED on IO17 from 3V3 (C). No E-stop,
no supply relay. The CYD wiring pictures show the P4 speaker too.
Display on J1, all process I/O on J3 (separate cable bundles). All pins become `#define`s in the
per-board header (`src/board_s3.h`, section 3) — one place to change for a v1.1 board or a different
wiring.

## 2b. Second supported device: CYD (ESP32-2432S028R) — agreed 2026-09-28

The CYD stays supported with the same firmware features (two channels, both program types, web GUI),
built from the same sources with its own envs `cyd-1xlocal-1x485` / `cyd-2xlocal` / `cyd-2xlocal-2x485` (below).

- ESP32 (no PSRAM, 4 MB flash, ≈ 520 kB SRAM), ILI9341 **320×240**, XPT2046 **resistive** touch, RGB
  LED (active low: R 4, G 16, B 17), backlight GPIO21, SD slot on VSPI (CS 5, SCK 18, MISO 19,
  MOSI 23), RS485 unchanged: RX 35, TX 22, DE/RE 27 (UART2).
- **Free GPIOs** (checked 2026-09-29 against the schematic ESP32-2432S028 V0.1 and the
  randomnerdtutorials CYD pinout): connectors P3 = GND, IO35 (input only), IO22, IO21 (backlight, not
  usable); CN1 = GND, IO22, IO27 (10 k pull-up R18), 3V3 (IO22 on CN1 pin 3 is missing in schematic
  V0.1 but present on the board — checked by Rost); P1 = VIN, IO1/IO3 (UART0, shared with the
  CH340). **IO6–IO11 are not usable**: they are the WROOM-32 module's internal SPI flash; the empty U4
  footprint on the CYD is only an optional second flash chip on the same lines. **Only IO22, IO27, IO35 are free on connectors — RS485 uses all three.** Taken: IO0 BOOT
  button, IO4/16/17 RGB LED (status), IO26 speaker amplifier (SC8002B, P4), IO34 LDR, display
  2/12/13/14/15 (LCD reset = EN), touch 25/32/33/36/39, IO21 backlight (AO3402, pull-down). The SD slot
  pins IO5/18/19/23 are free (SD not used), reachable with a micro-SD breakout ("sniffer") adapter;
  they have 10 k pull-ups (R13, RN2) except IO18 (CLK) per the schematic — to be measured.
- **Three CYD variants, one env each (decided by Rost, 2026-09-29)** — the RGB LED is the status LED,
  so the old SSR pins 4/16 are gone, and there are too few pins for RS485 + two local channels.
  Pins fixed at compile time in `board_cyd.h` (`-D CYD_1XLOCAL_1X485` / `-D CYD_2XLOCAL` /
  `-D CYD_2XLOCAL_2X485`, with
  `HAS_RS485`, `LOCAL_CHANNELS_MAX`, `BOARD_VARIANT`); pictures `claude/wiring/cyd-wiring-a.png` / `-b.png` / `-c.png`:

  | Env | Channels | Pins |
  |---|---|---|
  | `cyd-1xlocal-1x485` (wiring A, Rost's preferred) | RS485 (one or two Modbus controllers on the bus) + **one** local channel, bottom or top | RS485 TX 22 (P3-2), RX 35 (P3-3), DE+/RE 27 (CN1-2); TC SCK 23, SO 19, CS 5 (`PIN_TC_CS_LOCAL`); SSR 18 (`PIN_SSR_LOCAL`) — SD slot pins |
  | `cyd-2xlocal` (wiring B) | two local channels, **no RS485** (Modbus not selectable) | TC SCK 23, SO 19; bottom CS 5 / SSR 18 (SD slot); top CS 27 (CN1-2) / SSR 22 (P3-2); IO35 free (input) |
  | `cyd-2xlocal-2x485` (wiring C, added 2026-09-29) | RS485 + two local channels — every channel local or Modbus. **On-board RGB LED removed** (frees IO4/16/17) | RS485 as in wiring A; TC SCK 23, SO 19; bottom CS 5 / SSR 18; top CS 4 (LED pad R, **external 10 k pull-up**, floating at boot) / SSR 16 (LED pad G); **status LED = external WS2812 on IO17** (LED pad B, powered from 3V3 so the 3.3 V data level is in spec) |

  SD slot pins through a micro-SD breakout adapter, no card inserted. IO18 has no pull-up (SSR off at
  boot); IO5/IO23/IO19 have 10 k pull-ups (fine for CS / SCK / SO); IO27's pull-up keeps CS high.
  Settings (step 2/3) must refuse channel types the variant can't wire (more local channels than
  `LOCAL_CHANNELS_MAX`, Modbus without `HAS_RS485`). The step-1 firmware of `cyd-2xlocal` still has the
  single-channel Modbus code: without RS485 every request fails at once ("no answer", LED red).
- Limits to respect: no PSRAM → chart history, web page buffers and LVGL buffers must fit in internal
  RAM (TempCtl 1.2 left 97 kB free with one channel; measure after adding the second channel);
  `huge_app.csv` (3 MB app) — the web page and fonts must fit next to the firmware.
- Touch UI: its **own set of 320×240 screens** next to the 480×320 set (see 6).

## 3. Build and toolchain

TempCtl 1.2 (current state of the Duo repo):
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
- Measured on the CYD (1.2 on LVGL 9.6): flash ≈ 1.36 MB (before the web redesign, which
  added ≈ 17 kB); free heap 249 kB before UI, 158 kB after UI,
  97 kB after net/web/control (largest block 51 kB); loop stack ≈ 3 kB used in setup.

**Duo: planned build changes** (decided unless noted):
- New env `s3`: board `esp32-s3-devkitc-1`, stay on `espressif32@6.9.0` / Arduino 2.0.17 (same core as
  TempCtl), `board_build.arduino.memory_type = qio_opi`, `board_build.flash_size = 16MB`,
  `board_upload.flash_size = 16MB`, a 16 MB partition table (e.g. `default_16MB.csv` or a custom one
  with a larger LittleFS), `-D BOARD_HAS_PSRAM`.
- **CYD envs `cyd-1xlocal-1x485`, `cyd-2xlocal`, `cyd-2xlocal-2x485`** (2b; replace env `cyd`, share the section `[cyd]`
  in `platformio.ini`; the CYD is a second supported device). Board differences live in
  per-board headers (**decided**: `src/board_s3.h`, `src/board_cyd.h`, selected by the build flag
  `-D BOARD_S3` / `-D BOARD_CYD` in the env) — pins, display/touch driver, resolution, LED type, memory sizes;
  the rest of the code is shared.
- Upload and log monitor (decided): the **"UART" USB port** (USB-to-UART bridge, auto-reset via
  DTR/RTS). `ARDUINO_USB_CDC_ON_BOOT=0` → `Serial` = UART0 (GPIO 43/44), log + exception decoder.
- **Control port on the S3 = the native "USB" port** (Rost, 2026-09-28): the serial console (7a) and
  the Web Serial page (7b) use it, so opening it never resets the board. **Decided**: USB-OTG mode with
  TinyUSB CDC (`ARDUINO_USB_MODE=0`, `USBSerial` / `USBCDC`) and **`enableReboot(false)`**, so no
  DTR/RTS pattern or 1200-baud touch can restart the chip; flashing stays on the UART port. (The default
  USB Serial/JTAG mode can itself reset the chip on certain DTR/RTS states — avoid it for this port;
  verify on hardware.)
- **Drivers per board** (decided): S3 — display through LVGL's ST7796 driver (below), touch through
  our own FT6336U driver. CYD — display through **LVGL's ILI9341 driver** with the same SPI/DMA glue
  (so TFT_eSPI can be removed from both envs), touch keeps **XPT2046_Touchscreen** (resistive, with the
  existing calibration/mapping).
- **S3: no TFT_eSPI / XPT2046.** Display through **LVGL's own ST7796 driver**
  (`LV_USE_ST7796`, `lv_st7796_create()` with send-cmd / send-color callbacks on the ESP-IDF
  `spi_master` with DMA; `lv_display_flush_ready()` from the post-transaction callback; RGB565 byte swap
  with `lv_draw_sw_rgb565_swap()`; rotation via `lv_display_set_rotation()` → MADCTL).
  Draw buffers in internal DMA-capable RAM; large allocations (LVGL heap, chart history) may go to PSRAM.
- **Touch: own FT6336U driver** (Rost chose this, not a third-party library). LVGL has no built-in
  driver for I²C touch controllers on MCUs — only the `lv_indev` read callback. Plan
  (`src/touch_ft6336.*`, ≈ 40 lines + indev glue): reset pulse on CTP_RST (low ≥ 5 ms, then wait
  ≈ 300 ms), `Wire` on SDA 16 / SCL 15 at 400 kHz, read registers 0x02–0x06 in one transfer
  (0x02 TD_STATUS low nibble = touch count; 0x03 P1_XH bits 7:6 event flag, bits 3:0 X[11:8];
  0x04 P1_XL; 0x05 P1_YH bits 3:0 Y[11:8]; 0x06 P1_YL), map coordinates to the display rotation,
  optional CTP_INT to skip reads when not touched, **any I²C error → report "released"** (no stuck
  presses). Verify register details against the FT6336U datasheet / register table on lcdwiki.
- Verification toolchain in the cloud container must change to **xtensa-esp32s3** gcc + the S3 variant
  of arduino-esp32 2.0.17 (section 8).

## 4. Source layout (TempCtl 1.2 base)

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
9999 min); name ≤ 23 chars (`PROG_NAME_LEN` 24 is part of the file header). Run states
Idle/Running/Paused/Finished/Aborted ("stopped" in the API), phases Ramp/Hold.

Duo: new files planned — `src/channel.*` (channel interface + Modbus and local backends),
`src/tc_sensor.*` (MAX31855/MAX6675), `src/pid.*` + `src/autotune.*` (host-testable like
`program.cpp`), `src/board_s3.h` / `src/board_cyd.h`, `src/touch_ft6336.*` (S3), display glue for
ST7796 (S3) and ILI9341 (CYD), `src/console.*` (serial console + `api` mode, 7a/7b). `app_state` gets two channels and the
two-channel run engine (9.3).

## 5. Behaviour (TempCtl 1.2, per channel in Duo)

- **Program run**: the first ramp starts at the PV measured at start. Ramp mode "ESP32 writes setpoint
  steps" (min. step and min. seconds between writes, to spare the PMA EEPROM) or "controller ramps"
  (writes target + r.SP gradient). Optional guaranteed soak (hold time counts only inside ±band) —
  **removed in Duo** (9.3). End/stop action: keep SP, set end SP, or outputs off (C.Off). Front panel lock while running
  (L-R). Pause after link loss (s). Setpoint limits spMin/spMax.
  Duo: phases and timing follow 9.3; the Modbus-only options (ramp mode, C.Off, L-R) apply to Modbus
  channels; a local channel always
  follows the engine's setpoint directly (its PID gets the SP every cycle) and "outputs off" = SSR off.
- **Manual setpoint**: on/off switch. On = write SP and switch the outputs on (C.Off = 0); off =
  outputs off (C.Off = 1). Starting a program ends manual mode. Duo: one per channel, and a run ends
  manual mode only on channels that have phases/steps in the program (9.4).
- **Chart**: idle = rolling 5-min window, PV scrolls right-to-left, the selected program is shown
  dashed as a preview on its own time scale across the full width. Running = fixed axis over program
  total + max(10 %, 30 s); plan and PV grow from the left. Solid = active, dashed = not active. Series
  order PV, SP, program (LVGL draws the first series last → PV on top). Duo: two channels on one chart
  and one plan curve per channel (layout open, 9.7).
- **Step times** are stored in seconds; editors offer min or s per value.
- **Web UI parity**: Status (Main), Programs, System, Controller, Tools on both UIs.
- PMA KS 4x-1 register defaults ("1dP" column, value = register/10, DP1 offset in `settings.cpp`):
  PV 13294, SP.EF 13293, Ypid 13295, St.Cntr 5100, St.Ala 250, SP 11372, SP.LO 11292, SP.Hi 11293,
  r.SP 11295, C.Off 1086, L-R 1080; read fn 3, write fn 6, off raw −32000.

## 6. Touch UI (LVGL 9.6 + LVGL Pro Editor)

> **Changed 2026-10-02 (Rost, step 6): the new screens are hand-written LVGL C code** (`src/tui_*.cpp`),
> not LVGL Pro XML — the exporter can't run in the cloud, and C is leaner (helps the CYD's RAM). Previews
> are rendered on the PC (`test/ui_host`) for Rost's review. Order: **S3 first** (done in 1.0.19), then the
> lean 320×240 set for the CYD (done in 1.0.21; the exported TempCtl 1.2 screens below are no longer built,
> and **removed in 1.0.28**: `ui/`, `src/ui.cpp`, `src/ui_gen.*`, `src/program.*` — the text below about
> them is history).
> Look = the web page (Barlow / Barlow Semi Condensed via `tools/lv_font_gen.py`, the web palette, accent-
> derived channel colours, dark / light from the touch setting). The rest of this section describes the
> old exported UI (still used on the CYD).

- LVGL 9.5+ has no open-source XML loader; the UI is **C code exported by LVGL Pro Editor** from the
  XML in `ui/` ("Export C code", `project.xml` `lvgl_version="9.5.0"` — the editor offers at most 9.5;
  the export compiles fine with 9.6). XML and exported files are committed together. After XML changes
  Rost exports; simple string/number changes may be patched in both XML and `*_gen.c` by hand (a
  re-export produces the same).
- Duo: **two sets of screens** (Rost, 2026-09-28) — 480×320 (S3) and 320×240 (CYD, second supported
  device), as separate XML screens in the LVGL Pro project that share components, styles, fonts,
  translations and **the same widget names**, so `ui.cpp` binds to either set without changes (a widget
  that exists only in one set is looked up optionally). The build includes only the set of its board
  (per-board source filter or a flag in `ui_gen.c`), so the CYD doesn't carry the 480×320 screens.
  During hardware bring-up the old 320×240 UI may simply run in a corner of the 480×320 screen.
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
- **Colours — TempCtl 1.2 base**: 6 palettes (Default, Dracula, Solarized, Gruvbox, Catppuccin, One)
  × dark/light, stored in Settings (`themePalette`, `themeDark`); `color_*` in `ui/globals.xml` =
  Default dark = the editor preview, the others are constants `<palette>_<dark|light>_<role>` (14 roles:
  bg panel border text muted accent accent2 row_selected danger pv sp plan ok warn); `src/ui_gen.c`
  builds them (`PAL()`, `paletteLoad()`, `PALETTE_NAMES`) and recolours the styles listed in
  `STYLE_COLORS`; `ui.cpp` `applyTheme()` re-inits the LVGL default theme and sets chart series and
  table colours. **Rule: widgets get colours only through named styles**, never `style_…_color` on the
  widget.
- **Duo: no colour palettes any more — only the accent colour is selectable** (Rost, 2026-09-28).
  - One fixed base palette (the `color_*` constants in `globals.xml`); remove the other palette
    constants, `PAL()`/`paletteLoad()`/`PALETTE_NAMES`, the palette dropdown in Setup › System and
    `themePalette` / `themes[]` in settings and API.
  - The user picks the **accent colour** (Setup › System and web System page); **stored as an RGB
    value** (`accentColor`, decided) — the UI offers the 6 presets now, and a free colour picker can be
    added later without a settings change. The channel colours are always computed from the stored RGB
    (split-complementary + contrast rule, below), so any accent works. Applying it: recolour the named styles that use the accent role
    (`style_text_accent`, `style_border_accent`, … — the `STYLE_COLORS` mechanism reduced to the accent
    role) and pass it as the primary colour to `lv_theme_default_init()`, so switch/slider indicators,
    checkboxes and focus outlines follow the accent too.
  - Channel colours: each channel has its own PV / SP / plan colour roles (e.g. `pv_b sp_b plan_b`,
    `pv_t sp_t plan_t`), all derived from the accent (below).
  - **Accent = preset list** of the 6 former palette accents (decided). **Channel PV colours are
    split-complementary to the accent (agreed 2026-09-28, replaces "PV = complement")**: bottom PV =
    accent hue − 150°, top PV = accent hue + 150°, same HSL saturation/lightness as the accent (base
    values before the per-theme contrast adjustment below), computed in C when the accent is applied
    and by the same rule in the web page:

    | Accent | Hex | Bottom PV | Top PV |
    |---|---|---|---|
    | Blue | 2196F3 | F3E721 (57°) | F3212D (357°) |
    | Pink | D1449B | 44D1C0 (173°) | 55D144 (113°) |
    | Violet | 6C71C4 | 9DC46C (87°) | C4936C (27°) |
    | Orange | D65D0E | 0E23D6 (234°) | 0ED6C1 (174°) |
    | Mauve | 8839EF | 45EF39 (116°) | EFE339 (56°) |
    | Green | 50A14F | A14F79 (329°) | 774FA1 (269°) |

    The PV/SP roles therefore become accent-driven (recolour the PV/SP named styles, chart series and
    table colours with the accent). Known closeness to status colours (Rost accepted it after seeing the
    samples): Blue → bottom yellow / top red, Pink → top green, Violet → top orange, Mauve → green /
    yellow; Orange and Green are clean. Check on hardware; status colours may be adjusted if it confuses.
  - **SP colours (agreed): same hue as the channel's PV, lighter** — one hue per channel.
  - **Light theme uses darker variants (agreed)** for readability. Rule (agreed; same code on device
    and web): keep the hue and saturation, darken the lightness until the colour reaches a minimum
    contrast against the panel colour — **PV ≥ 4.5:1, SP ≥ 3:1** (WCAG luminance ratio); SP starts
    0.12 lighter than its PV. On the dark theme a colour is only lightened if it is below the same
    minimums. Computed once when the accent or the theme changes.
  - **Theme (decided)**: the touch UI keeps a **Dark / Light setting** (Setup › System, stored in
    Settings). The web page **follows the system theme** by default and has its own Dark / Light / Auto
    switch that the browser remembers (localStorage) — it does not follow the device setting.
  - **The same palette drives the Web-GUI** (Rost, 2026-09-28): accent, PV and SP colours of both
    channels are identical on touch and web (the settings API delivers them; section 7).
  - The rule "colours only through named styles" stays.
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

- **Look (decided 2026-10-14, Rost; version 1.0.40): frosted glass after his NVBHarvester tool**, worked out on a
  concept page (claude.ai artifact "TempCtl Duo Glass Concept") and moved into `web/index.html` as one CSS block at
  the end of the style (`html:root …` rules = class specificity, no class needed: the device / launcher copies
  rewrite the exact `<html lang="en">` tag) plus a few scripts. Layout, Barlow, accent and channel colours stay.
  - Backdrop: mesh gradient tinted by the accent + SVG grain (fixed, painted once). Sidebar and panels: translucent
    glass, `backdrop-filter: blur(22px) saturate(1.25)`, 18 px radius; only these blur (controls on them are plain
    translucent fills). `prefers-reduced-transparency: reduce` → no blur. Buttons and fields: pills / 12 px.
  - Logo (new colours from `logo/TempCtl.svg`, Rost 2026-10-13: light blue square #86C9FF, gear #0062D3,
    thermometer #FF5555): `web/icons/logo.svg`, `favicon.ico` (16/32/48), `apple-touch-icon.png` (180, square);
    54 px in the sidebar, no frame, soft blue glow.
  - Title bar: stays on top; behind it a copy of the backdrop (blurred), faded out at its bottom edge; its opacity
    follows the scroll (`--tf`: 0 at the top of the page, 1 when the first card reaches the top).
  - Channel cards: the channel colour as a glowing strip on the top edge. The "Manual" switch on the cards looks
    like the sidebar's power button (CSS only; off glass, on channel colour).
  - Output on (manual or program, live `outOn`): the card turns into the channel colour — 20 % tint over the glass,
    4 px edge in the lit channel colour, soft glow, no top strip; back to normal when the output is off (Rost,
    2026-10-14).
  - The backdrop's lower-left gradient follows the accent in dark mode too (was a fixed teal).
  - Channel colours: same rule (split-complementary, 4.5:1), now against the frosted card colour (#E4EBEF light,
    #1A2329 dark). Extra "lit" colours `--vB` / `--vT` (same hue, full saturation, only 1.6:1 in light mode) for
    strips, glows, meters, the tab arc and the sidebar dots, so the yellow stays vivid in light mode; text and
    chart lines keep the contrast-fitted colour.
  - Channels page tabs: css-tip "rounded tab" shapes (`shapeTabs()`): first tab flush with the card's left edge
    (".left" variant), the others curve out into the card's top edge; the inactive tab tucks under the selected
    one's foot, its fill lighter (dark) / darker (light), the card's top line runs on under it. The selected tab
    and the card share one frosted background (`.tabbg`, clipped to the union of both shapes: no seam); outlines
    are 1 px paths on the card border's pixel grid; the card's top edge is open under the selected tab
    (`--g0` / `--g1`). Reshaped on tab change, resize, fonts ready and card size changes (ResizeObserver).
  - Dropdown lists: solid theme colour for the options (the translucent field colour made them light).
- **Chart: every reading (decided 2026-10-14, Rost; version 1.0.41)**: the web chart draws each PV reading of the
  polling (250 ms … 10 s) instead of the 1-s / per-bin slots, live view and run chart. Device (`src/raw_hist.h`,
  host-tested): per channel a ring of (millis, PV) pairs, 2 words each (32-bit access: the CYD keeps it in its IRAM
  heap), one entry per poll that brought a new value (`ChannelLive.lastOkMs` changed; two Modbus controllers are
  read in turns), a gap entry (PV −32768) when a channel has no valid reading. S3 2 MB PSRAM = 131 072 per channel
  (36 h at 1 s, 9 h at 250 ms), CYD 600 per channel (10 min at 1 s; allocated before the live history, which gets
  that much less of the IRAM heap). Run time against millis: `RunAnchors` (start, pause, continue, jumps; linear in
  between), sent with the run chart (`ra`, `rn`). `GET /api/raw?a0=&a1=&win=&max=` (binary, see README). Page:
  `rawPull()` each poll (only readings after the newest it has; the whole window again when the window grows, e.g.
  a longer live period), `applyRaw()` puts them on the chart (live: seconds before the chart's `ms`; run: run time
  from the anchors, after a finished run nothing), older than the first kept reading the slots as before. More
  points than pixels: per pixel column first / lowest / highest / last (`thin()`), so no peak is lost; zoomed in,
  every reading. Over USB (Web Serial) the slots as before. Touch chart unchanged (slots). Tested in the mock:
  12 h at 250 ms = 167 000 readings, 19 requests, 52 ms to place and draw.
  - Cost: about +5 kB gzipped page (70 kB); no RAM or CPU change on the device; the blur is GPU work in the
    browser.
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
  `themePalette`, `themeDark`, `themes[]` (Duo: replaced by the accent colour, see 6).
- **Duo: new Web-GUI design — the 9.3 run simulator is the starting point** (Rost, 2026-09-28). The
  MoldControl look described above is the TempCtl 1.2 page; Duo's page takes over the simulator's
  look and structure instead: `claude/web-sim/tempctl-duo-93-sim.html` in the repo (copy of the
  published simulator, ≈ 81 kB, ≈ 24 kB gzipped).
  - Taken over: light/dark following the system setting (theme tokens on `:root`), panels on a calm
    background, Barlow / Barlow Semi Condensed with tabular numbers; run panel with the state badge,
    run clock and remaining time, Start / Pause-Continue / Stop / Next phase; the chart with the two
    state lanes, phase labels + end times, pause markers, forecast (dashed), drag handles (target /
    ramp end / hold end), zoom / scroll; channel cards (PV, SP, output bar, status line, target ±1/±5
    + Set, Extend hold +10 s/+30 s/+1 min, manual switch + SP for a channel without phases); program
    editor (sequence table / phone cards, two timeline tables), Save / Discard / Undo / Redo; the
    "Action required" overlay; event log; the save-after-run dialog.
  - Dropped (simulation only): speed selector, Reset, the thermal model, the forecast of PV, the
    "How this reads 9.3" notes. The forecast of the setpoints stays (computed from the program as in
    the firmware engine).
  - Must change for the device:
    - **Works without internet (AP mode)**: no Google Fonts — the Barlow fonts are served by the
      device (woff2 subsets from LittleFS/PROGMEM, gzip, long cache) with a system-font fallback.
    - Data from the JSON API instead of the in-page engine; edits are sent as commands (run edits go
      to the control task, 9.3.4). The browser keeps only view state (zoom, drafts until saved,
      undo/redo stack) — Save writes the program to the device.
    - Served gzipped (`Content-Encoding: gzip`). Size: no problem on the S3 (16 MB flash); on the
      **CYD** (3 MB app partition, no PSRAM) page + Barlow subsets (≈ 24 kB + fonts) must fit next to
      the firmware and be served from flash without large RAM buffers — measure.
    - The other pages (Programs list, Channels/Controller, System, Tools — full parity with the touch
      UI) are added in the same visual language.
  - **Navigation: sidebar as in TempCtl 1.2** (Rost): page tiles in one column, always visible
    ≥ 860 px, slide-in panel with a menu button on phones; link status, network info, clock and log
    download in its footer — restyled in the simulator's look.
  - **One colour palette for touch and web** (Rost): accent and the PV/SP colours of both channels
    come from the device's palette (section 6: accent preset, split-complementary PV colours, lighter SP, contrast rule per theme). The web
    page reads the palette from the settings API and sets its CSS colour tokens from it, so a change of
    the accent on either UI shows on both. The simulator's fixed bottom blue / top orange are only
    placeholders.
- Duo: the API gets a channel dimension (status/history per channel, `/api/manual` and the skip command
  with a channel, program type (Sequence/Timeline), Sequence phases (Bottom/Top/Both, bottom Idle, "wait for bottom") or
  Timeline step lists, run edits (extend hold, 9.3.4), channel settings,
  PID/autotune/calibration endpoints). `/api/select` stays one program for the run. Status page layout: 9.7.

## 7a. Serial console (Duo, requested by Rost 2026-09-28)

Control over the serial port, on both boards (**S3: the native "USB" port** — control only, the log
stays on the "UART" port; **CYD: its only USB serial**, shared with the log),
**115200 8N1** (decided; same as the monitor in `platformio.ini`). Scope: **manual temperature
control, Wi-Fi setup, help** (Rost); status is added because the manual commands need feedback.

**Behaviour (decided — all serial proposals accepted by Rost, 2026-09-28):**
- **On boot** the device prints a short banner (name, FW version, board) and the **help text**, then a
  prompt `> `, on the control port (S3: also a one-line banner on the UART log port). `help` prints it
  again. Because the S3's USB CDC port only exists once the host has opened it, the S3 also prints the
  banner + help when a terminal opens the port (DTR goes active) — so the user always sees it.
- Line based: a command ends with CR or LF; commands and keys are case-insensitive, values are not.
  The device echoes typed characters and handles Backspace, so it works in PlatformIO's monitor and
  in a plain terminal. Parameters are `key=value`; a value with spaces is quoted: `s="My Net"`.
- Log output shares the port: a log line that arrives while the user is typing is printed on its own
  line and the prompt with the typed text is shown again.
- **Passwords are never echoed, logged or printed**: the password prompt shows `*` per character; an
  empty password means an open network (station) / is rejected for an AP (WPA2 needs 8–63 chars).
  A `pw=` given on the command line is not echoed back either.
- The reader is non-blocking in the loop task; waiting for a Wi-Fi connection does not block the loop
  (watchdog 15 s) — the result is printed when it arrives. Commands use the same code paths as the
  touch UI and the web page (`net.*`, `sendCmd`), so all three stay consistent.
- Errors are one line starting with `error:` (unknown command, missing parameter, value out of the
  setpoint limits, channel is running a program, …).

**Commands:**

| Command | Result |
|---|---|
| `help` | the command list below |
| `status` | per channel: PV, SP, output %, state (idle / manual / program phase); run state; Wi-Fi state |
| `manual b on sp=180` · `manual t sp=200` · `manual b off` | manual setpoint per channel (`b` = bottom, `t` = top): `on` = write SP and switch the outputs on, `sp=` alone changes the SP (turns manual on if needed), `off` = outputs off. Same rules as the touch/web manual switch: not allowed for a channel that is part of the running program (`error: bottom is running a program`) |
| `wifi scan` | numbered list of networks, strongest first: `1  MyNet  -52 dBm  WPA2  ch 6`; the list is kept for `n=` |
| `wifi connect s=<SSID> [pw=<PASSWORD>]` · `wifi connect n=<number> [pw=<PASSWORD>]` | without `pw=` the device asks `Password:` (masked). Then `Connecting to "MyNet"…` and the result: `connected, IP 192.168.1.23, RSSI -55 dBm` or `failed: wrong password / network not found / timeout`. On success the credentials are saved like on the touch/web Wi-Fi page |
| `wifi ap=<AP SSID> [pw=<PASSWORD>]` | without `pw=` the device asks `Password:` (masked, 8–63 chars). Starts the access point and prints `AP "TempCtl-Duo" started, IP 192.168.4.1`; saved as the AP settings |
| `wifi status` | mode (station / AP / off), SSID, IP, RSSI |
| `programs` | numbered list of the stored programs with type (sequence / timeline) and total time; the selected one is marked |
| `program n=<number>` | select a program for the next run |
| `show` | the selected (or running) program printed read-only: phases / steps with their values. **No editing of the program over serial** (Rost) |
| `start` · `pause` · `resume` · `stop` | run control, as the run bar on touch / web |
| `next` | next phase (Sequence only; `error:` for a Timeline, where it is disabled) |
| `continue` | continue after a program pause ("Action required"); `stop` ends the run instead |
| `resume run` | resume from the interrupted phase after "Run interrupted by a reset" (9.3.5) |
| `extend b\|t +<s>` · `target b\|t <C>` | the run edits of the touch status screen: extend the current hold, change the current (or kept) target — same rules as 9.3.4 (not table editing) |

- **Run events are printed** as they happen: phase changes, "waiting for bottom", end of run, faults.
  A program pause with the "Action required" option prints a highlighted block
  (`*** ACTION REQUIRED — phase 2: Place the IR head — type "continue" or "stop" ***`, with the
  pause's custom text if it has one), repeated every 30 s.
- After a run with changes the device asks on all interfaces; over serial: `run changed the program —
  type "save" or "discard"`.

**Help text printed on boot (draft):**
```
TempCtl Duo 1.0.0 (esp32-s3) - serial commands:
  help                         this list
  status                       temperatures, setpoints, outputs, run and Wi-Fi state
  manual b|t on sp=<C>         manual setpoint on (b = bottom, t = top)
  manual b|t sp=<C>            change the manual setpoint
  manual b|t off               outputs off
  wifi scan                    list networks (numbered)
  wifi connect s=<SSID> [pw=<PASSWORD>]
  wifi connect n=<number> [pw=<PASSWORD>]
  wifi ap=<SSID> [pw=<PASSWORD>]   start an access point
  wifi status                  current Wi-Fi state
  programs | program n=<n> | show   list, select, show programs
  api on | api off              machine (JSON lines) mode for the web page (7b)
  start | pause | resume | stop | next | continue   run control
  extend b|t +<s> | target b|t <C>   extend the running hold, change a target
  resume run                   resume after "Run interrupted by a reset"
Without pw= the password is asked for (hidden).
```

**Program control over serial: agreed (Rost), editing of program tables: not over serial.**

## 7b. Desktop page without install — Web Serial (agreed 2026-09-28)

A **static web page, no installation** (Rost), working in **Chrome / Edge** on the desktop (the Web
Serial API is not available in Firefox / Safari). Delivered for now by a portable launcher that
contains a tiny `localhost` server (below) — nothing is installed and no separate server program is
needed.

- **Two ways to connect:**
  1. **IP**: the user enters the device's address (or `tempctl-duo.local`) and the page simply **opens
     the device's own Web-GUI** (`http://<address>/`, new tab or same tab). Nothing else — the device
     serves its page, so no API check is needed.
  2. **Serial (Web Serial API)**: the page itself shows **the same Web-GUI** (look and functions of
     the device page, section 7) and talks to the device over the serial port. Only this path needs the
     **API compatibility check**.
- **One GUI source**: the device page and the serial page are built from the same source; all API
  access goes through one small JS transport layer (`fetch` on the device, serial JSON lines here).
- **How the page is delivered** — Web Serial needs a secure context: https, or `http://localhost`.
  **Now: the local app (2). GitHub Pages (1) is a planned feature for later** (Rost, 2026-09-28) —
  keep the page buildable as plain static files so it can be published there without changes.
  1. **GitHub Pages** (https) — *planned, later*. GitHub Free: Pages only from **public** repositories; from a private
     repository it needs GitHub Pro (personal account) or Team/Enterprise, and even then **the published
     site itself is public** (only Enterprise Cloud can restrict access). Options: make `TempCtl-Duo`
     public, get Pro, or (proposal) publish only the built page from a small **separate public repo**
     (e.g. `justr0st/tempctl-duo-web`, updated by a script/Action from the firmware repo) while the
     firmware repo stays private. The page contains no secrets — access to a device still needs the
     local serial-port permission or the device's own address. Optionally cached for offline use.
     **Alternative: Cloudflare Pages** (asked 2026-10-13, not decided): free plan, its Git integration
     also builds from a **private** GitHub repo (or Direct Upload of the built folder with `wrangler`);
     build command `python3 tools/build_site.py <out>`. Same limit as GitHub Pages: an https page reaches
     a device only via Web Serial (the device's http API is mixed content).
     **Chosen (Rost, 2026-10-14; 1.0.45, b16a94b): Cloudflare Workers static assets** (the dashboard's "Create application"
     now leads to Workers; Pages only via a side link). `wrangler.jsonc` (name `tempctl-duo`, assets `dist/`),
     Workers Builds on push to `main`: build `python3 tools/build_site.py dist --web` (no shell.html, `_headers`:
     nosniff, no-referrer, DENY framing, Permissions-Policy serial=(self); `/f/*` cached a year), deploy
     `npx wrangler deploy` → `https://tempctl-duo.<account subdomain>.workers.dev`. No device search / tabs there.
     Live since 2026-10-14: https://tempctl-duo.just-r0st.workers.dev/ (checked: page = repo's index.html, connect screen,
     fonts load, headers as in `_headers`, HTML / icons revalidated, fonts cached a year, shell.html not deployed).
     Rost (2026-10-14): Web Serial from the hosted page works.
  2. **Local app, no installation** — *the delivery for now*. **Decided: one Go executable**
     (`tempctl-duo.exe`, ≈ 6 MB, Windows first; macOS/Linux builds possible from the same source) with
     the page files embedded (`embed`), serving them on **`http://localhost:8765`** (fixed) and opening
     Edge or Chrome as an app window; if the port is taken it tells the user (a second instance just
     opens the window). Built by a script in the repo from the same page files as the device page;
     carries the project version (9.8). In short: a small portable launcher (one file, no runtime needed) that serves
     the same page on **`http://localhost:<fixed port>`** and opens it in Chrome/Edge as an app window
     (`--app=`). localhost counts as a secure context, and the **fixed port** keeps the origin the same,
     so the granted serial port is remembered and automatic reconnect works. Built from the same page
     files as the Pages version. (Opening the page straight from a file, `file://`, is not reliable for
     Web Serial and its remembered permissions — not the recommended way.)
  - Serial permissions belong to the origin: the Pages version and the local app each ask once.
  - Opening the device's `http://` page from the https page is a plain navigation, so there is no
    mixed-content problem.

**Serial session:**
- **Connect** (button → browser port picker): open the port at 115200 8N1, then send **`api on`**. The
  device answers with a hello line: `{"t":"hello","api":1,"fw":"1.0.12","board":"s3","name":"…"}`. The
  page compares `api` with the versions it supports; if incompatible it says so (and which firmware /
  page version is needed) and does not control the device. Then it loads the full state (status,
  programs, settings, chart history) and follows the status/event stream.
- **Disconnect** (UI button): send **`api off`**, then close the port. The device returns to the human
  console (7a).
- **Automatic reconnect without touching the run**: if the cable is unplugged, the tab is closed or
  the browser crashes, **the device keeps running the program**; losing the serial client never changes
  the run. The page reopens a previously granted port automatically — on page load
  (`navigator.serial.getPorts()`, matching USB vendor/product id) and when the device is plugged in
  again (`navigator.serial` `connect` event) — sends `api on` again, re-reads the full state and continues
  where the run is now. An "Action required" pause or a pending "save run changes?" question is shown
  again after reconnecting.
- **Device side of `api` mode**: JSON lines, one object per line — requests with an `id`
  (`{"id":7,"m":"GET","p":"/api/status"}`), responses with the same `id`, pushed messages with a type
  (`status` ~1/s, `event`, `log`); the same endpoints and JSON as the HTTP API (7). Plain log output is
  wrapped as `{"t":"log",…}` while in api mode. If no request arrives for **15 s** (decided; the page sends a
  keep-alive **every 5 s**), the device drops back to the console by itself — so a closed page never
  leaves the port stuck in api mode, and the next `api on` just starts a new session.
- **Auto-reset** (decided): **S3 → the page uses the native USB port** (control port, section 3;
  TinyUSB CDC with reboot disabled), so opening the port cannot reset the board. **CYD → as described**:
  its only USB serial has the DTR/RTS auto-reset circuit; the page sets `dataTerminalReady` /
  `requestToSend` to the non-reset state right after opening (`port.setSignals`) — test during
  bring-up; if the CYD still resets on open, a hardware change (disable auto-reset during operation)
  is the fallback. In any case the device reports **"run interrupted by a reset"** (9.3.5).

## 8. Verification approach (cloud container)

Rebuild these in `/tmp` if the container was reset (they were not kept in the repo):
- **Duo: `claude/tools/build-check.py <s3|cyd> <project>`** (2026-09-29) — setup commands in its header
  (toolchains from GitHub releases, arduino-esp32 2.0.17 / LVGL 9.6 / ArduinoJson / XPT2046 via git
  clone; PyPI and the PlatformIO registry are blocked, so no `pio`). It takes the flags from the core's
  `tools/platformio-build-<mcu>.py`, compiles core + libraries + LVGL + `ui/` + `src/` (own files with
  `-Wall`), links `firmware.elf` and prints section sizes. First full build ≈ 3 min per env on 2 cores,
  then only changed files. Keep its env table in sync with `platformio.ini`.
- ESP32 compile check (TempCtl, classic ESP32): xtensa-esp32-elf gcc 8.4 (esp-2021r2-patch5) +
  arduino-esp32 2.0.17 + LVGL 9.6 (`git clone --depth 1 --branch v9.6.0 https://github.com/lvgl/lvgl`
  worked; curl to GitHub/registries was blocked) + TFT_eSPI + ArduinoJson; compile the changed `.cpp`
  with the project flags, C files with xtensa gcc directly; link everything for the flash size.
  **Duo: needs the xtensa-esp32s3-elf toolchain and the esp32s3 SDK part of arduino-esp32 2.0.17**;
  Keep the classic ESP32 toolchain too for the `cyd` env. If the container can't fetch them, rely on
  Rost's PlatformIO build output.
- Host simulator: LVGL 9.6 built for the host with the project's `lv_conf.h` and shims for Arduino,
  Preferences etc.; compiles `ui/*.c`, `src/ui_gen.c`, `src/ui.cpp`, `program.cpp`, `settings.cpp` +
  stubs; drives touches/tabs and writes PPM screenshots of all screens; ASan/UBSan build. Duo: 480×320,
  display/touch shims instead of TFT_eSPI/XPT2046. For heap checks use
  `GLIBC_TUNABLES=glibc.malloc.tcache_count=0`. Lesson: host tests get zeroed stack memory —
  uninitialised-variable bugs only show on hardware.
- Web UI: headless Chromium (Playwright) against a small Python mock of the JSON API (states running /
  paused / manual / no link), desktop 1366×820 and phone 390×844, check for page errors, look at the
  screenshots; `node --check` on the extracted script. Duo: mock with two channels.
- Host tests: `test/host_test.cpp` (see 4). Duo: add PID, autotune (against a simple first-order +
  dead-time heater model), calibration and thermocouple frame decoding (MAX31855 fault bits, MAX6675
  open bit).

## 9. TempCtl Duo — goals (agreed with Rost 2026-09-28)

### 9.1 Channels
- Two temperature channels: **Bottom** (preheater underneath) and **Top** (top heater).
- For **each channel** Rost selects the type in the settings:
  - **A. Modbus**: one Modbus RTU controller per channel (PMA KS 4x-1 preset or generic register map),
    each with its own slave address and register map. Both Modbus channels share the one RS485 bus
    (common baud/parity); the control task polls them alternately.
  - **B. Local**: thermocouple (MAX31855 or MAX6675, selectable) + SSR output on the ESP32, **PID on the
    ESP32**, with **calibration**.
- Any combination is allowed (Modbus + Modbus, local + local, mixed).
- Engine, chart, UI and web API talk to a common channel interface (read PV/SP/output/status, write SP,
  outputs on/off, fault state); they don't care about the backend.

### 9.2 Local channel details
- Sensor: MAX31855 (32-bit frame, 0.25 °C, fault bits OC/SCG/SCV, cold-junction temperature) or MAX6675
  (16-bit, 0.25 °C, open-input bit D2, ≥ 220 ms between conversions → read every 250 ms).
- Calibration: two-point (offset + gain), e.g. two reference points raw → true; defaults 0 / 1.
- PID: manual parameters (Kp, Ki, Kd; anti-windup; output 0–100 %, optional max output limit) **plus
  autotune** (relay method around a user setpoint with hysteresis, result as proposed Kp/Ki/Kd that
  the user accepts or discards). **Tuning rule: Tyreus–Luyben** for both heaters (decided after
  comparing it with Ziegler–Nichols in simulation: no overshoot, ZN overshot 9–13 °C):
  Kp = Ku / 2.2, Ti = 2.2 · Pu, Td = Pu / 6.3 (Ku = 4d / (π·a) from the relay swing). Relay 0 / 100 %
  output, hysteresis ±0.5 °C, several cycles; details (amplitude limit, max test time) in
  implementation.
- Output: time-proportional SSR drive — the SSR is on for the output share of every **window**
  (e.g. 40 % of 1 s = 0.4 s on, 0.6 s off). **Default window 1 s for both channels, settable per
  channel** (decided). With a zero-cross SSR on 50 Hz mains that gives 1 % steps (100 half-waves per
  second).
- **Max temperature (SSR cut-off) default: 400 °C for both channels** (decided; settable per channel).
  The setpoint limits (spMax) are kept at least 10 °C below the cut-off (enforced when saving settings).
- Safety (**agreed**): SSR off on sensor fault or stale reading, above a configurable max temperature,
  when the control task stops updating the output, during boot (pin low until control starts; hardware
  10 kΩ pull-down), after a watchdog reset.
- **Thermal runaway detection (agreed)**: available per channel with an enable switch; it is only active
  for the channel's **selected type**, and each type has its own runaway settings (switching a channel
  between Modbus and Local uses that type's settings):
  - Local: uses the ESP32's own PID output and PV; on a fault the SSR goes off.
  - Modbus: uses the controller's output (Ypid, register 13295 on the PMA) and PV; on a fault the ESP32
    switches the controller's outputs off (C.Off = 1).
  - Checks (decided, like 3D-printer firmware): *heating failed* — output ≥ X % for T s but PV rose
    less than ΔT; *runaway while holding* — PV leaves SP ± band for longer than T s after it was
    reached.
  - **All values incl. the timing are settings in the firmware** (Rost), per channel and per channel
    type, editable on touch and web (Channels page) and stored in Settings: min output %, heating
    period s, min rise °C, hold band °C, hold period s, enable.
  - **Defaults (decided)**: heating failed = output ≥ 80 % for **120 s** (**bottom channel 300 s**)
    with less than **5 °C** rise; runaway while holding = PV more than **±15 °C** from SP for **60 s**;
    enabled.
  - Bottom SP jumps (9.3) keep the bottom output at 100 % for a long time while the board heats
    slowly → generous "heating failed" defaults for the bottom channel. The local PID must avoid
    derivative kick and windup on setpoint jumps (derivative on measurement, clamped integrator).
  - A runaway fault stops the run (both channels, end action "outputs off" on the faulted channel),
    shows an alarm on screen and web, and must be acknowledged.

### 9.2a Protections (decided 2026-10-13, Rost picked from a list; version 1.0.38)
CFG = a setting, on by default unless noted. Channel ones live in the channel's Protection subsection
(web Channels, touch Channels, serial `b./t.<type>.runaway.*`), general ones on the Settings page
(web Settings › Safety, touch System, serial `safety.*`).
- **Heating while off** (stuck SSR / controller), CFG, `runaway.offEnable/offRise/offSec`, default 10 °C
  in 60 s: outputs off but the PV rises offRise within offSec (rise counted from the lowest reading; a new
  window after offSec). Starts 120 s after the outputs went off (heat still flowing) and pauses while the
  other channel heats (its heat reaches this sensor — e.g. the top TC on the board while the bottom
  heats). Modbus: only after this device wrote "outputs off" (`offKnown_`). Fault `FAULT_OFFHEAT`
  (latched; local: SSR relay off).
- **Reading frozen**, CFG, `runaway.frozenEnable/frozenSec`, default 120 s: outputs on and the unfiltered
  reading (local: converter value before the median; Modbus: PV) did not change at all. `FAULT_FROZEN`.
- **Converter too hot** (local, MAX31855 cold junction), CFG `local.cjEnable/cjMax`, default 70 °C:
  latched while heating (`FAULT_CJHOT`); outputs can't be switched on above it; Acknowledge refused until
  5 °C below.
- **Forgotten manual mode**, CFG off by default, `safety.manualIdle/manualIdleMin` (30 min): manual mode
  ends after that long without user activity (touch press, web POST, serial command, any `sendCmd`
  except the settings notification); counted from the later of activity and manual-on (also the kept
  setpoint after a run); a message + "action" beeps 1 min before.
- **Start temperature**: warning only (no setting): at Start, a used channel within 10 °C of (or above)
  its first target → "Warning: <ch> starts at X °C, near or above its first target Y °C".
- **E-stop input**, CFG off by default, `safety.estop`, S3 GPIO5, NC contact to GND (pull-up), read every
  50 ms (active after 2 readings, released after 10). Active: SSR timer and local task force the SSRs
  off, relay off ("SSR supply relay off: E-stop"), control task stops the run (no end action), manual /
  autotune off, Modbus outputs off; start / resume / manual / autotune refused while open. Not a latched
  fault: released = "Emergency stop released", outputs stay off until switched on again. Web: banner on
  the Dashboard + channel badges; touch: tile text + blinking badge. CYD: none (no free pin).
- **Alarm sound**, CFG `safety.sound`: S3 GPIO6, CYD IO26 (speaker amplifier P4 — Rost asked for IO35,
  which is input only). LEDC channel 6 (timer 3), 2.7 kHz, esp_timer 50 ms patterns: fault / E-stop 3
  long beeps every 3 s; "Action required" pause + manual-idle warning 2 short beeps every 10 s; run
  finished 3 short beeps once. Any user activity mutes the current level until it clears or a higher one
  comes. Test beep: `POST /api/beep`, touch switch on.
- **Chip temperature** (S3 only), CFG `safety.chipWarn/chipMaxC` (80 °C): `temperatureRead()` every 30 s
  (only while enabled), warning event above, "back to" 5 °C below; live `safety.chipC`.
- Module `safety.*` (E-stop, buzzer, activity, chip temperature); `GuardMonitor` in `runaway.*` (host
  tested: testGuard).

### 9.3 Programs — two program types (agreed 2026-09-28)
Replaces two earlier drafts ("one program per channel from a shared pool" and "parallel channel
sequences with 'wait for previous phases' barriers"). Chosen after comparing the models on charts.

- **One program drives both channels.** All programs are in one pool; the program list shows the type
  next to the name. The run uses one program.
- **Limits (decided), the same on both boards** so programs can move between S3 and CYD: **20
  programs**, up to **100 phases** (Sequence) or **100 steps per channel** (Timeline), **names up to 31
  characters** (JSON files, 9.3.5: typically 1–3 kB per program, ≤ ≈ 250 kB for 20 maximal programs —
  fits the CYD's LittleFS partition too).
- **Two program types**: **Sequence** (9.3.1) and **Timeline** (9.3.2).

#### 9.3.1 Sequence
- **Phases run strictly one after another.** Each phase has a **channel selector**:

  | Phase type | Bottom | Top | Phase duration |
  |---|---|---|---|
  | `Bottom` | target, ramp, hold | keeps its previous state | bottom ramp + hold (+ wait, below) |
  | `Top` | keeps its previous state | target, ramp, hold | top ramp + hold |
  | `Both` | **target only — the SP jumps** at the phase start (the PCB's thermal mass smooths it), **or `Idle`** | target, ramp, hold | (wait, below +) top ramp + hold |
  | `Pause` | keeps its previous state | keeps its previous state | until Continue (below) |

  - "Keeps its previous state" = holds its last target, or stays off if it has none yet or was set Idle.
  - `Idle` = the channel's outputs are off (SSR off / C.Off = 1) until a later phase gives it a target.
    Only the bottom can be Idle inside a `Both` phase (top Idle + bottom target is just a `Bottom`
    phase).
  - A `Bottom` phase with ramp 0 is a jump too.
  - **`Pause` phase** (agreed): both channels keep their setpoints from the previous phase; timers stop
    and the program waits for **Continue**. Option per phase: **show a big "Action required" message**
    on the device screen (and the web page) with a **Continue program** and a **Stop program** button.
    **Optional custom text per pause** (decided), up to **40 characters**, shown under the "Action
    required" title (e.g. "Place the IR head", "Check flux"); also printed over serial and shown in the
    event log. Available for Pause steps in Timelines too.
    Without the message the pause shows in the status ("Program pause") and the run bar's button reads
    Continue. "Next phase" = Continue. Kept setpoints can still be changed during the pause (9.3.4).
- **"Wait for bottom to reach target"** — per-phase switch, available in `Bottom` and `Both` phases
  (not in `Top` phases; add later if the top heater turns out to lag):
  - `Bottom` phase: the bottom SP ramps as programmed; after the ramp the **hold timer starts only when
    the bottom PV is within the band** of the target.
  - `Both` phase: the bottom SP jumps; the **top ramp starts only when the bottom PV is within the
    band** (meanwhile the top keeps its previous state).
  - Band: **one global setting** ("bottom reached" band, **default ±5 °C**, decided); phases only have
    the on/off switch.
  - While waiting, status and chart show "Waiting for bottom"; "Next phase" skips the wait (see below).
- **"Next phase"** (decided, replaces the per-channel skip chosen for the parallel draft): one button
  that moves **one stage further — ramp → hold → next phase — and never stops at a "wait for bottom"**:
  - `Bottom` phase with wait: during the ramp → straight to the hold (the wait is skipped); during the
    wait → the hold starts.
  - `Both` phase with wait: during the wait → the top ramp starts; during the top ramp → hold.
  - During a hold → next phase. In a `Pause` phase, Next = Continue.

#### 9.3.2 Timeline
- **Two independent step lists, one per channel, started at the same time.** Each channel has its own
  steps (target, ramp, hold — as in TempCtl), so ramps and holds of the two channels happen at
  independent points in time (e.g. the bottom runs ahead into its next step while the top is still in
  an earlier one).
- Purely time-based: **no "wait for bottom"** (it would shift only one channel and break the time
  alignment between the channels, which is the point of a Timeline).
- **`Idle` steps** (outputs off for a given time) are available in both step lists (agreed).
- Up to 100 steps per channel. A channel with no steps is controlled manually (9.3.5).
- **`Pause` steps** (agreed) are available in both step lists: when a channel reaches one, the **whole
  run pauses** (both channels' timers stop, both keep their setpoints — a channel in the middle of a
  ramp holds its current SP) until Continue; same "Action required" option as in Sequences. Continue
  moves past all pause steps reached at that moment.
- **"Next phase" is disabled** for Timeline programs (Rost). Pause / Resume / Stop act on both channels.
- The run ends when both step lists are done; **a channel that finishes earlier keeps its last target
  until then** (decided) — to switch it off earlier, the program ends that channel's list with an
  explicit `Idle` step. Then both apply their end actions.

#### 9.3.3 Editors
- **Device (touch)**: table editing for both types.
  - Sequence: one row per phase with the type selector, the fields of that type and the wait switch.
  - Timeline: **two separate tables next to each other (Bottom | Top) with a visible separation**
    (not tabs). On 480×320 each table gets ≈ 230 px — compact columns (#, target, ramp, hold; times as
    m:ss), details in the number editor popup.
  - **CYD 320×240 (decided)**: Timeline = **one table with a Bottom / Top switch**; Sequence = compact
    one-line summary per phase, **tap → phase edit popup** with all fields.
- **Web**: the same tables **plus a graphical editor for both types** on the chart. This is a
  deliberate exception to "full parity": the device can edit everything, but only the web has the
  graphical editor.
  - **Three kinds of handles on the chart, for both program types** (agreed):
    - **hold end** (circle) — drag left/right: changes that hold time (Sequence: bottom hold in a
      `Bottom` phase, top hold in `Top` and `Both` phases);
    - **ramp end** (diamond) — drag left/right: changes that ramp time;
    - **target** (square, in the middle of the hold / plateau) — drag up/down: changes that target
      temperature (also the bottom jump target of a `Both` phase).
    Snapping to whole seconds and °C; later phases/steps move in time but keep their values. Where
    handles would overlap (hold 0), the target and hold-end handles are drawn offset with a leader line.
    The table next to the chart updates live, and table edits update the chart.
  - The preview uses the programmed times and treats every "wait for bottom" as satisfied at once, so
    outside a run a hold can be dragged back to the end of its ramp. **When no program is
    running, the channels are edited independently** — moving a point of one channel moves only that
    channel's later steps (agreed); during a run see 9.3.4.
- **"Convert to Timeline"** (web; done in 1.0.32): one way only, with a **warning that the conversion
  can't be reversed**. Decided 2026-10-12 (Rost): Sequences **with** "wait for bottom" convert too — the
  waits are dropped (the warning lists the phases; the Timeline runs on the programmed times, as the
  preview does); the dialog asks each time **Replace this program** (the draft in the same slot, Undo
  brings the Sequence back until saved) or **Create a new program** (first empty slot, "… (Timeline)",
  unsaved draft). Mapping: one clock phase by phase; a channel that keeps its state gets its last hold
  (or Idle) lengthened to the next phase that gives it a target; a Both phase's bottom jump = ramp-0
  step (Idle = idle step); a Pause phase = pause step in the bottom list (top if the bottom is unused).
- **Starting a run with unsaved changes** (decided): Start asks **"Save and start" / "Start without
  saving" / "Cancel"**. "Start without saving" sends the draft to the device as the run's copy; after the
  run the usual "save the run's changes?" question comes (9.3.4). Same on touch and web.
- **Outside a run, edits (table or chart) go to a draft** of the program (agreed): **Save program** and
  **Discard changes** buttons appear/enable as soon as anything changed; the program list marks programs
  with unsaved changes. **Undo / Redo** (buttons, and Ctrl+Z / Ctrl+Y on the web) cover all edits
  outside a run, including Discard. During a run these buttons are disabled (run edits go to the running
  copy and are offered for saving after the run, 9.3.4). Starting a run uses the draft as shown.
- **The table/editor panel resizes with the number of phases/steps** (no fixed height).
- **Phase end times on the chart** (agreed): each phase boundary is labelled with its time (Sequence:
  phase number + start time of the next phase; Timeline: the end time of each step next to the curve),
  plus the program end. Past boundaries show actual times, future ones the forecast; they update live
  during the run (waits, pauses, edits).
- **Pauses on the chart**: a thick vertical line with a pause symbol where the program pauses; while a
  pause lasted, the area is lightly hatched.
- **Chart zoom and scroll** (web, agreed): the chart always fits the screen width (no sideways page
  scrolling on phones). Zoom − / + / Fit buttons, a scroll slider when zoomed, Ctrl + mouse wheel zoom
  at the cursor, Shift + wheel or horizontal swipe to scroll, drag on the chart background to scroll,
  pinch to zoom on touch screens. During a run a zoomed view follows the current time. Handles keep
  working when zoomed.
- **Compact editor**: narrow table (one "Option" column for wait / "Action required", ✕ delete
  buttons); on narrow screens (< 720 px) each phase becomes a small card instead of a table row.

#### 9.3.4 Editing during a run
- The graphical editor (and the tables) can be **activated during a program run**, e.g. to **extend the
  current phase's hold when the solder joints are not liquid yet** or to **raise a target temperature**. **Later phases stay untouched** —
  their parameters don't change, they only move later in time.
- Rules:
  - Only the current and later phases/steps can be changed; finished ones are locked, except for a
    target the channel still keeps (below) (decided).
  - A hold cannot be shortened below the time already elapsed; dragging it to "now" ends the hold at
    once (decided). **Chart drags take effect only when the user releases the handle** (decided) —
    while dragging, only a preview (ghost line + value) is shown; nothing is sent to the device.
  - Timeline **during a run**: when one channel's timing changes (hold, ramp, or a ramp time that
    changes because of a target change), the other channel is shifted by the same time **only through
    its holds** (agreed): the first hold/idle at or after that moment gets longer, or holds/idles at or
    after it get shorter. Its ramps are never changed or paused. If it has no hold left, nothing is
    shifted. Outside a run the channels are edited independently (9.3.3).
  - During a run the current and finished ramps have no ramp-time handle / field (locked, confirmed by
    Rost — dragging would change the slope of a running ramp); future ramps can be changed.
  - Changes go to the control task as commands (not written directly into the running engine).
  - Changes apply to the **running copy** of the program; after the run the device/web asks whether to
    save them into the stored program (agreed).
- **Touch equivalent (agreed)**: "Extend hold" buttons (e.g. +10 s / +30 s / +1 min) on the device
  status screen while a hold is active, so extending doesn't depend on the web. Same rules as above.
- **Target temperatures can be changed during a run too (agreed)** — in the web tables, by dragging a
  point vertically in the graphical editor, and on the device. **Slopes are kept, times adapt** (agreed):
  - The **slope of a ramp** = |target − start value| / ramp time as the ramp starts (start value =
    previous target, or the measured PV for a first ramp / after Idle). A ramp time of 0 = a jump.
  - Current phase/step **in a ramp**: the ramp continues from the current SP to the new target **with
    the same slope**; the remaining ramp time is recalculated (if the new target is below the current SP
    of a rising ramp, it ramps down with the same slope).
  - Current phase/step **in a hold**: the SP **ramps to the new target with the slope set for this
    step** (a jump if the step had ramp 0); the **hold timer keeps running** meanwhile, so the ramp uses
    up part of the remaining hold (agreed).
  - `Both` phase (bottom jump): the bottom SP jumps to the new bottom target; a running "wait for bottom"
    uses the new target (agreed).
  - **Later phases/steps keep their targets**; the ramp that follows the changed target **keeps its
    slope and its duration is adjusted** (agreed), so later phases/steps move in time.
  - Timeline: a temperature change on one channel doesn't change the other channel's targets or ramps
    (agreed); a resulting **ramp-time change shifts both channels' later steps** by the same amount
    (longer ramp → both later, shorter → both earlier), like "extend hold", so the time alignment is kept
    (agreed).
  - **Kept targets are adjustable (agreed)**: a channel that keeps the target of an earlier phase (e.g.
    the bottom during `Top` phases), or a Timeline channel whose steps are done and which keeps its last
    target, can still get a new target. The SP moves there with the slope of the phase/step that set
    it (a jump for a bottom jump target); the change is stored in that phase/step of the running copy.
  - Setpoint limits (spMin/spMax) apply; Idle phases/steps have no temperature to change (agreed).
  - The recalculated ramp times are part of the running copy and are saved with it if Rost chooses to
    save the run's changes.
  - Touch: target control on the main screen tile (9.7: 480×320 stepper, 320×240 SP button → number
    editor) for the current (or kept) target.

#### 9.3.5 Common rules
- **Start values**: a channel's first ramp — and the first ramp after it was Idle — starts at the PV
  measured at that moment; later ramps start at the channel's previous target.
- **Idle at the start of the run**: a channel with no target yet has its **outputs off** (agreed).
- **"Run interrupted by a reset"** (agreed): the run state (program, phase/step, run time, running
  copy changed yes/no) is kept in RTC memory while a program runs and cleared when it ends normally.
  If the board restarts during a run (watchdog, brown-out, crash, a serial auto-reset …), it starts
  with **all outputs off** and shows **"Run interrupted by a reset"** with the reset reason, program,
  phase and time on touch, web and serial (the event log keeps it). **Never an automatic resume;
  instead a "Resume from the interrupted phase" button** (decided; touch, web, serial `resume run`):
  - Sequence: the interrupted phase starts again from its beginning (ramps from the measured PV as
    after Idle, full hold, "wait for bottom" as programmed).
  - Timeline (decided): both channels continue at the run time of the interruption — a channel that
    was in a ramp ramps from its measured PV to the step's target in the remaining ramp time, holds
    continue with their remaining time.
  - The running copy (with edits) is kept for this: written to flash (`/duo/run.json`) at start
    and after each run edit, so it survives a reset or power loss.
  - The offer stays until the user resumes, dismisses it, or starts/edits something else.
  A normal power-off/power-on during a run is reported and offered the same way when it can be
  detected (`/duo/run.json` still present at boot).
- **Channel without any phase/step** in the program: **controlled manually** during the run (manual
  switch and SP stay available on touch and web). A program needs at least one phase/step to start.
- **Guaranteed soak is removed** (Rost, 2026-09-28): hold times are plain times; "wait for bottom" in
  Sequences is the only PV-dependent timing.
- **Run control**: Start / Pause / Resume / Stop act on the whole run; "Next phase" see 9.3.1 / 9.3.2.
- **End**: after the program each channel applies its own end action (keep SP / end SP / outputs off).
  **Decided**: run control (Start / Pause / Resume / Stop / Next) and end actions don't touch a manually
  controlled channel; a runaway or sensor fault on it still switches its outputs off and stops the run.
- **Timing / chart preview**: total time and per-channel plan curves come from the programmed times and
  are exact, except for "wait for bottom" phases, pauses and edits during the run (the preview assumes
  no wait and no pause time; the time axis and the remaining time adjust during the run; the total is
  shown as "… + waits").
- **Implementation (decided)**: **one pure C++ run engine for both types** (replaces the single-channel
  `ProgramEngine`, host-testable). Internally both types become per-channel segment lists (ramp / hold /
  idle) plus **sync points**: a Timeline has none, a Sequence has one at every phase boundary, and
  "wait for bottom" is a sync point with a PV condition. Run edits = changing a hold duration and
  shifting later segments.
- **Program files: JSON** (decided, Rost): one file per program, **`/duo/progN.json`** (N = 0–19) on
  LittleFS, the **same JSON as the web API** uses for programs (so export/import and the web editor
  need no conversion). Sketch:
  ```json
  {"fmt":1,"name":"Lead-free BGA","type":"seq","phases":[
    {"kind":"B","wait":true,"b":{"t":150,"r":60,"h":60}},
    {"kind":"P","msg":true,"text":"Place the IR head"},
    {"kind":"BT","wait":true,"b":{"t":160},"t":{"t":150,"r":60,"h":20}},
    {"kind":"BT","b":{"idle":true},"t":{"t":150,"r":40,"h":0}}]}
  ```
  Timeline: `{"fmt":1,"name":…,"type":"tl","B":[{"t":150,"r":60,"h":150},{"idle":true,"h":60}],
  "T":[{"pause":true,"msg":true,"text":"…"},…]}`. `fmt` = file format version (for later changes);
  unknown keys are ignored, missing keys take defaults. **Safe save**: write `progN.tmp`, then rename.
  Size: typical program 1–3 kB, worst case (100 phases) ≈ 12 kB; parsing with ArduinoJson 7 needs a
  similar amount of RAM for a moment (check on the CYD, no PSRAM). The running copy is stored the same
  way (`/duo/run.json`).

### 9.4 Manual mode
- Manual setpoint switch **per channel** (as in TempCtl: on = SP + outputs on, off = outputs off).
  Starting a run ends manual mode only on channels that have phases in the program; a channel without
  phases stays manually controllable during the run (9.3).

### 9.5 Storage — clean start
- **Settings: JSON file `/duo/settings.json`** (decided, Rost) — the same JSON as the web API's
  settings, with a `fmt` version field; unknown keys ignored, missing keys take defaults (so new
  settings need no migration); safe save (temp file + rename). Programs: `/duo/progN.json` (9.3.5).
  Nothing from TempCtl (NVS namespace `tempctl`, `/progN.bin`) is read or migrated.
- **Wi-Fi passwords** are stored in the settings file but **never returned by the API** or printed
  (the API only reports "set / not set").
- The "run interrupted" data lives in `/duo/run.json` (exists only while a run is active) plus RTC
  memory for the reset reason.
- **Settings content (decided)**: common (RS485 baud/parity, Wi-Fi + AP, accent colour (RGB), touch
  theme, "bottom reached" band; no guaranteed-soak settings) + per channel (**name**, type, end action,
  setpoint limits; Modbus: address + register map (PMA preset or custom) + ramp options; local: sensor
  type, calibration, PID, SSR window, max output, max temperature (cut-off); runaway enable + limits
  stored per channel type).
- **Fixed IP (Rost, 2026-10-01)**: optional static address for the station (IP, mask, gateway, DNS) in
  the Wi-Fi settings, web and touch.
- **Polling interval (Rost, 2026-10-01)**: the polling interval (`bus.pollMs`, 250–10000 ms, default
  1000 ms) is a normal device setting, **editable on the touch UI and on the web GUI** (not only through
  `/api/config`). It sets how often the control task polls the channels (Modbus round robin; local
  channels every cycle) and so how often PV / SP / output are refreshed for the displays. Touch: Setup
  (step 6); web: settings page (step 7c). Show a hint that with two Modbus controllers each one is
  polled every 2 × the interval.
- **Channel names (decided)**: a free name per channel, **shown on all screens instead of
  "Bottom" / "Top"** (touch, web, serial output, program editor headings); defaults "Bottom" and "Top",
  up to 16 characters. The roles stay fixed: channel 1 is the lower heater that "wait for bottom" and
  the bottom jump in `Both` phases refer to (the UI uses the name there too, e.g. "Wait for
  Preheater"); serial commands keep `b` / `t` as short channel keys.

### 9.6 Hardware
- ESP32-S3-DevKitC-1 v1.0 N16R8 (WS2812 assumed on GPIO48, test pending), 4.0" ST7796 + FT6336U
  module, pinout in 2a; both USB ports usable: flashing + log monitor via the "UART" port, control
  (serial console, Web Serial page) via the native "USB" port.
- **CYD stays supported as a second device** (2b): envs `cyd-1xlocal-1x485` / `cyd-2xlocal` / `cyd-2xlocal-2x485`, 320×240, resistive touch, same features.
- Display through LVGL's ST7796 driver, touch through our own FT6336U driver (section 3).

### 9.7 Layout
- Orientation: **landscape 480×320** (S3) — confirmed by Rost; the CYD set is landscape 320×240 as in
  TempCtl.
- **Touch main screen shows both channels** (Rost): both PV/SP/output readouts, the chart with both
  channels + plans, and the per-channel manual controls on the main screen (both screen sets).
  First mockups (Design canvas "TempCtl Duo main screen", 2026-09-28): header with program, phase,
  state and time; full-width chart with both channels; two channel tiles (PV, SP, output bar, status,
  target control, Manual switch). 480×320: run buttons in the header, target as **− value + stepper;
  tapping the value opens the number entry popup** (decided).
  **320×240 (Rost): no ± buttons and no separate SP readout — one SP button showing the current SP
  that opens the numeric input popup (as in TempCtl 1.2)**; run buttons + menu in a bottom bar.
  Final pixel details are settled in the LVGL Pro Editor.
- **Web Status page (decided)**: the simulator's layout (run panel with chart and zoom bar, the two
  channel cards, program editor, event log) plus the sidebar (7) — no separate mockup.
- Channel colours: split-complementary to the accent, lighter SP, contrast rule per theme (section 6,
  agreed) — no fixed colours for channel 2.

### 9.8 Identity and versioning (decided 2026-09-28)
- Product name **"TempCtl Duo"** (screen, web title, serial banner), host name **`tempctl-duo`**
  (mDNS `tempctl-duo.local`), AP name **`TempCtl-Duo-XXXX`** (last MAC digits). LVGL Pro project name
  stays `tempctl_ui` (no churn in the exported file names).
- **Version: the whole TempCtl Duo project starts at 1.0.0** (Rost) — firmware, web page / local app
  share one version number. **Every commit that is pushed increases the patch number** (1.0.0 → 1.0.1
  → …); minor/major only when Rost says so. Kept in one place (**decided: `src/version.h`**, used by the
  firmware, the web page build and the local app) and bumped in the commit itself.
- The **API version** (`api` in the hello line, 7b) is a separate integer, raised only when the serial /
  HTTP API changes incompatibly.

### 9.9 Other points
- **Example programs on first start (decided): three**, taken from the 9.3 simulator (target °C /
  ramp s / hold s; created only when no program exists):
  1. **Lead-free BGA (Sequence)**: Bottom 150/60/60 + wait · Pause ("Action required") · Both: bottom
     160 (jump), top 150/60/20 + wait · Top 190/40/40 · Top 225/30/20 · Top 245/20/30 · Both: bottom Idle,
     top 150/40/0.
  2. **Leaded BGA (Sequence)**: Bottom 130/40/60 + wait · Both: bottom 140, top 130/50/20 + wait ·
     Top 170/40/30 · Top 205/30/20 · Top 220/15/30 · Both: bottom Idle, top 120/40/0.
  3. **Lead-free BGA (Timeline)**: bottom 150/60/150 · 165/30/150 · Idle 60; top Idle 120 · 150/40/20 ·
     190/40/30 · 225/30/20 · 245/20/30 · 150/40/0.
  Values are starting points, to be tuned on the real station.
- **SD run logs (S3, 1.0.32; decided 2026-10-12)**: one CSV per program run in `/runs` on the card,
  **every 1 s, full detail** (time_utc, run_s, where, per channel pv / sp / prog / out / state) plus a row
  per event during the run (kind, event); **web list on the System page** (Download / Delete) + the card
  in a PC. The CYD has none (its SD pins carry the thermocouples). Shared SPI bus: the screen check frees
  the bus every second, so the card is mounted only while used, under `lcdBusLock()` (the check skips
  that second: "busy (SD card)").

### 9.10 Proposed implementation order
1. **Bring-up on the S3**: new env, per-board headers (S3 + CYD), ST7796 via LVGL + DMA, own FT6336U
   driver, RS485 on UART1, WS2812 status; existing single-channel TempCtl UI running (in a corner) so
   the board, display, touch, Wi-Fi/web and Modbus can be tested on hardware first. The `cyd` env must
   keep building (ILI9341 via LVGL, XPT2046) — Rost tests both boards.
2. Channel interface; Modbus backend for two channels on one bus; settings in `/duo/settings.json`.
3. Local backend: thermocouple reading + calibration, SSR time-proportional output, safety cutoffs.
4. PID + autotune (host-tested first).
5. Program types Sequence + Timeline, program format + one run engine (9.3), host tests for phase
   timing, jumps, Idle, "wait for bottom" and run edits.
6. Touch UI redesign for 480×320 / two channels (XML in LVGL Pro Editor, Rost exports).
7. Web UI and API for two channels, starting from the 9.3 simulator page (7), including the graphical
   program editor for both types and editing during a run (9.3.3, 9.3.4).
8. Serial console (7a) — can come early (after step 2), it helps with bring-up and Wi-Fi setup;
   then the `api` mode and the Web Serial page (7b). Test the DTR/RTS auto-reset behaviour early.
9. Identity (9.8) — can be done in step 1 already (name, host, AP, version 1.0.0).
10. Later / planned: the page on GitHub Pages (7b, option 1). (SD run logs and Convert to Timeline:
    done in 1.0.32.)

#### 9.10.1 State of step 1 (2026-09-29)
Implemented (both envs compile and link without warnings in our files; host tests pass):
- `platformio.ini`: envs `s3` (default; `esp32-s3-devkitc-1`, qio_opi, 16 MB, `default_16MB.csv`,
  `BOARD_HAS_PSRAM`, `board_build.extra_flags` = board flags with `ARDUINO_USB_MODE=0`,
  `ARDUINO_USB_CDC_ON_BOOT=0`) and `cyd`; TFT_eSPI removed, XPT2046 only in `cyd`. Both on COM6 (ask).
- `src/version.h` (PRODUCT_NAME, FW_VERSION 1.0.0, DEFAULT_HOSTNAME `tempctl-duo`, AP_NAME_PREFIX
  `TempCtl-Duo`, API_VERSION 1); `src/config.h` includes `board_s3.h` / `board_cyd.h` by `BOARD_S3` /
  `BOARD_CYD` (all pins of 2a / 2b, incl. the later TC/SSR pins, not used yet).
- `src/lcd.*`: LVGL `lv_st7796_create` / `lv_ili9341_create` on native portrait resolution with
  `LV_LCD_FLAG_MIRROR_Y` (+BGR), rotation 90° / 270° → MADCTL `MV|BGR` / `MV|MX|MY|BGR` = TFT_eSPI
  rotations 1 / 3. SPI2 via `spi_master`: commands polling, pixels one queued DMA transfer,
  `lv_display_flush_ready()` in `post_cb`, `lv_draw_rgb565_swap()` (the `_sw_` name is deprecated in
  9.6). Two draw buffers in internal DMA RAM (S3 480×32 lines = 30 kB each — an S3 SPI transfer is
  ≤ 32 kB; CYD 320×20). After LVGL's init a fix-up command list: ST7796 `B6 80 02 3B` (LVGL sets SS=1
  = mirrored), CYD the TFT_eSPI `ILI9341_2_DRIVER` power/VCOM/gamma/`B6 08 82 27` values. 40 MHz
  SPI on both. Backlight LEDC channel 0, switched on after the first frame. `LCD_INVERT 0` (MSP4031 is
  a TN panel per lcdwiki).
- `src/touch.h`, `touch_ft6336.cpp` (reset pulse, `Wire` 400 kHz with 20 ms timeout, regs 0x02–0x06,
  G_MODE 0, chip id 0xA3 printed at boot, I²C error → released; mapping `TOUCH_SWAP_XY 1`,
  `TOUCH_INVERT_Y 1` = x = raw y, y = 319 − raw x, derived for MADCTL MV with the panel's portrait
  touch axes — **unverified**; CTP_INT not used yet (`TOUCH_USE_INT 0`)), `touch_xpt2046.cpp` (old code).
- `src/status_led.*`: WS2812 via `neopixelWrite` (level 40/255) / CYD RGB LED via LEDC PWM (level
  120/255, 2026-09-29), colours and priority of 2a, called from the control task every 10 ms (fault = no Modbus answer, wait = paused, AP =
  `netApActive()`, running = program or manual).
- RS485 via `RS485_SERIAL` (Serial1 on 1/2/42 on the S3, Serial2 on the CYD).
- Identity: boot banner, web title/brand, `/api/status` `board`, AP name, host name default; NVS
  namespace `tempctl-duo` (clean start, no migration; step 2 replaces NVS by `/duo/settings.json`).
- S3 screen: the 320×240 tabview is resized to the top left corner; right of it a bring-up panel
  (version, board, heap, largest block, free PSRAM, touch up/down, raw and mapped coordinates) and a
  red dot on `lv_layer_sys()` at the touched point. Popups still use the full 480×320 layer.
- README updated (boards, pins, envs, identity); `docs/SPEC.md` is still the old first revision.

Without a display the S3 firmware runs normally (SPI writes go nowhere); a FT6336 that is missing at
boot is not accessed again (no I²C error log every 2 s; connect the display with the power off).
The status LED runs in its own small task (`statusled`, 20 ms) — in the control task the blinking was
irregular because Modbus timeouts without a controller block it for seconds.

**S3 bare board tested by Rost (2026-09-29, COM4)**: boots, PSRAM 8 386 279 bytes, free heap 287 kB
at start / 148 kB before net / 96 kB after (largest block 86 kB), no `ARDUINO_USB_MODE`
redefinition warnings, AP works (still the TempCtl 1.2 web page), FT6336 not found (expected), LED
blinked irregularly (fixed as above, re-test). Boot line "mode:DIO" is the ROM header, normal.

**Hardware checks — deferred per part (ask Rost for the serial log / a photo when each part arrives):**
- *Now — bare S3 board (no display, no PMA):* boots on the "UART" port with banner `TempCtl Duo
  1.0.0 (ESP32-S3)` and PSRAM ≈ 8 MB; `[lcd] 480x320 …`; `[touch] FT6336 NOT FOUND` (expected);
  WS2812 on GPIO48 blinks red (no Modbus answer — expected; confirms v1.0 board); Wi-Fi / AP
  `TempCtl-Duo-XXXX`, web page at `tempctl-duo.local`; free heap numbers in `[mem]`; no
  `ARDUINO_USB_MODE` redefinition warnings in the PlatformIO build.
- *Now — CYD:* picture not mirrored and colours right (ILI9341 fix-up list), touch as before,
  RGB LED blinking red without PMA (purple in AP mode), web page / identity; measure IO18 → 3.3 V
  (no pull-up expected) before wiring an SSR; free heap vs. TempCtl 1.2 (97 kB).
- *When the 4" display (MSP4031) arrives:* picture orientation (MADCTL), colours (`LCD_INVERT`, BGR),
  40 MHz SPI stable, backlight PWM; FT6336 chip id; touch mapping with the bring-up panel
  (`TOUCH_SWAP_XY` / `TOUCH_INVERT_*`), 180° rotation of picture and touch; then try CTP_INT
  (`TOUCH_USE_INT 1`).
- *When the thermocouple converters (MAX31855 / MAX6675) arrive* (not delivered yet, 2026-09-30):
  the step-3 test in 9.10.3 — readings and cold junction, calibration, PID + SSR duty on an LED, fault
  on a pulled thermocouple and `/api/ack`; later autotune (step 4) on the real heaters.
- *When the PMA controller arrives:* RS485 on UART1 (GPIO 1/2/42) with the PMA on both boards, LED
  turns green/blue, program run + manual setpoint as in TempCtl 1.2.

#### 9.10.2 State of step 2 (2026-09-29)
Decided with Rost: (a) until steps 5–7 the old single-channel touch UI, web page, program engine and
chart work on the **bottom channel** (`ENGINE_CH`); the top channel is polled, shown in `/api/status`
(`channels`) and the serial log, and configured through `/api/config`; (b) **channel type `off`**
(not used, not polled, no fault) in addition to `modbus` / `local`.
- `src/settings.h/.cpp` (pure C++, host-tested): `Settings` = common (bus, run, wifi, display, tools,
  selProgram) + `ChannelSettings ch[2]` (name, type, end action, SP limits, manual SP, `mb`: address,
  profile, RegMap, ramp options, lock, runaway; `loc`: sensor, calOffset/calGain, kp/ki/kd, window,
  maxOut, maxTemp, runaway). `settingsDefaults(s, BoardLimits)`: bottom Modbus addr 1, top Off
  (no RS485: both local); spMax 390; runaway defaults of 9.2 (bottom heatSec 300). `settingsValidate`:
  ranges, channel types the variant can't wire -> Off + message, same Modbus address on both channels ->
  error, local spMax <= maxTemp - 10. JSON: `settingsToJson(s, obj, forApi)` / `settingsFromJson` (merge,
  unknown keys ignored, wrong types rejected with the path, a new profile loads its register map;
  register addresses -1 = unused; enums as strings: type off/modbus/local, endAction keep/sp/off,
  profile pma/generic, rampMode host/controller, sensor max31855/max6675; accent "#RRGGBB";
  themeDark null = first-start default). Guaranteed soak (`run.guaranteedSoak/soakBand`) and
  `display.themePalette` are still in the file for the old engine/UI; they go with steps 5/6.
- `src/settings_store.*`: `/duo/settings.json` (pretty JSON, temp file + rename; unreadable file ->
  kept as `/duo/settings.bad`, defaults), `boardLimits()`, `serialConfigFor()`. NVS is no longer used.
  `storageBegin()` (program_store) mounts LittleFS and creates `/duo`.
- `src/channel.*`: `Channel` interface (poll, writeSp, setOutputs, canSwitchOff, spConfigured,
  modbus()); `ModbusChannel` (a `Controller` = slave address + RegMap on the shared `ModbusMaster`),
  `LocalChannel` stub ("not implemented yet (step 3)", outputs stay off), `OffChannel`; `ChannelSlot`
  picks the backend by type. `ChannelLive` = published values per channel.
- Control task (`app_state.cpp`): one bus (`modbusBusBegin`), local/off channels polled every cycle, the
  Modbus channels **round robin, one per cycle** (each every 2 x pollMs when both are Modbus); manual
  mode per channel (`CMD_MANUAL_*` with arg = channel, `setManualSp(v, ch)`); the engine + end action +
  controller ramp / lock on the bottom channel (Modbus extras only if it is Modbus); a channel type change
  switches that channel's manual mode off and stops a running program on the bottom channel. Link
  changes are logged once (`[ch] Top (modbus): link ok` / `... PV read: no response (timeout)`). Status
  LED fault = any channel that isn't Off without link. `gLive` keeps the TempCtl 1.2 flat fields as a
  mirror of the bottom channel + `ch[2]`.
- Web: `GET/POST /api/config` (whole document, passwords only as `passSet`/`apPassSet`), `channels` in
  `/api/status`, `ch` in `/api/manual` and in the bus tools / PMA parameters (`?ch=1` or `"ch":1`); the
  old `/api/settings` edits bottom + common and is validated.
- Host tests: settings defaults per variant, JSON round trip (file and API form), partial merge,
  error paths, variant rules; `-fsanitize=address,undefined` clean. All four envs build without
  warnings in our files.
- To test on the bare S3: boot log `[settings] no /duo/settings.json - defaults`, `channel 1: Bottom,
  modbus`, `channel 2: Top, off`, `[ch] Bottom (modbus): PV read: no response (timeout)` once;
  `http://tempctl-duo.local/api/config`; POST `{"channels":[null,{"type":"modbus"}]}` -> top polled too
  (address 2), second log line. **POST bodies need `Content-Type: application/json`**: curl's `-d`
  alone sends form encoding and the ESP32 WebServer then has no `plain` body ("Invalid JSON"). On Rost's
  Windows PC: PowerShell `Invoke-RestMethod -Method Post -Uri http://tempctl-duo.local/api/config
  -ContentType 'application/json' -Body '{"channels":[null,{"type":"modbus"}]}'`, or in cmd.exe
  `curl -X POST -H "Content-Type: application/json" -d "{\"channels\":[null,{\"type\":\"modbus\"}]}" URL`
  (PowerShell mangles the `\"` quoting of the cmd form).

#### 9.10.3 State of step 3 (2026-09-30)
- **The PID moved into step 3** (a local channel can't heat without it); step 4 is now autotune only.
- `src/tc_sensor.*` (pure, host-tested): `tcDecode31855` (14-bit TC 0.25 degC, 12-bit cold junction,
  OC/SCG/SCV) and `tcDecode6675` (12-bit, open bit D2); **no device** = frame all 0 / all 1 or reserved
  bits set (31855 D17/D3, 6675 D15/D1). SO has a pull-up, so a missing converter reads all ones.
- `src/pid.*` (pure): derivative on the measurement, low-pass Td/8 (>= 1 s) against the 0.25 degC steps,
  integrator clamped 0..outMax and frozen while saturated in the error's direction. Host test on a
  first-order + dead-time heater (gain 3 degC/%, tau 120 s, dead 8 s, quantised readings): kp 3, ki 0.03,
  kd 20 reaches 200 degC with < 1 degC overshoot. (kd 60 oscillated — autotune must keep Td sane.)
- `src/runaway.*` (pure): `RunawayMonitor` — heating failed (out >= minOut for heatSec with < minRise;
  the window restarts whenever the PV rose minRise) and hold runaway (only after a steady SP was reached,
  |PV - SP| <= min(2, holdBand); a SP move > 0.5 degC resets it); `ChannelFault` codes
  sensor / over temperature / heating failed / hold.
- `src/local_ctrl.*`: `localCtrlEarlyInit()` first in `setup()` (SSR pins low, TC CS high, SCK low, SO
  pull-up); task "local" (core 0, priority 4 above the control task, 250 ms): bit-banged frames (~4 us per
  bit) on the board's TC bus, calibration, faults latched **only while the outputs are on** (3 bad reads
  = sensor fault; PV > maxTemp = over temperature; runaway), PID, SSR duty; esp_timer 10 ms switches the
  SSR pins per window (`now % window < duty * window`) and turns them off if the duty isn't refreshed for
  1.5 s. Switching on is refused while faulted or above maxTemp. Ack refused without a fresh reading or
  above maxTemp - 5; after an ack the outputs stay off. Pins: S3 / CYD 2x variants per channel, CYD
  1x485 the one "LOCAL" pin set for whichever channel is local.
- Channels: `fault()` / `ack()` in the interface; `LocalChannel` = view of the local task (PV only when
  fresh, spEff = SP while on, out = PID %, coldC); `ModbusChannel` runs the same runaway check with Ypid
  (SP = SP.EF, else the last written SP) while its outputs are on and writes C.Off = 1 on a fault.
- Control task: a new fault is logged (`[ch] Bottom: FAULT heating failed - outputs off`), switches that
  channel's manual mode off and stops a running program (end action for the program's channel runs,
  a faulted channel's outputs stay off); manual on / program start are refused while a fault is latched;
  `CMD_ACK_FAULT`. Host SP stepping without minStep / minWriteSec on a local channel. LED fault also for a
  latched fault. `runMsg` 128 chars.
- Web: `channels[].fault` (text, "" = none) and `cold` (local), `POST /api/ack {"ch":n}`.
- Host tests added for thermocouple frames, PID, runaway; all envs build without warnings.
- To test (needs a MAX31855 / MAX6675 + thermocouple, an SSR driver or just an LED on the SSR pin):
  set a channel local (`{"channels":[null,{"type":"local"}]}` on the S3), check `channels[1].pv` and
  `cold`, manual on with `/api/manual {"ch":1,"on":true,"sp":50}`, watch the LED duty; pull the
  thermocouple -> `fault` "thermocouple open", `/api/ack` refused until it is back.

#### 9.10.4 State of step 4 (2026-09-30, version 1.0.1)
- `src/autotune.*` (pure, host-tested): `RelayAutotune` — heat-up at outMax until PV > sp + h, then relay
  bias +/- d with hysteresis h = 0.5 degC; after every cycle the bias is re-centred (Marlin) from the
  high/low times (bias limited to 10..90 % of outMax, d = distance to the nearer limit); 2 settling cycles,
  then 5 counted cycles, and a bias correction >= 2 % of outMax restarts the counting. Fails on period
  spread > 30 %, amplitude <= 1.2 h, time limit (30 min), PV > sp + maxOver, no reading, stop.
  Ku = 4d / (pi sqrt(a^2 - h^2)); Tyreus–Luyben: kp = Ku/2.2, ki = kp/(2.2 Pu), kd = kp Pu/6.3.
  Simulated heaters (gain 1.5–8 degC/%, tau 60–600 s, dead 3–40 s, 60–350 degC): all converge; the
  proposed PID reaches the SP from ambient with <= 0.1 degC overshoot (2.3 degC at 60 degC).
- `local_ctrl`: `localCtrlTuneStart/Stop/Get/Clear`; while tuning the relay drives the SSR (limited to
  maxOut), runaway only "heating failed" (hold check off), sensor / over-temperature faults latch and end
  the tune; "outputs off" ends it; start refused on a fault, without a reading, unless sp > PV + 10 and
  sp <= maxTemp - 15 (maxOver = min(40, maxTemp - sp - 5)).
- Control task: `CMD_TUNE_START` (local only, not while the program runs on that channel; switches its
  manual mode off), `CMD_TUNE_STOP`; manual on / program start refused while tuning; the end is logged
  (`[tune] Top: done: Ku 6.08, Pu 34 s -> kp ... (accept or discard)`); LED "running" while tuning.
- Web: `POST /api/autotune` start / stop / accept (kp/ki/kd -> settings, saved) / discard;
  `channels[].autotune` {state, sp, cycle, elapsed, msg, kp, ki, kd, ku, pu} in `/api/status`.
- To test with hardware (thermocouple + SSR + heater): `{"ch":1,"cmd":"start","sp":150}`, watch
  `channels[1].autotune`, then accept; afterwards a manual SP should settle without overshoot.

#### 9.10.5 State of step 5 (2026-09-30, version 1.0.2)
Decided with Rost before the step: the **old touch / web program editors become read-only** ("run
control only") until the new UIs; **order of the next steps: 5 → 7 → 6** (web page before touch UI).
- `src/duo_program.*` (pure, host-tested): `DuoProgram` (name 32, type, Sequence `SeqPhase ph[100]`
  {kind B/T/BT/P, wait, bIdle, msg, text, Seg b, Seg t} or Timeline `TlStep st[2][100]` {kind target /
  idle / pause, msg, text, Seg} in a union; Seg = {t, r, h}, times uint32 s <= 599940) ≈ 3.9 kB. Pause
  texts in a pool of **16 different texts per program** (40 characters each; decided by me, enough for
  real programs, keeps the struct small). JSON codec exactly as sketched in 9.3.5 (`fmt` 1; unknown keys
  ignored; r/h/wait/msg default; a missing target, wrong kind, > 100 phases / steps, name > 31 or text > 40
  bytes, fmt > 1 are errors with "Phase 3: Top target missing"-style messages using the channel names);
  `progCheckLimits` (spMin/spMax per channel); the three examples of 9.9.
- `src/run_engine.*` (pure, host-tested) replaces `ProgramEngine`. Per channel a setpoint generator
  (from → to over dur, or Idle = outputs off; slope kept for edits); Sequence stages Ramp → [Wait] → Hold,
  Pause; Timeline lanes Ramp → Hold, Idle, Pause, Done. Time advances **event by event** inside `tick()`,
  so results don't depend on the tick length (tested with 0.25 / 1 / 7.3 s). Program pause freezes the
  stage timers and the ramps that belong to a running stage; user pause freezes everything. Outputs per
  channel `ChOut` {inProgram, idle, moving, sp, target, rampLeft, gen}. Run edits `setHold` / `extendHold`
  / `setRamp` / `setTarget` (idx -1 = current / kept target) follow 9.3.4; Timeline shifts use the **old
  end time of the changed ramp/hold as "that moment"** (my reading of 9.3.4: things of the other channel
  before the change point stay aligned). `forecast()` runs a copy to the end (waits satisfied, pauses 0 s)
  and emits plan segments; `programPreview()` does the same from the start.
- `src/program_store.*`: `/duo/progN.json`, N = 0–19, safe save (`progN.tmp` + rename), summaries
  (name, type, items, uses, waits, planned total) kept in RAM; the examples are created when no program
  file exists. Nothing from TempCtl `/progN.bin` is read (9.5).
- Control task: one `RunEngine` drives both channels; per channel: outputs off while idle / on when it
  gets a target, host stepping or controller ramp (Modbus RAMP_CONTROLLER, per channel), end action per
  program channel (a faulted channel is skipped), ramp-register restore and front lock per Modbus channel;
  comm watchdog on the program's channels; manual mode and autotune refused only on the program's
  channels. Start checks: program not empty, used channels not Off / tuning / without SP register /
  without PV, setpoint limits, controller SP.LO/SP.HI. The running copy lives in the control task (web
  reads it under gLock); `CMD_RUN_EDIT` (sub hold / extend / ramp / target), `CMD_NEXT` (Continue in a
  program pause), `CMD_CONTINUE`, `CMD_SAVE_RUN` (writes the running copy into its slot). History and
  plan for both channels; the run chart doubles its span when the run gets longer than planned; the
  plan = the run's setpoints so far + the forecast.
- Settings: `guaranteedSoak` / `soakBand` removed (the old touch "soak band" field and the old page's
  `soakBand` now edit `bottomBand`).
- Web: `/api/programs` (20 slots), `/api/program` GET (Duo JSON + legacy `steps` view) / POST
  (`{"i":n,"program":{…}}` or `{"i":n,"delete":true}`), `/api/run` GET (run state + running copy) / POST
  (extend, hold, ramp, target, save, next, continue), `/api/status` `run` extended, `/api/history?both=1`,
  CSV with both channels. Old page: program editor locked with a note, "Next phase" becomes "Continue" in
  a program pause. Old touch UI: Program tab read-only (bottom channel as steps), Next = Continue.
- Done later: "Run interrupted by a reset" (1.0.18, see 9.10.8) and "Start without saving" (start from a
  draft sent by the web page).
- To test now (bare S3): `/api/programs` shows the 3 examples; `/api/program?i=0` returns the JSON;
  POST a changed program back (and a wrong one → error message); Start program 1 → "Cannot start: …
  uses Top, which is off" (default top = Off), with top set to `local` → "no valid temperature from …".
  Runs need a PV on the program's channels, so run tests wait for the thermocouple converters / PMA:
  then watch `/api/status` `run` (phase, stage, `progPause` after phase 1 → `{"cmd":"continue"}` on
  `/api/cmd`), `POST /api/run {"cmd":"extend","ch":0,"sec":30}` during a hold, `GET /api/run`.

#### 9.10.6 State of step 7a (2026-09-30, version 1.0.3)
Decided with Rost: **step 7 in sub-steps with a commit each** — 7a page base + Status page, 7b program
editor (tables + chart handles, drafts, undo/redo, run edits, start without saving), 7c the other pages
(Programs, Channels, System, Tools), 7d "run interrupted by a reset"; **the TempCtl 1.2 page stays at
`/old`** until 7c covers it, then it is removed.
- `web/index.html` = the new page (one file), built from the 9.3 simulator: sidebar (Status, links to
  `/old`, link status, network, clock, CSV download, theme Auto/Dark/Light in localStorage), run panel,
  chart (both channels: PV thick, SP thin, state lanes, phase lines + times, Timeline hold-end times, pause
  marks, forecast dashed, End; zoom/scroll/pinch as in the simulator; "Live" = the chosen period of the live history), channel cards
  (target ±1/±5 + Set, Extend hold +10 s/+30 s/+1 min, manual switch + SP when the channel isn't in the
  running program, fault + Acknowledge, "why" line), Action required overlay, save-after-run dialog,
  event log. Palette in JS by the spec 6 rule from `accent` (split-complementary ±150°, SP +0.12 L,
  contrast PV 4.5 / SP 3 against the panel; the accent itself ≥ 3:1).
- Preview of a stored program (no run shown) is computed in the page (`planProgram`, same rules as the
  firmware preview); during a run the forecast comes from the device (`/api/chart?v=run` → `fc`).
- Fonts: Barlow 400/500/600 + Barlow Semi Condensed 600 from google/fonts (OFL), subset with fontTools to
  Latin + Latin-1 + Latin Ext-A + a few symbols, woff2 (≈ 22 kB each, 90 kB in all).
- `tools/web_embed.py` (PlatformIO pre-script) → `src/web_assets.h` (gitignored): page gzip ≈ 17 kB.
  Flash: S3 30 %, CYD ≈ 64 % of the app partition. Static RAM +7 kB (event log 48 × 96 chars).
- Firmware: `RunEngine` reports transitions (`PlanEv`: phase / step start, ramp end, hold end, pause,
  continue, end, stop) during the run and in `forecast()`; `chState()` (off / ramp / hold / wait / jump /
  keep / halt / pause / done), `canExtend()`; forecast segments carry `src` and a ramp flag. Control task:
  run events and per-bin state lanes in the history, an engine copy for the web (forecast under gLock),
  **event log** (`logEvent`, ring of 48, also `[event]` on serial; every run message goes there).
  API: `/api/live`, `/api/chart?v=run|live`, `/api/log?since=`; `/` = new page, `/old` = old page,
  `/f/*.woff2` fonts.
- Tested: host tests (events, chState), page with a host mock of the API (engine-generated data):
  desktop/phone, dark/light, Sequence / Timeline / idle preview. Not on hardware yet.
- **1.0.4 (2026-10-01)**: Rost connected MAX31855 #1 + thermocouple to the S3 (bottom channel set to
  `local`): readings work, but the numbers were missing in the channel cards — **fix**: `setNum()` in
  `web.cpp` took a `JsonVariant`, and a `doc["key"]` proxy converted to `JsonVariant` does not create the
  member (ArduinoJson 7), so every number set that way was dropped (`/api/live`, `/api/status`, also on
  the old page); now it assigns through the proxy. The PV showed single-reading spikes of a few °C:
  **median of the last 3 readings** in the local task (≤ 250 ms delay); hardware hint: 10 nF across
  T+/T− at the converter, twisted thermocouple leads away from mains wiring. Event log: boot entry with
  the reset reason, channel link changes and autotune results go to the log too.

#### 9.10.7 State of step 7b (2026-10-01, version 1.0.5)
- Program editor on the Status page (from the simulator): Sequence table (wide) / cards (< 720 px) with
  type, targets, ramps, holds, bottom Idle, "Wait for <bottom name>", pause "Action required" + text;
  Timeline two tables (step / idle / pause); name field; Add / ✕; empty slot → New sequence / New
  timeline. Chart handles (target square, ramp-end diamond, hold-end circle; lifted with a leader line
  where they overlap; ghost + value while dragging, applied on release).
- Drafts per slot in the browser (normalised form, compared as device JSON), Save (`POST /api/program`,
  device validates incl. setpoint limits) / Discard / Undo / Redo (+ Ctrl+Z / Y), "– unsaved" in the
  program list. **Drafts live only in the page** (lost on reload) — enough for now.
- Start with unsaved changes: Save and start / **Start without saving** (`POST /api/run {"cmd":"start",
  "i":n,"program":…}` → control task `CMD_START` sub 1 starts that content; the slot stays as stored, run
  edits can be saved into it after the run) / Cancel.
- During a run the tables and handles edit the running copy through `/api/run` (target / ramp / hold);
  type, wait, pause options, add / delete are locked during a run (the engine has no structural edits).
- Status LED: fault breathing magenta was tried and taken back (1.0.6): red blinking again.
- 1.0.7: build fix for ArduinoJson 7.4 (its `MemberProxy` cannot be copied: helpers that write into
  `doc["x"]` take it by reference and use `.set()`). Checked with ArduinoJson 7.2.1 and 7.4.3.

#### 9.10.8 State of step 7c (2026-10-01, version 1.0.8)
- **The TempCtl 1.2 page (`/old`, `src/web_page.h`) and `/api/settings` are removed** (flash −44 kB in all on
  the S3); `/api/program` no longer sends the legacy `steps` view. `/api/status` stays (`/api/history` removed in 1.0.15)
  for scripts.
- The other pages are in the same page file, chosen by the address (`#/programs`, `#/channels`,
  `#/system`, `#/tools`, anything else = Status; my choice: one file keeps the drafts while switching
  pages and needs no extra request). Polling continues on every page (sidebar link, "Action required"
  overlay); the chart is only fetched / drawn on the Status page.
- **Programs**: list of the slots (type, channels in their colours, phases / steps, time, Selected /
  Running / unsaved badges); Open (selects the slot, opens the Status editor), Duplicate (stored version
  into the first empty slot, name + " (copy)"), Export (the program JSON = file format 9.3.5), Delete;
  empty slots collapsed into one row ("Show all slots") with New sequence / New timeline / Import into the
  first empty slot. During a run only the running program can be opened and nothing new created.
- **Channels**: card "Polling and bus" (`bus.pollMs` — 9.5 — with the 2 × hint, link-loss pause,
  "bottom reached" band, RS485 when the board has it); per channel (tabs with the channel names) one form:
  name, type, end action / end SP, SP limits; Modbus: address, controller type, "Load register defaults"
  (`"modbus":{"loadDefaults":true}` in `POST /api/config`), ramp mode (min step / min write time only for
  ESP32 steps), front lock, register map (collapsible); local: converter, calibration offset / gain with a
  **two-point helper** ("Now" takes the raw reading = (PV − offset) / gain; one point = offset only;
  points ≥ 5 °C apart), PID, SSR window, max. output, max. temperature; runaway limits of the selected type.
  Below: PID autotune (local; start / stop, progress, proposed vs current values, Accept / Discard) or
  the PMA parameter table + actions (Modbus). Changing the type during a run asks first.
- **Settings forms**: fields bound to `/api/config` paths; changed fields marked, out-of-range numbers
  block Save; Save posts only the changed keys and reports values the device adjusted; Revert; the save
  bar sticks to the bottom of the window while a channel form has changes. `cfgGen` in `/api/live`
  reloads the forms (unchanged ones) when the settings change elsewhere (touch, another browser).
- **System**: device info, Restart (`POST /api/restart`, refused during a run), Wi-Fi (network scan via
  `GET /api/wifiscan`, password "empty = keep", AP password, host name), display (accent presets, touch
  theme, brightness, rotation). The web theme stays the browser's own (sidebar).
- **Tools**: Modbus tools for a Modbus channel (find the SP address → "Use … as setpoint address", read,
  write); backup: settings download / restore (without passwords and `limits`), all programs as one file
  (`{"kind":"tempctl-duo-programs","programs":[{"i","program"}]}`) / restore into the same slots.
- `/api/config` GET has `limits` {rs485, localMax}; `/api/live` channels carry the full `autotune` object.
- Tested with the host mock (Playwright): every action posts what it should (partial config bodies,
  load defaults, params / actions with `ch`, Wi-Fi, restore), calibration maths, deep links, phone width.
  Page gzip ≈ 40 kB (was ≈ 24 kB); flash S3 29.6 %, CYD ≈ 63 % of the app partition. Not on hardware yet.
- **1.0.9 (Rost's first test of 1.0.8)**: (a) the program dropdown showed "(empty)" for slots not
  opened yet (labels came only from loaded drafts) → labels from the program list; (b) a shorter polling
  interval did not speed up the page → `/api/live` has `pollMs`, the page polls every
  clamp(pollMs, 250, 1000) ms and draws the newest PV on the chart (line from the last history point to
  a dot at "now"; the Live window moves smoothly, in a run the time marker follows `run.el`); the chart
  history itself is still fetched every 2 s (1 s bins live); (c) Wi-Fi: a new network/password was only
  joined after a restart — `WiFi.begin()` right after `WiFi.disconnect()` is cancelled by the late
  "assoc leave" disconnect event, which the Arduino core does not retry → `netApplyWifi()` disconnects
  and `netLoop()` calls `begin()` 1 s later; a station that stays disconnected is retried every 30 s
  (not during a scan).
- **1.0.10 (Rost)**: **fixed IP address** for the station: settings `wifi.staticIp`, `ip`, `subnet`,
  `gateway`, `dns1`, `dns2` (strings "a.b.c.d", "" = none; DNS empty = the gateway). Validation rejects a
  missing address, an invalid mask, network / broadcast address and a gateway outside the subnet (and
  falls back to DHCP) so a typo cannot lock the device out. Applied with `WiFi.config()` before every
  `WiFi.begin()` (DHCP = config with 0.0.0.0). Web System page: switch + fields (pre-filled from the
  current address), "Save and connect" posts them with the host name, then `/api/wifi`. **Show / hide
  button** (eye) inside both password fields. Touch Setup gets the same in step 6 (parity).
  ArduinoJson pinned to `^7.3.0` (before 7.3 a `const char*` was stored by pointer — found by the
  sanitizer in a new host test; Rost builds with 7.4.3, the cloud build now too).
- **1.0.11**: logo from `logo/` (Rost) as favicon (`/favicon.svg`, `/favicon.ico` 16/32/48,
  `/apple-touch-icon.png` 180 px, square for iOS) and next to the name in the sidebar; files in
  `web/icons/`, embedded by `tools/web_embed.py`. (The touch UI could show it in step 6.)
- **1.0.12 (Rost)**: icons regenerated from the edited logo; chart: state-lane names in the channel
  colours, a separator line under the lanes and more space above the temperature axis.
- **1.0.13 (Rost)**: light / dark **switch** (pill, knob with sun → moon icon, no glow / shadow /
  gradient) instead of the Auto / Dark / Light list — web theme in the sidebar (follows the system until
  switched; switching back to the system's mode returns to "follow") and the touch screen theme on the
  System page; theme changes **fade** (0.4 s colour transition, the chart keeps its elements);
  **thick brightness slider** (accent fill, value in %); **masonry tiles**: `.cards` grids give each
  tile a row span from its height (4 px rows), so short tiles move up next to tall ones (one column:
  normal flow).
- **1.0.14 (Rost)**: the Display settings apply **at once** (no Save / Revert): each change posts the
  changed key to `/api/config`; the brightness slider previews on the touch screen while dragged
  (`POST /api/brightness`, not saved, ≤ 1 per 120 ms) and saves on release. The UI loop already picks up
  brightness / rotation / theme changes from the settings.
- **1.0.15 — CYD out of memory at boot (Rost)**: 1.0.14 on `cyd-1xlocal-1x485` aborted in `webBegin()`
  (`operator new` failed while registering the routes); with `[mem]` logging the next try got to the end of
  `setup()` (49 kB free) but the RS485 UART driver could not be installed by the control task (it ran
  while the first screen was drawn and Wi-Fi was busy). Measured (1.0.15 diag): 242 kB at boot, 201 kB
  after settings/programs, 115 kB after the UI, 63 kB after Wi-Fi, 58 kB after web (TempCtl 1.2: 158 kB
  after UI, 97 kB at the end). Fixes: **control task (UART driver) starts right after `appInit()`**,
  before UI / Wi-Fi; CYD draw buffers 20 → 10 lines (−12.8 kB); chart history and the touch chart copy
  stored as **0.1 °C int16** (`Tenth`, −7.2 kB heap, −3.6 kB static); **`/api/history` removed** (old
  page only; −7.3 kB static). `[mem]` lines at boot stay (free, largest block, minimum).
  Second CYD try: boots (65 kB free at the end of `setup()`), but the page did not load: the "/"
  response stalled (`WiFiClient write(): errno 11` once per second) and the loop watchdog restarted the
  board. Now: assets are sent in 1436-byte pieces straight on the socket (non-blocking, a connection
  without progress for 5 s is dropped and logged with the heap numbers; no watchdog reset), Wi-Fi
  modem sleep off (`WiFi.setSleep(false)`), CYD: `[mem] running` once a minute.
  Third try: still stalled, now in the JSON answers (the open browser tab polls right after boot;
  `WiFiClient::write` again), touch dead while the loop hangs, watchdog reset. Now **every** response
  body uses the same non-blocking sender (`sendBody`), plus Wi-Fi events (connected / IP / RSSI,
  disconnect reason, AP start) and the RSSI once a minute in the log. Open question: why TCP sends
  stall on the CYD (memory, Wi-Fi link, or something on the board); the log of the next try decides.
  Fourth try (CYD runs its own AP, a PC joined it): no crash any more, but even the first bytes of a
  small answer (`/api/live`, headers) cannot be sent (EAGAIN from the start, 50 kB heap free at that
  moment) — TCP sends on the CYD's AP do not get out. Responses are now written completely on the socket
  (status line + headers too; `extraHeaders`), so nothing blocks the loop for more than 5 s.
  **Touch bug found**: the touch drivers report screen coordinates, but LVGL 9 rotates pointer input by
  the display rotation itself → rotated twice, off screen (`Y is 269 which is greater than ver. res`).
  `ui.cpp` now converts to the panel's native orientation first (`touchToNative`, checked for 0/90/180/
  270°). The same applies to the S3 (its touch is untested so far).
  Fifth try (touch works): Wi-Fi scan from the touch screen → LVGL "No memory" for a 320-byte glyph
  buffer → abort; joining Rost's network → `disconnected (reason 202)` (auth failed) again and again.
  **Finding: the `[mem]` numbers were misleading** — `ESP.getFreeHeap()` / `getMaxAllocHeap()` count the
  IRAM heap too (`MALLOC_CAP_INTERNAL`), which only takes 32-bit data and is useless for Wi-Fi, lwIP,
  LVGL; the "largest block 47092" that never changed is that IRAM block. Real DRAM left after `setup()`
  was only ≈ 18 kB, so Wi-Fi (TX/handshake), lwIP and LVGL ran out — the stalls, the scan crash and
  probably the auth failures. Now: `[mem]` shows DRAM (8-bit internal) free / largest / minimum and the
  IRAM free size, plus loop / control task stack reserve once a minute; **chart history values in the
  IRAM heap** on boards without PSRAM (`Tenth` = int32, accessed word by word only, no memset/memmove;
  −7.2 kB DRAM); the program buffer of the loop task (web, UI) is allocated per request
  (`ScratchProg`, −3.9 kB) and the "start without saving" buffer is freed after the start.
  S3: same code paths (PSRAM: history in normal RAM); worked as AP and on Rost's network with 1.0.14.
  Sixth try: boots, joins Rost's network at once, AP works. Real DRAM: 172 kB after settings, 86 kB
  after the UI (the TempCtl 1.2 touch UI takes 86 kB), 34–37 kB after Wi-Fi, 29–32 kB after web. Loading
  the page (4 font downloads of 22 kB in parallel, plus the API) drove DRAM down to < 1 kB → stalls, and a
  Wi-Fi scan list then crashed LVGL (out of memory). Measured stack use: loop ≈ 7.5 kB of 24, control
  ≈ 3 kB of 8. Now, on boards without PSRAM: loop stack 16 kB (−8 kB), control task 6 kB (−2 kB), one
  LVGL draw buffer instead of two (−6.4 kB), event log 32 entries (−1.7 kB), the selected program's
  preview loaded into a temporary buffer only when it has to be recomputed (selection / stored program
  changed, start PV moved ≥ 1 °C, or every 10 s; −3.9 kB on all boards), and **the page without web fonts**
  (`/` = a variant without `@font-face` built by `tools/web_embed.py`, font routes not registered;
  system fonts). Step 6 should give the CYD a lean touch UI (the old one is the biggest user).
  **Result (Rost, 2026-10-02): CYD and S3 work** (page, page switching, touch, AP and Rost's network).
  CYD DRAM: 55 kB free after `setup()`, lowest seen 13 kB while the page was used; stack reserve loop
  8.9 kB of 16, control 3.1 kB of 6. Still tight: watch the `[mem] running` minimum; the lean CYD touch
  UI in step 6 is the real fix. Some `disconnected (reason 202)` while joining with the AP running.
  Committed as 1.0.15.
- **1.0.16 (Rost, video from the S3)**: the dot (newest reading, every poll) jumped up and down, the line
  did not show those peaks — a chart slot (live 1 s, run bins) kept only the reading at the moment it
  closed. Now every reading counts: lowest and highest per slot (`liveLo/liveHi`, `runLo/runHi`, in the
  IRAM block on the CYD; run compression keeps min / max), sent as `dlo` / `dhi` (0.1 °C below / above
  `pv`, 0 = none, keeps `/api/chart` small); the page draws each slot through both extremes (nearer one
  first). The visible dips are thermocouple noise (median-of-3 already in the local task): hardware hint
  stays 10 nF across T+/T−, twisted leads away from mains wiring.
- **1.0.17 (Rost: period setting, long history, export, auto scale)**: live history ring in RAM (lost on
  restart, no flash wear), one slot per second, 12 bytes for both channels: PV (last reading) and SP as
  int16 tenths, lowest / highest reading as one byte each below / above PV (0.1 °C up to 19.9 °C, then
  2 °C steps up to 130 °C, rounded outwards). No median of N: it would hide the peaks 1.0.16 made
  visible; last + min + max keeps them and is as compact. Size: S3 24 h (1 MB PSRAM); CYD only the
  IRAM-only heap (iram0.text ends at 0x400941DC → ~48 kB, the run rows take 14 kB; the old "IRAM free"
  also counted the D/IRAM part, which is DRAM): as many 5-min steps up to 1 h as leave 8 kB free —
  about 30 min (boot log `[hist] live history N min`; `[mem]` now prints IRAM-only free). The CYD has
  no cached view rows (computed from the ring per request); the S3 keeps 4 view rows, rebuilt once per
  slot. `display.liveMin` (5…1440, clamped to the history) = Live period for web and touch; the view is
  300 bins of k = period / 300 slots aligned to the slot count (complete bins don't change), newest
  partial (`sh`). Page: Period select (5, 10, 15, 30 min, 1, 2, 4, 8, 12, 24 h up to the history, plus
  the whole history), "N recorded" / "memory holds N", Export CSV; Live Y axis always 0 … highest value
  in view (incl. the newest reading) on a round step. `/api/history.csv` streams (no Content-Length,
  ends with the connection; ~4 MB / 86400 rows on the S3) time_s, uptime_s, per channel pv, min, max,
  sp; `/api/log.csv` without a run = the same.
- **1.0.18 = step 7d "Run interrupted by a reset"** (9.3.5). Engine: `RunPos position()` (run time;
  Sequence phase + stage; Timeline step, stage and seconds left per channel) and `resumeAt()` (host-tested):
  Sequence = the phase from its beginning, channels the phase starts begin at the measured PV, the other
  channel gets the target of the phases before it **at once** (jump; decided by me, Rost may change it);
  Timeline = at the run time, ramp from the measured PV in the time left, hold / idle with the time left,
  a pause asks for Continue again, a finished lane keeps its last target. Firmware: `/duo/run.json`
  (slot, run id, edited, saved controller gradients, running copy) at the start and after each run edit /
  save; `/duo/runpos.json` on every phase / step / stage change and every 60 s; RTC_NOINIT record (magic,
  run id, position, checksum) every cycle — exact after a reset, the flash position after a power loss
  ("about"). Files and record removed when the run ends. Boot: offer + log entry with the reset reason;
  outputs-off written to the program's channels. Resume (`POST /api/run {"cmd":"recover"}`) runs the
  start checks ("Cannot resume: …"), keeps the saved gradient to restore at the end; Dismiss
  (`"dismiss"`) = outputs off, gradient and front lock restored. Starting another program ends the offer
  (after a confirmation on the page; the old gradients carry over to the channels the new run takes).
  `/api/live` `run.intr`; the page shows a banner in the run panel (Resume from phase N / Resume at
  m:ss, Dismiss). Chart after a resume: history before the resume is gone (RAM), lanes empty there.
  Not yet: touch Resume / Dismiss buttons (step 6; the touch message line shows the offer), serial
  `resume run` (step 8).
  Rost's S3 test (2026-10-02): offer and "about" after a power loss OK; the page's event log missed the
  boot / interruption entries (it asked for entries after the old sequence number, the device had started
  again at 1) → the page now notices a restart (`logSeq` or `up` went back), marks it and reloads the log.
  Also in 1.0.18 (Rost): the history records a setpoint only while it is active (Modbus: run, manual,
  kept / set by the end action, until manual off or a fault; local: while the output is on); the Live
  legend switches PV / SP per channel (chart, Y scale, export; localStorage `lvis`); Export CSV =
  `/api/history.csv?pv=<mask>&sp=<mask>` (only those columns, rows without values left out).
- **1.0.19 = step 6 for the S3** (new touch UI, `src/tui_*.cpp`, see section 6 note): main screen as in
  the agreed mockup (header with program / phase / state / time and run buttons, chart of both channels
  with program ↔ live on tap, two channel tiles with PV, SP, output, status, target stepper (run target
  or manual SP, sent 0.7 s after the last tap), manual switch, "+ Hold" (+10 s / +30 s / +1 min / +5 min),
  Acknowledge), overlays on every screen ("Action required", "Run interrupted by a reset" with Resume /
  Later / Dismiss, "Save the run's changes?", messages), menu with Programs (list + table editor: Sequence
  one table, Timeline two tables; undo, save, start / start without saving, rename, delete; during a run
  run edits of targets and times), Channels (general, Modbus, thermocouple, PID + autotune, runaway, RS485
  bus; registers and two-point calibration stay web-only), Network (scan, password with eye, AP password,
  host name, fixed IP), Display (brightness, dark, accent presets, rotation, live period), System (polling
  interval, band, info, restart), Event log. `chartSnapshot(d, live)` gives the live view during a run too;
  `runProgramCopy()` for the editor. Montserrat 16 / 20 enabled (symbols). Host harness: 28 screenshots,
  random-tap test (ASan / UBSan clean). The old exported UI is not built on the S3 (`UI_V2`).
  Rost's review of the previews (2026-10-06): chart without a tile (on the screen background), the
  program / forecast curves dashed in the SP colour at 50 % opacity (even dashes along the whole curve);
  setting rows without tiles and closer together; accent ring no longer clipped; "Manual" label removed,
  the manual switch is now an on / off button (power symbol) filled with the channel's PV colour while on.
  First test on the S3 (2026-10-08): the screen stayed dark on Rost's clone DevKitC, whose 5 V pin sits
  behind a diode (the display's 5 V supply too low); bridged, the screen works. The touch chart lost points
  while moving (1.45 px per point, every 2nd point skipped, so peaks / noise changed from second to
  second): now gathered per pixel column (first / lowest / highest / last value), and the PV of each point
  carries its lowest / highest reading (`ChartData.lo/hi`, S3 only), so short peaks show like on the web.
- 1.0.18 + 1.0.19 tested on the S3 and CYD (2026-10-09, mostly OK; committed as 1.0.19). **Modbus is a beta
  feature from now on**: Rost has no Modbus controller, so everything Modbus / RS485 stays in the firmware but
  is labelled "beta" (touch: channel type, Modbus heading, RS485 tab; web: type, sections, Modbus tools) and is
  left out of the test lists until a controller is available.
- **1.0.20** (Rost's test results):
  - End action "keep setpoint" / "end setpoint": the channel goes on in **manual mode** with that setpoint
    (manual button on, stepper shows it; the button switches the outputs off).
  - Resume after a reset: ramps keep their **planned speed** = |target − previous target| / ramp time
    (first ramp: the speed saved with the position, `RunPos.slope`, runpos.json 4th value), from the measured
    temperature. Sequence: the ramp takes as long as that needs. Timeline: the hold after it takes up the
    difference (`Lane.hold0`), so later steps keep their run time and the channels stay in step (Rost's choice).
  - Restart reason: a requested restart (touch / web) leaves a note in RTC memory, the next boot logs
    "restart on the touch screen / from the web page"; the restart itself is orderly (outputs off, no more
    drawing, backlight off, 0.4 s, then restart). A panic now logs "crash, rst N" (ROM reset code). Rost saw
    "crash" after every restart on the S3: if it stays "crash while restarting", the serial log just before the
    ROM banner has the panic.
  - Touch: the header shows the selected program once another one is chosen after a run; stepper − / + held
    longer than 3 s steps by 5 °C (round values), back to 1 °C after release; the "Run interrupted" dialog
    closes when resumed / dismissed on the web page; messages on the main screen sit over the lower edge of
    the chart (the tile controls stay free), elsewhere at the bottom; tile status "Autotune 200 °C" /
    "Autotune done"; the keyboard has a close button (no change); the Display screen follows changes made on
    the web page.
  - Charts (web and touch): SP lines at 50 % with the area below them filled at 20 % (Rost: fill under SP,
    not PV); live periods 1 min … 24 h
    (`LIVE_PERIODS`, at most what the history holds: the CYD's "whole history" 35 min entry is gone, 30 min is
    its longest); display.liveMin 1…1440.
  - S3 internal RAM went down to about 3 kB free (Rost, touch System info): LVGL allocated every object,
    style and label text with malloc(), which keeps blocks under 4 kB in internal RAM, and the touch screens
    stay created once visited. LVGL now allocates in PSRAM (`LV_STDLIB_CUSTOM`, src/lv_mem_psram.c; internal
    only when PSRAM is full), the touch chart's snapshot too (6 kB). The live history and chart rows were in
    PSRAM already. The S3 also logs `[mem] running` once a minute now (+ PSRAM free).
  - Missing files are checked with stat() (no "does not exist, no permits for creation" errors at boot).
  - Open: CYD "[mem] running" minimum went down to 2.3 kB with the page open and weak Wi-Fi (free stays
    about 43 kB, so no leak); watch it, a lower peak for /api/chart may be needed. The old CYD screens log two
    LVGL "deprecated flag" warnings at boot (generated code, goes with step 6 for the CYD).
- 1.0.20 committed (2adf575, 2026-10-09). Rost: open / untested points (Modbus, run tests on the CYD, 1.0.20
  items) are revisited later; development goes on.
- **1.0.21 = step 6 for the CYD** (2026-10-09): the CYD runs the new touch UI too (`UI_V2` in board_cyd.h);
  one code for both sizes, `TUI_SMALL` (SCREEN_W < 400) in tui.h with size constants (`T_HDR`, `T_BTN`,
  `T_DLG_W`). 320×240 set as decided (9.7, 9.3.3):
  - main: header (program / where, badge, time; tap: program picker), chart 76 px, two tiles (name, status,
    PV, **one SP button** = the setpoint in force (ramping: the moving one) in the SP colour, else the one
    that would be applied; tap: number pad for the run target or the manual setpoint; output bar, "Out x %",
    manual button / + Hold / Acknowledge), **bottom bar: Menu, Run, Stop, Next**; messages over the chart.
  - editor: **Sequence = one line per phase** (# | type | "150° 1:00 / 1:00", "B 160° · T 150° …" | option),
    tap → the phase menu with its values first ("Preheater target: 150 °C" → number pad), then type, wait,
    insert / move / delete; during a run only the values. **Timeline = one table + channel switch** (buttons
    "Preheater · 3" / "IR head · 6").
  - popups, pages, lists, channel settings, network, display, system, log: compact sizes.
  - RAM: only the main screen stays; other screens are built when opened and deleted when left (with the
    editor's draft and undo steps, `progEditRelease()`); undo 2 steps on the CYD (a program is ~3.9 kB).
    The touch chart's copy has no lo/hi on the CYD (`CHART_LOHI` only with PSRAM, 2.4 kB). PC measurement
    (64-bit, so ESP32 needs less): main screen 43 kB, biggest other screen +73 kB (channels), back on main
    +23 kB (allocator / caches, not growing; malloc tracking shows no leak).
  - System info: "RAM x kB free (lowest y)" = the internal byte-addressable heap (was ESP.getFreeHeap(),
    which counts the CYD's IRAM-only heap too).
  - CYD flash image 1.77 MB (56 %, was 65 %: the old exported screens and their fonts are no longer linked).
    `ui/`, `src/ui.cpp`, `src/ui_gen.c` are not built any more (to be removed once Rost has the new UI
    running on the CYD).
- 1.0.21 tested by Rost on the CYD and committed (db87cbc, 2026-10-09): screens fine; lowest free RAM
  14 kB after adding steps to a program without saving and scrolling through the Channels screen.
- **1.0.22 = step 8, part 1: the serial console (7a)** (2026-10-09), `src/console.cpp`:
  - S3: native USB port, TinyUSB CDC (`USBCDC`, ARDUINO_USB_MODE=0), product name "TempCtl Duo"; banner +
    help when a terminal raises DTR, one line on the UART log ("serial console on the USB port"); writes
    only while DTR is set (tx timeout 20 ms, never stalls the loop). CYD: `Serial`, shared with the log.
  - All log output goes through `LogSerial` (`Serial.print*` replaced): on the shared port a log line first
    clears the typed line (`\r ESC[K`), the console shows "> " + the typed text again afterwards.
  - Commands as in 7a, with these choices: **`wifi ap [pw=]`** (the AP name is fixed, `TempCtl-Duo-XXXX`
    per 9.8, so no `ap=<SSID>`; a given `ap=` gets a note); `wifi connect` keeps the new network only
    after it connected (then saved), on failure the previous SSID / password are restored; results:
    wrong password (auth fail / handshake timeouts / MIC failure), network not found (2 × NO_AP_FOUND),
    timeout (25 s); scan list `n  SSID  rssi dBm  auth  ch n` (net.cpp keeps the details of the last scan,
    `netScanDetail`). Added **`dismiss run`** next to `resume run`, and `save` / `discard` answer the
    "run changed the program" question. `start` refuses while an interrupted run is offered (resume /
    dismiss first). `reboot` (also `restart`; refused during a run, like touch / web; logs "Restart
    requested from the serial console"). `api on|off` comes with 7b.
  - Events: S3 prints new event-log entries on the console port (the CYD's log already shows them);
    "Action required" block every 30 s; interrupted run once (again for a newly opened terminal).
  - RAM: command snapshot (LiveData + Settings) on the heap only while a command runs; scan list (800 B)
    from the first scan on. CYD static RAM +0.5 kB; S3 +12 kB (TinyUSB).
  - Fixed on the way: `millis() | 1` as "non-zero now" can be 1 ms ahead, so `millis() - t` wrapped (the
    requested restart sometimes skipped its 0.4 s; touch UI timers fired early) → explicit `if (!t) t = 1`
    / `tTickNow()`.
  - PC test `test/console_host` (shims for Print / Stream / WiFi, stubs for the app layer): scripted typing.
- CYD colours (Rost's photo, 2026-10-09): every colour had red and blue swapped (pink accent shown violet,
  blue as orange, the swatch row in the wrong colours; neutrals unaffected, so the old UI hid it too) →
  `LCD_BGR 0` in board_cyd.h (this panel is RGB; other CYD revisions may need 1). In 1.0.22.
- 1.0.22 tested by Rost (CYD colours fixed, console fine) and committed (bf9a2b3, 2026-10-09).
- **1.0.23 = more console commands** (2026-10-09, help previewed and agreed with Rost):
  - `help` in sections (STATUS, MANUAL CONTROL, PROGRAMS AND RUNS, AUTOTUNE, WI-FI, SETTINGS, DEVICE);
    `help <section>` prints one; `help settings` (also `help set` / `help get`) the full settings page:
    every key with its range / choices, one `e.g.` line per section. Unknown section → error + the list.
  - `watch` (one status line per second, any key stops), `info` (firmware, board, uptime, internal RAM
    free / lowest / largest block, PSRAM, live history length, network, web URL, programs), `log [n]`
    (last n event-log entries, default 20), `ack b|t` (CMD_ACK_FAULT).
  - `tune b|t start temp=<C>` (local channel, 50 … maxTemp − 20, not during a run, no fault), `stop`,
    `accept` (kp / ki / kd into the channel settings, saved) / `discard`; `tune b|t` prints its state;
    `status` shows the autotune state of a local channel.
  - `get [prefix]` / `set k=v …` (up to 8): the web API keys (settingsToJson / settingsFromJson /
    settingsValidate), `b.` / `t.` = channels[0/1], sections bus / run / wifi / display. Value checked per
    type first (number range, int, on/off, choice, IP, register address or empty, accent preset name or
    #RRGGBB, parity none|even|odd); any error → "nothing changed". Applied to a copy, validated, then
    taken over; each key is reported as stored, or "(adjusted: it depends on other settings)", e.g.
    spMax after a lower maxTemp. Saved + notified at once; Wi-Fi keys re-apply Wi-Fi. Modbus / bus keys
    hidden on boards without RS485 (except pollMs). Allowed during a run (like touch / web).
  - `factory reset yes`: settingsDefaults (Wi-Fi included, programs kept), save, restart; refused during
    a run; without `yes` only a warning.
  - `touch cal` (CYD only): overlay on the top layer with 4 crosses 24 px from the corners; the raw
    XPT2046 values are averaged while pressed; result extrapolated to the screen edges (clamped 0..4095),
    rejected if a raw span < 300; 60 s timeout; any key on the console cancels; saved + used at once
    (`touchSetCal`). UI harness: simulated taps give x 199 … 3850, y 3700 … 301 (expected 200 … 3850,
    3700 … 300).
  - `wifi forget`: clears SSID / password, saved, access point only (its name printed).
  - Builds: S3 flash 1.83 MB, static RAM 92.1 kB; CYD 57.5–57.8 % of the app partition, 79.0–79.5 kB.
  - Rost's test on CYD + S3 (2026-10-09): touch cal, set (spMax adjusted), info, factory reset, help
    settings, static IP fine. Fixed: `watch` stopped at once with a CR LF terminal (the LF counted as
    "any key"; the LF of CR LF is now dropped before the watch / calibration key checks); `set set k=v`
    ignored the stray word (now an error, also more than 8 pairs); Wi-Fi re-applied after the `set`
    report (its log lines no longer cut into it); the restart reason was cut to 19 characters ("factory
    res"; RTC note 48 now, magic RST2, reason worked out once); `factory reset` names the real AP;
    "access point started" logged once (mode + softAP both fire AP_START).
- 1.0.23 committed (5430930, 2026-10-09) after Rost's test.
- **1.0.24** (2026-10-09, Rost's requests):
  - Custom accent colour. Web: a 7th "Custom" choice whose circle holds an `<input type=color>` (rainbow
    circle until a custom colour is used, then that colour; the "Custom" text opens the picker too); saved
    on the picker's `change`. Touch: a 7th circle with a pen (outlined; filled with the colour when a
    custom one is active) opens "Custom colour": swatch + #RRGGBB, sliders Hue (scale of 6 two-colour
    gradient segments under a knob-only slider), Saturation, Brightness (20 … 100; darker is no usable
    accent), Cancel / OK. The colour stays exact if no slider is moved (HSV round trip is not exact).
    S3 setting hints now wrap (were cut at 280 px).
  - Screen restart without a device restart: web System → "Restart screen" (`POST /api/screen/restart`),
    serial `screen restart` (also `screen reset`). Done in the UI task: touch controller (FT6336: I2C bus
    recovery = up to 9 SCL clocks + STOP, reset pin, Wire set up again, chip id read; XPT2046: SPI + IRQ
    set up again), pending press dropped (`lv_indev_reset`), display controller: hardware reset (S3 pin
    14; CYD none) and LVGL's whole init sequence sent by a short-lived second LVGL display on the same SPI
    device, PANEL_FIXUP + invert, rotation set again (MADCTL), everything redrawn, brightness again. The
    console reports "display set up / touch controller answers / does NOT answer", or after 5 s "the
    screen task does not respond". Not on the touch screen itself.
  - Console: Up / Down keys recall the last commands (20, 1 kB ring; lines with `pw=` and password
    entries not kept; ESC [ A/B and ESC O A/B). A key that stops `watch` / cancels `touch cal` is used up
    whole (an arrow's sequence does not recall). A `pw=` value stays masked when the line is shown again
    after a log line (was shown in clear).
  - Builds: S3 static RAM 93.2 kB; CYD 80.1–80.6 kB (+1.1 kB: history).
- 1.0.24 committed (7ec0973, 2026-10-09, before Rost's test; he asked to commit).
- **1.0.25 = automatic screen check** (2026-10-09, Rost: "is it possible to detect an unresponsive screen?"):
  - Setting `display.screenCheck` (default on; web Display switch, touch Display row "Screen check",
    `set display.screenCheck=`). Every second in the UI task (`screenWatch`, tui.cpp; was 5 s).
  - Display: LCD MISO now used (S3 GPIO13 shared with the unused SD card; CYD GPIO12). `lcdCheck()` reads
    0Ah (power: bits 4 sleep out + 2 display on), 0Bh MADCTL (bits MY MX MV BGR vs the driver's
    madctl_reg), 0Ch (bits 2..0 = 5). Reads at 4 MHz half duplex: the display's SPI device is removed,
    a slow one (command phase 8 bits + 8 bits in, same CS) added for the reads, then the display's added
    again (devCfg kept). ILI9341: index register D9h = 10h before each read (TFT_eSPI readcommand8).
    The first read right after the init decides whether the status reads back at all (all three as set
    up); otherwise the display part stays off ("[lcd] status: ... check off").
  - Touch: FT6336 chip id read (`touchCheck`), must match the id found at boot; not found at boot = no
    check. XPT2046: -1 (cannot be asked).
  - Stuck press (both): pressed and moved <= 3 px for 60 s → touch restart (3 tries until a release).
  - Two failed checks in a row → restart of that part (display failure: display, plus touch if that
    failed too); max one automatic restart per 30 s; 3 in a row without a passing check → event "restarting
    did not help", stops until a check passes. Each restart: event "Screen restarted automatically: ...".
  - Console `screen check`: display values (power / MADCTL / format), touch state, on/off, automatic
    restarts so far. On the hardware: check the boot line "[lcd] status: ..." — if it says "check off",
    the read needs adjusting (dummy clock / D9 index quirk).
  - Rost's S3 boot log: "power 0xBC, MADCTL 0x78 (0x28 expected), pixel format 0xFF" = value | value << 1
    for all three (0x9C, 0x28, 0x55): the panel answers correctly, the hardware SPI sampled each bit while
    the line already changed to the next. → The status is read in software (~60 kHz): device removed,
    bus freed (spi_bus_free), pins as GPIOs, reads, then spi_bus_initialize + add device again. Sampling
    point chosen at boot from two (late in the high phase / late in the low phase): the one that reads
    the setup back; the boot line then shows "OK (sample n), check on" or both raw sets for diagnosis.
  - Verified on the hardware (Rost, 2026-10-09): S3 "power 9C, MADCTL 28, format 55: OK (sample 1)";
    CYD "power 9C, MADCTL 20, format 05: OK (sample 0)" (ILI9341 with the D9h index read works; its
    format reads 05). The two panels need different sampling points, so the boot-time choice stays.
  - Hidden serial command `screen check test` (not in the help): DISPOFF (28h) + SLPIN (10h) to the
    panel, resets the automatic-restart limits; refused when the check is off or the status is not
    readable. Expected: two failed checks → "Screen restarted automatically: display lost its setup".
  - Tested by Rost on S3 and CYD (2026-10-09): `screen check test` → power 08 → restarted, screen back.
    Then the interval 5 s → 1 s (Rost; cost ~1–2 ms per check: software read + SPI bus set up again,
    one I2C read; a hanging display is noticed after ~2 s).
- 1.0.25 committed (858126d, 2026-10-09) after Rost's test on both boards.
- **Step 8 part 2 = 7b, in three commits** (Rost: "continue with the development", 2026-10-09):
  1. **1.0.26 device `api` mode** — 2. 1.0.27 the page (connect screen + serial transport, same
  index.html) — 3. 1.0.28 the Go launcher. Simplification against 7b: no pushed `status`; the page
  polls `/api/live` over serial exactly as over HTTP (same page code; the polling is the keep-alive).
  - 1.0.26: console.cpp `api on` → `{"t":"hello","api":1,"fw","name","board","variant","host"}`; request
    lines `{"id","m":GET|POST|PING,"p":"/api/...?q","b":{}}` → `webApiRequest()` (web.cpp): SerialReq
    context, `reqHas/reqArg/parseBody/reqUri` read from it, `sendBody/sendStream` answer as JSON lines
    (`{"id","code","body":<raw JSON>}`; text: `{"id","code","type","chunk"}`… `{"id","end":true}`, 1 kB
    pieces with `apiFlush(8192)`); one route table (`ROUTES`) for HTTP and serial. Output queue (heap,
    grows, freed after big answers) written as the port takes it (`availableForWrite`); mutex because
    log lines come from other tasks. CYD: log lines in api mode → `{"t":"log","m"}` (dropped while
    > 8 kB queued, then "(n log lines dropped)"); S3: the log stays on the UART. `api off` or 15 s
    without a request → `{"t":"bye","why"}` and the human console. Request lines up to 16 kB (buffer
    grows, freed after). Buffers: S3 USB CDC RX 4 kB; CYD Serial RX 4 kB / TX 2 kB (was 256 / 0).
  - **Page (part 2)**: web/index.html gets a transport layer: `api()` uses `serReq()` when connected
    over USB (JSON lines; text answers assembled from chunks; per-request timeout ≥ 15 s, re-armed per
    chunk). Off the device (no `data-device` mark — web_embed.py marks the device's copy; the launcher
    marks its copy `data-launcher`) the page shows a connect screen: USB cable (`requestPort`, open
    115200 with DTR + RTS on: no CYD auto-reset, S3 sends only with DTR) or Wi-Fi (address → the
    device's page; remembered). "api on" up to 5 × (CYD may reset on open), hello's `api` checked
    against `API_OK = [1]`. Auto-connect on load to the remembered port (USB ids, until "Disconnect
    USB"); `navigator.serial` `connect` → reload (fresh state, also after a device restart); a non-JSON
    line while connected (CYD restarted) → "api on" again; a `bye` not asked for → "api on" again.
    PING every 5 s when idle. CSV links fetched over USB and saved as files. Playwright test with a
    simulated port answering from the mock server (mock/ser.js).
  - **Launcher (part 3)**: tools/launcher (Go, no dependencies): embeds the site (tools/build_site.py:
    device URL layout), serves `http://localhost:8765` (127.0.0.1 + ::1), `/launcher/id`,
    `/launcher/alive` (the page calls it every 5 s; ends 30 s after the last one, or 2 min without
    any), `/api/*` → 404. Port taken: our launcher → just open a window; else a message box. Opens
    Chrome (Program Files / LocalAppData) or Edge with `--app=`, else the default browser. build.sh:
    version from src/version.h, Windows exe with `-H windowsgui` (6.6 MB), `all` also macOS / Linux.
  - Rost's test (2026-10-09): console api OK (live, chart, CSV chunks, POST); launcher over USB "works
    perfectly". Added: `help api` (also `help json`): how to use the api mode, with examples. Device
    search (Rost chose it for "firewall prompt for network access"): launcher discover.go — legacy
    unicast mDNS PTR queries for `_tempctl._tcp` + `_http._tcp` from a UDP socket bound to each IPv4
    address (Windows asks once: "Allow access"), answers checked via GET /api/live (fw + run present),
    devices kept 30 s; `/launcher/devices` → {devices:[{host,ip,port,fw,board,variant,net}],searching};
    the connect screen (launcher only) lists them every 3 s, a click opens http://ip/. Firmware
    announces `_tempctl._tcp` port 80 with TXT fw / board / api (net.cpp). Tested with a fake responder.
  - Tabs (Rost, 2026-10-09, look of Windows Terminal tabs): web/shell.html = the launcher window at `/`
    (tab bar: icon USB / Wi-Fi / logo + status dot ok/lost, title, ×, middle click closes, "+" new tab;
    active tab in the page colour, separators between inactive ones; light / dark by the system).
    Each tab an iframe (`allow="serial"`): `/app?tab=<id>` (the page; USB session of its own, per-tab
    storage keys `tcd.usb.<id>` / `tcd.auto.<id>`) or the device's own page over Wi-Fi (checked first
    with a no-cors fetch; "does not answer" note, retried every 5 s). The pages report to the shell
    by postMessage `{tcd:1, open|usb|disconnect|state}`; the shell sends `{close}` (USB: api off).
    Tabs saved (`tcd.tabs`) and restored. A device's page shown in a tab has **Disconnect** (bottom
    left) → the tab goes back to the connect screen. Auto-connect tries all granted ports with the
    tab's USB ids (another tab may hold one). Keyboard shortcuts left out (Ctrl+T/W belong to the
    browser). Tested with Playwright: found device opened, USB tab, Disconnect, restore, close.
- Committed 2026-10-10: **1.0.26** (99410ae) serial api mode + help api; **1.0.27** (6e04872) Web Serial
  page, launcher, device search, tabs. Next 1.0.28. (Delete permission per session is needed for git.)
- **1.0.28 = old touch UI removed** (Rost chose it, 2026-10-10): `ui/` (LVGL Pro project, 3 MB), `src/ui.cpp`,
  `src/ui_gen.c/.h`, `src/program.*` (legacy TempCtl step view; its host test and `loadLegacyView` too),
  the `UI_V2` switch (board headers, `#ifdef` around tui_*), the old palette API (`uiPaletteNames/Count`),
  `symlink://ui` in platformio.ini, the ui/ lines in .gitignore, README 4a "old touch UI" and the layout
  lines. Kept: the legacy fields of `/api/status` (published API) and `Settings::themePalette` (in the
  JSON, unused). Flash and RAM unchanged (the linker had dropped the old UI already).
- 1.0.28 committed (23960da, 2026-10-10). Line endings: the repo mixes CRLF and LF files; writing a CRLF
  file back with LF shows it as fully rewritten (happened unnoticed in 1.0.22-1.0.25 for settings.cpp,
  lcd.cpp, tui_menu.cpp, web.cpp, index.html, app_state.cpp, console.cpp, net.cpp, tui.cpp, README;
  fixed before the 1.0.28 commit by amending).
- **1.0.29** (a8d0237, 2026-10-10, Rost agreed): `.gitattributes` — `* text=auto` (stored LF; Git for Windows
  checks out CRLF with its default core.autocrlf=true), `*.sh text eol=lf`, png / ico / woff2 / ttf / exe
  binary; 36 CRLF files renormalized (content unchanged: the diff ignoring CR is only .gitattributes and
  version.h). Since then line endings of written files no longer matter for commits. Note: some files
  in "Claude outputs/" are tracked (the wiring PNGs).
- Rost's retest (2026-10-11): OK — 1-min chart period, stepper acceleration, restart reason, end action →
  manual mode; the Wi-Fi "Scanning…" freeze (section 10) never seen since the fix → closed. Still to
  retest: resume keeps the ramp speed, dialog auto-close / toast position, autotune status; program runs
  on the CYD; CYD memory peak with the page open (2.3 kB once); Modbus (no controller yet).
- Rost (2026-10-11): dialogs / toasts OK. Found: manual screen restarts not logged; the **S3** showed a blank
  white screen in the morning with nothing in the log (manual restart from the web helped); wants a full
  scrollable, colour-coded log on the System page. Decided with Rost: the log holds **everything**
  (events + technical serial lines); **events survive restarts** (flash).
- **1.0.30** (2026-10-11):
  - src/syslog.*: kinds (0 technical, 1 event, 2 run, 3 warning, 4 error; `logClassify` by words);
    byte ring of all lines [len, kind, uptime, epoch, text] (S3 256 kB PSRAM ≈ 7000 lines, CYD 4 kB
    ≈ 50); LogPort::write tees every serial log line (logEvent's "[event]" lines are stored by logEvent
    with their kind); boot number in /duo/boot.txt; events saved to /duo/events.txt ("boot\tepoch\tup\t
    kind\ttext", from the event ring, at most every 30 s and before a requested restart; > 40 kB →
    newest 300 kept). Wall clock: NTP once on Wi-Fi (configTime UTC), else the page POSTs /api/time.
    Host test of the ring (eviction, order, cut at 199, byte limit, tee).
  - LogEntry + kind + epoch, LOG_LEN 96 → 120; `logRun()` for phase / step / end; say() → run if the text
    is about the program / run, else event. Touch Event log and serial `log` show the kinds (colours /
    "ERROR:" "warning:"). /api/log adds kind + epoch; /api/syslog, /api/eventlog.txt, POST /api/time;
    live: sysSeq, boot, time.
  - Web System page: Log panel (filters per kind, search, download; earlier starts from the file as
    sections; time = local wall time when known, else +uptime); Status page event log coloured.
  - Screen: manual restart (web / serial) → event "Screen restarted from … - before: display OK (regs
    9C 28 55 00), touch OK" (to learn what the white screen was); display check reads 0Dh too and
    compares 0Ah (all mode bits incl. booster), 0Ch, 0Dh with the values right after the init (MADCTL
    with the driver's current value) — a panel with the booster off (white, setup intact) is caught now.
  - RAM: CYD static +1.6 kB (LOG_LEN), heap −4 kB (ring); S3 ring in PSRAM.
  - Fix after Rost's test: the System log showed one text on every line (or "�"). ArduinoJson ≥ 7.3 keeps a
    `const char[]` as a pointer (taken for a string literal); syslogRead reuses (and frees) one SysLine,
    so all lines pointed to the last text / freed memory. Now `x.add(JsonString(l.text))` (copied). Rule:
    texts from reused or freed buffers go in as JsonString / String. Also: log texts cut to fit (120 / 199
    bytes, long serial lines split by the tee) no longer end in half a UTF-8 character (`utf8Fit`).
  - Rost's S3 showed "check off": the automatic screen check was switched off there, which is why the
    white screen was not restarted (System → Display "Restart a hanging screen by itself").
- **1.0.31** (2026-10-12): channel outputs on → highlighted tile. ChannelLive.outOn (Modbus: outputsOn_,
  local: PID on or autotune), /api/live + /api/channels `outOn`. Touch main screen: style tileOn[ch] —
  border 4 px in the PV colour, background lv_color_mix(PV, panel, 20 %), padding 3 px less so the content
  stays in place (LVGL counts the border into the content area). Web channel card `.chan.on`: 4 px border,
  color-mix 20 % background, padding 14/11/11. Checked: UI harness shots (both boards, dark / light),
  Playwright (card content at the same x on / off).
  Then: the orange inset ring of a manual card (.chan.man) is removed (the outputs-on style shows it);
  the web manual switch turns manual off at once (no "Switch off?" question), like the touch button.
- **1.0.32** (2026-10-12): Convert to Timeline (web, `seqToTl` in index.html) and SD run logs (S3).
  - Convert: checked against the page's preview model (400 random Sequences: same per-channel curves,
    same pause times, codec round trip) and the device's run engine (300 random Sequences without
    waits, both types simulated side by side: same setpoints / idle every 0.5 s, same end time); UI
    test: dialog, Replace + Undo, Create a new program.
  - src/sdlog.*: PSRAM double buffer 2 × 64 kB (≈ 15 min of rows if the card is not written),
    writer task flushes every 30 s / at the run end / at half full; IDF sdspi + FAT on SPI2_HOST
    (`esp_vfs_fat_sdspi_mount` with the display's bus; unmount removes only the SD device; CS driven high
    again), never formats; file names `YYYY-MM-DD_HHMMSS_<program>.csv` (UTC) or
    `start<N>_run<M>_<program>.csv`, `_2` … if taken. Web: `/api/sd`, `/api/sd/file?name=` (≤ 4 MB,
    copied to PSRAM first so the bus is free while it is sent), `POST /api/sd/delete`; live `sd`.
    Host test of the logger with a folder as the card (names, CSV widths / quoting, no card, list, load,
    delete, the running file protected, dropped rows reported). Static RAM: S3 +0.4 kB, CYD +0.1 kB.
  - To watch on the hardware: the SD card on the display's MISO (the screen check reads the display
    on the same line), UI stutter while the card is written (sdspi holds the bus during busy waits),
    a run start without a card (the probe takes the bus for a moment).
- **1.0.33** (2026-10-12, Rost's video): (1) "Run changed" was not closed on the touch screen when
  answered on the web — the web's "Keep the stored program" only closed its own dialog. New
  `CMD_DISCARD_RUN` (`/api/run {"cmd":"discard"}`, touch Discard, serial `discard`) marks the question
  answered (event "Run changes not saved …"); touch closes its dialog when `runEdited` goes false (or a
  run starts), the web closes `saveDlg` the same way, serial drops its pending question. (2) The chart's SP
  fill moved only with the history (every 2 s / one run bin): `chartHead()` now also takes the active SP
  from each `/api/live` poll (`headSp`: controller SP, else program / manual SP while the outputs are on)
  and extends the SP line + fill to it, like the PV head; the join distance is max(8 s, 2.5 bins).
- **1.0.34** (2026-10-12): Rost's S3 restarted by a watchdog when he stopped a run (1.0.33; no backtrace
  available, the RAM log is lost with the reset). Likely the new SD writer: the SDK's task watchdog also
  watches IDLE on core 0, the writer task had no core affinity (priority 1 > idle), and the sdspi driver
  waits for the card in polling loops without yielding (esp_timer); with no card the MISO line floats
  and noise can look like answers -> long busy retries. Fixes: writer pinned to core 1; internal
  pull-up on MISO before each mount (no card -> 0xFF -> fails at once); mount done step by step
  (sdspi_host_init_device, sdmmc_card_init, ff_diskio_register_sdmmc, esp_vfs_fat_register, f_mount) so
  every failure removes the SD device from the bus again (a device left on the bus would make the screen
  check's spi_bus_free fail); readRegs checks spi_bus_free and puts the LCD device back if another
  device is still there; the control task no longer writes the card (a run start waits up to 2 s for
  the writer to close the previous log). Diagnosis: src/crumbs.* - stage codes of the loop (UI, screen
  check, LVGL, LCD SPI wait, console, web + request path), the control task (cmd n, request, poll, cycle,
  waiting) and the SD card (bus wait, mount, read/write, unmount) in RTC memory; after a watchdog / crash
  the boot logs an Error event "task watchdog - loop: LCD SPI wait 15.0s; control: waiting 0.0s; SD:
  mount 15.2s". Reset reasons now "task watchdog" / "interrupt watchdog" / "watchdog".
- **1.0.35** (2026-10-13): the breadcrumbs caught it: "crash, rst 12 - loop: web /api/sd 0.1s; SD: mount
  0.1s" — the reset at the end of a run was the **web handler mounting the card in the loop task**
  (System page: the SD panel reloads its list when a run starts / ends; most likely a stack overflow:
  FATFS with long names + the SD driver on top of the loop's web / LVGL stack). Rost's files also showed
  rows lost: 1.0.33 file cut mid-row (reset during a write), 1.0.34 file row 0 then 32 s+ (the write at
  30 s failed and the swapped buffer was already gone). Now: **all card work in the SD task** (12 kB stack,
  core 1): web list / load / delete are requests to it (the web waits up to 8 s, "SD card busy - try
  again" otherwise; an abandoned request is cleaned up by the SD task); rows go from the double buffer
  into `pend` (192 kB PSRAM) which is emptied only when the card took it — a failed write is retried
  every 5 s (warning event once "SD card: writing the run log failed (…) - trying again", a warning row in
  the file if pend overflowed), at the run end 3 tries 1 s apart, then "SD card: the end of the run log
  could not be written". Mount: one retry when a card answered but failed; SD clock 10 MHz (shared lines
  with the display). Host test: failed write in the middle -> all 61 rows in order + one warning row;
  web requests through the SD task; end with a failing card gives up with the event.
- **1.0.36** (2026-10-13): 1.0.35 confirmed by Rost (runs logged completely, no crash at stop). Time zone
  (decided: CSV times **local without offset**, zone **from the browser + setting**): `Settings.tzName`
  ("Europe/Berlin" / "UTC+02:00" / "" = UTC; only the name is stored, `tzLookup()` gives the POSIX rule —
  75 zones with summer time rules in settings.cpp, plus UTC±hh[:mm] fixed); applied by `syslogLoop`
  (setenv TZ + tzset when it changes). Auto: the page POSTs `/api/time {zone, fixed}` once while
  `live.tz` is "" (the device takes it only while unset; event "Time zone set from the browser").
  Setting: web System › Clock (select from `/api/timezones` + the browser's offset), touch System › Time
  zone (two steps: region, then place; tChoice now up to 40 rows), serial `set time.zone=…`, `help time`
  lists the zones. SD CSV: column `time` = "2026-10-13 01:05:35" (local), file names local; the web shows
  file times as written. Host test: offsets of Berlin / London / Kyiv / New York / Sydney / Kolkata /
  fixed, the Berlin change on 25.10.2026, every list rule parsed by glibc, settings JSON. Static RAM:
  CYD +0.7 kB (tzName in ~10 Settings copies, the touch lists).
- **1.0.37** (2026-10-13, Rost's list): web pages **Settings** (was System, cog icon; `#/system` still
  works) and **Log** (SD run logs + system log); "RS485 bus" with the yellow beta badge (form groups take
  `beta:true`); title bar 60 px on every page, the jump buttons only when the Status page is one column
  (≤ 1100 px); after a run: **Export CSV** (`/api/log.csv`) + **Back to preview**; sidebar section on
  every page (program, state · time, play/pause + stop = the run bar's buttons; per channel PV · state and
  a manual on/off button; SSR relay state) between the pages and the footer, thin lines between; the two
  CSV links removed from the footer. Fault waiting for Acknowledge: blinking red warning badge on the top
  edge of the channel card (web) and tile (touch, 500 ms). **SSR supply relay** (decided: S3 GPIO 4,
  level selectable `relay.activeLow`, S3 only): local task, after each 250 ms step: on = some enabled
  local channel has outputs on / autotune, and no local channel has a fault; off at once on a fault
  (event "SSR supply relay off: <fault>"); the pin stays an input until the settings set the level
  (then level first, output second). Settings: web Channels › SSR supply relay, touch System, serial
  `set relay.activeLow=on`; live `relay`, config `limits.relay`.
- **1.0.38** (2026-10-13): protections of 9.2a; web **Dashboard** (was Status; `#/dashboard` works),
  Settings page jump buttons (Device / Wi-Fi / Safety / Display / Clock, one column only; a second row
  below the title on phones) and a Safety tile; Channels page: "All channels" tile (polling, link-loss
  pause, bottom band, SSR relay), the channel tile with **page tabs** (name, colour dot, type) attached
  to it, subsections apart with a thin line (`.seps`), PID autotune inside "PID control", the RS485 bus
  inside the tile of a Modbus channel (one shared bus), "Protection" subsection. Touch: Channels rows for
  the new checks, System rows (manual timeout, E-stop, sound, chip warning) and info (E-stop contact,
  chip temperature). Serial: `safety.*` keys, `info` safety line. SSR supply relay: only an active-high
  DC SSR on GPIO4 (`relay.activeLow` removed; the pin is low from the first line of setup); the S3
  schematic in `hardware/s3-schematic/`. Static RAM CYD 84.6 kB.
- **1.0.39** (2026-10-13): firmware update without USB (9.11): web upload (S3 + CYD), SD card (S3),
  image check, rollback; CYD OTA partition table; each board embeds only its own web page. CYD wiring pictures with
  the P4 speaker, CYD KiCad schematics (`hardware/cyd-schematic/`, generator moved to `hardware/gen/`).
  Door / lid switch and 5 V supply check dropped for good (Rost, 2026-10-13: not planned anymore).
  Web: drag and drop of firmware.bin; SSR supply relay switch `safety.relay` in Settings › Safety (the note
  block on the Channels page removed).
- **1.0.40** (2026-10-14): web page in the frosted-glass look (section 7, "Look"), new logo colours (web icons). Dashboard cards
  in the channel colour while the output is on. Touch menu: Programs, Channels, System and Event log use the web
  sidebar's line icons (`src/tui_icons.c`: A8 alpha maps, 24 px, drawn in the accent via the image recolour of
  `S_.acc`); Network and Display keep their LVGL symbols.
- **1.0.41** (2026-10-14): the web chart draws every reading of the polling (section 7, "Chart: every reading").
- **1.0.42** (2026-10-14, c083734): backup of settings + programs on the SD card (9.12); tested by Rost.
- Rost's hardware results (2026-10-14): S3 4" display 180° rotation of picture and touch OK; web firmware upload on
  the S3 OK (CYD not tried yet); touch menu icons OK on the S3; a run without an SD card runs normally (a couple of
  touch hiccups at the start = the card probe). **Open**: (1) with a card inserted, the touch stutters about once a
  second during a run — a held stepper never reaches the 5 °C steps (the press seems to be released each second);
  (2) "Check the SD card" for firmware.bin answers "SD card busy - try again" (likely: reading the whole 1.9 MB
  file through the image scanner takes longer than the 8 s the web / touch / serial wait).
- **1.0.43** (2026-10-14, 4e4510c, tested by Rost: SD check 0.5 s, gaps bridged): Rost: the stutter is only in the **bottom-left corner** of the
  panel, with or without card (probably the panel). (a) The SD firmware check runs in the SD task in the background
  (`sdlogFirmwareCheckStart` / `sdlogFirmwareCheckResult`; 16 kB DMA-RAM reads with open/read; log "[sd] firmware.bin
  checked: n kB in x s"); web `GET /api/ota/sd?start=1` then polls (`{"busy":true}`), touch toast + lv_timer poll,
  serial `fw sd` prints when done (`fwPoll`). (b) Touch drop-out statistics (release + press within 150 ms, by reason
  lift-up / no point / I2C error; logged once a minute, totals in serial `screen check`). (c) `TOUCH_USE_INT 1` on the S3
  with a run-time check (every 25th read regardless; 3 touches found with the line high → polling again). (d) KiCad 10
  ERC (Rost's reports): the only error "Output and Output connected" = the two MAX31855 SO pins on one line → SO and
  the display's SDO are `tri_state` now; the "doesn't match copy in library 'power'" warnings → the power symbols are
  in the project library (`TempCtlDuo:GND` …). `.gitignore`: KiCad `.history/`, backups, `.kicad_prl`, `*.rpt`.
- Rost (2026-10-14): ERC 0 errors / 0 warnings; INT line works; SD install and rollback (power pulled during the
  30 s probation) work; the SD check took ~30 s (whole file, display slow meanwhile). Touch log: only "no point"
  drop-outs (no lift-up, no I2C errors), all over the panel, most bottom-left → (e) gaps of up to 2 reads (~60 ms)
  are bridged (press kept at its last point; lift-up releases at once), stats now "bridged by length / longer";
  log wording avoids "error" (the line was classified as an Error). (f) `OTA_IMAGE_ID` in `.rodata_custom_desc`
  (right after esp_app_desc: address +0x100, file offset 288); the SD check stops at the first 16 kB block that
  has it, size = file size (`ImageScanner::setSize`); older images: whole file. Causes of the gaps to test:
  backlight PWM (5 kHz LEDC; brightness 100 % = steady), USB supply noise / no earth, SPI display traffic.
  Rost's check: 31 bridged gaps per minute at 50 % brightness, 19 at 100 % (none longer): the backlight PWM adds
  some, the rest is the panel / supply. Bridged fine. **1.0.46: backlight PWM 20 kHz** (`LEDC_PWM_HZ` in config.h;
  the CYD's RGB status LED shares channel 0's timer, so the same). Rost (20 kHz): 50 % 2–20 gaps / min (was 31); 100 %
  (255, steady) once 83–144, minutes later 1–5 → sporadic, supply / environment dependent; all bridged, none longer.
  SD card writes during a run: no screen hiccups (Rost).
- **1.0.44** (2026-10-14, 46bae3a, tested by Rost): **slide off to cancel** on the touch screen (Rost, 2026-10-14): a press released more than
  10 px (CYD 6 px) outside the widget it started on does nothing; `slideOffCancels()` in tui.cpp is an event
  callback on the touch indev (its events come before the widget's): RELEASED outside → `lv_indev_reset` (widget gets
  only INDEV_RESET: no RELEASED / CLICKED, so no switch toggle either); LONG_PRESSED(_REPEAT) outside →
  `lv_indev_stop_processing` (stepper repeats pause). Not for sliders, button matrix (keyboard / number pad),
  keyboard, text areas; scrolls cancel clicks already. UI host test `slide()`: slid away = no backup, 5 px = click.
- **1.0.45** (2026-10-14, b16a94b): hosted page on Cloudflare Workers static assets (7b, "Chosen").
- **1.0.46** (2026-10-14, 2811e83): backlight PWM 5 → 20 kHz; `docs/SPEC.md` = pointer to `claude/tempctl-duo-spec.md` (the repo
  copy of this document, updated with each commit); old untracked files moved to `.old/` (ignored).
- **1.0.47** (2026-10-14): web Settings › Display brightness label: 10 (the lowest) = "1 %", linear up to "99 %", "100 %"
  only at 255 (fully on, no PWM) — Rost; "100 %" at 254 had hidden that the backlight still switched.
- Next: step 7b is done, the old UI removed. Open: the older untested points (Modbus beta, runs on the CYD, ...); later GitHub Pages (7b
  option 1) and a release build of the launcher.
- Tested with the host mock (Playwright): table edit → unsaved → undo / redo, target and hold drags,
  add phase, save (POST body checked), start without saving, new Timeline in an empty slot with a pause
  text, phone width (no sideways scrolling).

Keep the conventions (full web/touch parity, colours via named styles) unless Rost decides otherwise
(the LVGL Pro ones — translations.xml, widgets bound by name, exported code — ended with the old UI).

### 9.11 Firmware update without USB (decided 2026-10-13, Rost; version 1.0.39)
Decisions: web upload on S3 **and** CYD (CYD gets an OTA partition table), SD card update on the S3 with a
button + confirmation, self-test + rollback.
- Module `ota.*`: one stream API for both sources (`otaBegin(src,total)` / `otaWrite` / `otaEnd` /
  `otaAbort`, Arduino `Update` into the next OTA slot), status for web / touch (`otaStatus`), the
  conditions `otaCanStart` (second slot present, no run active or paused, no channel outputs on — manual,
  autotune —, not already busy / on probation).
- Image check (`ota_image.*`, pure, host-tested): ESP image magic 0xE9, chip id at byte 12 (0 ESP32,
  9 ESP32-S3, `BOARD_CHIP`), and the id string `TCDUO-IMAGE|<BOARD_ID>|<version>|end` (`OTA_IMAGE_ID` in
  ota.cpp, logged at boot so the linker keeps it; `BOARD_ID` = env name in board_*.h). Found while the
  image streams (across chunk borders); an image for another env is refused (header / chip at once,
  the id at the end before `Update.end`). The web page does the same checks on the file before uploading.
- Web: `POST /api/ota?size=` multipart (WebServer upload callback in the loop task; the whole upload runs
  inside one `handleClient()`: `esp_task_wdt_reset()` per chunk, touch overlay refreshed via
  `uiOtaRefresh()` every 300 ms); answer `{ok, version}`, restart 2 s later (`otaLoop` →
  `appRequestRestart`). `GET /api/ota` (info), live `ota` while running / on probation. Upload only when
  the page is served by the device (not over Web Serial). The file comes from "Choose firmware file…" or
  by drag and drop onto the dashed box around it (highlighted while dragging; several files → the first
  `.bin`; ignored while an update runs; a file dropped elsewhere on the page is not opened by the browser).
- SD (S3): `/firmware.bin` in the card root. `GET /api/ota/sd` = SD task request RQ_FWCHECK (reads the
  whole file through the scanner, ~3 s); `POST /api/ota/sd` starts `installFirmware()` in the SD task (card
  mounted under `lcdBusLock` for the whole install, ~20–30 s; the display keeps drawing, the screen check
  waits), then renames it `firmware.done`. Touch System › Firmware update › SD card (check + dialog),
  serial `fw`, `fw sd`, `fw sd install yes`.
- Rollback: the core's sdkconfig has `CONFIG_APP_ROLLBACK_ENABLE`; `extern "C" verifyRollbackLater()`
  returns true, so a new image stays PENDING_VERIFY until `otaLoop` marks it valid (30 s after the start
  and > 10 control cycles). `/duo/ota.json` {from, to, src} written at the end of an update: the new
  firmware logs "Firmware updated from A to B - checking it for 30 s" (note kept until confirmed), the old
  one after a rollback "Firmware update to B failed: it did not start correctly, A runs again".
- CYD flash: `partitions_cyd_ota.csv` (nvs, otadata, app0/app1 0x1E0000 each, spiffs 0x30000 = 192 kB,
  no coredump). The first USB upload with it erases LittleFS (settings, programs: back up first). To fit,
  `tools/web_embed.py` compiles only each board's own page (`#ifdef BOARD_HAS_PSRAM`: page with fonts +
  font files on the S3; `#ifndef`: the page without fonts, served at `/`, on the CYD); CYD image ≈ 1.77 MB
  (90 % of a slot), S3 1.92 MB of 6.25 MB.
- Touch: full-screen overlay (lv_layer_sys) while running / written: version, source, %, bar.

### 9.12 Backup on the SD card (decided 2026-10-14, Rost; version 1.0.42)
Decisions (Rost picked the proposals): web, touch and serial; list + restore (settings, programs or both) +
delete on the web; only by hand (no automatic backups); S3 only (the CYD's SD pins carry the thermocouples).
- One file per backup: `/backup/<local yyyy-mm-dd_hhmmss>.json` (`_2` … if taken; without a clock
  `start<boot>_<n>.json`), `{"fmt":1,"kind":"tempctl-duo-backup","fw","board","host","time","settings":{as GET
  /api/config without limits and passSet},"programs":[{"i","program"}]}` (the used slots). Same pieces as the
  web's backup files, so the web file restore takes a backup file too (settings: its `settings`). Wi-Fi
  passwords are not in it; restoring keeps the stored ones.
- `src/backup.*` (S3 only, `HAS_SD` in config.h; stubs elsewhere): `backupCreate` (JSON built in the calling
  task, PSRAM buffer handed to the SD task: request `RQ_BACKUP`, which frees it in any case), `backupInfo`
  (time, fw, settings, program count, slot list + mask), `backupRestore(name, BACKUP_SETTINGS|PROGRAMS)`:
  refused during a run; settings first (`settingsFromJson` + `settingsValidate` on a copy, as POST
  /api/config; changed network settings → "used after a restart"), then each program checked
  (`progFromJson` with the channel names, `progCheckLimits` against the restored limits) and saved into its
  slot; slots not in the backup stay. Result text + event "Restored from the SD card backup/…". One backup
  operation at a time (`Busy`). `backupLabel()` (header, host-tested) makes "2026-10-14 15:30:12".
- `sdlog`: list / load / delete take a folder (`SdDir`: `SD_RUNS` *.csv, `SD_BACKUPS` *.json).
- Web: `GET/POST /api/backup`, `GET /api/backup/info`, `POST /api/backup/restore`, `POST /api/backup/delete`,
  `GET /api/backup/file`; Tools › Backup › "SD card" (shown when live `sd` exists): Back up to SD card,
  Refresh, list newest first (Restore… / Download / Delete); Restore… reads the info and asks with
  "Restore both" / "Settings only" / "Programs only" / Cancel, then reloads settings / programs (drafts of
  the restored slots dropped).
- Touch System: "Backup to SD card" (Back up → toast with the name) and "Restore from SD card" (Choose… →
  list of up to 40 backups → "Restore what?" → confirmation with date, firmware and slots → result dialog).
- Serial: `backup sd`, `backup sd list`, `backup sd restore n=<n> [settings|programs] [yes]` (without yes:
  what it holds), `backup sd delete n=<n> yes`; `help backup`. CYD: "no SD slot".
- Tests: host (backupLabel), console host S3 build (`BOARD=s3 test/console_host/build.sh`), UI host shots
  14e–14i, mock + Playwright (`mock/backup.js`).

## 10. Open items carried over from TempCtl

- (Closed 2026-10-11: not seen again since the assert handler / loop watchdog fix.) Sporadic freeze once seen on the Wi-Fi "Scanning…" screen (not reproduced). The assert handler and
  loop watchdog now turn a freeze into a restart with a backtrace; ask for the log if it happens.
  Suspect: Arduino WiFi scan timeout (`_scanTimeout` = 20 × 300 ms) → our retry calls
  `scanNetworks`/`scanDelete` while the old hardware scan finishes (race with `_scanDone` in the event
  task).
- (TempCtl optional item "palette-driven colours for LVGL theme parts" is obsolete in Duo: palettes are
  dropped; the accent goes into `lv_theme_default_init()` as the primary colour.)
- Program names longer than 23 characters: decided for Duo — up to 31 characters (9.3).
