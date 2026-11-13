// ---- autotune from the Dashboard and the sidebar: stop it, clear a failed / stopped one, review a result
async function tuneAction(i,a){
  if(a==='stop'){ if(!await ask(`Stop the autotune of ${chName(i)}? Its outputs go off.`,'Stop autotune')) return; await api('/api/autotune',{ch:i,cmd:'stop'}); }
  else if(a==='clear') await api('/api/autotune',{ch:i,cmd:'discard'});
  else if(a==='review'&&live&&live.ch[i].autotune) return tuneDialog(i,live.ch[i].autotune);
  poll(); }
// a finished autotune: the dialog opens by itself once per result while the Dashboard is shown
const tuneAsked={};
function tuneOffer(i,a){ const k=`${a.ku}/${a.pu}/${a.kp}`; if(tuneAsked[i]===k) return; if(page!=='status'||$('tuneDlg').open||document.querySelector('dialog[open]')) return;
  tuneAsked[i]=k; tuneDialog(i,a); }
let tuneCh=-1, tuneEdit=false, tuneModel=null;
// the heater model of an autotune result (feed-forward, spec 9.23); l = the channel's current local settings
function tuneModelText(a,l){ const m=a.model;
  if(!m) return 'No heater model from this test (feed-forward keeps its current model).';
  return `Heater model: ${(+m.k).toFixed(2)} °C per % output, time constant ${Math.round(m.tau)} s, dead time ${(+m.dead).toFixed(1)} s`+
    (l?` (current ${(+l.ffK).toFixed(2)} / ${Math.round(l.ffTau)} s / ${(+l.ffDead).toFixed(1)} s). Accept takes it over; feed-forward stays ${l.ff?'on':'off'}.`:'.'); }
async function tuneDialog(i,a){
  if(!CFG) await loadCfg();
  const l=CFG&&CFG.channels[i]?CFG.channels[i].local:null; tuneCh=i; tuneEdit=false;
  $('tdTitle').textContent=`${chName(i)}: autotune done`;
  $('tdTxt').textContent=`Test at ${f1(a.sp)} °C: Ku ${(+a.ku).toFixed(2)}, Pu ${Math.round(a.pu)} s. Accept the proposed PID values, edit them first, or dismiss them (the current values stay). ${tuneModelText(a,l)}`;
  tuneModel=a.model||null;
  const cur=k=>l?(+l[k]):NaN, D={kp:3,ki:5,kd:2};
  $('tdBody').innerHTML=['kp','ki','kd'].map(k=>`<tr><td>${k[0].toUpperCase()+k.slice(1)}</td><td class="n">${isNaN(cur(k))?'–':cur(k).toFixed(D[k])}</td>
    <td class="n"><span id="tdv_${k}">${(+a[k]).toFixed(D[k])}</span><input type="number" id="tdi_${k}" step="any" value="${(+a[k]).toFixed(D[k])}" hidden aria-label="New ${k}"></td></tr>`).join('');
  $('tdAccept').textContent='Accept'; $('tdEdit').hidden=false; setMsg('tdMsg','');
  $('tuneDlg').showModal(); }
$('tdEdit').onclick=()=>{ tuneEdit=true; for(const k of ['kp','ki','kd']){ $('tdv_'+k).hidden=true; $('tdi_'+k).hidden=false; } $('tdEdit').hidden=true; $('tdAccept').textContent='Save these values'; $('tdi_kp').focus(); };
$('tdAccept').onclick=async()=>{ const i=tuneCh; if(i<0) return;
  if(!tuneEdit){ const r=await api('/api/autotune',{ch:i,cmd:'accept'}); if(r.err) return setMsg('tdMsg',r.err,true); }
  else { const body={}, v={}; for(const k of ['kp','ki','kd']){ v[k]=parseFloat($('tdi_'+k).value); if(isNaN(v[k])||v[k]<0) return setMsg('tdMsg','Enter numbers (0 or more).',true); sp(body,`channels.${i}.local.${k}`,v[k]); }
    if(tuneModel){ sp(body,`channels.${i}.local.ffK`,+tuneModel.k); sp(body,`channels.${i}.local.ffTau`,+tuneModel.tau); sp(body,`channels.${i}.local.ffDead`,+tuneModel.dead); }  // (as Accept does)
    const r=await postCfg(body); if(r.err) return setMsg('tdMsg',r.err,true); CFG=r; await api('/api/autotune',{ch:i,cmd:'discard'}); }
  $('tuneDlg').close(); await loadCfg(); note(`${chName(i)}: new PID values${tuneModel?' and heater model':''} saved.`); poll(); };
$('tdDismiss').onclick=async()=>{ const i=tuneCh; $('tuneDlg').close(); if(i>=0) await api('/api/autotune',{ch:i,cmd:'discard'}); poll(); };
$('tdLater').onclick=()=>$('tuneDlg').close();
$('chans').addEventListener('click',async e=>{
  const b=e.target.closest('button'); if(!b||!live) return;
  const d=b.dataset;
  if(d.dt!=null){ const i=+d.dt, t=live.ch[i].target; if(t!=null) await runCmd({cmd:'target',ch:i,idx:-1,t:Math.round(t+(+d.v))}); }
  else if(d.set!=null){ const i=+d.set, v=parseFloat($('tg'+i).value); if(!isNaN(v)){ await runCmd({cmd:'target',ch:i,idx:-1,t:v}); $('tg'+i).blur(); } }
  else if(d.ex!=null) await runCmd({cmd:'extend',ch:+d.ex,sec:+d.v});
  else if(d.mset!=null){ const i=+d.mset; await api('/api/manual',{ch:i,sp:+$('manSp'+i).value}); $('manSp'+i).blur(); }
  else if(d.ack!=null) await api('/api/ack',{ch:+d.ack});
  poll();
});
async function runCmd(body){ await api('/api/run',body); runProgAt=0; chartAt=0; }
const ST_TXT={none:'',off:'Off',ramp:'Ramp',hold:'Hold',wait:'Waiting',jump:'Jump target',keep:'Keeps target',halt:'Program pause',pause:'Paused',done:'Done'};
function whyText(c,i,r,active){
  const T=c.target!=null?f1(c.target)+' °C':'';
  if(c.type==='off') return 'Channel off (Settings).';
  if(c.fault) return '';
  if(estopOn()) return 'E-stop: outputs off.';
  if(c.autotune){ const a=c.autotune; return a.state==='running'?`Autotune at ${f1(a.sp)} °C: ${a.msg} (${fmt(a.elapsed)}).`:a.state==='done'?`Autotune ${a.msg} — review the result.`:`Autotune failed: ${a.msg}.`; }
  if(!active||!c.inProgram){
    if(active) return `No ${r.type==='tl'?'steps':'phases'} for this channel in “${r.name}”: controlled manually.`;
    if(c.manual.on) return `Manual setpoint ${f1(c.manual.sp)} °C${c.manual.synced?'':' (writing…)'}.`;
    return c.ok?'Idle.':'No answer: '+(c.err||'');
  }
  const seq=r.type==='seq', L=c.lane, N=seq?r.seq.idx+1:(L?L.idx+1:0), W=seq?`Phase ${N}`:`Step ${N}`;
  switch(c.state){
    case 'wait': return `${W}: waiting until PV is within ±${live.band} °C of ${T}.`;
    case 'ramp': return `${W}: ramping to ${T}.`;
    case 'hold': { const s=seq?r.seq:L; return `${W}: hold ${fmt(s.el)} of ${fmt(s.dur)}.`; }
    case 'jump': return `${W}: setpoint jumped to ${T}.`;
    case 'keep': return `Keeps ${T}${c.kept>=0?` from ${seq?'phase':'step'} ${c.kept+1}`:''}; the target can still be changed.`;
    case 'done': return `All steps done: keeps ${T} until the other channel is done.`;
    case 'halt': return `Program pause: ${c.idle?'outputs stay off':'setpoint kept at '+T}. Press Continue.`;
    case 'pause': return 'Paused: timers and setpoints stop.';
    case 'off': return c.idle?(seq?'Outputs off until a phase gives this channel a target.':(L&&L.stage==='idle'?`${W}: Idle ${fmt(L.el)} of ${fmt(L.dur)} (outputs off).`:'Outputs off.')):'';
  }
  return '';
}
function updateChans(){
  if(!live) return;
  const key=live.ch.map(c=>c.name+c.type).join('|');
  if(key!==chansKey){ chansKey=key; buildChans(); legend(); }
  const r=live.run, active=r.state==='running'||r.state==='paused';
  for(const i of CH){
    const c=live.ch[i], man=!(active&&c.inProgram)&&c.type!=='off';
    $('nm'+i).textContent=c.name; $('ty'+i).textContent=c.type==='modbus'?'Modbus':c.type==='local'?'local':'off';
    $('pv'+i).textContent=c.pv!=null?f1(c.pv)+' °C':(c.ok&&c.pvFlag&&c.pvFlag!=='ok'?c.pvFlag:'–');
    $('sp'+i).textContent=c.sp!=null?f1(c.sp)+' °C':(active&&c.inProgram&&c.idle?'off':'–');
    $('out'+i).style.width=(c.out!=null?Math.max(0,Math.min(100,c.out)):0)+'%'; $('ov'+i).textContent=c.out!=null?Math.round(c.out)+' %':'';
    const tune=c.autotune&&(c.autotune.state==='running'||c.autotune.state==='done')?c.autotune.state:'';  // (the touch screen shows the same)
    let st=c.fault?'Fault':active&&c.inProgram?ST_TXT[c.state]||c.state:tune==='running'?'Autotune':tune==='done'?'Autotune done':c.manual.on?'Manual':c.type==='off'?'Off':c.ok?'Idle':'No link';
    $('st'+i).textContent=st; $('st'+i).className='state '+(c.fault||(!c.ok&&c.type!=='off')?'fault':active&&c.inProgram?(c.state==='halt'||c.state==='pause'||c.state==='wait'?'pause':'run'):tune==='running'?'run':tune==='done'?'pause':c.manual.on?'man':'');
    $('card'+i).classList.toggle('on',!!c.outOn);
    $('why'+i).textContent=whyText(c,i,r,active);
    $('flt'+i).hidden=!c.fault; $('fltT'+i).textContent=c.fault?'Fault: '+c.fault:''; $('fwn'+i).hidden=!c.fault&&!(estopOn()&&c.type!=='off');
    $('runRows'+i).hidden=!(active&&c.inProgram);
    const canT=active&&c.inProgram&&c.kept>=0&&c.target!=null;
    document.querySelectorAll(`[data-dt="${i}"],[data-set="${i}"]`).forEach(b=>b.disabled=!canT);
    const tg=$('tg'+i); tg.disabled=!canT; if(document.activeElement!==tg) tg.value=c.target!=null?Math.round(c.target*10)/10:''; tg.min=c.spMin; tg.max=c.spMax;
    document.querySelectorAll(`[data-ex="${i}"]`).forEach(b=>b.disabled=!c.canExtend);
    const at=c.autotune?c.autotune.state:'';  // running / done / failed (stopped, time limit, ...)
    $('manRow'+i).hidden=!man||at==='running'||at==='done';
    $('atRow'+i).hidden=!at; for(const b of $('atRow'+i).querySelectorAll('[data-atx]')) b.hidden=b.dataset.atx!==(at==='running'?'stop':at==='done'?'review':'clear');
    if(at==='done') tuneOffer(i,c.autotune);
    const sw=$('man'+i); if(!sw.busy||Date.now()-sw.busy>1500){ sw.checked=c.manual.on; sw.busy=0; } sw.disabled=!!c.fault;
    const ms=$('manSp'+i); if(document.activeElement!==ms) ms.value=c.manual.sp!=null?c.manual.sp:''; ms.min=c.spMin; ms.max=c.spMax;
  }
}

