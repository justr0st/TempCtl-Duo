// Wiki pictures of the browser demo (docs/wiki/Demo.md): the demo page from a built site (tools/build_site.py
// <dir> --web with demo/out/tempctl-demo.wasm), served on <base>; runs the first example program at 10x and
// takes the dashboard with the demo panel open, dark and light.
//   node tools/wiki/shots_demo.js http://127.0.0.1:8790 <out dir>      (tools/wiki/make_shots.sh demo)
const { chromium } = require('playwright');
const path = require('path');
const BASE = process.argv[2] || 'http://127.0.0.1:8790';
const OUT = process.argv[3] || '.';

(async () => {
  const b = await chromium.launch();
  for (const scheme of ['dark', 'light']) {
    const ctx = await b.newContext({ viewport: { width: 1280, height: 900 }, colorScheme: scheme });
    const p = await ctx.newPage();
    await p.goto(BASE + '/demo/');
    await p.waitForSelector('.dm-chip');
    await p.waitForTimeout(2000);
    await p.click('.dm-chip');
    await p.click('.dm-sp button[data-s="10"]');
    await p.click('#bStart');
    // to the top heater's ramp (Continue at the "Action required" pause)
    for (let i = 0; i < 90; i++) {
      await p.waitForTimeout(1000);
      if (await p.isVisible('#hCont')) await p.click('#hCont', { timeout: 1500 }).catch(() => {});
      const t = await p.textContent('#clock');
      if (/^7:[0-9]{2}$/.test(t)) break;
    }
    await p.click('.dm-sp button[data-s="1"]');
    await p.waitForTimeout(1500);
    await p.screenshot({ path: path.join(OUT, `demo-${scheme}.png`) });
    console.log(path.join(OUT, `demo-${scheme}.png`));
    await ctx.close();
  }
  await b.close();
})();
