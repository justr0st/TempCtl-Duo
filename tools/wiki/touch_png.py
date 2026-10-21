#!/usr/bin/env python3
"""Touch screen pictures for the wiki (tools/wiki/make_shots.sh): the UI host's raw shots (test/ui_host,
RGB565) -> PNG at twice the size (pixels kept sharp), in a dark frame like the display module.
  python3 tools/wiki/touch_png.py [out dir]     (default docs/wiki/img/touch)
Reads test/ui_host/shots{,_light,_cyd,_cyd_light}/<name>.raw for the names in PICK; writes
<name>-dark.png / <name>-light.png (S3, 480x320) and cyd-<name>-dark.png / -light.png (CYD, 320x240)."""
import os, struct, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(HERE, '..', '..', 'test', 'ui_host')
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'docs', 'wiki', 'img', 'touch')

# the screens the wiki shows (test/ui_host/host_app.cpp names)
PICK = ['01-idle', '01b-tune-toast', '02-picker', '03-running', '04-stepper', '05-numpad', '06-action', '07-live',
        '09-interrupted', '10-fault', '11-menu', '12-display', '12b-accent-picker', '12e-display-spi', '13-network',
        '14-system', '14b-tz-regions', '14d-system-safety', '14e0-system-backup', '14f-restore-list', '14j-online-update',
        '15-log', '16-keyboard', '19-programs', '20-edit-seq', '21-rowmenu', '21b-phase-menu', '22-edit-tl', '22c-step-menu',
        '24-chan-local', '25-chan-modbus', '26-chan-bus', '27-chan-pid', '28-chan-runaway', '30-ota',
        '29-touch-cal', '14e0-system-online']


def load(path, w, h):
    v = struct.unpack('<%dH' % (w * h), open(path, 'rb').read())
    im = Image.new('RGB', (w, h))
    im.putdata([(((x >> 11) & 31) * 255 // 31, ((x >> 5) & 63) * 255 // 63, (x & 31) * 255 // 31) for x in v])
    return im


def framed(im):
    im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
    b, r = 18, 22  # frame width, corner radius
    out = Image.new('RGBA', (im.width + 2 * b, im.height + 2 * b), (0, 0, 0, 0))
    d = ImageDraw.Draw(out)
    d.rounded_rectangle([0, 0, out.width - 1, out.height - 1], r, fill=(24, 26, 30, 255), outline=(60, 64, 72, 255), width=2)
    out.paste(im, (b, b))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    n = 0
    for sub, (w, h), pre in (('shots', (480, 320), ''), ('shots_cyd', (320, 240), 'cyd-')):
        for theme, suf in (('dark', ''), ('light', '_light')):
            for name in PICK:
                p = os.path.join(UI, sub + suf, name + '.raw')
                if not os.path.exists(p): continue
                framed(load(p, w, h)).save(os.path.join(OUT, '%s%s-%s.png' % (pre, name, theme)), optimize=True)
                n += 1
    print('touch_png: %d pictures in %s' % (n, os.path.relpath(OUT)))


if __name__ == '__main__':
    main()
