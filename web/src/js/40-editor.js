/* ---------- program editor (tables + chart handles; spec 9.3.3 / 9.3.4) ---------- */
const running=()=>!!live&&(live.run.state==='running'||live.run.state==='paused');
// a cooling fan is configured (an optional pin has a fan function): /api/live has "fan" then (as CFG.limits.fan, which
// the dashboard does not load): phases / steps get the "Fan" option; the flag stays in a program either way
const fanWired=()=>!!(live&&live.fan);
function editProg(){ return running()?runProg:(live?DRAFT[live.sel]:null); }
function curIdx(ch){ if(!running()) return -1; const r=live.run; if(r.type==='seq') return r.seq?r.seq.idx:-1; const L=live.ch[ch].lane; return L?(L.stage==='done'?1e9:L.idx):-1; }
function refTarget(p,ch,idx){ if(!p) return null; if(p.type==='seq'){ const x=p.phases[idx]; if(!x) return null;
  if(ch===0) return x.kind==='B'||(x.kind==='BT'&&!x.b.idle)?x.b.t:null; return x.kind==='T'||(x.kind==='BT'&&!x.t.idle)?x.t.t:null; }
  const x=p[KEY[ch]][idx]; return x&&x.kind==='step'?x.t:null; }
function rampLocked(ch,idx){ if(!running()) return false; const r=live.run;
  if(r.type==='seq'){ const c=r.seq?r.seq.idx:-1; if(idx>c) return false; return !(idx===c&&ch===1&&r.seq.stage==='wait'); }
  return idx<=curIdx(ch); }
const clampS=v=>Math.max(0,Math.min(MAXSEC,Math.round(v)));
const clampT=(ch,v)=>{ const c=live&&live.ch[ch]; return Math.round(Math.max(c?c.spMin:0,Math.min(c?c.spMax:500,v))*10)/10; };
function newPhase(kind){ return {kind,wait:false,msg:kind==='P',text:'',fan:false,b:{t:150,r:60,h:60,idle:false},t:{t:200,r:30,h:20}}; }
function newStep(){ return {kind:'step',t:180,r:30,h:30,msg:false,text:'',fan:false}; }
function newDraft(i,type,prog){ UNDO[i]=UNDO[i]||[]; REDO[i]=[]; UNDO[i].push(clone(DRAFT[i]===undefined?null:DRAFT[i]));
  DRAFT[i]=prog?clone(prog):type==='seq'?{name:`Program ${i+1}`,type:'seq',phases:[newPhase('B')]}:{name:`Program ${i+1}`,type:'tl',B:[newStep()],T:[]}; }

/* Sequence -> Timeline (spec 9.3.3): the same temperatures and times on two step lists. Phase by phase on
   one clock; a channel that "keeps its previous state" gets its last hold (or Idle) lengthened up to the
   next phase that gives it a target; Bottom jumps (Both) become ramp-0 steps; a Pause phase becomes a
   pause step (bottom list, or top if the bottom is not used: a pause step stops the whole run). "Wait for
   bottom" is dropped (the Timeline uses the programmed times, as the preview does). Returns {prog} or {err}. */
function seqToTl(p,name){
  const L=[[],[]], E=[0,0], used=[progUses(p,0),progUses(p,1)];
  const add=(c,x)=>{ while(x.h>MAXSEC){ L[c].push(Object.assign({},x,{h:MAXSEC})); x=Object.assign({},x,{r:0,h:x.h-MAXSEC}); } L[c].push(x); };
  const pad=(c,T)=>{ const gap=T-E[c]; E[c]=Math.max(E[c],T); if(gap<=0||!used[c]) return;
    let k=L[c].length-1; while(k>=0&&L[c][k].kind==='pause') --k;
    const last=L[c][k];
    if(k===L[c].length-1&&last&&last.h+gap<=MAXSEC&&!last.fan){ last.h+=gap; return; }  // the last step holds longer (not one that runs the fan)
    add(c,last&&last.kind==='step'?{kind:'step',t:last.t,r:0,h:gap,msg:false,text:''}:{kind:'idle',t:0,r:0,h:gap,msg:false,text:''}); };
  const step=(c,s,T,fan)=>{ pad(c,T); add(c,{kind:'step',t:s.t,r:s.r,h:s.h,msg:false,text:'',fan:!!fan}); E[c]=T+s.r+s.h; };
  // (a phase's fan flag goes to the step that spans the phase: the one that drives it; a pause phase's to its pause step)
  let T=0;
  for(const x of p.phases){
    if(x.kind==='B'){ step(0,x.b,T,x.fan); T+=x.b.r+x.b.h; }
    else if(x.kind==='T'){ step(1,x.t,T,x.fan); T+=x.t.r+x.t.h; }
    else if(bothOff(x)){ for(const c of CH){ pad(c,T); if(used[c]) add(c,{kind:'idle',t:0,r:0,h:x.b.h,msg:false,text:'',fan:!!x.fan}); E[c]=T+x.b.h; } T+=x.b.h; }
    else if(x.kind==='BT'&&x.t.idle){ pad(1,T); L[1].push({kind:'idle',t:0,r:0,h:0,msg:false,text:''}); E[1]=T;
      step(0,x.b,T,x.fan); T+=x.b.r+x.b.h; }
    else if(x.kind==='BT'){ pad(0,T); L[0].push(x.b.idle?{kind:'idle',t:0,r:0,h:0,msg:false,text:''}:{kind:'step',t:x.b.t,r:0,h:0,msg:false,text:''}); E[0]=T;
      step(1,x.t,T,x.fan); T+=x.t.r+x.t.h; }
    else { const c=used[0]||!used[1]?0:1; pad(c,T); L[c].push({kind:'pause',t:0,r:0,h:0,msg:!!x.msg,text:x.text||'',fan:!!x.fan}); }
  }
  const over=CH.filter(c=>L[c].length>MAXN);
  if(over.length) return {err:`The Timeline would need more than ${MAXN} steps for ${over.map(chName).join(' and ')}.`};
  return {prog:{name:(name||p.name).slice(0,31),type:'tl',B:L[0],T:L[1]}};
}
async function convertToTl(){
  if(running()||!live) return; const i=live.sel, p=DRAFT[i]; if(!p||p.type!=='seq') return;
  const waits=p.phases.map((x,k)=>x.wait&&(x.kind==='B'||(x.kind==='BT'&&!x.b.idle))?k+1:0).filter(Boolean);
  const free=progs?progs.programs.findIndex((q,k)=>!q.used&&!DRAFT[k]):-1;
  const a=await choose(`Convert “${p.name}” to a Timeline?\nA Timeline can't be turned back into a Sequence.`+
    (waits.length?`\n“Wait for ${chName(0)}” is dropped (phase${waits.length>1?'s':''} ${waits.join(', ')}): the Timeline runs on the programmed times.`:'')+
    `\n\nReplace this program: Undo brings the Sequence back until you save.\n`+(free<0?'No empty slot left for a new program.':`Create a new program: the Sequence stays, the Timeline goes to slot ${free+1}.`),
    [['replace','Replace this program',true],...(free<0?[]:[['new','Create a new program']]),['cancel','Cancel']]);
  if(a==='replace'){ const r=seqToTl(p); if(r.err){ edNote(r.err,true); return; }
    const q=beginEdit(); if(!q) return; DRAFT[i]=r.prog; afterEdit(); edNote('Converted to a Timeline - not saved yet (Undo brings the Sequence back).'); }
  else if(a==='new'){ const r=seqToTl(p,p.name+' (Timeline)'); if(r.err){ edNote(r.err,true); return; }
    await openProg(free,'tl',r.prog); edNote(`The Timeline is in slot ${free+1} - not saved yet.`); }
}
$('toTl').onclick=()=>convertToTl();

function beginEdit(){ if(running()||!live) return null; const i=live.sel; if(!DRAFT[i]) return null;
  UNDO[i].push(clone(DRAFT[i])); if(UNDO[i].length>200) UNDO[i].shift(); REDO[i]=[]; return DRAFT[i]; }
function blurEditor(){ const a=document.activeElement; if(a&&$('edP').contains(a)&&a.matches('input')) a.blur(); }
function afterEdit(){ const i=live.sel, u=UNDO[i]; if(u.length&&J(u[u.length-1])===J(DRAFT[i])) u.pop();
  showLast=false; previewDraft(); edKey=''; fillProgSelect(); updateEditor(); }
async function saveDraft(i){ const r=await api('/api/program',{i,program:denormProg(DRAFT[i])});
  if(r.err){ edNote(r.err,true); return false; }
  SAVED[i]=normProg(r.program); DRAFT[i]=clone(SAVED[i]); savedGen[i]=-1; edNote('Saved.'); await loadPrograms(); edKey=''; updateEditor(); return true; }
function edNote(t,bad){ const m=$('edMsg'); m.textContent=t||''; m.className='msg'+(bad?' bad':''); }
function toolAct(a){ if(running()||!live) return; const i=live.sel; if(!UNDO[i]) return;
  if(a==='undo'&&UNDO[i].length){ REDO[i].push(clone(DRAFT[i])); DRAFT[i]=UNDO[i].pop(); }
  else if(a==='redo'&&REDO[i].length){ UNDO[i].push(clone(DRAFT[i])); DRAFT[i]=REDO[i].pop(); }
  else if(a==='save'){ saveDraft(i); return; }
  else if(a==='discard'&&isDirty(i)){ UNDO[i].push(clone(DRAFT[i])); REDO[i]=[]; DRAFT[i]=clone(SAVED[i]); }
  else return;
  blurEditor(); edNote(''); showLast=false; previewDraft(); edKey=''; fillProgSelect(); updateEditor(); }
document.addEventListener('click',e=>{ const b=e.target.closest('button[data-act]'); if(b) toolAct(b.dataset.act); });
document.addEventListener('keydown',e=>{ if(!(e.ctrlKey||e.metaKey)||e.target.matches('input,select,textarea')) return;
  const k=e.key.toLowerCase(); if(k==='z'&&!e.shiftKey){ toolAct('undo'); e.preventDefault(); } else if(k==='y'||(k==='z'&&e.shiftKey)){ toolAct('redo'); e.preventDefault(); } });

function numIn(f,v,en,lab,extra){ return `<input type="number" class="v" data-f="${f}" value="${v}" step="1" min="0" ${en?'':'disabled'} aria-label="${esc(lab)}" ${extra||''}>`; }
// ramp / hold times: m:ss (h:mm:ss), seconds also accepted
function timeIn(f,v,en,lab){ return `<input type="text" inputmode="numeric" class="v tm" data-f="${f}" data-tm value="${fmt(v)}" ${en?'':'disabled'} aria-label="${esc(lab)}" title="m:ss or seconds" autocomplete="off">`; }
function parseT(t){ t=String(t).trim(); if(!t) return NaN; const p=t.split(':'); if(p.length>3||p.some(x=>!/^\d+(\.\d+)?$/.test(x))) return NaN;
  return p.reduce((a,x)=>a*60+parseFloat(x),0); }
// the reorder handle of a phase / step row (rowDrag below)
function grip(what){ return `<button type="button" class="grip" data-grip aria-label="Move ${esc(what)}: drag, or Alt+Up / Alt+Down" title="Drag to move (Alt+↑ / ↓)"><svg viewBox="0 0 10 16" aria-hidden="true"><circle cx="2.5" cy="3" r="1.5"/><circle cx="7.5" cy="3" r="1.5"/><circle cx="2.5" cy="8" r="1.5"/><circle cx="7.5" cy="8" r="1.5"/><circle cx="2.5" cy="13" r="1.5"/><circle cx="7.5" cy="13" r="1.5"/></svg></button>`; }
function buildEditor(){
  const p=editProg(), run=running(), i=live?live.sel:0, box=$('editor');
  $('pName').disabled=run||!p; if(document.activeElement!==$('pName')) $('pName').value=p?p.name:'';
  if(!p){ $('edTitle').textContent=`Program ${i+1}`; $('edHint').textContent='This slot is empty.';
    box.innerHTML=`<div class="edbar"><button data-new="seq" ${run?'disabled':''}>New sequence</button><button data-new="tl" ${run?'disabled':''}>New timeline</button></div>`; return; }
  const n=[chName(0),chName(1)], fanOn=fanWired();
  $('edTitle').textContent=`${p.name} —${p.type==='seq'?'sequence':'timeline'}${run?' (running copy)':''}`;
  const ph=p.type==='seq'?'phases':'steps';
  $('edHint').textContent=run?`Editing the running copy: finished ${ph} are locked (a target the channel still keeps can change); in the current one the targets and the hold. Changes go to the device at once; after the run you can save them into the program.`
    :'Times as m:ss (or seconds); a ramp of 0 is a jump. The step type and every darker field can be changed; on the chart: drag squares up / down (target), diamonds (ramp end) and circles (hold end) left / right (touch screen: hold a point briefly, then drag).';
  const kept=[live&&live.ch[0].kept,live&&live.ch[1].kept];
  if(p.type==='seq'){
    const cur=run?curIdx(0):-1, narrow=box.clientWidth<720;
    let tEnd=0; const ends=p.phases.map(x=>(tEnd+=x.kind==='P'?0:bothOff(x)?x.b.h:bDrv(x)?x.b.r+x.b.h:x.t.r+x.t.h));
    const na=t=>`<span class="na">${t}</span>`, del=k=>`<button class="x" data-del="${k}" ${run?'disabled':''} aria-label="Delete phase ${k+1}" title="Delete phase">✕</button>`;
    const gr=k=>run||p.phases.length<2?'':grip(`phase ${k+1}`);
    const rows=p.phases.map((x,k)=>{
      const done=run&&k<cur, isCur=run&&k===cur, hasB=x.kind==='B'||x.kind==='BT', hasT=x.kind==='T'||x.kind==='BT', bIdle=x.kind==='BT'&&x.b.idle, tIdle=x.kind==='BT'&&x.t.idle, bD=bDrv(x), isP=x.kind==='P';
      const en=f=>{ if(!run) return true; if(done||(isCur&&isP)) return (f==='b.t'&&k===kept[0])||(f==='t.t'&&k===kept[1]);
        if(isCur){ if(f==='b.t') return hasB&&!bIdle; if(f==='t.t') return hasT&&!tIdle; if(f==='b.h') return bD; if(f==='t.h') return hasT&&!tIdle; if(f==='t.r') return !tIdle&&!rampLocked(1,k); return false; }
        return ['b.t','t.t','b.r','t.r','b.h','t.h'].includes(f); };
      const opt=(v,t)=>`<option value="${v}"${x.kind===v?' selected':''}>${t}</option>`;
      const sel=`<select class="k${isP?' pause':''}" data-f="kind" ${run?'disabled':''} aria-label="Phase ${k+1} type">${opt('B',esc(n[0]))}${opt('T',esc(n[1]))}${opt('BT','Both')}${opt('P','Pause')}</select>`;
      const cb=(f,on,lab,dis)=>`<label class="cbl"><input type="checkbox" data-f="${f}" ${on?'checked':''} ${dis?'disabled':''}> ${lab}</label>`;
      const fcb=fanOn?cb('fan',x.fan,'Fan',run):'';  // (the cooling fan runs in this phase; only with a fan configured)
      const c={
        bt: hasB&&!bIdle?numIn('b.t',x.b.t,en('b.t'),`${n[0]} °C, phase ${k+1}`):na(bIdle?'off':'keeps'),
        br: bD&&!bIdle?timeIn('b.r',x.b.r,en('b.r'),`${n[0]} ramp, phase ${k+1}`):na(x.kind==='BT'&&!bIdle?'jump':'—'),
        bh: bD?timeIn('b.h',x.b.h,en('b.h'),bIdle?`Both off, time, phase ${k+1}`:`${n[0]} hold, phase ${k+1}`):na('—'),
        bi: x.kind==='BT'?cb('b.idle',x.b.idle,'Idle',run):'',
        tt: hasT&&!tIdle?numIn('t.t',x.t.t,en('t.t'),`${n[1]} °C, phase ${k+1}`):na(tIdle?'off':'keeps'),
        tr: hasT&&!tIdle?timeIn('t.r',x.t.r,en('t.r'),`${n[1]} ramp, phase ${k+1}`):na('—'),
        th: hasT&&!tIdle?timeIn('t.h',x.t.h,en('t.h'),`${n[1]} hold, phase ${k+1}`):na('—'),
        ti: x.kind==='BT'?cb('t.idle',x.t.idle,'Idle',run):'',
        opt: isP?cb('msg',x.msg,'"Action required"',run)+` <input type="text" data-f="text" maxlength="40" value="${esc(x.text)}" placeholder="text (optional)" ${run?'disabled':''} aria-label="Pause text, phase ${k+1}">`+fcb
          :(x.kind!=='T'?cb('wait',x.wait,`Wait for ${esc(n[0])}`,run||bIdle)+fcb:fcb||na('—'))
      };
      const cls=`${done?'done':''}${isCur?' cur':''}`;
      if(narrow) return `<div class="pc ${cls}" data-i="${k}"><div class="pch">${gr(k)}<b>${k+1}</b>${sel}<span class="grow"></span>${del(k)}</div>`+
        (isP?`<div class="pcr"><span class="na">Both channels keep their setpoints.</span></div><div class="pcr">${c.opt}</div>`:
        `<div class="pcr"><span class="pl gB">${esc(n[0])}</span>${!hasB?na('keeps its setpoint'):bIdle&&tIdle?na('off')+`<label class="fl"><span>Both off for</span>${c.bh}</label>`+c.bi:bIdle?na('off')+c.bi:x.kind==='BT'&&!bD?`<label class="fl"><span>°C</span>${c.bt}</label>${na('jump')}${c.bi}`:`<label class="fl"><span>°C</span>${c.bt}</label><label class="fl"><span>Ramp</span>${c.br}</label><label class="fl"><span>Hold</span>${c.bh}</label>${c.bi}`}</div>
         <div class="pcr"><span class="pl gT">${esc(n[1])}</span>${!hasT?na('keeps its setpoint'):tIdle?na('off')+c.ti:`<label class="fl"><span>°C</span>${c.tt}</label><label class="fl"><span>Ramp</span>${c.tr}</label><label class="fl"><span>Hold</span>${c.th}</label>${c.ti}`}</div>
         ${x.kind!=='T'||fanOn?`<div class="pcr">${c.opt}</div>`:''}`)+`</div>`;
      return `<tr class="${cls}" data-i="${k}"><td class="nr">${gr(k)}${k+1}</td><td>${sel}</td>`+
        (isP?`<td class="bl" colspan="4">${na('keeps its setpoint')}</td><td class="bl" colspan="4">${na('keeps its setpoint')}</td>`:
        `<td class="bl">${c.bt}</td><td>${c.br}</td><td>${c.bh}</td><td>${c.bi}</td><td class="bl">${c.tt}</td><td>${c.tr}</td><td>${c.th}</td><td>${c.ti}</td>`)+
        `<td class="bl">${c.opt}</td><td class="end">${fmt(ends[k])}</td><td>${del(k)}</td></tr>`;
    });
    let h=narrow?`<div class="pcs">${rows.join('')}</div>`:
      `<div class="ed-wrap"><table class="ed"><thead><tr><th rowspan="2">#</th><th rowspan="2">Phase type</th><th colspan="4" class="gB bl">${esc(n[0])}</th><th colspan="4" class="gT bl">${esc(n[1])}</th><th rowspan="2" class="bl">Option</th><th rowspan="2" class="r">Ends</th><th rowspan="2"></th></tr>
      <tr><th class="bl">°C</th><th>Ramp</th><th>Hold</th><th></th><th class="bl">°C</th><th>Ramp</th><th>Hold</th><th></th></tr></thead><tbody>${rows.join('')}</tbody></table></div>`;
    h+=`<div class="edbar"><button id="addPh" ${run||p.phases.length>=MAXN?'disabled':''}>Add phase</button><span class="zr">${p.phases.length} / ${MAXN} phases</span></div>`;
    box.innerHTML=h;
  } else {
    const side=ch=>{ const k=KEY[ch], L=p[k], q=run?live.ch[ch].lane:null, keptI=run?kept[ch]:-1;
      let h=`<h3 style="color:${PV[ch]}">${esc(n[ch])}</h3><div class="ed-wrap"><table class="ed"><thead><tr><th>#</th><th>Step type</th><th>°C</th><th>Ramp</th><th>Hold</th>${fanOn?'<th title="The cooling fan runs during this step">Fan</th>':''}<th class="r">Ends</th><th></th></tr></thead><tbody>`;
      let tEnd=0;
      L.forEach((x,j)=>{ tEnd+=x.kind==='pause'?0:x.kind==='idle'?x.h:x.r+x.h;
        const done=run&&q&&(q.stage==='done'||j<q.idx), isCur=run&&q&&q.stage!=='done'&&j===q.idx;
        const en=f=>{ if(done) return f==='t'&&j===keptI; if(!run) return true; if(isCur) return x.kind!=='pause'&&(f==='h'||(f==='t'&&x.kind==='step')); return x.kind!=='pause'&&f!=='kind'; };
        const opt=(v,t)=>`<option value="${v}"${x.kind===v?' selected':''}>${t}</option>`, nm=`${n[ch]} step ${j+1}`;
        h+=`<tr class="${done?'done':''}${isCur?' cur':''}" data-k="${k}" data-i="${j}"><td class="nr">${run||L.length<2?'':grip(nm)}${j+1}</td>
          <td><select class="k${x.kind==='step'?'':' '+x.kind}" data-f="kind" ${run?'disabled':''} aria-label="${esc(nm)} type">${opt('step','Step')}${opt('idle','Idle')}${opt('pause','Pause')}</select></td>`;
        if(x.kind==='pause') h+=`<td colspan="3"><label class="cbl"><input type="checkbox" data-f="msg" ${x.msg?'checked':''} ${run?'disabled':''}> "Action required"</label>
          <input type="text" data-f="text" maxlength="40" value="${esc(x.text)}" placeholder="text (optional)" ${run?'disabled':''} aria-label="${esc(nm)} pause text"></td>`;
        else h+=`<td>${x.kind==='idle'?'<span class="na">off</span>':numIn('t',x.t,en('t'),nm+' °C')}</td>
          <td>${x.kind==='idle'?'<span class="na">—</span>':timeIn('r',x.r,en('r')&&!rampLocked(ch,j),nm+' ramp')}</td>
          <td>${timeIn('h',x.h,en('h'),nm+(x.kind==='idle'?' idle time':' hold'))}</td>`;
        if(fanOn) h+=`<td><input type="checkbox" data-f="fan" ${x.fan?'checked':''} ${run?'disabled':''} aria-label="${esc(nm)} fan"></td>`;
        h+=`<td class="end">${fmt(tEnd)}</td><td><button class="x" data-del="${j}" ${run?'disabled':''} aria-label="Delete ${esc(nm)}" title="Delete step">✕</button></td></tr>`; });
      h+=`</tbody></table></div><div class="edbar"><button data-add="${k}" ${run||L.length>=MAXN?'disabled':''}>Add step</button><span class="zr">${L.length} / ${MAXN}</span></div>`; return h; };
    box.innerHTML=`<div class="tl2"><div>${side(0)}</div><div>${side(1)}</div></div>`;
  }
}
function updateEditor(){
  if(!live) return;
  const p=editProg(), run=running(), i=live.sel, r=live.run;
  const pos=run?J([r.seq&&[r.seq.idx,r.seq.stage],live.ch.map(c=>[c.lane&&c.lane.idx,c.lane&&c.lane.stage,c.kept])]):'';
  const key=($('editor').clientWidth<720?'n':'w')+J(p)+run+pos+i+chName(0)+chName(1)+fanWired();
  const ae=document.activeElement, typing=ae&&$('edP').contains(ae)&&ae.matches('input[type=number],input[type=text]');
  if(key!==edKey&&!typing&&!rowDrag){ edKey=key; buildEditor(); }
  const d=!run&&isDirty(i);
  document.querySelectorAll('button[data-act]').forEach(b=>{ const a=b.dataset.act;
    b.disabled=run||!UNDO[i]||(a==='undo'&&!UNDO[i].length)||(a==='redo'&&!REDO[i].length)||((a==='save'||a==='discard')&&!d); });
  $('unsaved').classList.toggle('on',!!d);
  const cp=!run&&DRAFT[i]; $('toTl').hidden=!(cp&&cp.type==='seq'&&cp.phases.length);
}
$('pName').onchange=e=>{ const v=e.target.value.trim().slice(0,31); if(!v){ e.target.value=DRAFT[live.sel]?DRAFT[live.sel].name:''; return; }
  const q=beginEdit(); if(!q) return; q.name=v; afterEdit(); };
$('editor').addEventListener('change',async e=>{
  const el=e.target, f=el.dataset.f; if(!f||!live) return;
  const row=el.closest('[data-i]'), i=+row.dataset.i, k=row.dataset.k;
  let v=el.type==='checkbox'?el.checked:(f==='kind'||f==='text')?el.value:el.dataset.tm!=null?parseT(el.value):parseFloat(el.value);
  if(typeof v==='number'&&isNaN(v)){ edKey=''; updateEditor(); return; }
  const p=editProg(); if(!p) return; const seq=p.type==='seq';
  if(running()){
    let body=null;
    if(seq){ if(f==='b.t'||f==='t.t') body={cmd:'target',ch:f==='b.t'?0:1,idx:i,t:v}; else if(f==='b.r'||f==='t.r') body={cmd:'ramp',ch:f==='b.r'?0:1,idx:i,sec:clampS(v)};
      else if(f==='b.h'||f==='t.h') body={cmd:'hold',ch:f==='b.h'?0:1,idx:i,sec:clampS(v)}; }
    else { const ch=k==='B'?0:1; if(f==='t') body={cmd:'target',ch,idx:i,t:v}; else if(f==='r') body={cmd:'ramp',ch,idx:i,sec:clampS(v)}; else if(f==='h') body={cmd:'hold',ch,idx:i,sec:clampS(v)}; }
    if(body) await runCmd(body); edKey=''; poll(); return;
  }
  const q=beginEdit(); if(!q) return;
  if(seq){ const x=q.phases[i];
    if(f==='kind'){ x.kind=v; if(v==='P') x.msg=true; } else if(f==='wait'||f==='msg'||f==='fan') x[f]=v; else if(f==='b.idle'){ x.b.idle=v; if(v&&x.t.idle&&!x.b.h) x.b.h=x.t.h; }  // (both off: the phase's time is the bottom's hold)
    else if(f==='t.idle'){ x.t.idle=v; if(v&&!x.b.r&&!x.b.h){ x.b.r=x.t.r; x.b.h=x.t.h; } }  // (top off: the bottom drives, with the top's times if it had none)
    else if(f==='text') x.text=v.slice(0,40);
    else { const [a,b]=f.split('.'); x[a][b]=b==='t'?clampT(a==='b'?0:1,v):clampS(v); } }
  else { const x=q[k][i]; if(f==='kind'){ x.kind=v; if(v==='pause') x.msg=true; } else if(f==='msg') x.msg=v; else if(f==='fan') x.fan=v; else if(f==='text') x.text=v.slice(0,40); else x[f]=f==='t'?clampT(k==='B'?0:1,v):clampS(v); }
  afterEdit();
});
$('editor').addEventListener('click',e=>{
  const b=e.target.closest('button'); if(!b||!live||running()||b.dataset.act||b.dataset.grip!=null) return; const i=live.sel;
  if(b.dataset.new){ newDraft(i,b.dataset.new); afterEdit(); return; }
  const q=beginEdit(); if(!q) return;
  if(b.id==='addPh'&&q.phases.length<MAXN) q.phases.push(newPhase('T'));
  else if(b.dataset.add&&q[b.dataset.add].length<MAXN) q[b.dataset.add].push(newStep());
  else if(b.dataset.del!=null){ const tr=b.closest('[data-i]'); if(q.type==='seq') q.phases.splice(+b.dataset.del,1); else q[tr.dataset.k].splice(+b.dataset.del,1); }
  afterEdit();
});

