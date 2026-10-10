#!/usr/bin/env python3
# shots/*.raw (RGB565, 480x320) -> shots/*.png
import glob, struct
from PIL import Image
W, H = 480, 320
for f in sorted(glob.glob('shots/*.raw')):
    v = struct.unpack('<%dH' % (W * H), open(f, 'rb').read())
    im = Image.new('RGB', (W, H))
    im.putdata([(((x >> 11) & 31) * 255 // 31, ((x >> 5) & 63) * 255 // 63, (x & 31) * 255 // 31) for x in v])
    im.save(f[:-4] + '.png')
