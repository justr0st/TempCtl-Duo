#!/usr/bin/env python3
"""Compile + link check for TempCtl Duo in the cloud container (no PlatformIO there),
mimicking PlatformIO espressif32 6.9.0 / Arduino-ESP32 2.0.17: flags come from the core's
tools/platformio-build-<mcu>.py (mocked SCons env); compiles core, libraries, LVGL, ui/ and src/,
links firmware.elf and prints the section sizes.

Usage: build-check.py <s3|cyd-1xlocal-1x485|cyd-2xlocal|cyd-2xlocal-2x485> <project_dir> [--jobs N]
       (outputs in /tmp/build/out-<env>; object files are cached, src/ is always rebuilt)

Setup (GitHub downloads work, PyPI / PlatformIO registry do not):
  mkdir -p /tmp/tc && cd /tmp/tc
  for t in esp32s3 esp32; do curl -sSLO https://github.com/espressif/crosstool-NG/releases/download/\
esp-2021r2-patch5/xtensa-$t-elf-gcc8_4_0-esp-2021r2-patch5-linux-amd64.tar.gz && tar xzf xtensa-$t-*.tar.gz; done
  git clone --depth 1 --branch 2.0.17 https://github.com/espressif/arduino-esp32 /tmp/ae
  git clone --depth 1 --branch v9.6.0 https://github.com/lvgl/lvgl /tmp/lvgl
  git clone --depth 1 --branch v7.2.1 https://github.com/bblanchon/ArduinoJson /tmp/ArduinoJson
  git clone --depth 1 https://github.com/PaulStoffregen/XPT2046_Touchscreen /tmp/XPT2046_Touchscreen
Keep the env settings below in sync with platformio.ini."""
import sys, os, types, glob, subprocess, hashlib, json, shlex
from concurrent.futures import ThreadPoolExecutor

AE = '/tmp/ae'; LVGL = '/tmp/lvgl'
target, proj = sys.argv[1], os.path.abspath(sys.argv[2])
jobs = int(sys.argv[sys.argv.index('--jobs') + 1]) if '--jobs' in sys.argv else 2
T = {
 's3': dict(mcu='esp32s3', variant='esp32s3', tc='/tmp/tc/xtensa-esp32s3-elf/bin/xtensa-esp32s3-elf-',
            board={'build.core': 'esp32', 'build.variant': 'esp32s3', 'name': 'Espressif ESP32-S3-DevKitC-1-N8 (8 MB QD, No PSRAM)',
                   'build.partitions': 'default_16MB.csv', 'build.arduino.memory_type': 'qio_opi',
                   'build.flash_mode': 'qio'},
            extra=['ARDUINO_ESP32S3_DEV', ('ARDUINO_USB_MODE', 0), ('ARDUINO_RUNNING_CORE', 1),
                   ('ARDUINO_EVENT_RUNNING_CORE', 1), 'BOARD_S3', 'BOARD_HAS_PSRAM', ('ARDUINO_USB_CDC_ON_BOOT', 0)],
            libs_extra=[], app_size=0x640000),
 'cyd-1xlocal-1x485': dict(mcu='esp32', variant='esp32', tc='/tmp/tc/xtensa-esp32-elf/bin/xtensa-esp32-elf-',
            board={'build.core': 'esp32', 'build.variant': 'esp32', 'name': 'Espressif ESP32 Dev Module', 'build.partitions': 'huge_app.csv',
                   'build.flash_mode': 'dio'},
            extra=['ARDUINO_ESP32_DEV', 'BOARD_CYD', 'CYD_1XLOCAL_1X485'], libs_extra=['/tmp/XPT2046_Touchscreen'], app_size=0x300000),
 'cyd-2xlocal': dict(mcu='esp32', variant='esp32', tc='/tmp/tc/xtensa-esp32-elf/bin/xtensa-esp32-elf-',
            board={'build.core': 'esp32', 'build.variant': 'esp32', 'name': 'Espressif ESP32 Dev Module', 'build.partitions': 'huge_app.csv',
                   'build.flash_mode': 'dio'},
            extra=['ARDUINO_ESP32_DEV', 'BOARD_CYD', 'CYD_2XLOCAL'], libs_extra=['/tmp/XPT2046_Touchscreen'], app_size=0x300000),
 'cyd-2xlocal-2x485': dict(mcu='esp32', variant='esp32', tc='/tmp/tc/xtensa-esp32-elf/bin/xtensa-esp32-elf-',
            board={'build.core': 'esp32', 'build.variant': 'esp32', 'name': 'Espressif ESP32 Dev Module', 'build.partitions': 'huge_app.csv',
                   'build.flash_mode': 'dio'},
            extra=['ARDUINO_ESP32_DEV', 'BOARD_CYD', 'CYD_2XLOCAL_2X485'], libs_extra=['/tmp/XPT2046_Touchscreen'], app_size=0x300000),
}[target]

# ---- flags from the core's PlatformIO script (mock SCons env)
class Env(dict):
    def Append(self, **kw):
        for k, v in kw.items(): self.setdefault(k, []).extend(v)
    def PioPlatform(self): return types.SimpleNamespace(get_package_dir=lambda n: AE)
    def BoardConfig(self): return types.SimpleNamespace(get=lambda k, d=None: T['board'].get(k, d))
env = Env()
scons = types.ModuleType('SCons'); script = types.ModuleType('SCons.Script')
script.DefaultEnvironment = lambda: env
sys.modules['SCons'] = scons; sys.modules['SCons.Script'] = script
exec(compile(open(f"{AE}/tools/platformio-build-{T['mcu']}.py").read(), 'pb', 'exec'), {'__name__': 'pb'})

def defs(lst):
    out = []
    for d in lst:
        if isinstance(d, tuple):
            v = str(d[1]).replace('$BOARD_F_CPU', '240000000L').replace('\\"', '"')
            out.append(f'-D{d[0]}={v}')
        else: out.append(f'-D{d}')
    return out
D = defs(env['CPPDEFINES'] + T['extra'] + ['PLATFORMIO=60118', 'LV_CONF_INCLUDE_SIMPLE', 'LV_LVGL_H_INCLUDE_SIMPLE'])
ccflags = [f for f in env['CCFLAGS'] if f != '-MMD']
libdirs = {n: f'{AE}/libraries/{n}/src' for n in ['WiFi', 'WebServer', 'FS', 'LittleFS', 'Preferences', 'SPI', 'Wire', 'ESPmDNS']}
inc = [f'{proj}/include', f'{proj}/src', f'{proj}/ui', LVGL, f'{LVGL}/src', '/tmp/ArduinoJson/src',
       f'{AE}/cores/esp32', f"{AE}/variants/{T['variant']}"] + list(libdirs.values()) + T['libs_extra'] + env['CPPPATH']
I = ['-I' + p for p in inc]
build = f'/tmp/build/out-{target}'; os.makedirs(build, exist_ok=True)
CC, CXX = T['tc'] + 'gcc', T['tc'] + 'g++'

def cmd_for(src, extra=()):
    if src.endswith('.c'): return [CC] + env['CFLAGS'] + ccflags + list(extra) + D + I
    if src.endswith('.S'): return [CC] + env['ASFLAGS'] + env['ASPPFLAGS'] + ccflags + D + I
    return [CXX] + env['CXXFLAGS'] + ccflags + list(extra) + D + I

def obj_for(src, extra=()):
    key = hashlib.md5((src + ' '.join(extra)).encode()).hexdigest()[:10]
    return f'{build}/{os.path.basename(src)}.{key}.o'

def compile_one(item):
    src, extra, always = item
    o = obj_for(src, extra)
    if not always and os.path.exists(o) and os.path.getmtime(o) > os.path.getmtime(src): return o, None
    c = cmd_for(src, extra) + ['-c', src, '-o', o]
    r = subprocess.run(c, capture_output=True, text=True)
    msg = (r.stdout + r.stderr).strip()
    if r.returncode: return None, f'ERROR {src}\n{msg}'
    return o, (f'WARN {src}\n{msg}' if msg else None)

def srcs(d, exts=('.c', '.cpp', '.S'), rec=True):
    pat = '**/*' if rec else '*'
    return sorted(p for p in glob.glob(f'{d}/{pat}', recursive=rec) if p.endswith(exts))

items = []
core = srcs(f'{AE}/cores/esp32') + srcs(f"{AE}/variants/{T['variant']}")
items += [(s, ('-DARDUINO_CORE_BUILD',), False) for s in core]
for d in list(libdirs.values()) + T['libs_extra']: items += [(s, (), False) for s in srcs(d)]
items += [(s, (), False) for s in srcs(f'{LVGL}/src')]
ui = [s for s in srcs(f'{proj}/ui', ('.c',)) if '/preview' not in s and '/build/' not in s]
items += [(s, ('-Wno-deprecated-declarations', '-Wno-unused-function'), False) for s in ui]
own = srcs(f'{proj}/src')
items += [(s, ('-Wall',), True) for s in own]

errs, warns, objs = [], [], []
with ThreadPoolExecutor(jobs) as ex:
    for (o, m), (s, _, _) in zip(ex.map(compile_one, items), items):
        if o: objs.append(o)
        if m and m.startswith('ERROR'): errs.append(m)
        elif m and (s in own or s in ui): warns.append(m)
for w in warns: print(w[:3000])
if errs:
    for e in errs: print(e[:6000])
    print(f'{len(errs)} file(s) failed'); sys.exit(1)

core_objs = [obj_for(s, ('-DARDUINO_CORE_BUILD',)) for s in core]
ar = f'{build}/libFrameworkArduino.a'
if os.path.exists(ar): os.remove(ar)
subprocess.run([T['tc'] + 'ar', 'rcs', ar] + core_objs, check=True)
others = [o for o in objs if o not in core_objs]
mem = T['board']['build.arduino.memory_type'] if 'build.arduino.memory_type' in T['board'] else 'qio_qspi'
linkflags = [f.replace('${BUILD_DIR}', build).replace('${PROGNAME}', 'firmware').replace('"', '') for f in env['LINKFLAGS']]
libpath = ['-L' + p for p in env['LIBPATH']]
elf = f'{build}/firmware.elf'
cmd = [CXX, '-o', elf] + linkflags + others + libpath + ['-Wl,--start-group', ar] + env['LIBS'] + ['-Wl,--end-group']
r = subprocess.run(cmd, capture_output=True, text=True)
if r.returncode:
    print('LINK FAILED'); print((r.stdout + r.stderr)[:8000]); sys.exit(1)
sz = subprocess.run([T['tc'] + 'size', '-A', elf], capture_output=True, text=True).stdout
for line in sz.splitlines():
    p_ = line.split()
    if p_ and p_[0] in ('.iram0.text','.dram0.data','.dram0.bss','.flash.text','.flash.rodata','.ext_ram.bss'): print(line[:20].ljust(22), p_[1])
# flash image size ~ sum of sections loaded into flash (text/rodata) + data
flash = 0; ram = 0
for line in sz.splitlines():
    p = line.split()
    if len(p) >= 3 and p[1].isdigit() and p[0].startswith('.'):
        n, s_ = p[0], int(p[1])
        if n in ('.flash.text', '.flash.rodata', '.flash.appdesc', '.iram0.text', '.iram0.vectors', '.dram0.data', '.rtc.text', '.rtc.data', '.rtc.force_fast', '.rtc_noinit', '.flash.rodata_noload') and n != '.flash.rodata_noload': flash += s_
        if n in ('.dram0.data', '.dram0.bss', '.noinit'): ram += s_
print(f'OK {target}: approx. flash image {flash} bytes ({100*flash/T["app_size"]:.1f} % of app partition), static RAM {ram} bytes, {len(warns)} warning file(s)')
