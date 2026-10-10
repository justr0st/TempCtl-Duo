# sheet.py [cyd] [names...]: converts shots(_cyd)/*.raw to png and puts the named ones on sheet(_cyd).png
import sys, glob, os, struct
from PIL import Image
args = sys.argv[1:]
cyd = bool(args) and args[0] == 'cyd'
if cyd: args = args[1:]
W, H = (320, 240) if cyd else (480, 320)
D = 'shots_cyd' if cyd else 'shots'
def load(p):
    v = struct.unpack('<%dH' % (W * H), open(p, 'rb').read())
    im = Image.new('RGB', (W, H))
    im.putdata([(((x >> 11) & 31) * 255 // 31, ((x >> 5) & 63) * 255 // 63, (x & 31) * 255 // 31) for x in v])
    return im
files = sorted(glob.glob(D + '/*.raw'))
for f in files: load(f).save(f[:-4] + '.png')
names = args or [os.path.basename(f)[:-4] for f in files]
cols = 3 if cyd else 2; rows = (len(names) + cols - 1) // cols
sh = Image.new('RGB', (cols * (W + 10), rows * (H + 10)), (128, 128, 128))
for i, n in enumerate(names):
    sh.paste(Image.open(D + '/' + n + '.png'), ((i % cols) * (W + 10), (i // cols) * (H + 10)))
out = 'sheet_cyd.png' if cyd else 'sheet.png'
sh.save(out); print(out, len(names))
