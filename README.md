# TempCtl Duo: two-channel programmable temperature controller (ESP32-S3 / ESP32 CYD)

Firmware for a two-channel temperature controller, made for the **bottom and top heater of a BGA rework
station**: reflow and desolder profiles that drive both heaters together, from a touch screen, a web page or a
USB cable. Version **1.0.65**.

<picture><source media="(prefers-color-scheme: dark)" srcset="docs/wiki/img/touch/03-running-dark.png"><img src="docs/wiki/img/touch/03-running-light.png" alt="Touch screen of the ESP32-S3 build during a run" width="480"></picture>

**Main features**

- **Two channels**, bottom and top: each one **local** (thermocouple + SSR, PID with autotune on the ESP32),
  **Modbus RTU** (an RS485 controller such as a PMA KS 40-1; beta) or off. Optional third thermocouple to watch
  (S3).
- **20 programs** that drive both channels: *Sequence* (phases one after another, "wait for the bottom", pauses
  with "Action required") or *Timeline* (two independent step lists). Change targets and times while a program
  runs; resume after a power loss.
- **Touch screen** on the device: run, edit programs, all settings.
- **Web page** with the same functions: live chart, program editor (tables, drag points on the chart, drag rows
  to reorder), channel setup, logs, CSV export and replay. Works on a phone, without internet.
- **Over USB** in a desktop app or the hosted page (Chrome / Edge), and a **serial console**.
- **Protections**: runaway, heating while off, frozen reading, converter too hot, E-stop input, SSR supply relay,
  alarm sound.
- **Updates** from the release page (every board through the web page, the S3 also by itself), SD card logs and
  backups (S3).

**[Wiki](docs/wiki/Home.md)** (instructions, wiring, every screen) ·
**[Browser demo](https://tempctl-duo.just-r0st.workers.dev/demo/)** (the firmware with a simulated rework
station, in your browser) · **[Release page](https://tempctl-duo.just-r0st.workers.dev)** (downloads, the page
over USB)

> **Status:** local thermocouple + SSR channels are the tested path, on both boards. Modbus RTU channels are built
> in but not tested with a real controller yet (beta).

## Screenshots

| | |
|---|---|
| <picture><source media="(prefers-color-scheme: dark)" srcset="docs/wiki/img/web/dashboard-run-dark.webp"><img src="docs/wiki/img/web/dashboard-run-light.webp" alt="Web page: Dashboard during a run" width="420"></picture> | <picture><source media="(prefers-color-scheme: dark)" srcset="docs/wiki/img/web/editor-seq-dark.webp"><img src="docs/wiki/img/web/editor-seq-light.webp" alt="Web page: program editor" width="420"></picture> |
| Web page: Dashboard during a run | Program editor: drag the grips to reorder, the points on the chart to change targets and times |
| <picture><source media="(prefers-color-scheme: dark)" srcset="docs/wiki/img/touch/20-edit-seq-dark.png"><img src="docs/wiki/img/touch/20-edit-seq-light.png" alt="Touch screen: program editor" width="420"></picture> | <picture><source media="(prefers-color-scheme: dark)" srcset="docs/wiki/img/touch/cyd-03-running-dark.png"><img src="docs/wiki/img/touch/cyd-03-running-light.png" alt="CYD touch screen during a run" width="300"></picture> |
| Touch screen (S3): program editor | The compact CYD build (320×240) |
| <picture><source media="(prefers-color-scheme: dark)" srcset="docs/wiki/img/web/channels-local-dark.webp"><img src="docs/wiki/img/web/channels-local-light.webp" alt="Web page: channel settings" width="420"></picture> | <picture><source media="(prefers-color-scheme: dark)" srcset="docs/wiki/img/touch/27-chan-pid-dark.png"><img src="docs/wiki/img/touch/27-chan-pid-light.png" alt="Touch screen: PID and autotune" width="420"></picture> |
| Channel settings: thermocouple, PID, autotune, protections | Touch screen: PID and autotune |

More on the wiki pages [Touch screen](docs/wiki/Touch-screen.md) and [Web page](docs/wiki/Web-page.md).

## Hardware

Two boards run the same firmware:

| Board | Screen | |
|---|---|---|
| **ESP32-S3-DevKitC-1 N16R8** + 4.0" SPI display MSP4031 | 480×320, capacitive touch | the main target: two local channels, RS485, monitor thermocouple, E-stop, SSR supply relay, buzzer, SD card |
| **ESP32-2432S028R "Cheap Yellow Display"** | 320×240, resistive touch | a compact build with few free pins: three wirings (RS485 + one local channel, two local channels, or both) |

<img src="docs/wiki/img/wiring/overview.svg" alt="Block diagram: board, phone / PC, E-stop and buzzer, thermocouple converters, SSR drivers, SSR supply relay, RS485 transceiver, thermocouples, SSRs and heaters, controllers" width="820">

A local channel is a MAX31855 (or MAX6675) thermocouple converter on SPI and an SSR switched by an NPN
transistor; all modules are connected with cables:

<img src="docs/wiki/img/wiring/s3-thermocouples.svg" alt="S3: thermocouple converters on the shared SPI lines" width="820">

<img src="docs/wiki/img/wiring/s3-ssr.svg" alt="S3: SSR drivers, SSRs and the SSR supply relay" width="820">

All diagrams, pin tables and notes: [ESP32-S3 board](docs/wiki/Hardware-S3.md) · [CYD board](docs/wiki/Hardware-CYD.md).
Schematics (PDF, A3; KiCad projects next to them): [ESP32-S3](hardware/s3-schematic/TempCtl-Duo-S3.pdf) ·
CYD wiring [A](hardware/cyd-schematic/TempCtl-Duo-CYD-A.pdf), [B](hardware/cyd-schematic/TempCtl-Duo-CYD-B.pdf),
[C](hardware/cyd-schematic/TempCtl-Duo-CYD-C.pdf).

## Getting started

1. **Flash**: VS Code + PlatformIO, environment `s3` or one of `cyd-1xlocal-1x485`, `cyd-2xlocal`,
   `cyd-2xlocal-2x485` (`pio run -e s3 -t upload`). Or, without PlatformIO, write the full image of your board from
   the release page at address 0x0 (`esptool.py write_flash 0x0 tempctl-duo-<version>-s3-full.bin`).
2. **Connect**: the device opens the access point `TempCtl-Duo-XXXX` (password `tempctl123`); set your Wi-Fi on
   the touch screen (*Menu › Network*) or at `http://192.168.4.1`. Then: `http://tempctl-duo.local`.
3. **Set up the channels**: type and converter, check the readings, run the PID autotune once.
4. **Run** one of the three example programs, or write your own.

Step by step: [Getting started](docs/wiki/Getting-started.md).

> **Safety:** a software controller must never be the only protection against overheating. Fit an independent
> over-temperature limiter and a contactor in the heater supply ([Safety and protections](docs/wiki/Safety.md)).

## Documentation

| | |
|---|---|
| Start | [Home](docs/wiki/Home.md) · [Getting started](docs/wiki/Getting-started.md) · [Browser demo](docs/wiki/Demo.md) |
| Hardware | [ESP32-S3 board](docs/wiki/Hardware-S3.md) · [CYD board](docs/wiki/Hardware-CYD.md) |
| Using it | [Touch screen](docs/wiki/Touch-screen.md) · [Web page](docs/wiki/Web-page.md) · [Programs and runs](docs/wiki/Programs.md) · [Channels, PID and autotune](docs/wiki/Channels.md) · [Safety and protections](docs/wiki/Safety.md) |
| Maintenance | [Updates and backups](docs/wiki/Updates-and-backups.md) · [Desktop app and USB](docs/wiki/Desktop-app.md) · [Serial console](docs/wiki/Serial-console.md) · [Troubleshooting](docs/wiki/Troubleshooting.md) · [Modbus controllers (beta)](docs/wiki/Modbus.md) |
| Developers | [For developers](docs/wiki/Development.md) (build, tests, releases, project layout) · [Web API](docs/wiki/Web-API.md) · [How it works inside](docs/wiki/Internals.md) |

The wiki is also on the release page: <https://tempctl-duo.just-r0st.workers.dev/wiki/> (linked from the device's
web page, *Help (wiki)*). Design notes and decisions: `claude/tempctl-duo-spec.md`.

## Built with

PlatformIO, Arduino-ESP32 2.0.17, LVGL 9.6, ArduinoJson. Fonts: Barlow / Barlow Semi Condensed (SIL Open Font
License, `web/fonts/OFL.txt`). S3 drawing routines from Espressif's esp-bsp (Apache-2.0, `src/simd/`).

## License

[MIT](LICENSE) — the firmware, web page, tools, documentation and schematics. The third-party parts keep their
own licences: [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
