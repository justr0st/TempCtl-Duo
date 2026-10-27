#!/usr/bin/env python3
"""Demo device for the wiki screenshots (tools/wiki/make_shots.sh): serves web/index.html and answers the
web API like a TempCtl Duo (S3) would. Settings, example programs and time zones come from the firmware's
own code, runs from its run engine (tools/wiki/demo_run.cpp: DEMO_RUN = the built program).
  DEMO_RUN=/tmp/demo_run python3 tools/wiki/mock/server.py [port]       (default 8770)
POST /demo/scene {"name": ...} switches what the device shows (SCENES below); no X-TempCtl header needed.
Not a test mock: it accepts what the page sends and changes little (enough for the pictures)."""
import copy, http.server, json, math, os, re, subprocess, sys, time, urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
WEB = os.path.join(ROOT, 'web')
DEMO = os.environ.get('DEMO_RUN', '/tmp/demo_run')
FW = re.search(r'#define FW_VERSION "([^"]+)"', open(os.path.join(ROOT, 'src', 'version.h')).read()).group(1)
T0 = time.time()
EPOCH0 = 1792483200  # 2026-10-20 08:00 UTC: the device clock in the pictures (fixed, so they don't change)


def demo(*args):
    return json.loads(subprocess.check_output([DEMO] + [str(a) for a in args]))


CFG = demo('config')
CFG['limits'] = {'rs485': True, 'localMax': 2, 'monitor': True, 'relay': True, 'estop': True, 'sound': True, 'chipTemp': True}
CFG['monitor'].update({'on': True, 'name': 'Board edge'})  # the monitor channel (1.0.60): a third thermocouple


def mon_of(b):
    """the monitor channel's reading for a bottom PV: the board's edge, about half of its rise"""
    return None if b is None else round(26 + 0.55 * (b - 26), 1)
CFG['wifi'].update({'ssid': 'Workshop', 'passSet': True})
CFG['time']['zone'] = 'Europe/Berlin'
for i, c in enumerate(CFG['channels']):
    c['type'] = 'local'
    c['local'].update({'kp': [5.2, 3.1][i], 'ki': [0.031, 0.047][i], 'kd': [21.0, 9.5][i]})
CFG['channels'][0]['manualSp'] = 150
CFG['display']['liveMin'] = 30
CFG['channels'][1]['manualSp'] = 180
ZONES = demo('zones')
PROGS = demo('examples') + [None] * 17
PROGS[3] = {'fmt': 1, 'name': 'QFN rework', 'type': 'seq', 'phases': [
    {'kind': 'B', 'wait': True, 'b': {'t': 120, 'r': 45, 'h': 30}},
    {'kind': 'T', 't': {'t': 200, 'r': 30, 'h': 20}},
    {'kind': 'T', 't': {'t': 235, 'r': 20, 'h': 25}},
    {'kind': 'P', 'msg': True, 'text': 'Lift the part'},
    {'kind': 'BT', 'bIdle': True, 't': {'t': 120, 'r': 30, 'h': 0}}]}
GEN = {'cfg': 1, 'prog': 1}
SCENE = {'name': 'idle'}
DEMOCACHE = {}

SCENES = {  # name: (example program, run time, continue a pause after s) or None = no run
    'idle': None, 'manual': None, 'intr': None, 'tune': None, 'fault': None, 'modbus': None,
    'run': (0, 330, 10), 'run-early': (0, 100, 10), 'pause': (0, 135, 1e9), 'run-tl': (2, 330, 10), 'done': (0, 600, 10),
}


def run_demo():
    s = SCENES.get(SCENE['name'])
    if not s: return None
    if s not in DEMOCACHE: DEMOCACHE[s] = demo(*s)
    return DEMOCACHE[s]


def uptime(): return int(time.time() - T0) + 5400


def chan(i, d):
    c = CFG['channels'][i]
    name = SCENE['name']
    pv = round(bottom_at(0)[0], 1) if i == 0 else 25.8
    man = name == 'manual' and i == 0
    o = {'key': 'bt'[i], 'name': c['name'], 'type': c['type'], 'ok': True, 'err': '', 'pv': pv, 'pvFlag': 'ok',
         'sp': 150.0 if man else None, 'out': 31.0 if man else 0.0, 'outOn': man, 'cold': [31.5, 30.8][i], 'fault': '',
         'spMin': c['spMin'], 'spMax': c['spMax'], 'inProgram': False, 'state': 'none', 'idle': False, 'runSp': None,
         'target': None, 'moving': False, 'kept': -1, 'canExtend': False, 'synced': True,
         'manual': {'on': man, 'sp': c['manualSp'], 'synced': True}}
    if c['type'] == 'modbus':
        o.pop('cold'); o['mode'] = 'automatic'
    if d:
        x = d['ch'][i]
        o.update({k: x[k] for k in ('pv', 'sp', 'out', 'outOn', 'inProgram', 'state', 'idle', 'runSp', 'target', 'moving', 'kept', 'canExtend')})
        if 'lane' in x: o['lane'] = x['lane']; o['steps'] = x['steps']
        if d['run']['state'] != 'running': o.update({'inProgram': False, 'state': 'none', 'outOn': False, 'out': 0.0})
    if name == 'tune' and i == 0:
        o['autotune'] = {'state': 'done', 'sp': 200, 'cycle': 6, 'elapsed': 1830, 'msg': 'done: Ku 7.12, Pu 41 s',
                         'kp': 2.84, 'ki': 0.0297, 'kd': 15.3, 'ku': 7.12, 'pu': 41}
    if name == 'fault' and i == 1:
        o.update({'fault': 'Heating failed: output at least 80 % for 120 s, the reading rose less than 5 °C', 'pv': 31.2})
    return o


def live():
    d = run_demo()
    L = {'up': uptime(), 'fw': FW, 'board': 'ESP32-S3', 'variant': '', 'net': 'STA Workshop 192.168.1.50',
         'sel': CFG['selProgram'], 'progGen': GEN['prog'], 'logSeq': len(EVLOG), 'sysSeq': len(SYSLOG), 'boot': 12,
         'time': EPOCH0 + uptime(), 'tz': CFG['time']['zone'], 'relay': SCENE['name'] in ('manual', 'run', 'run-tl', 'run-early', 'pause'),
         'safety': {'estop': False, 'contact': 'closed', 'chipC': 47.5, 'chipHot': False, 'muted': False},
         'sd': {'card': 1, 'logging': bool(d and d['run']['state'] == 'running'), 'file': 'runs/2026-10-20_101530_Lead-free_BGA.csv' if d else '', 'err': ''},
         'cfgGen': GEN['cfg'], 'accent': CFG['display']['accent'], 'band': CFG['run']['bottomBand'], 'pollMs': CFG['bus']['pollMs'],
         'run': {'state': 'idle', 'prog': -1, 'msg': ''}}
    if d:
        L['run'] = copy.deepcopy(d['run'])
        if L['run']['state'] == 'finished': L['run']['msg'] = 'Program "Lead-free BGA" finished (7:28)'
    if SCENE['name'] == 'intr':
        L['run'].update({'prog': 0, 'name': PROGS[0]['name'], 'type': 'seq', 'phases': 7,
                         'msg': 'Run interrupted by a reset (power loss)',
                         'intr': {'reason': 'power loss', 'exact': False, 'el': 262, 'i': [3, 0], 'st': ['hold', 'none'], 'left': [0, 0]}})
    L['ch'] = [chan(i, d) for i in range(2)]
    L['mon'] = {'name': CFG['monitor']['name'], 'pv': mon_of(L['ch'][0]['pv']), 'raw': mon_of(L['ch'][0]['pv']), 'cold': 30.9, 'err': ''}
    return L


def bottom_at(t):
    """bottom PV t s before now (live chart): 'manual' heats up to 150 degC under the manual setpoint (on at
    70 % of the window), 'idle' cools down after manual mode was switched off (at 60 % of the window)"""
    span = CFG['display']['liveMin'] * 60; w = 0.4 * math.sin(t / 7)
    if SCENE['name'] == 'manual':
        a = t + span * 0.7
        return (150 - 124 * math.exp(-a / 70) + w, 150.0) if a > 0 else (26 + 0.2 * math.sin(t / 30), None)
    a = t + span * 0.6
    if a > 0: return 26 + 124 * math.exp(-a / 600) + 0.2 * w, None
    return 150 - 124 * math.exp(-(a + span * 0.4) / 70) + w if a + span * 0.4 > 0 else 26.0, 150.0 if a + span * 0.4 > 0 else None


def live_chart():
    """/api/chart?v=live: the last liveMin minutes"""
    n = 300; span = CFG['display']['liveMin'] * 60; k = max(1, span // n)
    pv = [[], []]; sp = [[], []]
    for j in range(n):
        t = (j - n + 1) * k  # s before now
        v, s = bottom_at(t)
        pv[0].append(round(v, 1)); sp[0].append(s)
        pv[1].append(round(25.8 + 0.3 * math.sin(t / 45), 1)); sp[1].append(None)
    pv.append([mon_of(v) for v in pv[0]])
    return {'mode': 'live', 'bin': k, 'span': span, 'sh': 0, 'have': 86400, 'cap': 86400, 'ms': uptime() * 1000, 'raw': 0,
            'mon': CFG['monitor']['name'], 'pv': pv, 'sp': sp, 'dlo': [[2] * n, [3] * n, [1] * n], 'dhi': [[2] * n, [3] * n, [1] * n]}


EVLOG = [  # [seq, uptime s, run time, text, kind, epoch]
    [1, 3, None, 'TempCtl Duo %s started (power on)' % FW, 1],
    [2, 9, None, 'Wi-Fi connected: Workshop, 192.168.1.50', 1],
    [3, 610, None, 'Bottom: manual setpoint on (150 °C)', 1],
    [4, 1800, None, 'Bottom: manual setpoint off', 1],
    [5, 1830, 0, 'Program "Lead-free BGA" started', 2],
    [6, 1830, 0, 'Phase 1 (Bottom)', 2],
    [7, 1949, 119, 'Phase 2 (Pause): Action required - press Continue', 3],
    [8, 1960, 130, 'Continue', 2],
    [9, 1960, 130, 'Phase 3 (Both)', 2],
    [10, 2054, 224, 'Phase 4 (Top)', 2]]
for e in EVLOG: e.append(EPOCH0 + e[1])
SYSLINES = [(0, '[boot] TempCtl Duo %s (ESP32-S3), reset reason: power on' % FW), (0, '[psram] 8 MB, 7.6 MB free'),
            (1, 'TempCtl Duo %s started (power on)' % FW), (0, '[settings] /duo/settings.json loaded'),
            (0, '[lcd] ST7796S 480x320, SPI 40 MHz, status: read back ok'), (0, '[touch] FT6336U id 0x64, INT line works'),
            (0, '[wifi] connecting to Workshop'), (1, 'Wi-Fi connected: Workshop, 192.168.1.50'),
            (0, '[time] NTP time set: 2026-10-20 10:00:09 (Europe/Berlin)'), (0, '[mem] running: DRAM free 151244, largest 126964, min 149876'),
            (1, 'Bottom: manual setpoint on (150 °C)'), (3, 'Top: converter at 71 °C - outputs can\'t be switched on above 70 °C'),
            (1, 'Bottom: manual setpoint off'), (2, 'Program "Lead-free BGA" started'), (2, 'Phase 1 (Bottom)'),
            (0, '[sd] run log runs/2026-10-20_101530_Lead-free_BGA.csv'), (2, 'Phase 2 (Pause): Action required - press Continue'),
            (4, 'Top: no reading (thermocouple open) - outputs off'), (1, 'Top: fault acknowledged'), (2, 'Phase 3 (Both)')]
SYSLOG = [[i + 1, 3 + i * 97, EPOCH0 + 3 + i * 97, k, t] for i, (k, t) in enumerate(SYSLINES)]
SDFILES = [['2026-10-20_101530_Lead-free_BGA.csv', 48211, EPOCH0 + 2600], ['2026-10-19_164205_QFN_rework.csv', 22790, EPOCH0 - 58000],
           ['2026-10-18_093011_Leaded_BGA.csv', 41552, EPOCH0 - 167000]]
BACKUPS = [['2026-10-19_170244.json', 6214, EPOCH0 - 54000], ['2026-10-12_081530.json', 5980, EPOCH0 - 690000]]


def summary(i):
    p = PROGS[i]
    if not p: return {'used': False, 'name': 'Program %d' % (i + 1), 'steps': 0, 'total': 0}
    if p['type'] == 'tl':
        items = len(p.get('B', [])) + len(p.get('T', []))
        tot = max(sum(s.get('r', 0) + s.get('h', 0) for s in p.get(k, [])) for k in 'BT')
        return {'used': True, 'name': p['name'], 'type': 'tl', 'steps': items, 'total': tot, 'waits': False,
                'uses': [len(p.get('B', [])) > 0, len(p.get('T', [])) > 0]}
    ph = p['phases']; tot = 0
    for x in ph:
        for k in ('b', 't'):
            s = x.get(k)
            if s and not (x['kind'] == 'BT' and k == 'b'): tot += s.get('r', 0) + s.get('h', 0)
    return {'used': True, 'name': p['name'], 'type': 'seq', 'steps': len(ph), 'total': tot, 'waits': any(x.get('wait') for x in ph),
            'uses': [any(x['kind'] in ('B', 'BT') for x in ph), any(x['kind'] in ('T', 'BT') for x in ph)]}


def run_csv():
    """an SD run log (sdlog.cpp columns) of the 'done' run, for the CSV replay picture"""
    d = demo(0, 600, 10); r = d['rows']; ev = sorted(d['chart']['ev'], key=lambda e: e[0])
    kinds = {1: 'Phase %d', 4: 'Hold of phase %d ended', 5: 'Program pause (phase %d)', 6: 'Continue', 7: 'Program finished'}
    out = ['time,run_s,where,bottom_pv,bottom_sp,bottom_prog,bottom_out,bottom_state,top_pv,top_sp,top_prog,top_out,top_state,monitor_pv,kind,event']
    f = lambda v: '' if v is None else '%.1f' % v
    k = 0
    for j in range(0, len(r), 7):
        t = r[j]; ts = time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(EPOCH0 + 8130 + t))
        out.append(','.join([ts, '%d' % t, '', f(r[j + 1]), f(r[j + 2]), f(r[j + 2]), f(r[j + 3]), '', f(r[j + 4]), f(r[j + 5]), f(r[j + 5]), f(r[j + 6]), '', f(mon_of(r[j + 1])), '', '']))
        while k < len(ev) and ev[k][0] <= t:
            e = ev[k]; k += 1
            if e[1] in kinds: out.append(','.join([ts, '%d' % t] + [''] * 12 + ['run', '"%s"' % (kinds[e[1]] % (e[3] + 1) if '%' in kinds[e[1]] else kinds[e[1]])]))
    return '\n'.join(out) + '\n'


def merge(dst, src):
    for k, v in src.items():
        if isinstance(v, dict) and isinstance(dst.get(k), dict): merge(dst[k], v)
        elif isinstance(v, list) and isinstance(dst.get(k), list) and v and isinstance(v[0], (dict, type(None))):
            for i, x in enumerate(v):
                if isinstance(x, dict) and i < len(dst[k]): merge(dst[k][i], x)
        else: dst[k] = v


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def send(self, code, body, ctype='application/json'):
        b = body if isinstance(body, bytes) else (body if isinstance(body, str) else json.dumps(body)).encode()
        self.send_response(code); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(b)))
        self.send_header('Cache-Control', 'no-store'); self.end_headers(); self.wfile.write(b)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = urllib.parse.parse_qs(u.query); p = u.path
        g = lambda k, d='': q.get(k, [d])[0]
        if p == '/':  # the device's own copy of the page (marked as tools/web_embed.py does)
            return self.send(200, open(os.path.join(WEB, 'index.html'), 'rb').read().replace(b'<html lang="en">', b'<html lang="en" data-device>', 1), 'text/html')
        if p == '/hosted':  # the hosted page's copy (no device mark: connect screen)
            return self.send(200, open(os.path.join(WEB, 'index.html'), 'rb').read(), 'text/html')
        if p == '/demo/run.csv': return self.send(200, run_csv(), 'text/csv')
        if p in ('/favicon.svg', '/favicon.ico', '/apple-touch-icon.png'):
            f = {'/favicon.svg': 'logo.svg'}.get(p, p[1:])
            return self.send(200, open(os.path.join(WEB, 'icons', f), 'rb').read(), {'svg': 'image/svg+xml', 'ico': 'image/x-icon', 'png': 'image/png'}[f.rsplit('.', 1)[1]])
        if p.startswith('/f/'): return self.send(200, open(os.path.join(WEB, 'fonts', p[3:]), 'rb').read(), 'font/woff2')
        if p == '/api/live': return self.send(200, live())
        if p == '/api/chart':
            if g('v') == 'live': return self.send(200, live_chart())
            d = run_demo()
            if not d: return self.send(200, {'mode': 'none'})
            c = copy.deepcopy(d['chart']); c.update({'ms': uptime() * 1000, 'raw': 0, 'ra': [], 'rn': [0, 0]})
            c['pv'].append([mon_of(v) for v in c['pv'][0]]); c['mon'] = CFG['monitor']['name']
            for k in ('dlo', 'dhi'):
                if k in c: c[k].append([0] * len(c['pv'][0]))
            return self.send(200, c)
        if p == '/api/programs': return self.send(200, {'sel': CFG['selProgram'], 'max': 20, 'maxPhases': 100, 'maxSteps': 100, 'programs': [summary(i) for i in range(20)]})
        if p == '/api/program':
            i = int(g('i', '0'))
            return self.send(200, {'i': i, 'used': PROGS[i] is not None, 'program': PROGS[i]} if PROGS[i] else {'i': i, 'used': False})
        if p == '/api/run':
            d = run_demo(); L = live()
            return self.send(200, {'run': L['run'], 'program': d['program'] if d else None})
        if p == '/api/log':
            since = int(g('since', '0'))
            return self.send(200, {'seq': len(EVLOG), 'up': uptime(), 'log': [e for e in EVLOG if e[0] > since]})
        if p == '/api/syslog':
            since = int(g('since', '0'))
            return self.send(200, {'lines': [l for l in SYSLOG if l[0] > since], 'seq': len(SYSLOG), 'first': 1, 'more': False, 'boot': 12, 'up': uptime(), 'time': EPOCH0 + uptime()})
        if p == '/api/eventlog.txt':
            return self.send(200, ''.join('12\t%d\t%d\t%d\t%s\n' % (e[5], e[1], e[4], e[3]) for e in EVLOG).encode(), 'text/plain')
        if p == '/api/timezones': return self.send(200, {'zones': ZONES})
        if p == '/api/config': return self.send(200, CFG)
        if p == '/api/sd': return self.send(200, {'slot': True, 'card': True, 'logging': False, 'file': '', 'err': '', 'totalKb': 7775936, 'freeKb': 7701120, 'files': SDFILES})
        if p == '/api/backup': return self.send(200, {'slot': True, 'card': True, 'err': '', 'totalKb': 7775936, 'freeKb': 7701120, 'files': BACKUPS})
        if p == '/api/backup/info': return self.send(200, {'time': '2026-10-19 17:02:44', 'fw': FW, 'settings': True, 'programs': 4, 'slots': '1, 2, 3, 4', 'mask': 15})
        if p == '/api/ota': return self.send(200, {'board': 's3', 'version': FW, 'slot': 'app0', 'slotKb': 6400, 'canStart': True, 'probation': False, 'sd': True, 'status': {'phase': 'idle'}})
        if p == '/api/ota/sd': return self.send(200, {'busy': False, 'none': True})
        if p == '/api/wifiscan': return self.send(200, {'networks': ['Workshop', 'Workshop-5G', 'Lab', 'Guest']})
        if p == '/api/mb/findsp': return self.send(200, {'busy': False, 'none': True})
        if p == '/api/mb/read':
            a = int(g('addr', '0')); n = int(g('n', '1'))
            return self.send(200, {'ok': True, 'values': [(a * 7 + k * 13) % 2000 for k in range(n)]})
        if p == '/api/params':
            return self.send(200, {'supported': True, 'params': [
                {'name': 'Pb1', 'addr': 11308, 'scaled': True, 'desc': 'Proportional band 1', 'value': 12.5},
                {'name': 'ti1', 'addr': 11309, 'scaled': True, 'desc': 'Integral time 1 (s)', 'value': 120},
                {'name': 'td1', 'addr': 11310, 'scaled': True, 'desc': 'Derivative time 1 (s)', 'value': 30}],
                'actions': ['Start self-tuning', 'Stop self-tuning', 'Outputs off', 'Outputs on']})
        if p in ('/api/history.csv', '/api/log.csv'): return self.send(200, 'time_s,uptime_s\n', 'text/csv')
        self.send(404, '{}')

    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0)); raw = self.rfile.read(n)
        try: body = json.loads(raw or b'{}')
        except ValueError: body = {}
        p = self.path
        if p == '/demo/scene':
            SCENE['name'] = body.get('name', 'idle')
            for i, c in enumerate(CFG['channels']): c['type'] = 'modbus' if SCENE['name'] == 'modbus' and i == 1 else 'local'
            sc = SCENES.get(SCENE['name']); CFG['selProgram'] = sc[0] if sc else 0
            GEN['cfg'] += 1
            return self.send(200, {'ok': True})
        if p == '/demo/config': merge(CFG, body); GEN['cfg'] += 1; return self.send(200, {'ok': True})
        if p == '/api/config':
            merge(CFG, body); GEN['cfg'] += 1
            lo = 100 if all(c['type'] == 'off' or (c['type'] == 'local' and c['local']['sensor'] == 'max31855') for c in CFG['channels']) else 250
            CFG['bus']['pollMs'] = max(lo, min(10000, CFG['bus']['pollMs']))  # (as settings.cpp pollMinMs)
            return self.send(200, CFG)
        if p == '/api/select': CFG['selProgram'] = body.get('i', 0); return self.send(200, {'ok': True, 'sel': CFG['selProgram']})
        if p == '/api/time': return self.send(200, {'ok': True, 'time': EPOCH0 + uptime(), 'zone': CFG['time']['zone']})
        self.send(200, {'ok': True})


if __name__ == '__main__':
    http.server.ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1]) if len(sys.argv) > 1 else 8770), H).serve_forever()
