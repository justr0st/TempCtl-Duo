# CYD board

The **ESP32-2432S028R** ("Cheap Yellow Display"): an ESP32 with a 2.8" 320×240 ILI9341 display and a
resistive touch screen on one board. It has very few free pins, so there are three wirings, each with its own
firmware build (PlatformIO env). The pins are in `src/board_cyd.h`; a schematic of each wiring (PDF, A3) is linked in its section below, the
KiCad projects are in `hardware/cyd-schematic/`.

| Wiring | Env | Channels | Schematic |
|---|---|---|---|
| **A** | `cyd-1xlocal-1x485` | RS485 (one or two Modbus controllers) + one local channel (bottom or top) | [PDF](../../hardware/cyd-schematic/TempCtl-Duo-CYD-A.pdf) |
| **B** | `cyd-2xlocal` | two local channels, no RS485 | [PDF](../../hardware/cyd-schematic/TempCtl-Duo-CYD-B.pdf) |
| **C** | `cyd-2xlocal-2x485` | RS485 + two local channels, each channel local or Modbus; the on-board RGB LED is removed | [PDF](../../hardware/cyd-schematic/TempCtl-Duo-CYD-C.pdf) |

> **Note:** the ST7789 variant of the board with two USB ports is not supported yet.

## Connectors

| Connector | Pins |
|---|---|
| **P3** (JST 1.25 mm, 4 pin) | 1 IO21 (backlight, don't use) · 2 IO22 · 3 IO35 (input only) · 4 GND |
| **CN1** (JST 1.25 mm, 4 pin) | 1 3V3 · 2 IO27 (10 kΩ pull-up) · 3 IO22 · 4 GND |
| **P1** (USB serial) | 1 VIN 5 V · 2 TX · 3 RX · 4 GND |
| **P4** speaker | IO26 drives the on-board SC8002B amplifier (bridged output: SPK− is not ground) |
| **micro-SD slot** | the converters and SSRs use its pins (IO5 CS, IO23 MOSI, IO18 CLK, IO19 MISO) through a micro-SD breakout ("sniffer") adapter plugged into the slot — no SD card |

## Wiring A: RS485 + one local channel

<img src="img/wiring/cyd-a.svg" alt="CYD wiring A: RS485 module, one MAX31855 and one SSR driver" width="900">

Schematic: [TempCtl-Duo-CYD-A.pdf](../../hardware/cyd-schematic/TempCtl-Duo-CYD-A.pdf)

## Wiring B: two local channels

<img src="img/wiring/cyd-b.svg" alt="CYD wiring B: two MAX31855 and two SSR drivers" width="900">

Schematic: [TempCtl-Duo-CYD-B.pdf](../../hardware/cyd-schematic/TempCtl-Duo-CYD-B.pdf)

## Wiring C: RS485 + two local channels

<img src="img/wiring/cyd-c.svg" alt="CYD wiring C: RS485, two MAX31855, two SSR drivers and an external status LED" width="900">

Schematic: [TempCtl-Duo-CYD-C.pdf](../../hardware/cyd-schematic/TempCtl-Duo-CYD-C.pdf)

The RGB LED's three pads become the top channel's chip select (R, IO4, with a 10 kΩ pull-up to 3V3), the top
SSR (G, IO16) and the data line of an external WS2812 status LED (B, IO17).

## Notes for all three

- **Power:** USB on the CYD. 3V3 for the modules from CN1-1 or the SD slot's VDD pin, 5 V for the SSR inputs
  from P1-1 (VIN). One ground for the board and every module.
- **RS485:** only a 3.3 V module (MAX3485 / SP3485); a 5 V MAX485 would drive 5 V into IO35.
- **No E-stop input, no SSR supply relay and no chip temperature** on the CYD: there are no free pins. The
  alarm sound uses a small 8 Ω speaker (0.5–2 W) or a passive piezo on P4.
- **Status LED:** the on-board RGB LED (wiring A, B) or the external WS2812 (C), with the same colours as the
  S3: green idle, blue running, amber blinking paused, red blinking fault, purple access point.
- **Touch calibration:** the defaults (200…3700 / 240…3800) suit most CYDs. If touches land off, type
  `touch cal` in the [serial console](Serial-console.md) and tap the four crosses.
- **Firmware size:** the CYD's two firmware slots are 1.875 MB each; the firmware uses about 93 %. The web
  page is served without the web fonts there (system fonts), and the online update goes through the
  browser ([Updates](Updates-and-backups.md)).

## The touch screen on the CYD

The CYD shows the same screens as the S3 in a compact set ([Touch screen](Touch-screen.md#cyd-320240)):

<picture><source media="(prefers-color-scheme: dark)" srcset="img/touch/cyd-03-running-dark.png"><img src="img/touch/cyd-03-running-light.png" alt="CYD touch screen during a run" width="320"></picture>
