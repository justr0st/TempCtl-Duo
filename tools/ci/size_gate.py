#!/usr/bin/env python3
"""Flash size gate: fails when a CYD image fills more than 97 % of its app slot.
  python3 tools/ci/size_gate.py [--limit 97]
Reads .pio/build/<env>/firmware.bin; the CYD slot from partitions_cyd_ota.csv (app0 / app1). Prints a table and
appends it to $GITHUB_STEP_SUMMARY when set."""
import os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
ENVS = ['s3', 'cyd-1xlocal-1x485', 'cyd-2xlocal', 'cyd-2xlocal-2x485']
S3_SLOT = 6553600  # default_16MB.csv (PlatformIO / Arduino-ESP32): app0 and app1 0x640000
LIMIT = 97.0


def csv_slot(path):
    sizes = []
    for ln in open(path):
        ln = ln.split('#')[0].strip()
        c = [x.strip() for x in ln.split(',')]
        if len(c) >= 5 and c[1] == 'app':
            sizes.append(int(c[4], 0))
    if not sizes:
        raise SystemExit('no app slot in ' + path)
    return min(sizes)


def main():
    global LIMIT
    if '--limit' in sys.argv:
        LIMIT = float(sys.argv[sys.argv.index('--limit') + 1])
    cyd = csv_slot(os.path.join(ROOT, 'partitions_cyd_ota.csv'))
    rows, bad = [], False
    for e in ENVS:
        p = os.path.join(ROOT, '.pio', 'build', e, 'firmware.bin')
        if not os.path.exists(p):
            print('missing: ' + p); bad = True; continue
        n = os.path.getsize(p)
        slot = S3_SLOT if e == 's3' else cyd
        pct = 100.0 * n / slot
        over = e != 's3' and pct > LIMIT
        bad |= over
        rows.append((e, n, slot, pct, slot - n, 'FAIL (> %g %%)' % LIMIT if over else 'ok'))
    t = ['| env | bytes | slot | % of slot | free | |', '|---|---:|---:|---:|---:|---|']
    t += ['| %s | %d | %d | %.1f | %d | %s |' % r for r in rows]
    t = '\n'.join(t)
    print(t)
    s = os.environ.get('GITHUB_STEP_SUMMARY')
    if s:
        with open(s, 'a') as f:
            f.write('### Flash size\n\n' + t + '\n')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
