/* ---------- chart drawing (from the 9.3 simulator) ---------- */
let CW=1000,CHh=452,CL=58,CR=18,PT=86,PB=410,YMAX=250,YMIN=0; const LANE=[22,46],LH=18;
let xfull=600,V0=0,V1=600,lastChart=0; const zv={fit:true,v0:0,span:600,follow:true};
function layout(){ const w=Math.floor($('chartbox').clientWidth)||1000; CW=Math.max(280,w); const n=CW<600;
  CL=n?50:58; CR=n?10:18; const lv=M&&M.kind==='live'; CHh=(n?340:Math.round(Math.min(460,Math.max(380,CW*0.42))))+(lv?0:16); PT=lv?20:n?118:102; PB=CHh-40; }
const plotW=()=>CW-CL-CR;
const X=t=>CL+plotW()*(t-V0)/(V1-V0);
const Y=v=>PB-(PB-PT)*(v-YMIN)/(YMAX-YMIN);
const tAtX=x=>V0+(x-CL)/plotW()*(V1-V0);
function setDomain(){
  if(M.kind==='live'){ const a=M.head?M.head.t:0; V0=M.t0+a; V1=a; return; }
  if(zv.fit||zv.span>=xfull){ zv.fit=true; V0=0; V1=xfull; return; }
  if(M.now!=null&&M.kind==='run'&&zv.follow&&M.now>zv.v0+zv.span*.85) zv.v0=M.now-zv.span*.3;
  zv.v0=Math.max(0,Math.min(zv.v0,xfull-zv.span)); V0=zv.v0; V1=zv.v0+zv.span;
}
function zoomAt(t,f){ if(!M||M.kind==='live') return; const sp0=zv.fit?xfull:zv.span, v0=zv.fit?0:zv.v0, span=Math.max(20,Math.min(xfull,sp0*f)), r=(t-v0)/sp0;
  zv.fit=span>=xfull; zv.span=span; zv.v0=t-r*span; afterView(); }
function afterView(){ if(!zv.fit){ zv.v0=Math.max(0,Math.min(zv.v0,xfull-zv.span)); zv.follow=M&&M.kind==='run'&&M.now>=zv.v0&&M.now<=zv.v0+zv.span; } lastChart=0; }
// more points than the chart has pixels: per pixel column only the first, lowest, highest and last (in time
// order), so the drawing stays light and no peak is lost; zoomed in, every point
function thin(pts){ const W=plotW(); if(pts.length<=W*3) return pts;
  const out=[]; let col=null,g=[],before=null;
  const flush=()=>{ if(!g.length) return; let lo=g[0],hi=g[0]; for(const p of g){ if(p[1]<lo[1]) lo=p; if(p[1]>hi[1]) hi=p; }
    const keep=[g[0],lo,hi,g[g.length-1]].filter((p,i,a)=>a.indexOf(p)===i).sort((a,b)=>a[0]-b[0]); out.push(...keep); g=[]; };
  for(const p of pts){ const c=Math.floor(X(p[0]));
    if(c<CL-2){ before=p; continue; }                       // left of the view: only the nearest point (the line enters)
    if(before){ out.push(before); before=null; }
    if(c>CL+W+2){ flush(); out.push(p); break; }           // right of it: one point (the line leaves), then stop
    if(p[1]==null){ flush(); out.push(p); col=null; continue; }
    if(c!==col){ flush(); col=c; } g.push(p); }
  flush(); return out; }
// the newest value appended to a line (points in between: the live view breaks a line at gaps longer than M.gap,
// and in the demo at 5x / 10x the newest value is up to 20 device seconds ahead of the history)
const joinHead=(pts,t,v)=>{ const L=pts[pts.length-1], out=pts.slice(), st=(M.kind==='live'?M.gap:Infinity)*0.9;
  for(let x=L[0]+st;x<t;x+=st) out.push([x,L[1]+(v-L[1])*(x-L[0])/(t-L[0])]); out.push([t,v]); return out; };
const pathOf=pts0=>{ let d='',pen=false,lt=null; const gap=M.kind==='live'?M.gap:Infinity, pts=thin(pts0);
  for(const [t,v] of pts){ if(v==null){pen=false;continue;} if(lt!=null&&t-lt>gap) pen=false; d+=(pen?'L':'M')+X(t).toFixed(1)+','+Y(v).toFixed(1); pen=true; lt=t; } return d; };
// closed area between a line (same gaps as pathOf) and the bottom of the chart
const areaOf=pts=>{ let d='',run=[],lt=null; const gap=M.kind==='live'?M.gap:Infinity, b=Y(YMIN).toFixed(1);
  const flush=()=>{ if(run.length>1) d+='M'+run[0][0]+','+b+run.map(p=>'L'+p[0]+','+p[1]).join('')+'L'+run[run.length-1][0]+','+b+'Z'; run=[]; };
  for(const [t,v] of pts){ if(v==null){flush();lt=null;continue;} if(lt!=null&&t-lt>gap) flush(); run.push([X(t).toFixed(1),Y(v).toFixed(1)]); lt=t; }
  flush(); return d; };
function segPath(segs){ let d='',lx=null,ly=null; for(const s of segs){ if(s.fl&1||s.v0==null){lx=null;continue;} const x0=X(s.t0),y0=Y(s.v0),x1=X(s.t1),y1=Y(s.v1);
  d+=(lx!=null&&Math.abs(lx-x0)<.5&&Math.abs(ly-y0)<.5?'L':'M')+x0.toFixed(1)+','+y0.toFixed(1)+'L'+x1.toFixed(1)+','+y1.toFixed(1); lx=x1; ly=y1; } return d; }
function tickStep(){ const span=V1-V0,px=plotW(); for(const st of [5,10,15,30,60,120,300,600,900,1800,3600,7200,10800,14400,21600,43200]) if(px/(span/st)>=64) return st; return 86400; }
const pauseSym=(x,y)=>`<g><circle cx="${x}" cy="${y}" r="11" style="fill:var(--amber)"/><rect x="${x-5}" y="${y-5.5}" width="3.5" height="11" rx="1" style="fill:var(--panel)"/><rect x="${x+1.5}" y="${y-5.5}" width="3.5" height="11" rx="1" style="fill:var(--panel)"/></g>`;
const LANE_ST={x:['url(#hatch)',.9],p:['var(--muted)',.28],r:[null,.35],h:[null,.85],j:[null,.6],k:['var(--muted)',.28],w:['var(--amber)',.85],m:[null,.2],d:['var(--muted)',.15]};
function lanesSvg(i){ let o=''; for(const r of M.lanes[i]){ const m=LANE_ST[r.c]; if(!m) continue; const x0=X(r.t0),w=Math.max(X(r.t1)-x0,.5);
  o+=`<rect x="${x0.toFixed(1)}" y="${LANE[i]}" width="${w.toFixed(1)}" height="${LH}" style="fill:${m[0]||PV[i]};opacity:${m[1]*(r.f?.7:1)}"/>`; } return o; }
let YSTEP=50;
function yRange(){ let mx=0; const see=v=>{ if(v!=null) mx=Math.max(mx,v); }, lv=M.kind==='live';
  const inView=p=>!lv||p[0]>=M.t0;  // live: a short period shows only the newest points
  for(const i of CH){ if(!lv||LVIS.pv[i]) M.hist[i].pv.forEach(p=>{ if(inView(p)) see(p[1]); }); if(!lv||LVIS.sp[i]) M.hist[i].sp.forEach(p=>{ if(inView(p)) see(p[1]); }); M.segs[i].forEach(s=>{see(s.v0);see(s.v1);}); }
  if(M.hist[MON]&&(!lv||LVIS.pv[MON])) M.hist[MON].pv.forEach(p=>{ if(inView(p)) see(p[1]); });
  YMIN=0;
  if(M.kind==='live'){ // always 0 .. the highest value in view (incl. the newest reading), on a round step
    if(M.head) M.head.v.forEach((v,i)=>{ if(LVIS.pv[i]&&(i<MON||M.hist[MON])) see(v); });
    const top=Math.max(mx*1.06,mx+2,10); YSTEP=[1,2,5,10,20,25,50,100,200,250,500].find(st=>top/st<=7)||1000; YMAX=Math.ceil(top/YSTEP)*YSTEP; return; }
  YMAX=Math.max(100,Math.ceil((mx+15)/50)*50); YSTEP=YMAX>300?100:50; }
let CHG={};  // the chart parts' current text (renderChart)
function chartPart(id,t){ if(CHG[id]!==t){ CHG[id]=t; $(id).innerHTML=t; } }
function renderChart(){
  const box=$('chartbox');
  if(!M){ box.innerHTML=`<p class="zr" style="padding:40px 8px">${live&&progs?'The selected program slot is empty.':'Loading…'}</p>`; $('zbar').hidden=true; return; }
  layout();
  xfull=M.kind==='live'?-M.t0:Math.max(60,Math.ceil(Math.max(M.end,M.now||0)*1.04/30)*30);
  setDomain(); yRange();
  let s='';  // axes (grid, scales, lane frames), then (clipped) the run's lanes and events, then the data
  for(let v=YMIN;v<=YMAX+1e-9;v+=YSTEP) s+=`<line x1="${CL}" x2="${CW-CR}" y1="${Y(v)}" y2="${Y(v)}" style="stroke:var(--line)"/><text x="${CL-6}" y="${Y(v)+4}" text-anchor="end" style="fill:var(--muted);font-size:12px">${v}</text>`;
  s+=`<text x="${CL-6}" y="${PT-8}" text-anchor="end" style="fill:var(--muted);font-size:12px">°C</text>`;
  const step=tickStep(), tl=a=>(step>=3600&&a?a/3600+' h':fmt(a));
  if(M.kind==='live'){
    // live: the marks are times before now, so they stay where they are while the curves move left (counted
    // from the right edge = now); solid vertical grid lines at them, as many as the width and period allow
    for(let k=0;V1-k*step>=V0-1e-6;k++){ const x=X(V1-k*step);
      if(k) s+=`<line x1="${x.toFixed(1)}" x2="${x.toFixed(1)}" y1="${PT}" y2="${PB}" style="stroke:var(--line)"/>`;
      s+=`<text x="${x.toFixed(1)}" y="${PB+18}" text-anchor="${k?'middle':'end'}" style="fill:var(--muted);font-size:12px">${k?'−'+tl(k*step):'now'}</text>`; }
  } else for(let t=Math.ceil(V0/step)*step;t<=V1+1e-6;t+=step) s+=`<text x="${X(t)}" y="${PB+18}" text-anchor="${X(t)>CW-18?'end':'middle'}" style="fill:var(--muted);font-size:12px">${(t<0?'−':'')+tl(Math.abs(t))}</text>`;
  const prog=M.kind!=='live', lv=!prog;
  if(legendKind!==(lv?'live':'prog')+(monLive()?'m'+live.mon.name:'')) legend(); else monLegend();
  if(prog){ for(const i of CH){ s+=`<text x="${CL-6}" y="${LANE[i]+13}" text-anchor="end" style="fill:${PV[i]};font-size:${CW<600?11:12}px;font-weight:600">${esc(chName(i).slice(0,CW<600?6:9))}</text><rect x="${CL}" y="${LANE[i]}" width="${plotW()}" height="${LH}" style="fill:none;stroke:var(--line)"/>`; }
    // separator between the state lanes and the chart
    s+=`<line x1="4" x2="${CW-4}" y1="${LANE[1]+LH+10}" y2="${LANE[1]+LH+10}" style="stroke:var(--line);stroke-width:2"/>`; }
  const sAxes=s; s='';
  if(prog){
    for(const i of CH) s+=lanesSvg(i);
    const evs=M.ev.slice().sort((a,b)=>a.t-b.t);
    // program pauses: band while it lasted, thick line and pause symbol at its start
    evs.forEach((e,n)=>{ if(e.type!==EV.PAUSE) return; const c=evs.slice(n+1).find(o=>o.type===EV.CONT||o.type===EV.END||o.type===EV.STOP);
      const t1=c?c.t:(e.past&&M.now!=null?M.now:e.t);
      if(t1-e.t>.6) s+=`<rect x="${X(e.t)}" y="${LANE[0]}" width="${Math.max(0,X(t1)-X(e.t))}" height="${PB-LANE[0]}" style="fill:url(#hatch);opacity:.12"/>`;
      s+=`<line x1="${X(e.t)}" x2="${X(e.t)}" y1="${LANE[0]-4}" y2="${PB}" style="stroke:var(--amber);stroke-width:4"/>`+pauseSym(X(e.t),PB-18); });
    const seen=new Set(); let lastX=-99,row=0;
    for(const e of evs){ if(e.type!==EV.PHASE||seen.has(e.idx)) continue; seen.add(e.idx); const x=X(e.t); const nw=CW<600; row=x-lastX<(nw?26:36)?row+1:0; lastX=x; const dy=nw?(row%3)*13-26:(row%3)*26;
      s+=`<line x1="${x}" x2="${x}" y1="${LANE[0]-4}" y2="${PB}" style="stroke:var(--muted);stroke-dasharray:2 4;opacity:.6"/><text x="${x+3}" y="${PT-8+dy}" style="fill:var(--muted);font-size:12px;font-weight:600;paint-order:stroke;stroke:var(--panel);stroke-width:3px">P${e.idx+1}</text>`;
      if(e.idx>0&&!nw) s+=`<text x="${x+3}" y="${PT+7+dy}" style="fill:var(--text);font-size:11.5px;paint-order:stroke;stroke:var(--panel);stroke-width:3px">${fmt(e.t)}</text>`; }
    const endE=evs.find(e=>e.type===EV.END||e.type===EV.STOP), endT=endE?endE.t:(M.kind==='last'?null:M.end);
    if(endT!=null&&endT>0) s+=`<line x1="${X(endT)}" x2="${X(endT)}" y1="${LANE[0]-4}" y2="${PB}" style="stroke:var(--text);stroke-dasharray:4 3;opacity:.7"/><text x="${X(endT)+(X(endT)>CW-44?-3:3)}" y="${PT-8}" text-anchor="${X(endT)>CW-44?'end':'start'}" style="fill:var(--muted);font-size:12px;font-weight:600;paint-order:stroke;stroke:var(--panel);stroke-width:3px">${endE&&endE.type===EV.STOP?'Stop':'End'}</text><text x="${X(endT)+(X(endT)>CW-44?-3:3)}" y="${PT+8}" text-anchor="${X(endT)>CW-44?'end':'start'}" style="fill:var(--text);font-size:11.5px;paint-order:stroke;stroke:var(--panel);stroke-width:3px">${fmt(endT)}</text>`;
    if(M.prog&&M.prog.type==='tl'){ const done=new Set();
      for(const e of evs){ if(e.type!==EV.HOLD_END) continue; const k=e.ch+':'+e.idx; if(done.has(k)) continue; done.add(k);
        const x=(M.prog[KEY[e.ch]]||[])[e.idx]; if(!x||x.kind==='pause') continue; const v=x.kind==='idle'?YMIN+5:x.t;
        s+=`<text x="${X(e.t)+6}" y="${Y(v)-11}" style="fill:var(--text);font-size:11.5px;paint-order:stroke;stroke:var(--panel);stroke-width:3px">${fmt(e.t)}</text>`; } }
  }
  const sBack=s; s='';
  const near=Math.max(8*devRate(),(M.bin||0)*2.5);  // the newest reading joins the history's last point when that is this close
  // (the history comes every 2 s: in the demo at 5x / 10x that is 10 / 20 device seconds)
  for(const i of CH){
    const shSp=!lv||LVIS.sp[i], shPv=!lv||LVIS.pv[i];
    // SP: half strength with the area under it lightly filled (the temperature line stays on top); the
    // newest setpoint from the last poll extends it, so a ramp moves on every poll, not only with the history
    let sp=M.hist[i].sp; const hs=M.head&&M.head.sp?M.head.sp[i]:null;
    if(hs!=null&&sp.length){ const L=sp[sp.length-1]; if(M.head.t>L[0]&&M.head.t-L[0]<near) sp=joinHead(sp,M.head.t,hs); }
    if(shSp) s+=`<path d="${areaOf(sp)}" style="fill:${SP[i]};opacity:.2;stroke:none"/>`;
    if(shSp) s+=`<path d="${pathOf(sp)}" style="fill:none;stroke:${SP[i]};stroke-width:1.8;stroke-linejoin:round;opacity:.5"/>`;
    if(shPv) s+=`<path d="${pathOf(M.hist[i].pv)}" style="fill:none;stroke:${PV[i]};stroke-width:2.5;stroke-linejoin:round"/>`;
    // newest reading (polled faster than the chart history is fetched): line from the last point + dot
    const hp=M.hist[i].pv, hv=shPv&&M.head&&!(M.rawOn&&M.rawOn[i])?M.head.v[i]:null;
    if(hv!=null){ const L=hp.length?hp[hp.length-1]:null;
      if(L&&M.head.t-L[0]<near) s+=`<path d="M${X(L[0]).toFixed(1)} ${Y(L[1]).toFixed(1)}L${X(M.head.t).toFixed(1)} ${Y(hv).toFixed(1)}" style="fill:none;stroke:${PV[i]};stroke-width:2.5"/>`;
      s+=`<circle cx="${X(M.head.t).toFixed(1)}" cy="${Y(hv).toFixed(1)}" r="3.5" style="fill:${PV[i]}"/>`; }
    s+=`<path d="${segPath(M.segs[i])}" style="fill:none;stroke:${SP[i]};stroke-width:2;stroke-dasharray:6 4;opacity:.95"/>`;
  }
  // the monitor channel: its PV only (accent colour), with the newest reading as for the channels
  if(M.hist[MON]&&(!lv||LVIS.pv[MON])){ const hp=M.hist[MON].pv, c=PV[MON];
    s+=`<path d="${pathOf(hp)}" style="fill:none;stroke:${c};stroke-width:2.2;stroke-linejoin:round"/>`;
    const hv=M.head&&!(M.rawOn&&M.rawOn[MON])?M.head.v[MON]:null;
    if(hv!=null){ const L=hp.length?hp[hp.length-1]:null;
      if(L&&M.head.t-L[0]<near) s+=`<path d="M${X(L[0]).toFixed(1)} ${Y(L[1]).toFixed(1)}L${X(M.head.t).toFixed(1)} ${Y(hv).toFixed(1)}" style="fill:none;stroke:${c};stroke-width:2.2"/>`;
      s+=`<circle cx="${X(M.head.t).toFixed(1)}" cy="${Y(hv).toFixed(1)}" r="3.5" style="fill:${c}"/>`; } }
  if(M.now!=null&&M.kind==='run') s+=`<line x1="${X(M.now)}" x2="${X(M.now)}" y1="${LANE[0]-4}" y2="${PB}" style="stroke:var(--text);stroke-width:1.5"/>`;
  handles=buildHandles(); if(handles.length) s+=drawHandles(handles);
  // The svg and its fixed parts stay; a part's elements are replaced only when its text changed (a poll
  // usually changes only the data paths), so the browser re-parses and re-lays out much less.
  const sk=`${CW},${CHh},${CL},${CR}`;
  if(!box.querySelector('svg')||box.dataset.sk!==sk){ box.dataset.sk=sk; CHG={};
    box.innerHTML=`<svg width="${CW}" height="${CHh}" viewBox="0 0 ${CW} ${CHh}" role="img" aria-label="Setpoints and temperatures of both channels over time">`+
      `<defs><pattern id="hatch" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="3" height="6" style="fill:var(--amber)"/></pattern><clipPath id="pclip"><rect x="${CL}" y="0" width="${plotW()}" height="${CHh}"/></clipPath></defs>`+
      `<g id="chAxes"></g><g clip-path="url(#pclip)"><g id="chBack"></g><g id="chData"></g><g id="chHover"></g></g><g id="ghost"></g></svg><div class="ctip" id="ctip" hidden></div>`; }
  chartPart('chAxes',sAxes); chartPart('chBack',sBack); chartPart('chData',s); $('ghost').textContent='';
  drawHover();
  const zs=$('zscroll'); $('zbar').hidden=!prog; zs.hidden=zv.fit; $('zfit').disabled=zv.fit;
  if(!zv.fit){ zs.max=Math.max(0,xfull-zv.span); zs.step=1; if(document.activeElement!==zs) zs.value=zv.v0; }
  $('zrange').textContent=zv.fit?'Whole program':`${fmt(V0)} – ${fmt(V1)}`;
  $('lmWrap').hidden=!lv; $('histCsv').hidden=!lv;
  const lastRun=M.kind==='last'; $('runCsv').hidden=!lastRun; $('bPreview').hidden=!lastRun;  // after a run: its CSV, back to the preview
  $('bLastRun').hidden=M.kind!=='preview'||!lastRunOfSel();  // the preview of the program whose run the device still has: back to that run
  $('chartCap').textContent=lv?(M.have<M.cap?`${fmt(M.have)} recorded`:`memory holds ${fmt(M.cap)}`):M.kind==='preview'?`Preview of “${M.prog?M.prog.name:''}”${hasWaits(M.prog)?' (waits not counted)':''}`:M.kind==='last'?'Last run':'';
}
// Live view: PV / SP of each channel can be switched off (chart, scale and CSV export; kept by the browser)
let LVIS={pv:[1,1,1],sp:[1,1]}; try{ const v=JSON.parse(localStorage.getItem('lvis')); if(v&&v.pv&&v.sp) LVIS=v; }catch(e){}
if(LVIS.pv[MON]==null) LVIS.pv[MON]=1;  // (kept before the monitor channel existed)
const lvMask=k=>CH.reduce((m,i)=>m|(LVIS[k][i]?1<<i:0),0)|(k==='pv'&&monLive()&&LVIS.pv[MON]?1<<MON:0);
function histHref(){ const a=$('histCsv'), p=lvMask('pv'), s=lvMask('sp'); a.href=`/api/history.csv?pv=${p}&sp=${s}`;
  a.classList.toggle('dis',!(p|s)); a.setAttribute('aria-disabled',!(p|s)); }
let legendKind='';
// the monitor channel in the legend: its name and current reading (monLegend() updates the value each poll)
const monVal=()=>live&&live.mon?(live.mon.pv!=null?f1(live.mon.pv)+' °C':(live.mon.err||'–')):'–';
function monLegend(){ const e=$('monLegV'); if(e) e.textContent=monVal(); }
function legend(){ const n=[chName(0),chName(1)]; legendKind=(M&&M.kind==='live'?'live':'prog')+(monLive()?'m'+live.mon.name:'');
  const mon=monLive()?esc(chName(MON)):null;
  if(legendKind.startsWith('live')){ histHref();
    $('legend').innerHTML=`<span class="lh">Show / export:</span>`+CH.map(i=>['pv','sp'].map(k=>`<button class="lt" data-lv="${k}${i}" aria-pressed="${!!LVIS[k][i]}"><i class="sw" style="background:${k==='pv'?PV[i]:SP[i]}${k==='sp'?';height:4px':''}"></i>${esc(n[i])} ${k.toUpperCase()}</button>`).join('')).join('')
      +(mon?`<button class="lt" data-lv="pv${MON}" aria-pressed="${!!LVIS.pv[MON]}"><i class="sw" style="background:${PV[MON]}"></i>${mon} <b class="lv" id="monLegV">${esc(monVal())}</b></button>`:'');
    return; }
  $('legend').innerHTML=CH.map(i=>`<span><i class="sw" style="background:${PV[i]}"></i>${esc(n[i])} PV</span><span><i class="sw" style="background:${SP[i]};height:4px"></i>${esc(n[i])} SP</span>`).join('')+
  (mon?`<span><i class="sw" style="background:${PV[MON]}"></i>${mon} <b class="lv" id="monLegV">${esc(monVal())}</b></span>`:'')+
  `<details class="lgx" id="lgx" ${lgxOpen()?'open':''}><summary>Chart key</summary><div class="legend"><span><svg width="22" height="10" aria-hidden="true"><line x1="0" y1="5" x2="22" y2="5" stroke="var(--muted)" stroke-width="2" stroke-dasharray="6 4"/></svg>Forecast / plan</span>
  <span><i class="sw" style="background:var(--amber)"></i>Waiting for ${esc(n[0])}</span>
  <span><svg width="16" height="16" aria-hidden="true"><circle cx="8" cy="8" r="7" style="fill:var(--amber)"/><rect x="4.5" y="4.5" width="2.5" height="7" style="fill:var(--panel)"/><rect x="9" y="4.5" width="2.5" height="7" style="fill:var(--panel)"/></svg>Program pause</span>
  <span><i class="sw" style="background:var(--muted);opacity:.35"></i>Keeps previous target</span>
  <span><svg width="16" height="16" aria-hidden="true"><rect x="3" y="3" width="10" height="10" rx="2" style="fill:var(--muted)"/></svg>Target: drag up / down</span>
  <span><svg width="16" height="16" aria-hidden="true"><rect x="4" y="4" width="8" height="8" transform="rotate(45 8 8)" style="fill:none;stroke:var(--muted);stroke-width:2"/></svg>Ramp end</span>
  <span><svg width="16" height="16" aria-hidden="true"><circle cx="8" cy="8" r="5" style="fill:none;stroke:var(--muted);stroke-width:2"/></svg>Hold end: drag left / right</span></div></details>`;
  $('lgx').ontoggle=e=>LS('lgx',e.target.open?'1':'0'); }
// (the key is open on a wide page, closed on a phone; a choice is kept)
function lgxOpen(){ const v=LS('lgx'); return v==='1'||(v!=='0'&&!matchMedia('(max-width:860px)').matches); }
function svgPt(ev){ const svg=$('chartbox').querySelector('svg'); const pt=svg.createSVGPoint(); pt.x=ev.clientX; pt.y=ev.clientY; return pt.matrixTransform(svg.getScreenCTM().inverse()); }
const ptrs=new Map(); let pz=null;
// touch / pen (Rost): a handle moves only after a short hold - a swipe over the chart still scrolls the page (and
// pans the chart sideways). The finger may be up to 24 px off the handle. Held: a short vibration, the handle's
// ghost; from then on the page does not scroll (touchmove prevented) until the finger is lifted.
const HOLD_MS=350; let hold=null;
function nearHandle(ev){ let best=null, bd=24*24;
  $('chartbox').querySelectorAll('[data-h]').forEach(el=>{ const r=el.getBoundingClientRect(), dx=(r.left+r.right)/2-ev.clientX, dy=(r.top+r.bottom)/2-ev.clientY, d=dx*dx+dy*dy; if(d<bd){ bd=d; best=el; } });
  return best; }
function holdCancel(){ if(hold){ clearTimeout(hold.tm); hold=null; } }
function holdDone(){ const q=hold; hold=null; if(!q||drag) return;
  ptrs.delete(q.id); if(!ptrs.size) pz=null;  // (no more panning with this finger)
  drag={h:q.h,px:q.p.x,py:q.p.y,t:q.h.t,v:q.h.v,touch:true}; try{$('chartbox').setPointerCapture(q.id);}catch(e){}
  try{ if(navigator.vibrate) navigator.vibrate(12); }catch(e){}
  dragMove({clientX:q.x,clientY:q.y}); }
$('chartbox').addEventListener('pointerdown',ev=>{ if(!M||M.kind==='live'||!$('chartbox').querySelector('svg')) return; if(ev.pointerType==='mouse'&&ev.button!==0) return;
  const fing=ev.pointerType!=='mouse';
  if(fing&&(hold||drag||ptrs.size)) holdCancel();  // (a second finger: zoom, no hold)
  const hc=fing?(ptrs.size||drag?null:ev.target.closest('[data-h]')||nearHandle(ev)):ev.target.closest('[data-h]');
  if(hc){ const h=handles[+hc.dataset.h]; if(!h) return; const p=svgPt(ev);
    if(fing) hold={id:ev.pointerId,h,p,x:ev.clientX,y:ev.clientY,tm:setTimeout(holdDone,HOLD_MS)};  // (and on: pans until held)
    else { drag={h,px:p.x,py:p.y,t:h.t,v:h.v}; try{$('chartbox').setPointerCapture(ev.pointerId);}catch(e){} ev.preventDefault(); return; } }
  const p=svgPt(ev); ptrs.set(ev.pointerId,p.x); try{$('chartbox').setPointerCapture(ev.pointerId);}catch(e){}
  pz={v0:zv.fit?0:zv.v0,span:zv.fit?xfull:zv.span,pts:new Map(ptrs)}; });
$('chartbox').addEventListener('pointermove',ev=>{ if(hold&&ev.pointerId===hold.id&&Math.hypot(ev.clientX-hold.x,ev.clientY-hold.y)>8) holdCancel();  // (moved: a swipe)
  if(drag){ dragMove(ev); return; } if(!ptrs.has(ev.pointerId)||!pz) return; const p=svgPt(ev); ptrs.set(ev.pointerId,p.x); const ids=[...pz.pts.keys()];
  if(ptrs.size>=2&&ids.length>=2){ const a0=pz.pts.get(ids[0]),b0=pz.pts.get(ids[1]),a1=ptrs.get(ids[0]),b1=ptrs.get(ids[1]); if(a1==null||b1==null) return;
    const d0=Math.max(10,Math.abs(b0-a0)),d1=Math.max(10,Math.abs(b1-a1)),m0=(a0+b0)/2,m1=(a1+b1)/2, span=Math.max(20,Math.min(xfull,pz.span*d0/d1)), tm=pz.v0+(m0-CL)/plotW()*pz.span;
    zv.fit=span>=xfull; zv.span=span; zv.v0=tm-(m1-CL)/plotW()*span; afterView(); }
  else if(ptrs.size===1&&!zv.fit){ const x0=pz.pts.get(ev.pointerId); if(x0==null) return; zv.v0=pz.v0-(p.x-x0)/plotW()*pz.span; afterView(); } });
const panUp=ev=>{ ptrs.delete(ev.pointerId); if(pz) pz={v0:zv.fit?0:zv.v0,span:zv.fit?xfull:zv.span,pts:new Map(ptrs)}; if(!ptrs.size) pz=null; };
$('chartbox').addEventListener('pointerup',ev=>{ if(hold&&ev.pointerId===hold.id) holdCancel(); panUp(ev); if(drag) dragEnd(); });
$('chartbox').addEventListener('pointercancel',ev=>{ if(hold&&ev.pointerId===hold.id) holdCancel(); panUp(ev); if(drag){ drag=null; lastChart=0; } });
$('chartbox').addEventListener('touchmove',ev=>{ if(drag&&drag.touch) ev.preventDefault(); },{passive:false});  // (held: the page stays)
$('chartbox').addEventListener('contextmenu',ev=>{ if(hold||drag) ev.preventDefault(); });
$('chartbox').addEventListener('wheel',ev=>{ if(!M||M.kind==='live') return;
  if(ev.ctrlKey||ev.metaKey){ ev.preventDefault(); zoomAt(tAtX(svgPt(ev).x),ev.deltaY>0?1.25:.8); }
  else if(!zv.fit&&(ev.shiftKey||Math.abs(ev.deltaX)>Math.abs(ev.deltaY))){ ev.preventDefault(); zv.v0+=(ev.shiftKey?ev.deltaY:ev.deltaX)/plotW()*zv.span; afterView(); } },{passive:false});
