// Web page screenshots for the wiki (tools/wiki/make_shots.sh): opens the page of the demo device
// (tools/wiki/mock/server.py) in Chromium (Playwright), one scene per picture, dark and light.
//   node tools/wiki/shots_web.js [http://127.0.0.1:8770] [out dir] [name filter]
// Output: <out>/<name>-dark.png, <name>-light.png (docs/wiki/img/web/).
const { chromium } = require('playwright');
const path = require('path');
const fs = require('fs');

const BASE = process.argv[2] || 'http://127.0.0.1:8770';
const OUT = process.argv[3] || path.join(__dirname, '..', '..', 'docs', 'wiki', 'img', 'web');
const ONLY = process.argv[4] || '';
const REL = 'https://tempctl-duo.just-r0st.workers.dev';
const VER = fs.readFileSync(path.join(__dirname, '..', '..', 'src', 'version.h'), 'utf8').match(/FW_VERSION "([^"]+)"/)[1];
const NEXT = VER.replace(/(\d+)$/, n => String(+n + 1));

// the release page as it would answer (a newer firmware than the demo device runs)
const MANIFEST = {
  version: NEXT, date: '2026-10-21T09:12:44Z', commit: 'a1b2c3d',
  firmware: Object.fromEntries(['s3', 'cyd-1xlocal-1x485', 'cyd-2xlocal', 'cyd-2xlocal-2x485'].map(e => [e, {
    url: `/dl/tempctl-duo-${NEXT}-${e}.bin`, size: e === 's3' ? 1916032 : 1874112, md5: '0'.repeat(32), sha256: '0'.repeat(64),
    full: { url: `/dl/tempctl-duo-${NEXT}-${e}-full.bin`, size: e === 's3' ? 1981568 : 1939648, md5: '0'.repeat(32), sha256: '0'.repeat(64) } }])),
  app: [['windows', 'x64', '.exe', 9541120], ['macos', 'arm64', '.zip', 4312210], ['macos', 'x64', '.zip', 4580105], ['linux', 'x64', '.tar.gz', 4402118]]
    .map(([os, arch, ext, size]) => ({ os, arch, url: `/dl/tempctl-duo-${NEXT}-${os}-${arch}${ext}`, size, md5: '', sha256: '' })),
};

async function scene(name) {
  await fetch(BASE + '/demo/scene', { method: 'POST', body: JSON.stringify({ name }) });
}

// name, scene, url (hash), size, what to do before the picture, what to show (clip selector / full page)
const SHOTS = [
  { n: 'dashboard-run', s: 'run', h: '#/', clip: '#pg-status', maxH: 1060 },
  { n: 'dashboard-run-phone', s: 'run', h: '#/', w: 390, hgt: 844, phone: true },
  { n: 'dashboard-manual', s: 'manual', h: '#/', clip: '#pg-status', maxH: 900 },
  { n: 'dashboard-cards', s: 'run', h: '#/', clip: '#chans' },
  { n: 'dashboard-action', s: 'pause', h: '#/', hgt: 820 },
  { n: 'dashboard-interrupted', s: 'intr', h: '#/', hgt: 820 },
  { n: 'dashboard-done', s: 'done', h: '#/', clip: '#pg-status', maxH: 860, act: async p => { await p.click('#vProg').catch(() => {}); } },
  { n: 'dashboard-autotune', s: 'tune', h: '#/', hgt: 820 },
  { n: 'dashboard-fault', s: 'fault', h: '#/', clip: '#chans' },
  { n: 'sidebar', s: 'run', h: '#/', clip: '#side' },
  { n: 'editor-seq', s: 'idle', h: '#/', clip: '#edP' },
  { n: 'editor-tl', s: 'idle', h: '#/', clip: '#edP', act: async p => { await p.selectOption('#prog', { index: 2 }).catch(() => {}); } },
  { n: 'editor-chart', s: 'idle', h: '#/', clip: '#chartbox', act: async p => { await p.click('#vProg').catch(() => {}); } },
  { n: 'programs', s: 'idle', h: '#/programs', hgt: 820 },
  { n: 'channels-local', s: 'idle', h: '#/channels', full: true },
  { n: 'channels-modbus', s: 'modbus', h: '#/channels', full: true, act: async p => { await p.locator('#chTabs button').nth(1).click(); } },
  { n: 'log', s: 'run', h: '#/log', hgt: 900 },
  { n: 'settings', s: 'idle', h: '#/settings', full: true },
  { n: 'settings-update', s: 'idle', h: '#/settings', clip: '#setDev', act: async p => { await p.click('#fwNetCheck').catch(() => {}); } },
  { n: 'tools', s: 'idle', h: '#/tools', full: true },
  { n: 'replay', s: 'idle', h: '#/replay', hgt: 820, act: async p => {
      const csv = await (await fetch(BASE + '/demo/run.csv')).text();
      await p.setInputFiles('#rpFile', { name: '2026-10-20_101530_Lead-free_BGA.csv', mimeType: 'text/csv', buffer: Buffer.from(csv) });
    } },
  { n: 'connect', s: 'idle', url: '/hosted', hgt: 1120 },
];

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const b = await chromium.launch();
  for (const sh of SHOTS) {
    if (ONLY && !sh.n.includes(ONLY)) continue;
    await scene(sh.s);
    for (const scheme of ['dark', 'light']) {
      const w = sh.w || 1280, h = sh.hgt || 900;
      const ctx = await b.newContext({ viewport: { width: w, height: h }, colorScheme: scheme, deviceScaleFactor: sh.phone ? 2 : 1,
        isMobile: !!sh.phone, hasTouch: !!sh.phone, timezoneId: 'Europe/Berlin', locale: 'en-GB' });
      const p = await ctx.newPage();
      const errs = [];
      p.on('pageerror', e => errs.push(e.message));
      await p.route(REL + '/**', r => {
        const u = r.request().url();
        if (u.endsWith('/dl/manifest.json')) return r.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(MANIFEST), headers: { 'Access-Control-Allow-Origin': '*' } });
        return r.fulfill({ status: 404, body: '' });
      });
      await p.goto(BASE + (sh.url || '/') + (sh.h || ''));
      await p.waitForTimeout(1800);
      if (sh.act) { await sh.act(p); await p.waitForTimeout(1200); }
      const file = path.join(OUT, `${sh.n}-${scheme}.png`);
      if (sh.clip) {
        const el = p.locator(sh.clip).first();
        await el.scrollIntoViewIfNeeded();
        const bb = await el.boundingBox();
        if (sh.maxH && bb.height > sh.maxH) bb.height = sh.maxH;
        // the backdrop is fixed to the window: make the window as tall as the part, so it is behind all of it
        await p.setViewportSize({ width: w, height: Math.ceil(Math.max(h, bb.y + bb.height + 20)) });
        await p.waitForTimeout(400);
        const bb2 = await el.boundingBox();
        await p.screenshot({ path: file, clip: { x: Math.max(0, bb2.x - 8), y: Math.max(0, bb2.y - 8), width: Math.min(w, bb2.width + 16), height: Math.min(sh.maxH || 1e9, bb2.height) + 16 } });
      } else if (sh.full) {
        const H = await p.evaluate(() => document.documentElement.scrollHeight);
        await p.setViewportSize({ width: w, height: Math.min(H, 2600) });
        await p.waitForTimeout(400);
        await p.screenshot({ path: file });
      } else {
        await p.screenshot({ path: file });
      }
      console.log(file + (errs.length ? '  ERRORS: ' + errs.join(' | ') : ''));
      await ctx.close();
    }
  }
  await b.close();
})();
