/* ---------- sidebar: run and channels on every page ---------- */
const SIDE_ST={ramp:'Ramp',hold:'Hold',wait:'Waiting',jump:'Jump',keep:'Keeps',halt:'Paused',pause:'Program pause',done:'Done',off:'Off'};
// text only when it changed: an unchanged rewrite still repaints (the glass sidebar flickered on every poll)
const setTxt=(e,t)=>{ if(e.textContent!==t) e.textContent=t; };
function updSide(){
  if(!live) return; $('sCtl').hidden=false;
  const r=live.run, active=r.state==='running'||r.state==='paused';
  setTxt($('sRunName'),active?(r.name||''):(progs&&progs.programs[live.sel]&&progs.programs[live.sel].used?progs.programs[live.sel].name:'No program'));
  setTxt($('sRunSt'),$('badge').textContent+(active||r.state==='finished'||r.state==='stopped'?' · '+$('clock').textContent:''));
  const pl=$('sPlay'), running=r.state==='running'&&!r.progPause;
  pl.classList.toggle('run',running);
  const lab=running?'Pause':r.state==='paused'?'Resume':active&&r.progPause?'Continue':'Start';
  pl.title=lab; pl.setAttribute('aria-label',lab);
  pl.disabled=running?$('bPause').disabled:r.state==='paused'?$('bStart').disabled:active&&r.progPause?$('bPause').disabled:$('bStart').disabled;
  $('sStop').disabled=!active;
  // (the monitor channel, while it is on: a third row with its reading only, no button)
  const key=live.ch.map(c=>c.name).join('|')+(live.mon?'|'+live.mon.name:'');
  if($('sChs').dataset.k!==key){ $('sChs').dataset.k=key;
    $('sChs').innerHTML=CH.map(i=>`<div class="sch" style="--c:${PV[i]}"><i></i><span class="n">${esc(chName(i))}</span>
      <button class="ib" data-sman="${i}" data-mode="man">${SIDE_ICON.man}</button>
      <span class="v" id="sv${i}"></span></div>`).join('')
      +(live.mon?`<div class="sch mon" style="--c:${PV[MON]}"><i></i><span class="n">${esc(live.mon.name)}</span><span class="v" id="sv${MON}"></span></div>`:''); }
  if(live.mon){ const m=live.mon, v=$('sv'+MON); setTxt(v,m.pv!=null?f1(m.pv)+' °C · Monitor':'– · '+(m.err||'no reading')); v.classList.toggle('bad',m.pv==null); }
  for(const i of CH){ const c=live.ch[i], inRun=active&&c.inProgram;
    const at=c.autotune?c.autotune.state:'';
    const st=c.fault?'Fault':estopOn()&&c.type!=='off'?'E-stop':!c.ok&&c.type!=='off'?'No link':c.type==='off'?'Off':inRun?(SIDE_ST[c.state]||c.state):at==='running'?'Autotune':at==='done'?'Autotune done':c.manual.on?'Manual':'Idle';
    const v=$('sv'+i); setTxt(v,(c.pv!=null?f1(c.pv)+' °C':'–')+' · '+st); v.classList.toggle('bad',!!c.fault);
    const b=document.querySelector(`[data-sman="${i}"]`); if(!b) continue;
    // the button: manual on / off, or while an autotune exists: stop it / review its result / clear it
    const mode=at==='running'?'stop':at==='done'?'review':at==='failed'?'clear':'man';
    if(b.dataset.mode!==mode){ b.dataset.mode=mode; b.innerHTML=SIDE_ICON[mode]; }
    b.classList.toggle('on',mode==='man'&&!!c.manual.on); b.classList.toggle('at',mode!=='man');
    b.disabled=mode==='man'?inRun||c.type==='off'||!!c.fault:false;
    const t=mode==='stop'?'Stop the autotune':mode==='review'?'Autotune done: review the result':mode==='clear'?'Clear the autotune message ('+(c.autotune.msg||'failed')+')':inRun?'In the program':c.manual.on?'Manual: switch off':'Manual: switch on';
    b.title=t; b.setAttribute('aria-label',`${chName(i)}: ${t}`); }
  { const fo=!!(live.fan&&live.fan.on), fc=$('fanChip'); fc.hidden=!fo; if(fo) setTxt(fc,'Fan on'+(live.fan.rpm!=null?' · '+live.fan.rpm+' rpm':'')); }  // (a fan output exists and runs)
  const rl=$('sRelay'); rl.hidden=live.relay==null; if(live.relay!=null) setTxt(rl,'SSR supply relay: '+(live.relay?'on':'off'));
}
$('sPlay').onclick=()=>{ if(!live) return; const r=live.run;
  if(r.state==='running'&&!r.progPause) $('bPause').click();
  else if(r.progPause) $('bPause').click();  // (continue)
  else $('bStart').click(); };  // start / resume (asks about unsaved changes, an interrupted run)
$('sStop').onclick=()=>$('bStop').click();
const SIDE_ICON={man:'<svg viewBox="0 0 24 24"><path d="M12 3v9"/><path d="M6.3 7.3a8 8 0 1 0 11.4 0"/></svg>',
  stop:'<svg viewBox="0 0 24 24"><rect x="6" y="6" width="12" height="12" rx="2"/></svg>',
  review:'<svg viewBox="0 0 24 24"><path d="M5 12l5 5L20 7"/></svg>',
  clear:'<svg viewBox="0 0 24 24"><path d="M6 6l12 12M18 6L6 18"/></svg>'};
$('sChs').addEventListener('click',async e=>{ const b=e.target.closest('[data-sman]'); if(!b||!live) return; const i=+b.dataset.sman, c=live.ch[i];
  if(b.dataset.mode&&b.dataset.mode!=='man') return tuneAction(i,b.dataset.mode);
  b.disabled=true;
  if(c.manual.on) await api('/api/manual',{ch:i,on:false});
  else await api('/api/manual',{ch:i,sp:c.manual.sp!=null?c.manual.sp:+$('manSp'+i).value,on:true});
  poll(); });

