/* ---------- chart model ---------- */
// M = {kind:'run'|'last'|'preview'|'live', now, end, prog, hist:[{pv:[[t,v]],sp:[[t,v]]}], lanes:[[{t0,t1,c}]], segs, ev:[{t,type,ch,idx,past}]}
function laneCode(M,i,s){
  if(s.fl&1) return 'o'; if(s.fl&2) return 'r';
  const p=M.prog, tm=(s.t0+s.t1)/2;
  if(p&&p.type==='seq'){ let k=-1; for(const e of M.ev) if(e.type===EV.PHASE&&e.t<=tm+1e-6) k=e.idx; const x=p.phases&&p.phases[k]; if(!x) return 'k';
    const tc=bDrv(x)?0:1; if(x.kind==='BT'&&!x.t.idle&&i===0&&s.src===k) return 'j'; return tc===i&&s.src===k?'h':'k'; }
  let lastEnd=-1; for(const e of M.ev) if(e.type===EV.HOLD_END&&e.ch===i) lastEnd=Math.max(lastEnd,e.t); return tm>lastEnd&&lastEnd>=0?'d':'h';
}
// PV points of one channel: each chart slot has its last reading (pv) and how far its lowest / highest
// reading lay below / above it (dlo / dhi, 0.1 °C). The line visits both extremes of a slot (the
// one nearer to the previous point first), so short peaks and dips stay visible.
function pvPts(d,i,tOf,out){ const pv=d.pv[i], lo=d.dlo&&d.dlo[i], hi=d.dhi&&d.dhi[i]; let prev=null;
  for(let k=0;k<pv.length;k++){ const v=pv[k]; if(v==null) continue; const t=tOf(k), a=lo?v-lo[k]/10:v, b=hi?v+hi[k]/10:v;
    if(a===b){ out.push([t,v]); prev=v; continue; }
    const aFirst=prev==null||Math.abs(prev-a)<=Math.abs(prev-b);
    out.push([t,aFirst?a:b],[t,aFirst?b:a]); prev=aFirst?b:a; }
  return out; }
function modelFromRun(d){
  const M={kind:d.mode,now:d.el,prog:runProg,hist:[],lanes:[],segs:[[],[]],ev:[],bin:d.bin,ms:d.ms,ra:d.ra,rn:d.rn,raw:d.raw};
  let lastT=0;
  for(const i of CH){ const pv=[],sp=[],ln=[]; const st=d.st[i]||'';
    pvPts(d,i,k=>k*d.bin,pv);
    for(let k=0;k<d.pv[i].length;k++){ const t=k*d.bin; if(d.sp[i][k]!=null) sp.push([t,d.sp[i][k]]);
      const c=st[k]||' '; if(c!==' '){ lastT=Math.max(lastT,(k+1)*d.bin); const t1=Math.min((k+1)*d.bin,d.el); if(ln.length&&ln[ln.length-1].c===c) ln[ln.length-1].t1=t1; else ln.push({t0:t,t1,c}); } }
    M.hist.push({pv,sp,pvBin:pv}); M.lanes.push(ln); }
  if(d.pv.length>MON){ const pv=pvPts(d,MON,k=>k*d.bin,[]); M.hist.push({pv,sp:[],pvBin:pv}); M.mon=d.mon||chName(MON); }
  for(const e of d.ev) M.ev.push({t:e[0],type:e[1],ch:e[2],idx:e[3],past:true});
  if(d.mode==='run'){ for(const i of CH) for(const s of d.fc.seg[i]) M.segs[i].push({t0:s[0]+d.el,t1:s[1]+d.el,v0:s[2],v1:s[3],src:s[4],fl:s[5]});
    for(const e of d.fc.ev) if(e[1]!==EV.PHASE||e[0]>0.01) M.ev.push({t:e[0]+d.el,type:e[1],ch:e[2],idx:e[3],past:false});
    M.end=d.el+d.rem; for(const i of CH) for(const s of M.segs[i]) M.lanes[i].push({t0:s.t0,t1:s.t1,c:laneCode(M,i,s),f:true}); }
  else M.end=Math.max(d.el,1);
  applyRaw(M);
  return M;
}
function modelPreview(p){
  const pv=CH.map(i=>live&&live.ch[i].pv!=null?live.ch[i].pv:null), P=planProgram(p,pv);
  const M={kind:'preview',now:null,prog:p,hist:[{pv:[],sp:[]},{pv:[],sp:[]}],lanes:[[],[]],segs:P.segs,ev:P.ev.map(e=>Object.assign({past:false},e)),end:P.end};
  for(const i of CH) for(const s of M.segs[i]) M.lanes[i].push({t0:s.t0,t1:s.t1,c:laneCode(M,i,s),f:true});
  return M;
}
// live view: bins of d.bin s, the newest ends now, the others d.sh s later than whole bins before it
function modelLive(d){
  const M={kind:'live',now:0,hist:[],lanes:[[],[]],segs:[[],[]],ev:[],end:0,t0:-d.span,at:Date.now(),bin:d.bin,gap:Math.max(3,d.bin*2.5),have:d.have||0,cap:d.cap||d.span,ms:d.ms,raw:d.raw};
  for(const i of CH){ const pv=[],sp=[]; const n=d.pv[i].length, tOf=k=>k===n-1?0:(k-n+1)*d.bin+(d.sh||0);
    pvPts(d,i,tOf,pv); for(let k=0;k<n;k++){ if(d.sp[i][k]!=null) sp.push([tOf(k),d.sp[i][k]]); }
    // the oldest bin starts up to one bin after the left edge: held to the edge, else a strip at the left comes
    // and goes with every chart update (and Chrome repaints the glass surfaces around it: flicker)
    for(const L of [pv,sp]) if(L.length&&L[0][0]>M.t0&&L[0][0]-M.t0<=1.5*d.bin) L.unshift([M.t0,L[0][1]]);
    M.hist.push({pv,sp,pvBin:pv}); }
  if(d.pv.length>MON){ const n=d.pv[MON].length, tOf=k=>k===n-1?0:(k-n+1)*d.bin+(d.sh||0), pv=pvPts(d,MON,tOf,[]);
    if(pv.length&&pv[0][0]>M.t0&&pv[0][0]-M.t0<=1.5*d.bin) pv.unshift([M.t0,pv[0][1]]);
    M.hist.push({pv,sp:[],pvBin:pv}); M.mon=d.mon||chName(MON); }
  liveMinSel(d);
  applyRaw(M);
  return M;
}
// period choices: up to what the device keeps
function liveMinSel(d){ const sel=$('liveMin'), cap=Math.floor((d.cap||d.span)/60), cur=Math.round(d.span/60);
  const v=[1,5,10,15,30,60,120,240,480,720,1440].filter(m=>m<=cap);
  const lab=m=>m%60?`${m} min`:`${m/60} h`, key=v.join()+'|'+cur;
  if(sel.dataset.k!==key&&document.activeElement!==sel){ sel.dataset.k=key; sel.innerHTML=v.map(m=>`<option value="${m}" ${m===cur?'selected':''}>${lab(m)}</option>`).join(''); } }
$('legend').onclick=e=>{ const b=e.target.closest('[data-lv]'); if(!b) return; const k=b.dataset.lv.slice(0,2), i=+b.dataset.lv[2];
  LVIS[k][i]=LVIS[k][i]?0:1; try{ localStorage.setItem('lvis',JSON.stringify(LVIS)); }catch(e){} legend(); lastChart=0; };
$('histCsv').onclick=e=>{ if(!(lvMask('pv')|lvMask('sp'))){ e.preventDefault(); note('Switch on at least one PV or SP to export.',true); } };
$('liveMin').onchange=async e=>{ const r=await postCfg({display:{liveMin:+e.target.value}}); if(r&&r.err) $('msg').textContent=r.err; chartAt=0; poll(); };
let M=null;
async function refreshChart(){
  const run=live&&(live.run.state==='running'||live.run.state==='paused');
  if(view==='live'){ const d=await api('/api/chart?v=live'); M=modelLive(d); lastChart=0; return; }
  if(run||(showLast&&live&&live.run.prog===live.sel)){
    const d=await api('/api/chart?v=run');
    if(d.mode==='run'||d.mode==='last'){ await ensureRunProg(d.mode==='run'); M=modelFromRun(d); lastChart=0; return; }
  }
  if(live) await loadSaved(live.sel);
  previewDraft();
}
// latest values from /api/live onto the chart (each poll; the history comes every 2 s): PV and the active
// setpoint (as the device records it: the controller's SP, else the program's / the manual one while the
// outputs are on)
function headSp(c,run){ if(c.sp!=null) return c.sp; if(!c.outOn) return null;
  if(run&&c.inProgram&&!c.idle&&c.runSp!=null) return c.runSp; return c.manual&&c.manual.on&&c.manual.sp!=null?c.manual.sp:null; }
// device seconds per real second (the demo's speed-up; a real device: 1)
const devRate=()=>DEMO?TCDemo.speed():1;
function chartHead(){
  if(!M||!live||drag) return;
  const run=live.run.state==='running'||live.run.state==='paused';
  const v=CH.map(i=>live.ch[i].pv!=null?live.ch[i].pv:null), sp=CH.map(i=>headSp(live.ch[i],run));
  v[MON]=live.mon&&live.mon.pv!=null?live.mon.pv:null;
  if(M.kind==='live'){ const r=devRate(); M.head={t:Math.min(Math.max(5*r,M.bin),(Date.now()-M.at)/1000*r),v,sp}; lastChart=0; }
  else if(M.kind==='run'&&live.run.state==='running'&&live.run.el!=null&&live.run.el>=M.now){ M.now=live.run.el; M.head={t:live.run.el,v,sp}; lastChart=0; }
}
// (only in the program view: in the live view an edit must not swap the chart for the preview until the next
// poll brings the live chart back - the page jumped by the height difference with every edit)
function previewDraft(){ if(view==='live') return; const p=live?DRAFT[live.sel]:null; M=p&&progItems(p)?modelPreview(p):null; lastChart=0; }
