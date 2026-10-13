# TempCtl Duo — CYD schematics (ESP32-2432S028R)

One connection schematic per CYD firmware env (pins as in `src/board_cyd.h`; the wiring pictures are
`claude/wiring/cyd-wiring-a/b/c.png`):

| Sheet | env | Contents |
|---|---|---|
| `TempCtl-Duo-CYD-A` | `cyd-1xlocal-1x485` | RS485 + one local channel (thermocouple + SSR on the SD-slot pins) |
| `TempCtl-Duo-CYD-B` | `cyd-2xlocal` | two local channels, no RS485 |
| `TempCtl-Duo-CYD-C` | `cyd-2xlocal-2x485` | RS485 + two local channels, RGB LED removed, external WS2812 status LED |

All three: the alarm speaker on P4 (IO26 drives the CYD's SC8002B amplifier; bridged output, SPK− is
not ground), SSR inputs on 5 V from P1-1 (VIN), 3V3 from CN1-1 / the SD slot's VDD. No E-stop, SSR
supply relay or chip temperature on the CYD (no free pins).

- `*.kicad_pro` / `*.kicad_sch`: KiCad 7 projects (open in KiCad 7, 8, 9), sharing the project library
  `TempCtlDuo.kicad_sym` (`sym-lib-table`); the symbols are also embedded in each schematic. The CYD is one
  symbol with its connectors; pin numbers are connector-pin (P3-2, CN1-2, SD-5, P4-1, ...).
- `*.pdf` / `*.png`: the sheets (A3), drawn by the generator in `hardware/gen/`.
  After editing a schematic in KiCad, don't regenerate it (that overwrites).
