# TempCtl Duo — ESP32-S3 schematic (env s3)

The connection schematic of the whole S3 device: DevKitC-1, display, RS485, thermocouple converters, SSR
drivers, SSR supply relay (an active-high DC solid-state relay driven directly by GPIO4), E-stop and
buzzer. Pins as in
`src/board_s3.h`; the wiring pictures are in `claude/wiring/`.

- `TempCtl-Duo-S3.kicad_pro` / `.kicad_sch`: KiCad 7 project (opens in KiCad 7 ... 10). The symbols are
  embedded in the schematic and also in `TempCtlDuo.kicad_sym` (project library, `sym-lib-table`), the
  power symbols (GND, +5V, +3V3, PWR_FLAG) too: KiCad's own `power:` copies differ between versions and ERC
  then warns "doesn't match copy in library" for each. The MAX31855 SO and the display's SDO are tri-state
  outputs (shared MISO lines). ERC in KiCad 10: no errors expected (warnings "global label only once" etc.
  can be ignored).
- `TempCtl-Duo-S3.pdf` / `.png`: the sheet (A3), drawn by the generator in `hardware/gen/`.
  After editing the schematic in KiCad, don't regenerate it (that overwrites).
