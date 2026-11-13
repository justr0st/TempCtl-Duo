/* ---------- programs: stored (SAVED), drafts (DRAFT), undo / redo (spec 9.3.3) ---------- */
function chName(i){ if(i===MON) return live&&live.mon?live.mon.name:CFG&&CFG.monitor?CFG.monitor.name:'Monitor';
  return live&&live.ch[i]?live.ch[i].name:(i?'Top':'Bottom'); }
const MAXN=100, MAXSEC=599940, J=o=>JSON.stringify(o), clone=o=>o==null?null:JSON.parse(J(o));
let rowDrag=null;  // a phase / step row being dragged (reorder, below)
// editable form: every phase has b {t,r,h,idle} and t {t,r,h,idle}; Timeline steps {kind,t,r,h,msg,text}
function normProg(d){ if(!d) return null;
  if(d.type==='tl'){ const st=x=>x.pause?{kind:'pause',t:0,r:0,h:0,msg:!!x.msg,text:x.text||'',fan:!!x.fan}:x.idle?{kind:'idle',t:0,r:0,h:x.h||0,msg:false,text:'',fan:!!x.fan}:{kind:'step',t:x.t,r:x.r||0,h:x.h||0,msg:false,text:'',fan:!!x.fan};
    return {name:d.name||'',type:'tl',B:(d.B||[]).map(st),T:(d.T||[]).map(st)}; }
  return {name:d.name||'',type:'seq',phases:(d.phases||[]).map(x=>({kind:x.kind,wait:!!x.wait,msg:!!x.msg,text:x.text||'',fan:!!x.fan,
    b:{t:x.b&&x.b.t!=null?x.b.t:150,r:x.b&&x.b.r||0,h:x.b&&x.b.h||0,idle:!!(x.b&&x.b.idle)},t:{t:x.t&&x.t.t!=null?x.t.t:180,r:x.t&&x.t.r||0,h:x.t&&x.t.h||0,idle:!!(x.t&&x.t.idle)}}))}; }
// the bottom drives the phase (ramp, wait, hold): a Bottom phase, or Both with the top Idle (1.0.60)
const bDrv=x=>x.kind==='B'||(x.kind==='BT'&&!!x.t.idle);
// a Both phase with both channels Idle: both outputs off for the bottom's hold time (b.h)
const bothOff=x=>x.kind==='BT'&&!!x.b.idle&&!!x.t.idle;
// device JSON (spec 9.3.5)
function denormProg(p){ if(!p) return null;
  if(p.type==='tl'){ const st=x=>Object.assign(x.kind==='pause'?Object.assign({pause:true},x.msg?{msg:true}:{},x.text?{text:x.text}:{}):x.kind==='idle'?{idle:true,h:x.h}:{t:x.t,r:x.r,h:x.h},x.fan?{fan:true}:{});
    return {fmt:1,name:p.name,type:'tl',B:p.B.map(st),T:p.T.map(st)}; }
  return {fmt:1,name:p.name,type:'seq',phases:p.phases.map(x=>{ const o={kind:x.kind}; if(x.fan) o.fan=true;
    if(x.kind==='P'){ if(x.msg) o.msg=true; if(x.text) o.text=x.text; return o; }
    if((x.kind==='B'||(x.kind==='BT'&&!x.b.idle))&&x.wait) o.wait=true;
    if(bothOff(x)) o.b={idle:true,h:x.b.h};  // both off: the phase's time
    else if(bDrv(x)) o.b={t:x.b.t,r:x.b.r,h:x.b.h};
    else if(x.kind==='BT') o.b=x.b.idle?{idle:true}:{t:x.b.t};
    if(x.kind==='BT'&&x.t.idle) o.t={idle:true}; else if(x.kind!=='B') o.t={t:x.t.t,r:x.t.r,h:x.t.h};
    return o; })}; }
function progUses(p,i){ if(!p) return false; if(p.type==='tl') return (p[KEY[i]]||[]).length>0; return (p.phases||[]).some(x=>x.kind==='BT'||x.kind===KEY[i]); }
function progItems(p){ return !p?0:p.type==='tl'?p.B.length+p.T.length:p.phases.length; }
function hasWaits(p){ return p&&p.type==='seq'&&(p.phases||[]).some(x=>x.wait&&(x.kind==='B'||(x.kind==='BT'&&!x.b.idle))); }
const SAVED={}, DRAFT={}, UNDO={}, REDO={}, savedGen={};
const isDirty=i=>i in DRAFT&&J(denormProg(DRAFT[i]))!==J(denormProg(SAVED[i]));
async function loadSaved(i,force){ const g=live?live.progGen:0; if(!force&&savedGen[i]===g) return;
  const r=await api('/api/program?i='+i), was=isDirty(i);
  SAVED[i]=r.used?normProg(r.program):null; savedGen[i]=g; if(!was) DRAFT[i]=clone(SAVED[i]); if(!UNDO[i]){ UNDO[i]=[]; REDO[i]=[]; } }
async function loadPrograms(){ progs=await api('/api/programs'); progsGen=live?live.progGen:-1; fillProgSelect(); }
function fillProgSelect(){ if(!progs) return; const s=$('prog'), cur=live?live.sel:progs.sel; if(document.activeElement===s) return;
  const h=progs.programs.map((p,i)=>{ const d=DRAFT[i], dirty=isDirty(i), q=d!=null?d:p.used?p:null;
    const lab=q?`${esc(q.name)} (${q.type==='tl'?'timeline':'sequence'}${!dirty&&p.used?', '+fmt(p.total):''})`:'(empty)';
    return `<option value="${i}">${i+1} ${lab}${dirty?' – unsaved':''}</option>`; }).join('');
  if(s.dataset.h!==h){ s.innerHTML=h; s.dataset.h=h; } s.value=String(cur); }
$('prog').onchange=async e=>{ const i=+e.target.value; showLast=false; await api('/api/select',{i}); if(live) live.sel=i; await loadSaved(i); chartAt=0; edKey=''; if(view!=='prog') setView('prog'); else poll(); };  // (a program chosen: its chart)

// Plan of a program from its start (as the device's preview: programmed times, waits met at once, pauses 0 s)
function planProgram(p,pv){
  const segs=[[],[]], ev=[], uses=[progUses(p,0),progUses(p,1)];
  const g=[{idle:true,v:null,src:-1},{idle:true,v:null,src:-1}];
  const push=(i,t0,t1,v0,v1,src,fl)=>{ if(uses[i]&&t1>t0) segs[i].push({t0,t1,v0,v1,src,fl}); };
  const flat=(i,t0,t1)=>{ const c=g[i]; if(c.idle) push(i,t0,t1,null,null,-1,1); else push(i,t0,t1,c.v,c.v,c.src,0); };
  const ramp=(i,t0,tgt,r,src)=>{ const c=g[i], from=c.idle?(pv[i]!=null?pv[i]:25):c.v; r=r||0; if(r>0) push(i,t0,t0+r,from,tgt,src,2); c.idle=false; c.v=tgt; c.src=src; return t0+r; };
  let t=0;
  if(p.type==='seq'){
    p.phases.forEach((x,k)=>{ ev.push({t,type:EV.PHASE,ch:-1,idx:k});
      if(x.kind==='P'){ ev.push({t,type:EV.PAUSE,ch:-1,idx:k}); ev.push({t,type:EV.CONT,ch:-1,idx:k}); return; }
      if(bothOff(x)){ for(const i of [0,1]){ g[i].idle=true; g[i].src=-1; } const t2=t+(x.b.h||0); flat(0,t,t2); flat(1,t,t2); t=t2; ev.push({t,type:EV.HOLD_END,ch:0,idx:k}); return; }
      const c=bDrv(x)?0:1, sg=c===0?x.b:x.t;
      if(x.kind==='BT'){ if(x.t.idle){ g[1].idle=true; g[1].src=-1; } else if(x.b.idle){ g[0].idle=true; g[0].src=-1; } else { g[0].idle=false; g[0].v=x.b.t; g[0].src=k; } }
      const o=1-c, t0=t, t1=ramp(c,t,sg.t,sg.r,k), t2=t1+(sg.h||0);
      ev.push({t:t1,type:EV.RAMP_END,ch:c,idx:k}); flat(c,t1,t2); flat(o,t0,t2); t=t2; ev.push({t,type:EV.HOLD_END,ch:c,idx:k});
    });
  } else {
    const ends=[0,0];
    for(const i of CH){ let tt=0; p[KEY[i]].forEach((x,k)=>{ ev.push({t:tt,type:EV.STEP,ch:i,idx:k});
      if(x.kind==='pause'){ ev.push({t:tt,type:EV.PAUSE,ch:i,idx:k}); ev.push({t:tt,type:EV.CONT,ch:i,idx:k}); return; }
      if(x.kind==='idle'){ g[i].idle=true; g[i].src=-1; push(i,tt,tt+(x.h||0),null,null,-1,1); tt+=x.h||0; ev.push({t:tt,type:EV.HOLD_END,ch:i,idx:k}); return; }
      const t1=ramp(i,tt,x.t,x.r,k); ev.push({t:t1,type:EV.RAMP_END,ch:i,idx:k}); flat(i,t1,t1+(x.h||0)); tt=t1+(x.h||0); ev.push({t:tt,type:EV.HOLD_END,ch:i,idx:k}); });
      ends[i]=tt; }
    t=Math.max(ends[0],ends[1]); for(const i of CH) if(ends[i]<t) flat(i,ends[i],t);
  }
  ev.push({t,type:EV.END,ch:-1,idx:-1});
  return {segs,ev,end:t};
}

