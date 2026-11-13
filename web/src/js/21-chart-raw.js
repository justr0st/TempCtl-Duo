/* ---------- every reading (spec 7, "Chart: every reading"): the device keeps each PV reading (millis, PV)
   per channel (GET /api/raw, binary); the page fetches only the new ones each poll and draws every one.
   Older than what the device keeps: the chart's slots (last / lowest / highest). Wi-Fi only. ---------- */
const RAW={t:[[],[],[]],v:[[],[],[]],after:[0,0,0],from:null,boot:null,busy:false,on:false,viaLive:null};
function rawReset(){ RAW.t=[[],[],[]]; RAW.v=[[],[],[]]; RAW.after=[0,0,0]; RAW.from=null; }
const RC=[0,1,2];  // raw readings: the channels and the monitor (a2: only asked for while it is on)
const rawA2=()=>monLive()?`&a2=${RAW.after[MON]}`:'';
async function rawPull(win){
  if(SER.on||OFF||RAW.busy||!M||!M.raw) return false;
  if(live&&RAW.boot!==live.boot){ rawReset(); RAW.boot=live.boot; }
  // a longer window than fetched so far (e.g. a longer live period): fetch it all again from its start
  const need=M.ms+(Date.now()-(M.at||Date.now()))-win*1000;
  if(RAW.from!=null&&need<RAW.from-2000) rawReset();
  if(RAW.from==null) RAW.from=need;
  RAW.busy=true; let got=false;
  try{
    for(let round=0;round<40;round++){
      const r=await fetch(`/api/raw?a0=${RAW.after[0]}&a1=${RAW.after[1]}${rawA2()}&win=${Math.ceil(win)}&max=8192`,{cache:'no-store'});
      if(!r.ok) break;
      const dv=new DataView(await r.arrayBuffer()); if(dv.byteLength<16) break;
      // format 1: now, n0, n1, 1; format 2 (with the monitor): ..., 2, n2
      const f2=dv.getUint32(12,true)===2&&dv.byteLength>=20, n=[dv.getUint32(4,true),dv.getUint32(8,true),f2?dv.getUint32(16,true):0]; let o=f2?20:16;
      for(const c of RC){
        for(let k=0;k<n[c];k++,o+=8){ const pv=dv.getInt16(o+4,true); if(rawAdd(c,dv.getUint32(o,true),pv===-32768?null:pv/10)) got=true; } }
      if(n[0]<8192&&n[1]<8192&&n[2]<8192) break;
    }
    rawTrim(); RAW.on=true;
  }catch(e){} finally{ RAW.busy=false; }
  return got;
}
// one reading (device millis, PV or null); false when overwritten meanwhile / not newer
function rawAdd(c,ms,pv){ const T=RAW.t[c]; if(!ms||(T.length&&ms<=T[T.length-1])) return false;
  T.push(ms); RAW.v[c].push(pv); RAW.after[c]=ms; return true; }
// the browser keeps at most what the device keeps (plus a little)
function rawTrim(){ const lim=Math.max(2000,(M&&M.raw||0)*1.2); for(const c of RC) if(RAW.t[c].length>lim){ const cut=RAW.t[c].length-lim; RAW.t[c].splice(0,cut); RAW.v[c].splice(0,cut); } }
// The new readings come with /api/live (?a0=&a1=: "raw" [[ms, pv tenths, ...], [...]]), so a poll needs no
// second request; /api/raw only fills the start and catches up ("rawMore": too many since the last poll).
function rawLiveQuery(){ return !SER.on&&RAW.on&&RAW.boot!=null&&page==='status'&&M&&M.raw&&!RAW.busy?`?a0=${RAW.after[0]}&a1=${RAW.after[1]}${rawA2()}`:''; }
function rawFromLive(L){ RAW.viaLive=null;
  if(!L.raw||L.boot!==RAW.boot||!RAW.on) return;
  let got=false; for(const c of RC){ const a=L.raw[c]||[]; for(let k=0;k+1<a.length;k+=2) if(rawAdd(c,a[k],a[k+1]==null?null:a[k+1]/10)) got=true; }
  rawTrim(); RAW.viaLive={got}; }
// run time of a reading (device millis) from the run's anchors (piecewise linear), after the newest point:
// + wall time while running
function rawRunTime(M,ms){ const A=M.ra||[]; if(!A.length||ms<A[0][0]) return null;
  const pts=A.concat([M.rn]); for(let k=1;k<pts.length;k++){ const [m1,r1]=pts[k], [m0,r0]=pts[k-1];
    if(ms<=m1) return m1>m0?(r0+(ms-m0)*(r1-r0)/(m1-m0))/1000:r1/1000; }
  if(M.kind!=='run') return null;  // after a finished run: not part of it
  const [mn,rn]=M.rn; return (rn+(live&&live.run.state==='running'?ms-mn:0))/1000; }
// PV points of the model: the slots older than the first kept reading, then every reading
function applyRaw(M){
  if(!M||!M.raw||!RAW.on||(M.kind!=='live'&&M.kind!=='run'&&M.kind!=='last')) return;
  M.rawOn=[false,false,false];
  for(let i=0;i<M.hist.length;i++){ const T=RAW.t[i],V=RAW.v[i]; if(!T||!T.length) continue;
    const tOf=M.kind==='live'?(ms=>(ms-M.ms)/1000):(ms=>rawRunTime(M,ms));
    const pts=[]; let first=null;
    for(let k=0;k<T.length;k++){ const t=tOf(T[k]); if(t==null) continue; if(M.kind==='live'&&t<M.t0-1) continue; if(first==null) first=t; pts.push([t,V[k]]); }
    if(!pts.length) continue;
    M.hist[i].pv=M.hist[i].pvBin.filter(p=>p[0]<first).concat(pts); M.rawOn[i]=true; }
  // a line breaks where readings are further apart than 2.5 polls of both channels (min. 3 s)
  if(M.kind==='live') M.gap=Math.max(M.gap,5*((live&&live.pollMs)||1000)/1000);
}
async function rawUpdate(){ const via=RAW.viaLive; RAW.viaLive=null;
  if(!M||!M.raw||page!=='status'||drag) return;
  const win=M.kind==='live'?-M.t0+5:(M.ra&&M.ra.length?(M.ms-M.ra[0][0])/1000+5:0); if(!win) return;
  const need=M.ms+(Date.now()-(M.at||Date.now()))-win*1000;  // (as rawPull: a longer window fetches again)
  if(via&&RAW.from!=null&&need>=RAW.from-2000){ if(via.got){ applyRaw(M); lastChart=0; } return; }
  if(await rawPull(win)){ applyRaw(M); lastChart=0; } }
async function ensureRunProg(running){ const now=Date.now(); if(!runProg||(running&&now-runProgAt>5000)){ const r=await api('/api/run'); runProg=normProg(r.program); runProgAt=now; } }

