# For developers

PlatformIO, Arduino-ESP32 2.0.17 (`espressif32@6.9.0`), LVGL 9.6. The design notes with the reasons behind the
decisions are in `claude/tempctl-duo-spec.md`; how the touch UI, the screen check and the storage work is on
[How it works inside](Internals.md), the HTTP API on [Web API](Web-API.md).

## Build

Environments: `s3`, `cyd-1xlocal-1x485`, `cyd-2xlocal`, `cyd-2xlocal-2x485` (`pio run -e s3 -t upload`; check
`upload_port` / `monitor_port` in `platformio.ini`). `tools/web_embed.py` gzips the web page into
`src/web_assets.h` before each build (PlatformIO runs it; by hand: `python tools/web_embed.py`).

## Tests without a board

| What | How |
|---|---|
| Programs, run engine, settings, PID, autotune, runaway, Modbus framing, image check, release manifest | `g++ -std=c++17 -Wall -Wextra -Isrc -I<ArduinoJson>/src test/host_test.cpp src/duo_program.cpp src/run_engine.cpp src/settings.cpp src/tc_sensor.cpp src/pid.cpp src/runaway.cpp src/autotune.cpp src/ota_image.cpp src/release.cpp && ./a.out` |
| Touch screens | `test/ui_host/build.sh` builds the screens for the PC and renders screenshots (`./uihost`; `BOARD=cyd` for the 320×240 set; `./uihost monkey` random taps under AddressSanitizer; more modes in [How it works inside](Internals.md#touch-ui)) |
| Serial console | `test/console_host/build.sh` + `./conhost`: types a script of commands and prints the output |
| Web page | Playwright against the browser demo (`tools/build_site.py <dir> --web`, served on :8790): `test/web_jumps.js` (layout jumps), `test/web_reorder.js` (reorder by drag), `test/web_chart_hold.js` (chart handles on touch) |
| Browser demo | `demo/build.sh` (wasi-sdk): the firmware's control code as WebAssembly, `demo/README.md` |

## Releases (GitHub Actions → Cloudflare)

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

## Project layout

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
src/tui*.cpp, tui.h, ui.h touch UI of both boards (480×320 S3, 320×240 CYD)
src/fonts/                touch UI fonts (tools/lv_font_gen.py from tools/fonts/*.ttf)
src/web.*, host_check.h    HTTP server, JSON API, request checks
src/safety.*              E-stop input, alarm sound, user activity, chip temperature
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
tools/release.py          release files + dl/manifest.json for the hosted page
tools/release_local.sh    the same release from a PC, without commit / push
docs/wiki/                the wiki (Markdown, _Sidebar.md = page list; img/: wiring SVGs, touch PNGs, web WebPs)
tools/wiki_site.py        the wiki as HTML pages for the hosted page (/wiki/, called by build_site.py --web)
tools/wiki/               the wiki pictures: make_shots.sh (all), wiring.py, touch_png.py, shots_web.js +
                          mock/server.py (demo device), demo_run.cpp (runs from the firmware's run engine)
demo/                     browser demo: the firmware's control code + simulated hardware (sim.cpp) as WebAssembly
                          (build.sh), demo.js connects the page to it; README.md
.github/workflows/        release.yml: builds firmware + desktop app, deploys page and files to Cloudflare
tools/launcher/           local app: Go launcher (build.sh → dist/tempctl-duo-<version>.exe)
test/ui_host/             touch UI preview on a PC (screenshots, random-tap test)
test/console_host/        serial console on a PC (scripted commands -> output)
src/console.*             serial console, LogSerial (log lines that don't break typed input)
test/host_test.cpp        g++ -std=c++17 -Wall -Wextra -Isrc -I<ArduinoJson>/src test/host_test.cpp src/duo_program.cpp src/run_engine.cpp src/settings.cpp src/tc_sensor.cpp src/pid.cpp src/runaway.cpp src/autotune.cpp src/ota_image.cpp src/release.cpp && ./a.out
```

## License

The project is under the **MIT License** (`LICENSE` in the repository); the third-party parts keep their own
licences, listed in `THIRD-PARTY-NOTICES.md`: Espressif's SIMD assembly in `src/simd/` and the two files that
follow its blend header (Apache-2.0, marked with SPDX lines), the Barlow fonts (SIL OFL 1.1), LVGL,
ArduinoJson and XPT2046_Touchscreen (MIT), the Arduino core (LGPL-2.1) and ESP-IDF (Apache-2.0 and others).
New third-party code: keep its notices, add it to `THIRD-PARTY-NOTICES.md`. The hosted page and the desktop
app serve both files as `/LICENSE.txt` and `/THIRD-PARTY-NOTICES.txt` (`tools/build_site.py`), linked under
Downloads.

## This wiki

- Pages: `docs/wiki/*.md` (GitHub shows them as they are), the page list: `docs/wiki/_Sidebar.md`.
- On the hosted page: `tools/wiki_site.py` turns them into `/wiki/*.html` (no packages: a small Markdown
  converter; links `Page.md` become `Page.html`).
- Pictures: `docs/wiki/img/` — regenerate them after a UI or wiring change with **`tools/wiki/make_shots.sh`**:

| Part | Made by |
|---|---|
| `img/wiring/*.svg` | `tools/wiki/wiring.py` (pins from `src/board_*.h`; keep them in step) |
| `img/touch/*.png` | the UI host (`test/ui_host`) in dark and light (`UIHOST_THEME=light`), framed by `tools/wiki/touch_png.py` |
| `img/web/*.webp` | `tools/wiki/shots_web.js` (Playwright) against a demo device, `tools/wiki/mock/server.py`, whose runs come from the firmware's own run engine (`tools/wiki/demo_run.cpp`) |

Theme-aware pictures use `<picture>` with a dark source; SVG diagrams follow the viewer's colour scheme by
themselves.
