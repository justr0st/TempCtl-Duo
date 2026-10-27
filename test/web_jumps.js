// Layout jump check of the web page (1.0.61): the browser demo is served on :8790 (tools/build_site.py <dir> --web,
// python3 -m http.server 8790 --directory <dir>); samples the page position and height of every panel each 250 ms
// while idle, while editing a program (live and program view), during manual heating and a run at 10x, on every
// page and Channels tab. Prints "stable" or what moved.   node test/web_jumps.js [width height]   (default 1280 900)
const { chromium } = require('playwright');
const W = +(process.argv[2]||1280), H = +(process.argv[3]||900);
(async () => {
  const b = await chromium.launch({ignoreDefaultArgs:['--hide-scrollbars']});
  const ctx = await b.newContext({viewport:{width:W,height:H}, colorScheme:'dark'}); const p = await ctx.newPage();
  const errs=[]; p.on('pageerror',e=>errs.push(e.message));
  await p.goto('http://127.0.0.1:8790/demo/'); await p.waitForTimeout(3500);
  const snap = () => p.evaluate(()=>{ const pg=document.querySelector('.pg:not([hidden])'); const o={sh:document.documentElement.scrollHeight};
    const els=[...pg.querySelectorAll(':scope > *, .panel, #chartbox, #legend, .chan, #edP, #edTbl, .ctab-panel, .ccards, #log, #devInfo')];
    els.forEach((e,i)=>{ if(e.offsetParent===null) return; const r=e.getBoundingClientRect(); o[(e.id||e.className||e.tagName).toString().slice(0,30)+'#'+i]=Math.round(r.top+scrollY)+'/'+Math.round(r.height); }); return o; });
  const watch = async (name, ms, act) => {
    let prev = await snap(); const changes = {};
    const t0 = Date.now(); let k = 0;
    while (Date.now()-t0 < ms) {
      if (act) await act(k++); await p.waitForTimeout(250);
      const s = await snap();
      for (const key in s) if (prev[key]!==undefined && prev[key]!==s[key]) (changes[key]=changes[key]||new Set()).add(prev[key]+'→'+s[key]);
      prev = s;
    }
    const keys = Object.keys(changes);
    console.log(`${name}: ${keys.length? keys.map(k=>k+' '+[...changes[k]].slice(0,4).join(',')).join(' | ') : 'stable'}`);
  };
  const go = async h => { await p.goto('http://127.0.0.1:8790/demo/'+h); await p.waitForTimeout(2500); await p.evaluate(()=>window.scrollTo(0, 400)); };
  await go('#/');
  await watch('dash live idle', 5000);
  await watch('dash live + editor edits', 6000, async k => { const acts=[
    ()=>document.querySelector('[data-act="undo"]')?.click(),
    ()=>document.querySelector('#edP input[data-f="wait"]')?.click(),
    ()=>document.querySelector('#edP input[data-f="wait"]')?.click(),
    ()=>{ const i=document.querySelector('#edP input[data-f="b.t"]'); if(i){ i.value=String(+i.value+1); i.dispatchEvent(new Event('change',{bubbles:true})); } }];
    await p.evaluate(acts[k%acts.length].toString().replace(/^\(\)=>/,'')).catch(()=>{}); });
  await p.click('#vProg'); await p.waitForTimeout(1500);
  await watch('dash prog idle', 4000);
  await watch('dash prog + edits', 5000, async k => { await p.evaluate(()=>{ const i=document.querySelector('#edP input[data-f="b.t"]'); if(i){ i.value=String(+i.value+1); i.dispatchEvent(new Event('change',{bubbles:true})); } }); });
  await p.evaluate(()=>{ const b=document.querySelector('[data-s="10"]'); if(b) b.click(); api('/api/manual',{ch:0,on:true,sp:150}); });
  await p.click('#vLive'); await p.waitForTimeout(1000);
  await watch('dash live, manual heating 10x', 8000);
  await p.evaluate(()=>api('/api/manual',{ch:0,on:false}));
  await p.evaluate(()=>api('/api/cmd',{cmd:'start',prog:live.sel})); await p.waitForTimeout(2500); await p.evaluate(()=>window.scrollTo(0,400));
  await watch('dash run (prog view) 10x', 12000);
  await p.evaluate(()=>window.scrollTo(0,0));
  await watch('dash run top 10x', 6000);
  await p.evaluate(()=>api('/api/cmd',{cmd:'stop'})); await p.waitForTimeout(1500);
  await watch('dash after stop', 4000);
  for (const h of ['#/programs','#/channels','#/settings','#/log','#/replay']) { await go(h); await watch(h, 4000); }
  await go('#/channels'); for (const i of [1,2]) { await p.click(`#chTabs button[data-cht="${i}"]`); await p.waitForTimeout(800); await watch('channels tab '+i, 3000); }
  console.log(errs.join('\n')||'no errors'); await b.close();
})();
