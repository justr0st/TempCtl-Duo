/* ---------- Programs page ---------- */
let plKey='', plAll=false;
const estopOn=()=>!!(live&&live.safety&&live.safety.estop);
const runActive=()=>!!live&&(live.run.state==='running'||live.run.state==='paused');
function renderProgs(force){
  if(!progs||!live) return;
  const act=runActive(), dirty=progs.programs.map((p,i)=>isDirty(i)), key=J([progs,live.sel,act,live.run.prog,dirty,chName(0),chName(1),Object.entries(DRAFT).map(([k,d])=>k+(d?d.name:''))]);
  if(!force&&key===plKey+plAll) return; plKey=key+plAll;
  const empty=progs.programs.map((p,i)=>!p.used&&!DRAFT[i]?i:-1).filter(i=>i>=0);
  $('plHint').textContent=`${progs.max-empty.length} of ${progs.max} slots used · Start on the Dashboard runs the selected program`;
  const emptyRow=empty.length&&!plAll?`<div class="prow empty" data-i="${empty[0]}"><span class="no"></span><span class="pn">${empty.length} empty slot${empty.length>1?'s':''}</span>
    <span class="pm">New programs go to slot ${empty[0]+1}. <button data-pa="all">Show all slots</button></span><span class="pa"><button data-pa="new-seq" ${act?'disabled':''}>New sequence</button><button data-pa="new-tl" ${act?'disabled':''}>New timeline</button><button data-pa="import">Import…</button></span></div>`:'';
  $('plist').innerHTML=progs.programs.map((p,i)=>{
    if(!plAll&&empty.includes(i)) return '';
    const d=DRAFT[i], runI=act&&live.run.prog===i, sel=live.sel===i, newD=!p.used&&d;
    const badges=(runI?'<span class="state run">Running</span>':sel?'<span class="state fin">Selected</span>':'')+(dirty[i]?`<span class="state pause">${p.used?'Unsaved changes':'Not saved yet'}</span>`:'');
    if(!p.used&&!d) return `<div class="prow empty" data-i="${i}"><span class="no">${i+1}</span><span class="pn">(empty)</span><span class="pm"></span>
      <span class="pa"><button data-pa="new-seq" ${act?'disabled':''}>New sequence</button><button data-pa="new-tl" ${act?'disabled':''}>New timeline</button><button data-pa="import">Import…</button></span></div>`;
    const typ=newD?d.type:p.type, uses=newD?[progUses(d,0),progUses(d,1)]:(p.uses||[true,true]), n=newD?progItems(d):p.steps;
    const meta=[typ==='tl'?'Timeline':'Sequence', CH.filter(c=>uses[c]).map(c=>`<span class="chip" style="color:${PV[c]}"><i></i>${esc(chName(c))}</span>`).join(' '),
      `${n} ${typ==='tl'?(n===1?'step':'steps'):(n===1?'phase':'phases')}`, newD?'':fmt(p.total)+(p.waits?' + waits':'')].filter(Boolean).map(x=>`<span>${x}</span>`).join('');
    return `<div class="prow${sel?' sel':''}" data-i="${i}"><span class="no">${i+1}</span><span class="pn">${esc(newD?d.name:p.name)}${badges}</span><span class="pm">${meta}</span>
      <span class="pa"><button data-pa="open" ${act&&!runI?'disabled':''}>${runI?'Show':'Open'}</button>${newD?`<button data-pa="discard">Discard</button>`:`<button data-pa="dup">Duplicate</button><button data-pa="export">Export</button><button data-pa="del" class="danger" ${runI?'disabled':''}>Delete</button>`}</span></div>`;
  }).join('')+emptyRow+(plAll&&empty.length?`<div class="row"><button data-pa="few">Hide the empty slots</button></div>`:'');
}
async function openProg(i,type,prog){
  if(!runActive()&&live.sel!==i){ await api('/api/select',{i}); live.sel=i; showLast=false; }
  await loadSaved(i); if(type){ newDraft(i,type,prog); }
  previewDraft(); edKey=''; chartAt=0; fillProgSelect(); location.hash='#/'; if(view!=='prog') setView('prog');  // (the program's chart, for its handles)
  setTimeout(()=>{ updateEditor(); $('edP').scrollIntoView({block:'start'}); if(type) $('pName').focus(); },60); poll(); }
async function storedProg(i){ const r=await api('/api/program?i='+i); return r.used?r.program:null; }
async function afterProgChange(i){ delete DRAFT[i]; delete SAVED[i]; savedGen[i]=-1; UNDO[i]=[]; REDO[i]=[]; await loadPrograms(); plKey=''; renderProgs(true); }
$('plist').addEventListener('click',async e=>{
  const b=e.target.closest('button[data-pa]'); if(!b||!live) return; const a=b.dataset.pa;
  if(a==='all'||a==='few'){ plAll=a==='all'; renderProgs(true); return; }
  const i=+b.closest('[data-i]').dataset.i, p=progs.programs[i];
  setMsg('plMsg','');
  try{
    if(a==='open') return openProg(i);
    if(a==='new-seq'||a==='new-tl') return openProg(i,a==='new-seq'?'seq':'tl');
    if(a==='discard'){ if(await ask(`Discard the unsaved new program in slot ${i+1}?`,'Discard')){ delete DRAFT[i]; UNDO[i]=[]; REDO[i]=[]; renderProgs(true); fillProgSelect(); if(live.sel===i){ edKey=''; previewDraft(); } } return; }
    if(a==='export'){ const pr=await storedProg(i); if(pr) download(fileName(pr.name)+'.json',pr); if(isDirty(i)) setMsg('plMsg','Exported the stored version (the unsaved changes are not included).'); return; }
    if(a==='dup'){ const j=progs.programs.findIndex((q,k)=>!q.used&&!DRAFT[k]); if(j<0){ setMsg('plMsg','No empty slot left.',true); return; }
      const pr=await storedProg(i); if(!pr) return; pr.name=(pr.name+' (copy)').slice(0,31);
      const r=await api('/api/program',{i:j,program:pr}); if(r.err){ setMsg('plMsg',r.err,true); return; }
      await afterProgChange(j); setMsg('plMsg',`Copied to slot ${j+1}${isDirty(i)?' (the stored version, without the unsaved changes)':''}.`); return; }
    if(a==='del'){ if(!await ask(`Delete “${p.name}” (slot ${i+1})? This cannot be undone.`,'Delete')) return;
      const r=await api('/api/program',{i,delete:true}); if(r.err){ setMsg('plMsg',r.err,true); return; } await afterProgChange(i); setMsg('plMsg',`Slot ${i+1} deleted.`); return; }
    if(a==='import'){ const t=await pickFile(); if(t==null) return; const o=parseJson(t);
      if(!o||typeof o!=='object'){ setMsg('plMsg','This file is not JSON.',true); return; }
      if(Array.isArray(o.programs)){ setMsg('plMsg','This file holds several programs: use Restore programs on the Tools page.',true); return; }
      if(o.type!=='seq'&&o.type!=='tl'){ setMsg('plMsg','This file is not a TempCtl Duo program.',true); return; }
      if(p.used&&!await ask(`Replace “${p.name}” (slot ${i+1}) with “${o.name||'?'}” from the file?`,'Replace')) return;
      const r=await api('/api/program',{i,program:o}); if(r.err){ setMsg('plMsg','Import failed: '+r.err,true); return; }
      await afterProgChange(i); setMsg('plMsg',`“${r.program.name}” imported into slot ${i+1}.`); return; }
  }catch(err){ setMsg('plMsg','Device not reachable.',true); }
});

