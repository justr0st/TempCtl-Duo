# TempCtl Duo — specification and working notes

Status: 2026-09-28 (third revision — all design decisions made). The first revision was written at the
end of the conversation that built TempCtl 1.2. Revisions 2–3 add everything agreed with Rost in a
claude.ai chat: the Duo goals and program model (9), the hardware — ESP32-S3 + 4.0" ST7796 as main
target and the CYD as second device (2, 2a, 2b), colours (6), the Web-GUI based on the 9.3 simulator
(7, `claude/web-sim/`), serial console (7a) and the Web Serial page / local app (7b). Every open point
was decided one by one; what is left is only for later (GitHub Pages, SD logging, "Convert to
Timeline") and hardware checks during bring-up (9.10).
2026-09-30: **steps 1–3 of 9.10 implemented** (S3 bring-up, per-board headers, three CYD variants,
identity 1.0.0; channel interface, two Modbus channels on one bus, `/duo/settings.json`; local channels
with thermocouple, SSR, PID and safety). All four envs compile and link in the cloud container; nothing
committed yet. State: 9.10.1–9.10.3. **The 4" display
and the PMA controller are still being delivered** (Rost): their checks are deferred until each part
arrives; meanwhile test what the bare S3 board and the CYD allow. Next: step 4 (autotune; the PID
itself came with step 3).

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
| Spare | 4, 5, 6 | J1 | e.g. buzzer, door / E-stop input |

**Status LED (decided)**: green = idle · blue = running · amber blinking = waiting for bottom / program
pause / action required · red blinking = fault · purple = AP mode. Priority when several apply: fault >
amber > purple > blue > green. Low brightness (≈ 20 %). **CYD: the on-board RGB LED (external WS2812 on IO17 in `cyd-2xlocal-2x485`) with the same
colours** (Rost, 2026-09-29; replaces "blue LED only"): PWM on LEDC channels 1–3, active low.

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
- **"Convert to Timeline"** (web; decided: **later, not in the first version**): for a Sequence without
  "wait for bottom" phases, one way only. When it comes, it shows a **warning that the conversion can't
  be reversed** (a Timeline can't be turned back into a Sequence) before converting.
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
- **SD slot (decided): not used now** — SD_CS is only driven high to keep the card deselected. Later
  maybe CSV run logging on the S3 (the CYD's SD pins carry the thermocouples).

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
10. Later / planned: the page on GitHub Pages (7b, option 1); maybe CSV run logging to SD (S3);
    "Convert to Timeline" with an irreversibility warning (9.3.3).

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

Keep the TempCtl conventions (full web/touch parity, colours via named styles, texts in
`translations.xml`, widgets bound by name, exported UI code committed) unless Rost decides otherwise.

## 10. Open items carried over from TempCtl

- Sporadic freeze once seen on the Wi-Fi "Scanning…" screen (not reproduced). The assert handler and
  loop watchdog now turn a freeze into a restart with a backtrace; ask for the log if it happens.
  Suspect: Arduino WiFi scan timeout (`_scanTimeout` = 20 × 300 ms) → our retry calls
  `scanNetworks`/`scanDelete` while the old hardware scan finishes (race with `_scanDone` in the event
  task).
- (TempCtl optional item "palette-driven colours for LVGL theme parts" is obsolete in Duo: palettes are
  dropped; the accent goes into `lv_theme_default_init()` as the primary colour.)
- Program names longer than 23 characters: decided for Duo — up to 31 characters (9.3).
