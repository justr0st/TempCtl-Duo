#!/usr/bin/env python3
# shots*/*.raw (RGB565) -> .png next to them; the size per directory: shots, shots_light = S3 480x320,
# shots_cyd, shots_cyd_light = CYD 320x240. Run from test/ui_host.
import glob, os, struct
from PIL import Image
for d in sorted(glob.glob('shots*/')):
    W, H = (320, 240) if 'cyd' in d else (480, 320)
    for f in sorted(glob.glob(d + '*.raw')):
        v = struct.unpack('<%dH' % (W * H), open(f, 'rb').read())
        im = Image.new('RGB', (W, H))
        im.putdata([(((x >> 11) & 31) * 255 // 31, ((x >> 5) & 63) * 255 // 63, (x & 31) * 255 // 31) for x in v])
        im.save(f[:-4] + '.png')
