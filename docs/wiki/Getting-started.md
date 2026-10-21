# Getting started

From a bag of parts to the first program run: what to buy, how to flash the firmware, how to connect it to
Wi-Fi and set up the two channels.

## 1. Parts

For the **ESP32-S3 build** (the main target):

| Part | Notes |
|---|---|
| ESP32-S3-DevKitC-1 **v1.0 N16R8** | 16 MB flash, 8 MB PSRAM. A v1.1 board has its RGB LED on GPIO38 instead of 48 (swap 38 ↔ 48 in `src/board_s3.h`) |
| 4.0" SPI display **MSP4031** | ST7796S 480×320 + FT6336U capacitive touch + micro-SD slot |
| 2 × **MAX31855** breakout (or MAX6675) | thermocouple converters, one per heater; type K thermocouples |
| 2 × SSR (DC input 3–32 V, zero-cross) + 2 × NPN driver (BC337 / 2N2222, 1 kΩ, 10 kΩ) | one per heater |
| DC solid-state relay, active high | the **SSR supply relay**: switches the +5 V of both SSR inputs |
| NC push button, passive piezo + 100 Ω | E-stop and alarm sound (optional, recommended) |
| 3.3 V RS485 module (MAX3485 / SP3485) | only for Modbus controllers (beta) |
| micro-SD card (FAT32) | run logs, backups, firmware files (optional) |
| independent over-temperature limiter + contactor | **required** in the heater supply, see [Safety](Safety.md) |

The **CYD build** uses the ESP32-2432S028R board instead of the DevKit and display; it has room for fewer
parts ([CYD board](Hardware-CYD.md)).

## 2. Wire it

Follow the diagrams of your board: [ESP32-S3](Hardware-S3.md) or [CYD](Hardware-CYD.md). Start with the
display only, flash, check that the screen and touch work, then add the thermocouples, then the SSRs.

## 3. Flash the firmware

The first install goes over USB; later updates go over Wi-Fi ([Updates and backups](Updates-and-backups.md)).

**Without a build environment** — the full image from the release page:

1. Open <https://tempctl-duo.just-r0st.workers.dev>, *Downloads* › *Firmware for the boards* and download the
   **full image** of your board (`tempctl-duo-<version>-<env>-full.bin`: bootloader, partition table and
   firmware in one file).
2. Write it at address **0x0** with esptool (`pip install esptool`), board on the USB port marked **"UART"**
   (S3) or the CYD's USB port:

   ```
   esptool.py --chip auto write_flash 0x0 tempctl-duo-1.0.56-s3-full.bin
   ```

   A web flasher that writes a .bin at 0x0 works too.

**With PlatformIO** (VS Code + PlatformIO extension):

1. Open the project folder, pick the environment: `s3`, `cyd-1xlocal-1x485`, `cyd-2xlocal` or
   `cyd-2xlocal-2x485`.
2. Check `upload_port` / `monitor_port` in `platformio.ini`, then *Upload* (`pio run -e s3 -t upload`).

> **Note:** the CYD's first flash with this firmware erases its file system (new partition table with two
> firmware slots). Back up settings and programs first if the board ran an older TempCtl.

## 4. Connect to Wi-Fi

On the first start there is no network configured: the device opens its own access point
**`TempCtl-Duo-XXXX`**, password **`tempctl123`**. Then either

- on the touch screen: *Menu › Network* — tap *Network name* (it scans), pick the network, enter the password;
- or join the access point with a phone, open `http://192.168.4.1` and use *Settings › Wi-Fi*;
- or over the USB serial console: `wifi scan`, then `wifi connect n=2` ([Serial console](Serial-console.md)).

Afterwards open **`http://tempctl-duo.local`** or the IP address shown under *Menu › System* (info).
Without a network the device keeps its access point; everything works there too.

<picture><source media="(prefers-color-scheme: dark)" srcset="img/touch/13-network-dark.png"><img src="img/touch/13-network-light.png" alt="Touch screen: Network settings" width="480"></picture>

## 5. Set up the channels

Each channel (bottom, top) needs a type: **local**, **Modbus** or **off**. The S3 starts with bottom = Modbus
and top = off; `cyd-2xlocal` with both local. For the usual local build:

1. Web page *Channels* (or touch *Menu › Channels*): bottom › *Type* = *Local (thermocouple + SSR)*, converter
   MAX31855 or MAX6675; the same for top. *Save*.
2. Check the readings: room temperature on both, and the right one rises when you warm a thermocouple tip.
   A reading that falls: swap T+ and T−.
3. Set the limits: *Highest setpoint* and the cut-off *max. temperature* of each heater.
4. Switch a heater on by hand with a low manual setpoint (Dashboard card or touch tile: the power button) and
   check that the right SSR clicks and the right temperature rises. Off again.
5. Run the **PID autotune** of each channel at a typical working temperature ([Channels](Channels.md)).

<picture><source media="(prefers-color-scheme: dark)" srcset="img/web/channels-local-dark.webp"><img src="img/web/channels-local-light.webp" alt="Web page: Channels, local channel settings" width="900"></picture>

## 6. The first run

On the first start three example programs are created: **Lead-free BGA**, **Leaded BGA** and **Lead-free BGA
(Timeline)**. Look at one on the Dashboard (the chart shows the plan), adjust the temperatures to your board
and heaters, *Save*, then *Start*. [Programs and runs](Programs.md) explains the phases, the pauses with
"Action required" and what you can change while it runs.

## 7. Before real work

- Fit the independent over-temperature limiter and contactor ([Safety](Safety.md)).
- S3: wire the E-stop and set *Settings › Safety › E-stop input* on; keep the SSR supply relay on.
- Make a backup of the settings and programs (*Tools › Backup*, or the SD card on the S3).
