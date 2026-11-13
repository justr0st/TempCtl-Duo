/* ---------- start with unsaved changes ---------- */
function choose(text,btns){ return new Promise(res=>{ const d=$('chDlg'); $('chTxt').textContent=text;
  $('chBtns').innerHTML=btns.map(([v,l,pri])=>`<button data-v="${v}" class="${pri?'pri':''}">${esc(l)}</button>`).join('');
  $('chBtns').onclick=e=>{ const b=e.target.closest('button'); if(!b) return; d.close(); res(b.dataset.v); }; d.onclose=()=>res('cancel'); d.showModal(); }); }

/* ---------- run bar, overlays ---------- */
// The report of the last run (live.report, none while a run runs): program, result, and per channel the peak,
// the largest deviation in the holds and the steepest rise; what was not measured is left out. The block is
// not touched while its content is the same (no layout jump while the live data refreshes), and takes no room
// while there is no report.
function runReport(rp){
  const el=$('runRep'), key=rp?J(rp)+chName(0)+chName(1)+chName(MON):'';
  if(el.dataset.k===key) return; el.dataset.k=key; el.hidden=!rp; if(!rp){ el.innerHTML=''; return; }
  const f1d=x=>(Math.round(x*10)/10).toFixed(1), line=(i,c,mon)=>{ if(!c||c.peak==null) return '';
    const parts=[`peak ${f1d(c.peak)} °C`+(c.peakT!=null?` at ${fmt(c.peakT)}`:'')];
    if(!mon&&c.dev!=null) parts.push(`hold ±${f1d(c.dev)} °C`);
    if(!mon&&c.rate!=null) parts.push(`max ${f1d(c.rate)} °C/s`);
    return `<div class="rl" style="--c:${PV[i]}"><i></i><span><b>${esc(chName(i))}</b> ${parts.join(' · ')}</span></div>`; };
  el.innerHTML=`<div class="rh"><b>Last run</b> · “${esc(rp.name||'')}” · ${rp.finished?'finished':'stopped'} · ${fmt(rp.dur||0)}</div>`+
    CH.map(i=>line(i,(rp.ch||[])[i])).join('')+line(MON,rp.mon,true);
}
function updateRun(){
  if(!live) return;
  const r=live.run, active=r.state==='running'||r.state==='paused', b=$('badge');
  if(active&&r.progPause){ b.textContent=r.actionRequired?'Action required':'Program pause'; b.className='state pause'; }
  else if(r.state==='running'){ b.textContent=r.waiting?'Waiting for '+chName(0):'Running'; b.className='state '+(r.waiting?'pause':'run'); }
  else if(r.state==='paused'){ b.textContent='Paused'; b.className='state pause'; }
  else if(r.state==='finished'){ b.textContent='Finished'; b.className='state fin'; }
  else if(r.state==='stopped'){ b.textContent='Stopped'; b.className='state stop'; }
  else if(live.ch.some(c=>c.manual.on)){ b.textContent='Manual'; b.className='state man'; }
  else { b.textContent='Ready'; b.className='state'; }
  const dr=DRAFT[live.sel];
  $('clock').textContent=r.prog>=0&&r.state!=='idle'?fmt(r.el):'0:00';
  const w=(active?hasWaits(runProg):hasWaits(dr))?' + waits':'';
  $('remain').textContent=active?`${fmt(r.rem)}${w} left${r.name?' · '+r.name:''}`:(M&&M.kind==='preview'?`${fmt(M.end)}${w} total`:'');
  { const pz=r.state==='running'||(active&&r.progPause); $('bStart').hidden=pz; $('bPause').hidden=!pz; $('bPause').classList.toggle('pri',!!(active&&r.progPause)); }
  $('bStart').disabled=r.state==='running'||(!active&&!(dr&&progItems(dr))); $('bStart').textContent=r.state==='paused'?'Resume':'Start';
  $('bPause').disabled=!(r.state==='running'||(active&&r.progPause)); $('bPause').textContent=active&&r.progPause?'Continue':'Pause';
  $('bStop').disabled=!active;
  $('bNext').disabled=!active||r.type!=='seq'||r.progPause||r.state==='paused';
  $('prog').disabled=active;
  note(r.intr&&/^Run interrupted/.test(r.msg||'')?'':r.msg,/cannot|fault|failed|not possible|no answer|e-stop|warning/i.test(r.msg||''));
  $('estopBar').hidden=!estopOn();
  runReport(active?null:live.report);
  // run interrupted by a reset: resume (never automatic) or dismiss
  const x=r.intr, ib=$('intr'); ib.hidden=!x;
  if(x){ const st=k=>({ramp:'ramp',hold:'hold',idle:'off',wait:'waiting',pause:'pause',done:'done'})[x.st[k]]||x.st[k];
    const where=r.type==='seq'?`phase ${x.i[0]+1}${r.phases?' of '+r.phases:''} (${st(0)})`:CH.map(k=>`${esc(chName(k))}: ${x.st[k]==='done'?'done':`step ${x.i[k]+1} ${st(k)}${x.left[k]?' ('+fmt(x.left[k])+' left)':''}`}`).join(', ');
    $('intrTxt').innerHTML=`(${esc(x.reason)}): “${esc(r.name||'')}” at run time ${x.exact?'':'about '}${fmt(x.el)}, ${where}.`;
    $('intrNote').textContent='Its channels were switched off. '+(r.type==='seq'?`Resume starts phase ${x.i[0]+1} again from the measured temperatures.`:'Resume goes on at that run time: ramps from the measured temperatures, holds with their remaining time.')+(x.exact?'':' (Position as of the last phase / step change or minute: the board was off.)');
    $('bRecover').textContent=r.type==='seq'?`Resume from phase ${x.i[0]+1}`:`Resume at ${fmt(x.el)}`; }
  const showHalt=active&&r.progPause&&r.actionRequired, hb=$('halt');
  if(hb.hidden===!!showHalt){ hb.hidden=!showHalt; if(showHalt) $('hCont').focus(); }
  if(showHalt){ $('haltText').textContent=r.pauseText||''; $('haltWhere').textContent=`${r.name}${r.seq?', phase '+(r.seq.idx+1):''}. Setpoints are kept until you continue.`; }
  // after a run with run edits: offer to save them
  const ended=(prevState==='running'||prevState==='paused')&&(r.state==='finished'||r.state==='stopped');
  if(ended){ showLast=true; askedSave=false; chartAt=0; }
  if((r.state==='finished'||r.state==='stopped')&&r.edited&&!askedSave){ askedSave=true; if(!$('saveDlg').open) $('saveDlg').showModal(); }
  if($('saveDlg').open&&!r.edited) $('saveDlg').close();  // answered on the touch screen or over serial
  if(r.state==='running'&&prevState!=='running'&&prevState!=='paused'){ runProg=null; zv.fit=true; showLast=true; if(view==='live') setView('prog'); }
  prevState=r.state;
}
const cmd=c=>api('/api/cmd',{cmd:c,prog:live?live.sel:-1}).then(()=>{chartAt=0;poll();});
$('bStart').onclick=async()=>{ if(!live) return; if(live.run.state==='paused'){ cmd('resume'); return; }
  if(live.run.intr&&!await ask('Start a new run? The interrupted run can no longer be resumed afterwards.','Start')) return;
  const i=live.sel; showLast=true;
  if(isDirty(i)){ const a=await choose(`“${DRAFT[i].name}” has unsaved changes.`,[['save','Save and start',true],['draft','Start without saving'],['cancel','Cancel']]);
    if(a==='save'){ if(await saveDraft(i)) cmd('start'); }
    else if(a==='draft'){ const r=await api('/api/run',{cmd:'start',i,program:denormProg(DRAFT[i])}); if(r.err) note(r.err,true); chartAt=0; poll(); }
    return; }
  cmd('start'); };
$('bPause').onclick=()=>{ if(live&&live.run.progPause) cmd('continue'); else cmd('pause'); };
$('bStop').onclick=async()=>{ if(await ask('Stop the program? Each channel applies its end action.','Stop')) cmd('stop'); };
$('bNext').onclick=()=>cmd('next');
$('bPreview').onclick=()=>{ showLast=false; previewDraft(); chartAt=0; };
// the device keeps the last run's chart (until a restart or the next run): the selected program's?
const lastRunOfSel=()=>!!live&&!runActive()&&live.run.prog===live.sel&&(live.run.state==='finished'||live.run.state==='stopped');
$('bLastRun').onclick=()=>{ showLast=true; zv.fit=true; chartAt=0; M=null; lastChart=0; poll(); };
$('hCont').onclick=()=>cmd('continue');
$('bRecover').onclick=async()=>{ const r=await api('/api/run',{cmd:'recover'}); if(r&&r.err) note(r.err,true); showLast=true; chartAt=0; poll(); };
$('bDismiss').onclick=async()=>{ if(await ask('Dismiss the interrupted run? Its channels stay off; it cannot be resumed afterwards.','Dismiss')){ await api('/api/run',{cmd:'dismiss'}); poll(); } };
$('hStop').onclick=async()=>{ if(await ask('Stop the program? Each channel applies its end action.','Stop')) cmd('stop'); };
$('dSave').onclick=async()=>{ $('saveDlg').close(); await api('/api/run',{cmd:'save'}); if(live){ savedGen[live.run.prog]=-1; delete DRAFT[live.run.prog]; } poll(); };
$('dDiscard').onclick=async()=>{ $('saveDlg').close(); await api('/api/run',{cmd:'discard'}).catch(()=>{}); poll(); };

