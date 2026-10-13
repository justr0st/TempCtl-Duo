#!/usr/bin/env python3
"""Generates the schematics: python3 make.py <hardware dir> [s3|cyd]
writes <hardware>/s3-schematic/ and <hardware>/cyd-schematic/ (KiCad projects + SVG; PNG / PDF via
node topng.js when Playwright is installed). Stops when the connectivity check reports a problem."""
import os
import subprocess
import sys

import kicad
import render
import s3
import cyd
from sheet import nets

SETS = {"s3": ("s3-schematic", lambda: [s3.build()]),
        "cyd": ("cyd-schematic", lambda: [cyd.build(v) for v in cyd.VARIANTS])}


def main():
    hw = sys.argv[1] if len(sys.argv) > 1 else "out"
    which = sys.argv[2:] or list(SETS)
    here = os.path.dirname(os.path.abspath(__file__))
    for key in which:
        folder, build = SETS[key]
        out = os.path.join(hw, folder)
        sheets = build()
        for S in sheets:
            _, problems = nets(S)
            if problems:
                sys.exit(f"{S.project}: " + "; ".join(problems))
            kicad.write(S, out)
            svg = os.path.join(out, S.project + ".svg")
            open(svg, "w").write(render.svg(S))
            try:
                subprocess.run(["node", os.path.join(here, "topng.js"), svg, svg[:-4] + ".png", svg[:-4] + ".pdf"],
                               check=True)
                os.remove(svg)
            except (OSError, subprocess.CalledProcessError):
                print(f"{S.project}: PNG / PDF skipped (needs node + playwright), SVG kept")
            print(f"{S.project}: ok")
        kicad.write_lib(sheets, out)


if __name__ == "__main__":
    main()
