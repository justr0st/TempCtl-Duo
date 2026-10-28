// Chart handles on a touch screen (1.0.64, Rost): a swipe over a handle scrolls the page and changes nothing; a short
// hold (350 ms) then a drag moves the handle and the page stays; the finger may be a little off the handle. Mouse:
// a drag moves it at once. Needs the browser demo on :8790 (see test/web_reorder.js).   node test/web_chart_hold.js
const { chromium } = require('playwright');
let fails = 0;
const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) fails++; };
(async () => {
  const b = await chromium.launch();
  // phone
  {
    const ctx = await b.newContext({ viewport: { width: 390, height: 844 }, colorScheme: 'dark', hasTouch: true, isMobile: true });
    const p = await ctx.newPage(); const errs = []; p.on('pageerror', e => errs.push(e.message));
    await p.goto('http://127.0.0.1:8790/demo/#/'); await p.waitForTimeout(3500);
    await p.evaluate(() => { setView('prog'); }); await p.waitForTimeout(1200);
    await p.evaluate(() => { const c = document.querySelector('#chartbox'); c.scrollIntoView({ block: 'center' }); }); await p.waitForTimeout(400);
    const c = await ctx.newCDPSession(p);
    const tp = (type, x, y) => c.send('Input.dispatchTouchEvent', { type, touchPoints: type === 'touchEnd' ? [] : [{ x, y }] });
    const target = () => p.evaluate(() => J(editProg().phases.map(x => [x.b && x.b.t, x.t && x.t.t])));
    const handle = () => p.evaluate(() => { const els = [...document.querySelectorAll('#chartbox [data-h]')].filter(e => handles[+e.dataset.h].type === 'target');
      const all = [...document.querySelectorAll('#chartbox [data-h]')], ctr = e => { const r = e.getBoundingClientRect(); return [(r.left + r.right) / 2, (r.top + r.bottom) / 2]; };
      const alone = e => { const [x, y] = ctr(e); return all.every(o => o === e || Math.hypot(ctr(o)[0] - x, ctr(o)[1] - y) > 24); };  // (no other handle near)
      const e = els.find(e => { const r = e.getBoundingClientRect(); return r.top > 120 && r.bottom < innerHeight - 120 && alone(e); }) || els[0];
      const r = e.getBoundingClientRect(); return { x: (r.left + r.right) / 2, y: (r.top + r.bottom) / 2, k: +e.dataset.h }; });
    const t0 = await target(); let h = await handle(); const s0 = await p.evaluate(() => scrollY);
    // a swipe: the page scrolls, nothing changes
    await tp('touchStart', h.x, h.y);
    for (let k = 1; k <= 8; k++) { await tp('touchMove', h.x, h.y - 20 * k); await p.waitForTimeout(16); }
    await tp('touchEnd'); await p.waitForTimeout(600);
    const s1 = await p.evaluate(() => scrollY);
    ok(s1 > s0 + 40, `swipe over a handle scrolls the page (${s0} -> ${s1})`);
    ok(await target() === t0, 'swipe changes nothing');
    // hold, then drag up: the target rises, the page stays
    await p.evaluate(y => scrollTo(0, y), s0); await p.waitForTimeout(400); h = await handle();
    await tp('touchStart', h.x + 4, h.y + 4); await p.waitForTimeout(500);  // (4 px off the handle)
    const ghost = await p.evaluate(() => !!document.querySelector('#ghost').firstChild);
    ok(ghost, 'held: the ghost shows');
    const s2 = await p.evaluate(() => scrollY);
    for (let k = 1; k <= 8; k++) { await tp('touchMove', h.x + 4, h.y + 4 - 6 * k); await p.waitForTimeout(16); }
    await p.screenshot({ path: process.argv[2] ? process.argv[2] + '/hold-390.png' : '/dev/null' }).catch(() => {});
    await tp('touchEnd'); await p.waitForTimeout(800);
    const s3 = await p.evaluate(() => scrollY), t1 = await target();
    ok(s3 === s2, `held drag: the page stays (${s2} -> ${s3})`);
    ok(t1 !== t0, `held drag changes the target: ${t1}`);
    ok(errs.length === 0, 'no page errors ' + errs.join(' | '));
    await ctx.close();
  }
  // desktop mouse: at once
  {
    const ctx = await b.newContext({ viewport: { width: 1280, height: 900 }, colorScheme: 'dark' });
    const p = await ctx.newPage(); const errs = []; p.on('pageerror', e => errs.push(e.message));
    await p.goto('http://127.0.0.1:8790/demo/#/'); await p.waitForTimeout(3500);
    await p.evaluate(() => { setView('prog'); }); await p.waitForTimeout(1200);
    const t0 = await p.evaluate(() => J(editProg().phases.map(x => [x.b && x.b.t, x.t && x.t.t])));
    const h = await p.evaluate(() => { const e = [...document.querySelectorAll('#chartbox [data-h]')].find(e => handles[+e.dataset.h].type === 'target'); const r = e.getBoundingClientRect(); return { x: (r.left + r.right) / 2, y: (r.top + r.bottom) / 2 }; });
    await p.mouse.move(h.x, h.y); await p.mouse.down(); await p.mouse.move(h.x, h.y - 40, { steps: 6 }); await p.mouse.up(); await p.waitForTimeout(600);
    const t1 = await p.evaluate(() => J(editProg().phases.map(x => [x.b && x.b.t, x.t && x.t.t])));
    ok(t1 !== t0, 'mouse drag moves a target at once');
    ok(errs.length === 0, 'no page errors ' + errs.join(' | '));
    await ctx.close();
  }
  await b.close();
  console.log(fails ? `${fails} FAILED` : 'all ok'); process.exit(fails ? 1 : 0);
})();
