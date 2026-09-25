# TempCtl: 6-step temperature programmer for ESP32-CYD + Modbus RTU

Firmware for the ESP32-2432S028R ("Cheap Yellow Display") that drives a **PMA KS 40-1 / 41-1 / 42-1**
(or another Modbus RTU temperature controller) over RS485 and runs a **6-step ramp/soak program**.

- LVGL 8.3 touch GUI: Run, Program (6 × target / ramp / hold), Chart (planned vs. setpoint vs. actual), Setup
- Web interface with the same functions plus controller parameters (PID, limits, self-tuning), full register
  map, register read/write tools, a setpoint-address finder, Wi-Fi setup and CSV log download
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
**4 segments** (`SP.01…SP.04`, `Pt.01…Pt.04`), so a 6-step profile has to be driven from outside by writing `SP`.

**EEPROM:** the interface description says the `SP` value in RAM is updated immediately, while storing it to
EEPROM is timed (about one write per half hour). Frequent setpoint writes over Modbus are therefore safe on
the KS 4x-1. This is controller-specific; cheaper controllers may write every change to EEPROM.

**Setpoint limits:** the controller clamps setpoints to `SP.LO…SP.Hi` (factory 0…900). Before starting, the
firmware reads both and refuses to start if an enabled step's target lies outside them.

### Setpoint ramp modes

- **Controller ramps (r.SP)**, default: per segment the ESP32 writes the start value, the gradient
  (|ΔT| / remaining minutes) and the target. About 4 writes per phase; the controller ramps smoothly.
  Ramps slower than 0.1 °/min (the 1dP resolution) fall back to host stepping for that segment.
- **ESP32 writes setpoint steps**: writes a new setpoint when it has moved by *minimum step* (default 0.5)
  and at least *minimum seconds* (default 5) have passed. Works with any controller.

The Run page shows the number of setpoint writes in the current run.

### Program behaviour

- Each step: target, ramp minutes (0 = jump), hold minutes. The first ramp starts at the PV measured at Start.
- *Guaranteed soak*: hold time only counts while PV is within ± band of the target.
- Pause freezes program time (and the controller ramp). Skip jumps to the next phase.
- End/stop action: keep setpoint, set an end setpoint, or switch controller outputs off (`C.Off`).
  The original `r.SP` is restored afterwards.
- If the controller doesn't answer for 30 s (configurable) the program pauses.
- If the ESP32 resets, the program stops; the controller keeps controlling at its last setpoint.

## 2. Hardware

| Part | Notes |
|---|---|
| ESP32-2432S028R (CYD) | 2.8" 320×240 ILI9341 + resistive touch. Some 2-USB versions use ST7789 (see `platformio.ini`) |
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
set `PIN_RS485_DE` to `-1` in `src/config.h`.

- **Polarity:** PMA's "Data A/B" naming is reversed relative to EIA-485, but in practice PMA DATA A goes to the
  module's A(+). If there is no answer, swapping A and B is harmless.
- **Termination:** PMA recommends *no* 120 Ω terminators for fewer than 8 devices at ≤ 9600 baud (up to 1000 m).
  Many cheap modules have a 120 Ω resistor fitted; remove it if communication is flaky on a short line, and make
  sure the module has fail-safe bias resistors.
- The on-board RGB LED shows the link: green = OK, blue = program running, red = no answer.

## 3. Can the LCTC M226-TE (from the screenshot) work the same way?

**Electrically and at protocol level: yes.** It is advertised with RS485 Modbus RTU, so the same transceiver and
the same Modbus master in this firmware can talk to it (it almost certainly uses FC 03 and 06).

**Register-wise: no, and I could not find its register map.** LCTC's TE-M226 product page and catalog don't publish
a Modbus table. PMA addresses (13294 etc.) will not work on it. To use it:

1. Get the communication manual from the seller/LCTC (address list for PV and SV, scaling, serial format).
2. Select *Generic Modbus RTU* on *Connection*, enter baud/parity/address as set on the controller.
3. Enter the PV and SV addresses (*Tools → Find the setpoint address* helps once PV/SV reads work) and the divisor (1 or 10). If you only know the front-panel values, use
   *Tools → Read registers* over 0…60 and look for the PV and SV numbers (e.g. SV 148 appears as 148 or 1480).
4. Use *ESP32 writes setpoint steps* (it probably has no gradient register) and keep *minimum step* ≥ 1 and
   *minimum seconds* ≥ 30 to spare its EEPROM.

PMA-specific extras (status word, self-tuning, outputs off, parameter page) won't apply unless you map equivalents.

## 4. Build and flash

1. Install VS Code + PlatformIO.
2. Open this folder, build and upload environment `cyd` (`pio run -t upload`).
3. First start: no Wi-Fi configured → the CYD opens access point `TempCtl-XXXX`, password `tempctl123`.
   Browse to `http://192.168.4.1`, set your Wi-Fi on the *Wi-Fi* page. Afterwards: `http://tempctl.local`
   or the IP shown on the display's *Setup* tab.
4. On *Connection*, check slave address, baud rate and parity match the controller (`ConF/othr`), then
   check that the Run page shows the process value.

Touch calibration defaults (200…3700 / 240…3800) suit most CYDs. If touches land off, adjust `tsMinX…tsMaxY`
through `POST /api/settings` and restart.

## 5. Web API

| Method | Path | Body / query |
|---|---|---|
| GET | `/api/status` | live values, run state |
| GET | `/api/history` | 240 points: `pv`, `sp`, `plan` |
| GET | `/api/log.csv` | history as CSV |
| GET/POST | `/api/program` | `{"steps":[{"on":true,"t":100,"r":30,"h":60}, …6]}` |
| POST | `/api/cmd` | `{"cmd":"start"|"pause"|"resume"|"stop"|"skip"}` |
| GET/POST | `/api/settings` | partial updates allowed; `{"profile":0,"loadDefaults":true}` |
| POST | `/api/wifi` | `{"ssid","pass","apPass"}` then restart |
| GET | `/api/mb/read?addr=&n=&fn=` | raw registers |
| POST | `/api/mb/write` | `{"addr","value"}` raw |
| POST | `/api/mb/findsp` | `{"from","to"}` |
| GET/POST | `/api/params` | PMA parameters |
| POST | `/api/action` | `{"i":index}` PMA actions (self-tuning, outputs off, …) |

The web UI has no login. Keep it on a trusted network.

## 6. Project layout

```
platformio.ini            board, libraries, TFT_eSPI pin setup
include/lv_conf.h         LVGL configuration
src/config.h              pins
src/program.*             6-step ramp/soak engine (pure C++, unit-tested)
src/modbus_frame.h        Modbus RTU framing + CRC (pure C++, unit-tested)
src/modbus_rtu.*          half-duplex Modbus master on UART2
src/controller.*          scaling, special values, PMA parameter/action tables
src/settings.*            defaults + NVS storage
src/app_state.*           control task: polling, program, setpoint writes, history
src/ui.*                  LVGL screens
src/web.*, web_page.h     HTTP server, JSON API, single-page web UI
test/host_test.cpp        g++ -std=c++17 -Isrc test/host_test.cpp src/program.cpp && ./a.out
```

## 7. Safety

A software programmer must never be the only protection against overheating. Fit an independent
over-temperature limiter (PMA recommends the TB 40-1 in the KS 4x-1 manual) and use the controller's own
limit alarms and `SP.Hi` setpoint limit. Set *Highest allowed setpoint* on *Connection* to match `SP.Hi`.

Status: the program engine and Modbus framing pass host unit tests; the firmware has not been
compiled against the ESP32 toolchain or tested on hardware yet. Expect to adjust small details
(display variant, touch calibration, CN1 pin order) on first bring-up.
