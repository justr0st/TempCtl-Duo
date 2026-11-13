#!/usr/bin/env python3
"""Screenshot check (CI): regenerates the wiki's touch pictures from the UI host the same way as
tools/wiki/make_shots.sh (test/ui_host build, ./uihost and ./uihost_cyd dark + light, tools/wiki/touch_png.py) into a
temporary directory and compares them pixel by pixel with docs/wiki/img/touch/*.png (not byte by byte: PNG encoders
differ). A UI change that alters a screen without regenerating the pictures fails.
  LVGL=<lvgl 9.6 src> AJ=<ArduinoJson src> python3 tools/wiki/check_shots.py [--write DIR] [--no-build]
Prints every picture that differs (name, differing pixels, bounding box); exit 1 if any differ or a committed picture
has no counterpart (or the reverse). --write DIR: also saves <name>.new.png and <name>.diff.png (differences in red).
--no-build: use the existing test/ui_host/uihost and uihost_cyd."""
import argparse, os, shutil, subprocess, sys, tempfile
from PIL import Image, ImageChops

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
UI = os.path.join(ROOT, 'test', 'ui_host')
COMMITTED = os.path.join(ROOT, 'docs', 'wiki', 'img', 'touch')


def norm(im):
    """RGBA with the colour of fully transparent pixels zeroed (it carries no information)."""
    im = im.convert('RGBA')
    bg = Image.new('RGBA', im.size, (0, 0, 0, 0))
    return Image.composite(im, bg, im.getchannel('A'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--write', metavar='DIR')
    ap.add_argument('--no-build', action='store_true')
    a = ap.parse_args()
    env = dict(os.environ)
    env.setdefault('LVGL', '/tmp/lvgl'); env.setdefault('AJ', '/tmp/AJ743/src')
    if not a.no_build:
        subprocess.check_call(['bash', 'build.sh'], cwd=UI, env=env, stdout=subprocess.DEVNULL)
        subprocess.check_call(['bash', 'build.sh'], cwd=UI, env=dict(env, BOARD='cyd'), stdout=subprocess.DEVNULL)
    tmp = tempfile.mkdtemp(prefix='tempctl-shots-')
    try:
        # the host app writes shots*/ relative to its working directory: run in a temporary one
        for d in ('shots', 'shots_light', 'shots_cyd', 'shots_cyd_light'):
            os.makedirs(os.path.join(tmp, d))
        for b in ('uihost', 'uihost_cyd'):
            for theme in ('dark', 'light'):
                subprocess.check_call([os.path.join(UI, b)], cwd=tmp, env=dict(env, UIHOST_THEME=theme),
                                      stdout=subprocess.DEVNULL)
        # the same picture steps as make_shots.sh: touch_png.py, pointed at the temporary shots
        sys.argv = ['touch_png.py']
        sys.path.insert(0, os.path.join(ROOT, 'tools', 'wiki'))
        import touch_png
        newdir = os.path.join(tmp, 'png')
        touch_png.UI, touch_png.OUT = tmp, newdir
        touch_png.main()
        new = set(os.listdir(newdir)); old = {f for f in os.listdir(COMMITTED) if f.endswith('.png')}
        bad = 0
        if a.write: os.makedirs(a.write, exist_ok=True)
        for f in sorted(old | new):
            if f not in new:
                print('%s: committed, but the UI host no longer makes it' % f); bad += 1; continue
            if f not in old:
                print('%s: made by the UI host, but not committed' % f); bad += 1
                if a.write: shutil.copy(os.path.join(newdir, f), os.path.join(a.write, f[:-4] + '.new.png'))
                continue
            A = norm(Image.open(os.path.join(COMMITTED, f))); B = norm(Image.open(os.path.join(newdir, f)))
            if A.size != B.size:
                print('%s: size %s -> %s' % (f, A.size, B.size)); bad += 1
                if a.write: shutil.copy(os.path.join(newdir, f), os.path.join(a.write, f[:-4] + '.new.png'))
                continue
            d = ImageChops.difference(A, B)
            mask = d.convert('L').point(lambda v: 255 if v else 0)
            box = mask.getbbox()
            if not box: continue
            n = sum(1 for v in mask.getdata() if v)
            print('%s: %d pixels differ, box %s' % (f, n, box)); bad += 1
            if a.write:
                shutil.copy(os.path.join(newdir, f), os.path.join(a.write, f[:-4] + '.new.png'))
                R = B.convert('RGB').point(lambda v: v // 2)
                R.paste((255, 0, 0), mask=mask)
                R.save(os.path.join(a.write, f[:-4] + '.diff.png'))
        print('check_shots: %d pictures, %d differ' % (len(new), bad))
        sys.exit(1 if bad else 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    main()
