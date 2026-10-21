#!/usr/bin/env python3
"""Wiring diagrams for the wiki (docs/wiki/img/wiring/*.svg): one small picture per part of the device, the
board's pins on the left, the module in the middle, what it connects to on the right; straight wires,
coloured by function, light and dark (the SVG follows the viewer's colour scheme).
  python3 tools/wiki/wiring.py [out dir]
Pins as in src/board_s3.h / src/board_cyd.h and hardware/gen (the KiCad schematics); keep them in step."""
import html, os, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'docs', 'wiki', 'img', 'wiring')

# wire colours (light, dark)
COL = {'3v3': ('#d32f2f', '#ff6b6b'), '5v': ('#8e24aa', '#ce93d8'), 'gnd': ('#37474f', '#b0bec5'),
       'lcd': ('#5e35b1', '#b39ddb'), 'rs485': ('#1565c0', '#64b5f6'), 'tc': ('#2e7d32', '#81c784'),
       'ssr': ('#e65100', '#ffb74d'), 'safe': ('#c2185b', '#f48fb1'), 'led': ('#00838f', '#4dd0e1'),
       'field': ('#546e7a', '#90a4ae')}

ROW, HEAD, GAP = 26, 46, 22  # row height, block title height, gap between blocks
XB0, XB1 = 16, 236           # board block
XM0, XM1 = 450, 680          # module block
XF0, XF1 = 800, 965          # far block (sensor, controller, SSR ...)
W = 980


def esc(s): return html.escape(str(s))


class Diagram:
    def __init__(self, title, board, sub=''):
        self.title, self.board, self.sub = title, board, sub
        self.mods = []
        self.notes = []

    def module(self, title, sub, rows, far=None, far_rows=None):
        """rows: (board pin text, signal, module pin, colour key); module pin '' = no module pin (wire ends
        at the module's edge: e.g. a pull-up drawn as text); far: (title, sub); far_rows: (module right pin,
        far pin, colour key, label) - module right pin None = the far pin is fed from `label` (a stub)"""
        self.mods.append((title, sub, rows, far, far_rows or []))
        return self

    def note(self, *lines):
        self.notes += lines
        return self

    def svg(self):
        o = []
        y = 74
        boxes = []
        for title, sub, rows, far, far_rows in self.mods:
            n = max(len(rows), len(far_rows))
            h = HEAD + n * ROW + 8
            boxes.append((y, h, title, sub, rows, far, far_rows))
            y += h + GAP
        H = y + 10 + len(self.notes) * 18 + (14 if self.notes else 0)
        o.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="Barlow, Segoe UI, Roboto, Helvetica, Arial, sans-serif">')
        light = 'svg{--bg:#ffffff;--box:#f5f7fa;--line:#c5ccd6;--text:#1c2430;--muted:#5d6b7c;--board:#fff8e1;--boardl:#e0b84a' + \
            ''.join(';--%s:%s' % (k, a) for k, (a, _) in COL.items()) + '}'
        dark = 'svg{--bg:#12161c;--box:#1c222b;--line:#3a4452;--text:#e6ebf2;--muted:#94a3b5;--board:#2a2414;--boardl:#8a7330' + \
            ''.join(';--%s:%s' % (k, b) for k, (_, b) in COL.items()) + '}'
        css = [light, '@media (prefers-color-scheme:dark){' + dark + '}']
        css.append('text{fill:var(--text);font-size:13px}.m{fill:var(--muted);font-size:11.5px}.t{font-size:15px;font-weight:700}'
                   '.h{font-size:19px;font-weight:700}.s{font-size:12.5px;fill:var(--muted)}.w{font-size:11.5px;font-weight:700}'
                   '.box{fill:var(--box);stroke:var(--line);stroke-width:1.5}.brd{fill:var(--board);stroke:var(--boardl);stroke-width:1.5}'
                   '.pin{fill:var(--bg);stroke-width:1.6}')
        o.append('<style>' + ''.join(css) + '</style>')
        o.append(f'<rect width="{W}" height="{H}" rx="14" fill="var(--bg)"/>')
        o.append(f'<text x="{XB0}" y="30" class="h">{esc(self.title)}</text>')
        if self.sub: o.append(f'<text x="{XB0}" y="52" class="s">{esc(self.sub)}</text>')
        # the board: one block over all modules
        if boxes:
            y0, y1 = boxes[0][0], boxes[-1][0] + boxes[-1][1]
            o.append(f'<rect x="{XB0}" y="{y0}" width="{XB1 - XB0}" height="{y1 - y0}" rx="10" class="brd"/>')
            o.append(f'<text x="{XB0 + 14}" y="{y0 + 22}" class="t">{esc(self.board[0])}</text>')
            if len(self.board) > 1: o.append(f'<text x="{XB0 + 14}" y="{y0 + 38}" class="m">{esc(self.board[1])}</text>')
        for (y, h, title, sub, rows, far, far_rows) in boxes:
            o.append(f'<rect x="{XM0}" y="{y}" width="{XM1 - XM0}" height="{h}" rx="10" class="box"/>')
            o.append(f'<text x="{(XM0 + XM1) / 2}" y="{y + 20}" class="t" text-anchor="middle">{esc(title)}</text>')
            if sub: o.append(f'<text x="{(XM0 + XM1) / 2}" y="{y + 36}" class="m" text-anchor="middle">{esc(sub)}</text>')
            for i, (bp, sig, mp, c) in enumerate(rows):
                yy = y + HEAD + i * ROW + ROW / 2
                col = f'var(--{c})'
                if bp:
                    o.append(f'<text x="{XB1 - 14}" y="{yy + 4.5}" text-anchor="end">{esc(bp)}</text>')
                    o.append(f'<line x1="{XB1}" y1="{yy}" x2="{XM0}" y2="{yy}" stroke="{col}" stroke-width="2.6" stroke-linecap="round"/>')
                    o.append(f'<circle cx="{XB1}" cy="{yy}" r="4" class="pin" stroke="{col}"/>')
                    if sig: o.append(f'<text x="{(XB1 + XM0) / 2}" y="{yy - 5}" class="w" text-anchor="middle" style="fill:{col}">{esc(sig)}</text>')
                elif sig:  # a stub from a named source (e.g. "5 V from the relay")
                    o.append(f'<line x1="{XM0 - 90}" y1="{yy}" x2="{XM0}" y2="{yy}" stroke="{col}" stroke-width="2.6"/>')
                    o.append(f'<text x="{XM0 - 96}" y="{yy + 4.5}" class="w" text-anchor="end" style="fill:{col}">{esc(sig)}</text>')
                if mp:
                    o.append(f'<circle cx="{XM0}" cy="{yy}" r="4" class="pin" stroke="{col}"/>')
                    o.append(f'<text x="{XM0 + 12}" y="{yy + 4.5}">{esc(mp)}</text>')
            if far:
                o.append(f'<rect x="{XF0}" y="{y}" width="{XF1 - XF0}" height="{h}" rx="10" class="box"/>')
                o.append(f'<text x="{(XF0 + XF1) / 2}" y="{y + 20}" class="t" text-anchor="middle">{esc(far[0])}</text>')
                if len(far) > 1 and far[1]: o.append(f'<text x="{(XF0 + XF1) / 2}" y="{y + 36}" class="m" text-anchor="middle">{esc(far[1])}</text>')
            for i, (mp, fp, c, lab) in enumerate(far_rows):
                yy = y + HEAD + i * ROW + ROW / 2
                col = f'var(--{c})'
                if mp is not None:
                    o.append(f'<text x="{XM1 - 12}" y="{yy + 4.5}" text-anchor="end">{esc(mp)}</text>')
                    o.append(f'<line x1="{XM1}" y1="{yy}" x2="{XF0}" y2="{yy}" stroke="{col}" stroke-width="2.6"/>')
                    o.append(f'<circle cx="{XM1}" cy="{yy}" r="4" class="pin" stroke="{col}"/>')
                    if lab: o.append(f'<text x="{(XM1 + XF0) / 2}" y="{yy - 5}" class="w" text-anchor="middle" style="fill:{col}">{esc(lab)}</text>')
                elif lab:
                    o.append(f'<line x1="{XF0 - 60}" y1="{yy}" x2="{XF0}" y2="{yy}" stroke="{col}" stroke-width="2.6"/>')
                    o.append(f'<text x="{XF0 - 66}" y="{yy + 4.5}" class="w" text-anchor="end" style="fill:{col}">{esc(lab)}</text>')
                if fp:
                    o.append(f'<circle cx="{XF0}" cy="{yy}" r="4" class="pin" stroke="{col}"/>')
                    o.append(f'<text x="{XF0 + 12}" y="{yy + 4.5}">{esc(fp)}</text>')
        y = boxes[-1][0] + boxes[-1][1] + 26 if boxes else 80
        for i, s in enumerate(self.notes):
            o.append(f'<text x="{XB0}" y="{y + i * 18}" class="s">{esc(s)}</text>')
        o.append('</svg>')
        return '\n'.join(o) + '\n'


# ------------------------------------------------------------------ ESP32-S3 (env s3)
S3 = ('ESP32-S3-DevKitC-1', 'v1.0 N16R8 · J1 left, J3 right header')


def s3_diagrams():
    d = {}
    d['s3-display'] = Diagram('S3: display module (4.0" ST7796S + FT6336U touch)', S3,
                              'Pin names as printed on the module; keep the SPI wires short (80 MHz: GPIO 10–13 are the IO-MUX pins)').module(
        '4.0" SPI display', 'MSP4031, 14-pin header', [
            ('J1-21  5V', '5 V', 'VCC', '5v'), ('J1-22  GND', '', 'GND', 'gnd'),
            ('J1-16  GPIO10', 'LCD_CS', 'LCD_CS', 'lcd'), ('J1-20  GPIO14', 'LCD_RST', 'LCD_RST', 'lcd'),
            ('J1-15  GPIO9', 'D/C', 'LCD_RS', 'lcd'), ('J1-17  GPIO11', 'MOSI', 'SDI (MOSI)', 'lcd'),
            ('J1-18  GPIO12', 'SCK', 'SCK', 'lcd'), ('J1-12  GPIO8', 'backlight PWM', 'LED', 'lcd'),
            ('J1-19  GPIO13', 'MISO', 'SDO (MISO)', 'lcd'), ('J1-8  GPIO15', 'I²C SCL', 'CTP_SCL', 'lcd'),
            ('J1-10  GPIO17', 'touch reset', 'CTP_RST', 'lcd'), ('J1-9  GPIO16', 'I²C SDA', 'CTP_SDA', 'lcd'),
            ('J1-7  GPIO7', 'touch INT', 'CTP_INT', 'lcd'), ('J1-11  GPIO18', 'SD card CS', 'SD_CS', 'lcd')]).note(
        'VCC 5 V recommended (backlight ≈ 100 mA); the logic is 3.3 V. MISO is read back by the automatic screen check.',
        'The display\'s micro-SD slot holds the run logs, firmware.bin for an update and the backups (FAT32).')
    d['s3-thermocouples'] = Diagram('S3: thermocouple converters (local channels)', S3,
                                    'MAX31855 (or MAX6675) breakout boards; SCK and SO shared, one chip select each').module(
        'MAX31855 #1', 'bottom · 10 kΩ CS pull-up to 3V3', [
            ('J1-1  3V3', '3.3 V', 'VIN', '3v3'), ('J3-1  GND', '', 'GND', 'gnd'),
            ('J3-7  GPIO41', 'SCK (shared)', 'SCK / CLK', 'tc'), ('J3-8  GPIO40', 'SO (shared)', 'SO / DO', 'tc'),
            ('J3-9  GPIO39', 'CS bottom', 'CS', 'tc')],
        ('Thermocouple', 'type K, bottom heater'), [('T+', 'yellow (+)', 'field', ''), ('T−', 'red (−)', 'field', '')]).module(
        'MAX31855 #2', 'top · 10 kΩ CS pull-up to 3V3', [
            ('J1-2  3V3', '3.3 V', 'VIN', '3v3'), ('J3-21  GND', '', 'GND', 'gnd'),
            ('J3-7  GPIO41', 'SCK (shared)', 'SCK / CLK', 'tc'), ('J3-8  GPIO40', 'SO (shared)', 'SO / DO', 'tc'),
            ('J3-10  GPIO38', 'CS top', 'CS', 'tc')],
        ('Thermocouple', 'type K, top heater'), [('T+', 'yellow (+)', 'field', ''), ('T−', 'red (−)', 'field', '')]).note(
        'The pull-ups keep both converters deselected while the ESP32 boots. Twisted, screened thermocouple wire; screen to GND at one end.',
        'IEC colours: type K + green / − white; ANSI: + yellow / − red. A reading that falls when heating: swap T+ and T−.')
    d['s3-ssr'] = Diagram('S3: SSR drivers, SSRs and the SSR supply relay', S3,
                          'Each SSR input is switched by an NPN transistor; the relay switches the +5 V of both SSR inputs').module(
        'SSR driver, bottom', 'NPN (BC337 / 2N2222)', [
            ('J3-18  GPIO21', 'SSR bottom', 'IN → 1 kΩ → base', 'ssr'), ('J3-22  GND', '', 'base → 10 kΩ → GND', 'gnd'),
            ('J3-22  GND', '', 'emitter', 'gnd')],
        ('SSR, bottom', 'DC input 3–32 V, zero-cross'), [('collector', '− input', 'ssr', ''), (None, '+ input', '5v', '5 V relay')]).module(
        'SSR driver, top', 'NPN (BC337 / 2N2222)', [
            ('J3-17  GPIO47', 'SSR top', 'IN (1 kΩ to base)', 'ssr'), ('J3-21  GND', '', '10 kΩ base–GND', 'gnd'),
            ('J3-21  GND', '', 'emitter', 'gnd')],
        ('SSR, top', 'DC input 3–32 V, zero-cross'), [('collector', '− input', 'ssr', ''), (None, '+ input', '5v', '5 V relay')]).module(
        'SSR supply relay', 'DC solid-state relay, active high', [
            ('J1-4  GPIO4', 'RELAY', 'IN +', 'safe'), ('J1-22  GND', '', 'IN −', 'gnd')],
        ('both SSR + inputs', '"5 V relay"'), [('OUT +', '', '5v', '5 V (J1-21)'), ('OUT −', '+ inputs', '5v', '5 V relay')]).note(
        'The 10 kΩ base pull-downs keep the SSRs off during reset and flashing. Relay: input ≤ 20 mA at 3.3 V, output ≥ 0.5 A DC.',
        'The relay is on only while a local channel heats; off at once on a fault or E-stop. A shorted SSR output still needs a contactor.',
        'Load side (mains, not drawn): L – fuse – SSR 1/2 – heater – N. Setting safety.relay off only when no relay is fitted.')
    d['s3-rs485'] = Diagram('S3: RS485 transceiver (Modbus channels, beta)', S3,
                            'Only a 3.3 V module (MAX3485 / SP3485): a 5 V MAX485 drives 5 V into the ESP32').module(
        'RS485 module (3.3 V)', 'MAX3485 / SP3485, DE and RE bridged', [
            ('J1-1  3V3', '3.3 V', 'VCC', '3v3'), ('J3-1  GND', '', 'GND', 'gnd'),
            ('J3-4  GPIO1', 'TX', 'DI', 'rs485'), ('J3-5  GPIO2', 'RX', 'RO', 'rs485'),
            ('J3-6  GPIO42', 'direction', 'DE + RE', 'rs485')],
        ('PMA KS 40-1 / 41-1 / 42-1', 'Modbus option, terminals 12–17'), [('A (+)', '17 DATA A', 'rs485', ''), ('B (−)', '15 DATA B', 'rs485', ''),
                                                                         (None, '13 RGND', 'field', 'not connected')]).note(
        'Two controllers share the A / B pair with their own addresses. No answer: swap A and B (harmless).',
        'No 120 Ω terminators below 8 devices at ≤ 9600 baud; the module needs fail-safe bias resistors. Twisted pair, screen grounded at one end.',
        'Auto-direction module: leave DE / RE open and set PIN_RS485_DE to -1 in src/board_s3.h.')
    d['s3-safety'] = Diagram('S3: E-stop input and alarm buzzer', S3).module(
        'E-stop button', 'normally closed (NC) contact', [
            ('J1-5  GPIO5', 'E-STOP', '1', 'safe'), ('J1-22  GND', '', '2', 'gnd')]).module(
        'Piezo buzzer', 'passive, via 100 Ω', [
            ('J1-6  GPIO6', 'BUZZER (2.7 kHz)', '+ (via 100 Ω)', 'safe'), ('J3-22  GND', '', '−', 'gnd')]).note(
        'E-stop: internal pull-up; an open contact or a broken wire switches every output off and stops the run (setting safety.estop, off by default).',
        'A magnetic buzzer or small speaker needs an NPN driver like the SSR drivers, with a 1N4148 across it.')
    return d


# ------------------------------------------------------------------ CYD (ESP32-2432S028R)
CYD = ('ESP32-2432S028R "CYD"', 'P3 / CN1 (JST 1.25 mm) · micro-SD slot pins')
SDNOTE = 'SD-slot pins: plug a micro-SD breakout ("sniffer") adapter into the slot, no card. IO5 has the board\'s SD pull-up; IO18 has none.'


def tc_module(title, sub, rows):
    return (title, sub, rows, ('Thermocouple', 'type K'), [('T+', '+', 'field', ''), ('T−', '−', 'field', '')])


def cyd_diagrams():
    d = {}
    rs = ('RS485 module (3.3 V)', 'MAX3485 / SP3485', [
        ('CN1-1  3V3', '3.3 V', 'VCC', '3v3'), ('CN1-4  GND', '', 'GND', 'gnd'), ('P3-2  IO22', 'TX', 'DI', 'rs485'),
        ('P3-3  IO35', 'RX (input only)', 'RO', 'rs485'), ('CN1-2  IO27', 'direction', 'DE + RE', 'rs485')],
        ('Modbus controller(s)', 'e.g. PMA KS 4x-1'), [('A (+)', '17 DATA A', 'rs485', ''), ('B (−)', '15 DATA B', 'rs485', '')])

    def ssr(title, pin, sig):
        return (title, 'NPN driver (BC337) → SSR − input', [
            (pin, sig, 'IN (1 kΩ to base)', 'ssr'), ('P1-4  GND', '', 'emitter, 10 kΩ', 'gnd')],
            ('SSR', 'DC input'), [('collector', '− input', 'ssr', ''), (None, '+ input', '5v', '5 V (P1-1)')])

    a = Diagram('CYD wiring A: env cyd-1xlocal-1x485', CYD, 'RS485 (one or two Modbus controllers) + one local channel (bottom or top)')
    for m in (rs, tc_module('MAX31855', 'on the SD-slot pins', [('SD-4  3V3', '3.3 V', 'VIN', '3v3'), ('SD-6  GND', '', 'GND', 'gnd'),
                                                                ('SD-3  IO23', 'SCK', 'SCK', 'tc'), ('SD-7  IO19', 'SO', 'SO', 'tc'),
                                                                ('SD-2  IO5', 'CS', 'CS', 'tc')]),
              ssr('SSR driver', 'SD-5  IO18', 'SSR')):
        a.module(*m)
    a.note(SDNOTE, 'No E-stop input, no SSR supply relay and no chip temperature on the CYD (no free pins).')
    d['cyd-a'] = a
    b = Diagram('CYD wiring B: env cyd-2xlocal', CYD, 'Two local channels, no RS485')
    for m in (tc_module('MAX31855 bottom', 'SD-slot pins', [('SD-4  3V3', '3.3 V', 'VIN', '3v3'), ('SD-6  GND', '', 'GND', 'gnd'),
                                                           ('SD-3  IO23', 'SCK (shared)', 'SCK', 'tc'), ('SD-7  IO19', 'SO (shared)', 'SO', 'tc'),
                                                           ('SD-2  IO5', 'CS bottom', 'CS', 'tc')]),
              tc_module('MAX31855 top', 'CS on CN1-2 (board pull-up R18)', [('CN1-1  3V3', '3.3 V', 'VIN', '3v3'), ('CN1-4  GND', '', 'GND', 'gnd'),
                                                                          ('SD-3  IO23', 'SCK (shared)', 'SCK', 'tc'), ('SD-7  IO19', 'SO (shared)', 'SO', 'tc'),
                                                                          ('CN1-2  IO27', 'CS top', 'CS', 'tc')]),
              ssr('SSR driver, bottom', 'SD-5  IO18', 'SSR bottom'), ssr('SSR driver, top', 'P3-2  IO22', 'SSR top')):
        b.module(*m)
    b.note(SDNOTE)
    d['cyd-b'] = b
    c = Diagram('CYD wiring C: env cyd-2xlocal-2x485', CYD, 'RS485 + two local channels, each channel local or Modbus. The on-board RGB LED is removed')
    for m in (rs, tc_module('MAX31855 bottom', 'SD-slot pins', [('SD-4  3V3', '3.3 V', 'VIN', '3v3'), ('SD-6  GND', '', 'GND', 'gnd'),
                                                               ('SD-3  IO23', 'SCK (shared)', 'SCK', 'tc'), ('SD-7  IO19', 'SO (shared)', 'SO', 'tc'),
                                                               ('SD-2  IO5', 'CS bottom', 'CS', 'tc')]),
              tc_module('MAX31855 top', 'CS on LED pad R + 10 kΩ pull-up', [('CN1-1  3V3', '3.3 V', 'VIN', '3v3'), ('CN1-4  GND', '', 'GND', 'gnd'),
                                                                            ('SD-3  IO23', 'SCK (shared)', 'SCK', 'tc'), ('SD-7  IO19', 'SO (shared)', 'SO', 'tc'),
                                                                            ('LED-R  IO4', 'CS top', 'CS', 'tc')]),
              ssr('SSR driver, bottom', 'SD-5  IO18', 'SSR bottom'), ssr('SSR driver, top', 'LED-G  IO16', 'SSR top'),
              ('Status LED', 'external WS2812, from 3V3', [('LED-B  IO17', 'data', 'DIN', 'led'), ('CN1-1  3V3', '3.3 V', 'VCC', '3v3'),
                                                           ('P3-4  GND', '', 'GND', 'gnd')], None, None)):
        c.module(*m)
    c.note(SDNOTE, 'LED pads: use the pads that go to the ESP32 pins, not the ones to the 1 kΩ resistors (check with an ohmmeter).',
           'IO4 floats at boot: the 10 kΩ pull-up keeps the top converter deselected.')
    d['cyd-c'] = c
    return d


def overview():
    """block diagram of the whole device (both boards): what goes where"""
    W2, H2 = 980, 410
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W2} {H2}" width="{W2}" height="{H2}" font-family="Barlow, Segoe UI, Roboto, Helvetica, Arial, sans-serif">']
    light = 'svg{--bg:#fff;--box:#f5f7fa;--line:#c5ccd6;--text:#1c2430;--muted:#5d6b7c;--acc:#1976d2;--mains:#c62828;--hot:#fff3e0;--hotl:#ef6c00}'
    dark = 'svg{--bg:#12161c;--box:#1c222b;--line:#3a4452;--text:#e6ebf2;--muted:#94a3b5;--acc:#64b5f6;--mains:#ef9a9a;--hot:#2b2016;--hotl:#ffb74d}'
    o.append('<style>' + light + '@media (prefers-color-scheme:dark){' + dark + '}text{fill:var(--text);font-size:13px}.m{fill:var(--muted);font-size:11.5px}'
             '.t{font-size:15px;font-weight:700}.h{font-size:19px;font-weight:700}.box{fill:var(--box);stroke:var(--line);stroke-width:1.5}'
             '.hot{fill:var(--hot);stroke:var(--hotl);stroke-width:1.5}.a{stroke:var(--acc);stroke-width:2.4;fill:none;marker-end:url(#ar)}'
             '.mn{stroke:var(--mains);stroke-width:2.4;fill:none;stroke-dasharray:6 4}</style>')
    o.append('<defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
             '<path d="M0,0 L10,5 L0,10 z" style="fill:var(--acc)"/></marker></defs>')
    o.append(f'<rect width="{W2}" height="{H2}" rx="14" fill="var(--bg)"/>')
    o.append('<text x="16" y="30" class="h">TempCtl Duo: the parts of the device</text>')

    def box(x, y, w, h, t, sub=(), cls='box'):
        o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" class="{cls}"/>')
        o.append(f'<text x="{x + w / 2}" y="{y + 22}" class="t" text-anchor="middle">{esc(t)}</text>')
        for i, s in enumerate(sub): o.append(f'<text x="{x + w / 2}" y="{y + 40 + i * 16}" class="m" text-anchor="middle">{esc(s)}</text>')

    def arrow(x1, y1, x2, y2, both=False, lab=''):
        o.append(f'<path d="M{x1},{y1} L{x2},{y2}" class="a"' + (' marker-start="url(#ar)"' if both else '') + '/>')
        if lab: o.append(f'<text x="{(x1 + x2) / 2}" y="{(y1 + y2) / 2 - 6}" class="m" text-anchor="middle">{esc(lab)}</text>')

    box(270, 60, 190, 330, 'ESP32-S3 or CYD', ('touch screen', 'web page', 'serial console', 'programs, PID', 'protections'))
    box(20, 60, 180, 70, 'Phone / PC', ('browser, desktop app',))
    arrow(200, 95, 270, 95, True, 'Wi-Fi · USB')
    box(20, 150, 180, 70, 'E-stop · alarm', ('S3: NC contact, piezo', 'CYD: speaker (P4)'))
    arrow(270, 185, 200, 185, True, 'GPIO')
    for i, (t, sub) in enumerate((('Thermocouple converters', 'MAX31855 / MAX6675'), ('SSR drivers', 'NPN, 1 kΩ / 10 kΩ'),
                                  ('SSR supply relay', 'S3: +5 V of the SSR inputs'), ('RS485 transceiver', '3.3 V, Modbus (beta)'))):
        y = 60 + i * 90
        box(530, y, 200, 70, t, (sub,))
        if i == 0: arrow(530, y + 35, 460, y + 35, False, 'SPI')
        else: arrow(460, y + 35, 530, y + 35, i == 3, ('', 'GPIO', 'GPIO', 'UART')[i])
    for i, (t, sub, cls) in ((0, ('Thermocouples', 'type K, bottom · top', 'hot')), (1, ('SSRs → heaters', 'mains side', 'hot')),
                             (3, ('Controllers', 'e.g. PMA KS 4x-1', 'box'))):
        y = 60 + i * 90
        box(790, y, 170, 70, t, (sub,), cls)
    arrow(790, 95, 730, 95)
    arrow(730, 185, 790, 185)
    arrow(730, 365, 790, 365, True, 'A / B')
    o.append('<path d="M730,275 L760,275 L760,205 L790,205" class="a"/>')
    o.append('<text x="766" y="250" class="m">+5 V</text>')
    o.append('<text x="20" y="300" class="m">Heaters: fit an independent</text><text x="20" y="316" class="m">over-temperature limiter and a</text>'
             '<text x="20" y="332" class="m">contactor in their supply.</text>')
    o.append('</svg>')
    return '\n'.join(o) + '\n'


def main():
    os.makedirs(OUT, exist_ok=True)
    all_ = {**s3_diagrams(), **cyd_diagrams()}
    for name, dg in all_.items():
        open(os.path.join(OUT, name + '.svg'), 'w', encoding='utf-8', newline='\n').write(dg.svg())
    open(os.path.join(OUT, 'overview.svg'), 'w', encoding='utf-8', newline='\n').write(overview())
    print('wiring: %d diagrams in %s' % (len(all_) + 1, os.path.relpath(OUT)))


if __name__ == '__main__':
    main()
