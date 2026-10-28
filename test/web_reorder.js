// Reorder phases / steps on the web page (1.0.64): drag by the grip (mouse at 1280 px, touch at 390 px), Alt+Up /
// Down, undo, Escape, no grips while a program runs. Needs the browser demo on :8790 (tools/build_site.py <dir>
// --web, python3 -m http.server 8790 --directory <dir>).   node test/web_reorder.js [shots dir]
const { chromium } = require('playwright');
const OUT = process.argv[2] || '';
let fails = 0;
const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) fails++; };
(async () => {
  const b = await chromium.launch();
  for (const [W, H, touch] of [[1280, 900, false], [390, 844, true]]) {
    const ctx = await b.newContext({ viewport: { width: W, height: H }, colorScheme: 'dark', hasTouch: touch, isMobile: touch });
    const p = await ctx.newPage();
    const errs = []; p.on('pageerror', e => errs.push(e.message));
    await p.goto('http://127.0.0.1:8790/demo/#/'); await p.waitForTimeout(3500);
    await p.evaluate(() => { const s = document.querySelector('#edP'); s && s.scrollIntoView(); });
    await p.waitForTimeout(400);
    const kinds = () => p.evaluate(() => { const p = editProg(); return p.type === 'seq' ? p.phases.map(x => x.kind + (x.b ? x.b.t : '') + '/' + (x.t ? x.t.t : '')) : [p.B.map(x => x.t), p.T.map(x => x.t)]; });
    const rows = () => p.$$('#editor [data-i]');
    const before = await kinds();
    const n = before.length;
    ok(n >= 3, `${W}px: sequence with ${n} phases`);
    ok((await p.$$('#editor .grip')).length === n, `${W}px: a grip per phase`);
    // drag phase 1 below phase 3 (pointer: mouse; touch: CDP touch events)
    await (await rows())[0].evaluate(e => { e.scrollIntoView({ block: 'start' }); scrollBy(0, -80); }); await p.waitForTimeout(200);  // (rows 1-3 in view: no edge scrolling)
    const r = await rows(); const g0 = await r[0].$('.grip'); const gb = await g0.boundingBox(); const tb = await r[2].boundingBox();
    const x = gb.x + gb.width / 2, y0 = gb.y + gb.height / 2, y1 = tb.y + tb.height * 0.75;
    if (touch) {
      const c = await ctx.newCDPSession(p);
      const tp = (type, y) => c.send('Input.dispatchTouchEvent', { type, touchPoints: type === 'touchEnd' ? [] : [{ x, y }] });
      await tp('touchStart', y0);
      for (let k = 1; k <= 12; k++) { await tp('touchMove', y0 + (y1 - y0) * k / 12); await p.waitForTimeout(25); }
      if (OUT) await p.screenshot({ path: `${OUT}/drag-${W}.png` });
      await tp('touchEnd', y1);
    } else {
      await p.mouse.move(x, y0); await p.mouse.down();
      for (let k = 1; k <= 12; k++) { await p.mouse.move(x, y0 + (y1 - y0) * k / 12); await p.waitForTimeout(25); }
      if (OUT) await p.screenshot({ path: `${OUT}/drag-${W}.png` });
      await p.mouse.up();
    }
    await p.waitForTimeout(300);
    const after = await kinds();
    ok(J(after) === J([before[1], before[2], before[0], ...before.slice(3)]), `${W}px: drag 1 -> 3: ${J(after)}`);
    ok(await p.evaluate(() => document.querySelector('#unsaved').classList.contains('on')), `${W}px: "Unsaved changes" shown`);
    // undo
    await p.evaluate(() => toolAct('undo')); await p.waitForTimeout(200);
    ok(J(await kinds()) === J(before), `${W}px: undo restores the order`);
    // Escape cancels a drag
    {
      const r2 = await rows(); const gg = await (await r2[0].$('.grip')).boundingBox(); const t2 = await r2[2].boundingBox();
      await p.mouse.move(gg.x + 5, gg.y + 5); await p.mouse.down(); await p.mouse.move(gg.x + 5, t2.y + t2.height, { steps: 8 });
      await p.keyboard.press('Escape'); await p.mouse.up(); await p.waitForTimeout(200);
      ok(J(await kinds()) === J(before), `${W}px: Escape cancels`);
      ok(await p.evaluate(() => [...document.querySelectorAll('#editor [data-i]')].every(x => !x.style.transform)), `${W}px: rows back in place`);
    }
    // keyboard: Alt+Down on phase 1's grip
    await (await (await rows())[0].$('.grip')).focus(); await p.keyboard.press('Alt+ArrowDown'); await p.waitForTimeout(200);
    ok(J(await kinds()) === J([before[1], before[0], ...before.slice(2)]), `${W}px: Alt+Down moves 1 -> 2`);
    ok(await p.evaluate(() => document.activeElement && document.activeElement.closest('[data-i]') && document.activeElement.closest('[data-i]').dataset.i === '1'), `${W}px: the grip keeps the focus`);
    await p.evaluate(() => toolAct('undo')); await p.waitForTimeout(200);
    // Timeline: steps of one channel
    await p.evaluate(() => { newDraft(live.sel, 'tl'); const q = DRAFT[live.sel]; q.B = [newStep(), newStep(), newStep()]; q.B.forEach((s, k) => s.t = 100 + k); afterEdit(); });
    await p.waitForTimeout(300);
    {
      const rb = await p.$$('#editor tr[data-k="B"]'); ok(rb.length === 3, `${W}px: timeline bottom steps`);
      const gg = await (await rb[2].$('.grip')).boundingBox(); const t0 = await rb[0].boundingBox();
      await p.mouse.move(gg.x + 5, gg.y + 5); await p.mouse.down(); await p.mouse.move(gg.x + 5, t0.y + 4, { steps: 10 }); await p.mouse.up(); await p.waitForTimeout(300);
      const tl = await kinds(); ok(J(tl[0]) === J([102, 100, 101]), `${W}px: timeline drag 3 -> 1: ${J(tl[0])}`);
      if (OUT) await p.screenshot({ path: `${OUT}/timeline-${W}.png` });
      await p.evaluate(() => { toolAct('undo'); toolAct('undo'); });
    }
    // no grips while a program runs
    const st = await p.evaluate(async () => J(await api('/api/run', { cmd: 'start', i: live.sel, program: denormProg(DRAFT[live.sel]) }))); await p.waitForTimeout(2500);
    if (!(await p.evaluate(() => running()))) console.log('start:', st);
    ok(await p.evaluate(() => running()) && (await p.$$('#editor .grip')).length === 0, `${W}px: no grips while running`);
    await p.evaluate(() => api('/api/run', { cmd: 'stop' })); await p.waitForTimeout(1500);
    if (OUT) { await p.evaluate(() => { const s = document.querySelector('#edP'); s && s.scrollIntoView(); }); await p.waitForTimeout(300); await p.screenshot({ path: `${OUT}/editor-${W}.png` }); }
    ok(errs.length === 0, `${W}px: no page errors ${errs.join(' | ')}`);
    await ctx.close();
  }
  await b.close();
  console.log(fails ? `${fails} FAILED` : 'all ok');
  process.exit(fails ? 1 : 0);
})();
function J(o) { return JSON.stringify(o); }
