# TempCtl Duo — ESP32-S3 schematic (env s3)

The connection schematic of the whole S3 device: DevKitC-1, display, RS485, thermocouple converters, SSR
drivers, SSR supply relay (an active-high DC solid-state relay driven directly by GPIO4), E-stop and
buzzer. Pins as in
`src/board_s3.h`; the wiring pictures are in `claude/wiring/`.

- `TempCtl-Duo-S3.kicad_pro` / `.kicad_sch`: KiCad 7 project (opens in KiCad 7, 8, 9). The symbols are
  embedded in the schematic and also in `TempCtlDuo.kicad_sym` (project library, `sym-lib-table`);
  power symbols are KiCad's standard `power:` ones.
- `TempCtl-Duo-S3.pdf` / `.png`: the sheet (A3), drawn by the generator in `hardware/gen/`.
  After editing the schematic in KiCad, don't regenerate it (that overwrites).
