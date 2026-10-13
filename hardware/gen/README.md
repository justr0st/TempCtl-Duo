# Schematic generator

Python 3 (no packages) writes the KiCad schematics in `hardware/s3-schematic/` and
`hardware/cyd-schematic/`:

    python3 make.py ..          # both sets; python3 make.py .. cyd  (or s3) for one

- `model.py`: the symbols (grid 1.27 mm), `sheet.py`: sheet model + connectivity check,
  `s3.py` / `cyd.py`: the placement (`python3 cyd.py` prints the netlist and the check),
  `kicad.py`: KiCad 7 files, `render.py`: SVG, `topng.js`: PNG / PDF (Node + Playwright; without it the
  SVG is kept).
- `verify.py <file>.kicad_sch` reads a written schematic back (independent of the generator) and prints
  its netlist plus grid / dangling-wire / no-connect checks.
- Regenerating overwrites the schematics: after editing one in KiCad, don't run it for that set.
