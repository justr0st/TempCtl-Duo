/* ---------- settings forms (/api/config, partial POSTs) ---------- */
let CFG=null, cfgGen=-1;
const FORMS={};
const RAINBOW='conic-gradient(#f44,#fd3,#4d4,#3cf,#46f,#d4f,#f44)';
const ACCENTS=[['Blue','#2196F3'],['Pink','#D1449B'],['Violet','#6C71C4'],['Orange','#D65D0E'],['Mauve','#8839EF'],['Green','#50A14F']];
async function loadCfg(){ const g=live?live.cfgGen:-1; let c; try{ c=await api('/api/config'); }catch(e){ return; } if(!c||c.err) return;
  CFG=c; cfgGen=g; for(const F of Object.values(FORMS)) if(!F.dirty) fillForm(F); else updForm(F); }
const gp=(o,p)=>p.split('.').reduce((a,k)=>a==null?undefined:a[k],o);
const IO_FN=[['off','Not used'],['relay','SSR supply relay'],['estop','E-stop input'],['buzzer','Buzzer'],['fan','Fan (on / off)'],['fanPwm','Fan (PWM)'],['fanTach','Fan speed input']];  // (optional pins, io.fn)
function sp(o,p,v){ const ks=p.split('.'); let a=o; for(let j=0;j<ks.length-1;j++){ const k=ks[j]; if(a[k]==null) a[k]=/^\d+$/.test(ks[j+1])?[]:{}; a=a[k]; } a[ks[ks.length-1]]=v; }
// field {k (path), l (label), t num|sel|sw|text|accent, u (unit), min, max, step, o [[value,label]], num / bool (sel value type), def, show(g), h (hint), w (wide)}
function inpHtml(f){
  let inp;
  if(f.t==='sel') inp=`<select data-k="${f.k}">${f.o.map(([v,l])=>`<option value="${esc(v)}">${esc(l)}</option>`).join('')}</select>`;
  else if(f.t==='sw') inp=`<input type="checkbox" class="switch acc" data-k="${f.k}">`;
  else if(f.t==='theme') inp=`<span class="tsw"><input type="checkbox" role="switch" data-k="${f.k}" aria-label="${esc(f.l)}"><span class="tr" aria-hidden="true"><span class="kn"><svg class="su" viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4"/></svg><svg class="mo" viewBox="0 0 24 24"><path d="M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z"/></svg></span></span></span><span class="u tsl"></span>`;
  else if(f.t==='range') inp=`<span class="rng"><input type="range" class="thick" data-k="${f.k}" min="${f.min}" max="${f.max}" step="${f.step||1}" aria-label="${esc(f.l)}"><output></output></span>`;
  else if(f.t==='text') inp=`<input type="text" data-k="${f.k}" maxlength="${f.max}" autocomplete="off">`;
  else if(f.t==='accent'){ const cur=String(gp(CFG,f.k)||'#2196F3').toUpperCase(), cust=!ACCENTS.some(a=>a[1]===cur);
    inp=`<span class="acc-sw" data-k="${f.k}" role="radiogroup" aria-label="${esc(f.l)}">${ACCENTS.map(([n,h])=>`<label><input type="radio" name="r-${f.k}" value="${h}"><i style="background:${h}"></i>${n}</label>`).join('')}`+
      `<label class="acc-c" title="Any colour: tap the circle"><input type="radio" name="r-${f.k}" value="custom"><i style="background:${cust?cur:RAINBOW}"><input type="color" value="${cur}" aria-label="Custom colour"></i>Custom</label></span>`; }
  else inp=`<input type="number" data-k="${f.k}" min="${f.min}" max="${f.max}" step="${f.step||1}">`;
  return inp; }
function fldHtml(f){
  const inp=inpHtml(f), tag=f.t==='accent'?'div':'label';
  return `<${tag} class="fld${f.w||f.t==='accent'?' wide':''}" data-fk="${f.k}"><span>${esc(f.l)}</span><span class="in">${inp}${f.u?`<span class="u">${esc(f.u)}</span>`:''}</span>${f.h?`<small>${esc(f.h)}</small>`:''}</${tag}>`;
}
// groups [{h, note, fields, show(g), det (collapsible), extra (html)}]; o {title, top (html), save, sticky, confirm(F,body), onUpd(F)}
// card layout (o.cards; the channel tiles): one card per group (g.c colour, g.sub subtitle, g.full / g.det = whole
// width, g.c2 = rows in two columns), one row per setting: name left, value right, hint behind (i). Field extras:
// j (joins the previous row), pre (text before the input), rl (row label instead of l), sent (a sentence row:
// switch, bold name, the joined values as a sentence), btn (html after the input), rh (row hint instead of h).
function rowsHtml(fs){
  const rows=[];
  for(const f of fs){ if(f.j&&rows.length) rows[rows.length-1].push(f); else rows.push([f]); }
  const item=f=>`<label class="fld ci" data-fk="${f.k}"><span${f.pre?'':' hidden'}>${esc(f.pre||'')}</span>${inpHtml(f).replace(/^<(\w+) /,`<$1 aria-label="${esc(f.l.replace(/^[…\s]+/,''))}" `)}${f.u?`<span class="u">${esc(f.u)}</span>`:''}</label>${f.btn||''}`;
  return rows.map(r=>{ const a=r[0], h=a.rh||a.h, lab=esc(a.rl!=null?a.rl:a.l)+(a.beta?' <span class="beta">beta</span>':''), info=h?` <button type="button" class="ci-i" title="${esc(h)}" aria-label="Explain">i</button>`:'', hint=h?`<small class="ch" hidden>${esc(h)}</small>`:'';
    if(a.sent){ const sw=a.t==='sw';
      return `<div class="crow sent"><span class="csw">${sw?item(a):''}</span><b class="cl">${lab}${info}</b><span class="cc">${(sw?r.slice(1):r).map(item).join('')}</span>${hint}</div>`; }
    return `<div class="crow"><span class="cl">${lab}${info}</span><span class="cc">${r.map(item).join('')}</span>${hint}</div>`; }).join(''); }
function cardsHtml(groups){
  return `<div class="ccards">${groups.map((g,gi)=>{
    const hh=esc(g.h||'')+(g.beta?' <span class="beta">beta</span>':'')+(g.sub?`<span class="sub">${esc(g.sub)}</span>`:'');
    const body=(g.note?`<p class="hint">${esc(g.note)}</p>`:'')+`<div class="crows${g.c2?' c2':''}">${rowsHtml(g.fields)}${g.extra||''}</div>`;
    const st=`style="--sc:${g.c||'var(--acc)'}"`;
    return g.det?`<details class="ccard full" data-g="${gi}" ${st}><summary>${hh}</summary>${body}</details>`:`<section class="ccard${g.full?' full':''}" data-g="${gi}" ${st}><h3>${hh}</h3>${body}</section>`;
  }).join('')}</div>`; }
function mkForm(id,box,groups,o={}){
  const F={id,box,groups,fields:groups.flatMap(g=>g.fields),o,dirty:false};
  FORMS[id]=F; box.dataset.form=id;
  if(o.cards) box.innerHTML=(o.title?`<h2>${esc(o.title)}</h2>`:'')+(o.top||'')+cardsHtml(groups);
  else box.innerHTML=(o.title?`<h2>${esc(o.title)}</h2>`:'')+(o.top||'')+groups.map((g,gi)=>{
    const body=(g.note?`<p class="hint">${esc(g.note)}</p>`:'')+`<div class="form">${g.fields.map(fldHtml).join('')}</div>`+(g.extra||'');
    const hh=esc(g.h||'')+(g.beta?' <span class="beta">beta</span>':'');
    return g.det?`<details class="grp" data-g="${gi}"><summary>${hh}</summary>${body}</details>`:`<div data-g="${gi}">${g.h?`<h3>${hh}</h3>`:''}${body}</div>`;
  }).join('');
  box.innerHTML+=(o.auto?'<div class="msg" data-fmsg></div>':`<div class="fbar"><button class="pri" data-fsave="${id}">${esc(o.save||'Save')}</button><button data-frev="${id}">Revert</button><span class="msg" data-fmsg></span></div>`);
  fillForm(F); return F;
}
const fEl=(F,f)=>F.box.querySelector(`[data-k="${f.k}"]`);
function cfgVal(f){ const v=gp(CFG,f.k); return v==null&&f.def!==undefined?f.def:v; }
function readIn(F,f){ const el=fEl(F,f); if(!el) return undefined;
  if(f.t==='sw'||f.t==='theme') return el.checked;
  if(f.t==='accent'){ const r=el.querySelector('input[type=radio]:checked'); return !r?cfgVal(f):r.value==='custom'?el.querySelector('input[type=color]').value.toUpperCase():r.value; }
  const s=el.value; if(f.t==='sel') return f.num?Number(s):f.bool?s==='true':s;
  if(f.t==='text') return s.trim();
  return s.trim()===''?NaN:Number(s); }
const same=(a,b)=>typeof a==='number'&&typeof b==='number'?Math.abs(a-b)<1e-6:a===b;
const isBad=(f,v)=>(f.t==='num'||f.t==='range')&&(isNaN(v)||v<f.min||v>f.max);
// visual state of special inputs (slider fill + value, theme switch caption)
function decor(F,f,el){ if(f.t==='range'){ const v=+el.value; el.style.setProperty('--p',((v-f.min)/(f.max-f.min)*100)+'%'); el.nextElementSibling.textContent=f.fmt?f.fmt(v):v; }
  else if(f.t==='theme') el.closest('.in').querySelector('.tsl').textContent=el.checked?'Dark':'Light'; }
const fv=F=>k=>{ const f=F.fields.find(x=>x.k===k); return f?readIn(F,f):gp(CFG,k); };
function fillForm(F){ if(!CFG) return;
  for(const f of F.fields){ const el=fEl(F,f); if(!el) continue; const v=cfgVal(f);
    if(f.t==='sw'||f.t==='theme') el.checked=!!v; else if(f.t==='accent') accFill(el,v);
    else el.value=v==null?'':String(v); }
  updForm(F); }
function changedFields(F){ return F.fields.filter(f=>fEl(F,f)&&!same(readIn(F,f),cfgVal(f))); }
function updForm(F){ if(!CFG) return;
  let dirty=false, bad=false; const g=fv(F), badL=[];
  for(const f of F.fields){ const el=fEl(F,f); if(!el) continue; const lab=el.closest('.fld'), v=readIn(F,f), ch=!same(v,cfgVal(f)), vis=!f.show||f.show(g), b=vis&&isBad(f,v);
    lab.hidden=!vis; lab.classList.toggle('chg',ch); lab.classList.toggle('bad',b); dirty=dirty||ch; bad=bad||b; decor(F,f,el);
    if(f.t==='num'&&el.min!==String(f.min)) el.min=f.min;  // (a limit that depends on other settings: pollMin)
    if(b) badL.push(`${f.l.replace(/^[…\s]+/,'')}: ${isNaN(v)?'a number ':''}${f.min} … ${f.max}${f.u?' '+f.u:''}`); }
  // why Save is off: the values outside their range (the message goes again when they are fixed)
  if(badL.length){ fMsg(F,'Not saved — outside the allowed range: '+badL.join('; ')+'.',true); F.badMsg=true; }
  else if(F.badMsg){ fMsg(F,''); F.badMsg=false; }
  F.groups.forEach((gr,gi)=>{ if(gr.show){ const e=F.box.querySelector(`[data-g="${gi}"]`); if(e) e.hidden=!gr.show(g); } });
  F.box.querySelectorAll('.crow').forEach(r=>{ const fs=[...r.querySelectorAll('.fld')]; if(!fs.length) return;
    r.hidden=!fs.some(x=>!x.hidden); r.classList.toggle('chg',fs.some(x=>x.classList.contains('chg'))); r.classList.toggle('bad',fs.some(x=>x.classList.contains('bad'))); });
  if(!F.o.auto){ F.box.querySelector('[data-fsave]').disabled=!dirty||bad; F.box.querySelector('[data-frev]').disabled=!dirty; }
  if(F.o.sticky&&!F.o.auto) F.box.querySelector('.fbar').classList.toggle('sticky',dirty);
  F.dirty=dirty; if(F.o.onUpd) F.o.onUpd(F); }
function fMsg(F,t,bad){ const m=F.box.querySelector('[data-fmsg]'); m.textContent=t||''; m.className='msg'+(bad?' bad':''); }
async function postCfg(body){ try{ return await api('/api/config',body); }catch(e){ return {err:'Device not reachable.'}; } }
async function saveForm(F){
  const ch=changedFields(F).filter(f=>!isBad(f,readIn(F,f))); if(!ch.length) return;
  const body={}, sent=ch.map(f=>[f,readIn(F,f)]); for(const [f,v] of sent) sp(body,f.k,v);
  if(body.io&&body.io.fn){ const a=((CFG.io&&CFG.io.fn)||[]).slice(); body.io.fn.forEach((v,i)=>{ a[i]=v; }); body.io.fn=a; }  // (the whole list: one name per optional pin)
  if(F.o.confirm&&!(await F.o.confirm(F,body))) return;
  const seq=F.seq=(F.seq||0)+1;
  fMsg(F,'Saving…'); const poll0=CFG.bus.pollMs; const r=await postCfg(body);
  if(r.err){ fMsg(F,r.err,true); if(F.o.auto&&seq===F.seq) fillForm(F); return; }
  CFG=r; for(const G of Object.values(FORMS)) if((G===F&&seq===F.seq)||!G.dirty) fillForm(G);
  if(seq!==F.seq) return;
  const adj=sent.filter(([f,v])=>!same(v,cfgVal(f))).map(([f])=>f.l.replace(/^[…\s]+/,''));
  const raised=F.id!=='common'&&CFG.bus.pollMs>poll0?` The polling interval was raised to ${CFG.bus.pollMs} ms (the lowest for a MAX6675 or Modbus channel).`:'';
  fMsg(F,(adj.length?'Saved. Adjusted to the allowed range: '+adj.join(', ')+'.':'Saved.')+raised); poll(); }
// accent: a preset, or "Custom" with the colour input inside its circle (the custom colour stays shown)
function accFill(el,v){ const u=String(v||'').toUpperCase(); let hit=false;
  el.querySelectorAll('input[type=radio]').forEach(r=>{ r.checked=r.value===u; hit=hit||r.checked; });
  if(!hit&&u){ el.querySelector('input[value=custom]').checked=true; el.querySelector('input[type=color]').value=u; el.querySelector('.acc-c i').style.background=u; } }
document.addEventListener('click',e=>{ const l=e.target.closest('.acc-c'); if(!l||e.target.type==='color') return;  // "Custom" text opens the picker too
  e.preventDefault(); const c=l.querySelector('input[type=color]'); try{ c.showPicker(); }catch(x){ c.click(); } });
document.addEventListener('input',e=>{ const c=e.target; if(c.type!=='color'||!c.closest('.acc-sw')) return;
  c.closest('label').querySelector('input[type=radio]').checked=true; c.parentElement.style.background=c.value; });
document.addEventListener('input',e=>{ const b=e.target.closest('[data-form]'); if(b&&FORMS[b.dataset.form]&&e.target.closest('[data-k]')) updForm(FORMS[b.dataset.form]); });
document.addEventListener('click',e=>{ const b=e.target.closest('.ci-i'); if(!b) return; const h=b.closest('.crow').querySelector('.ch'); if(h){ h.hidden=!h.hidden; b.classList.toggle('on',!h.hidden); } });
document.addEventListener('change',e=>{ const b=e.target.closest('[data-form]'), F=b&&FORMS[b.dataset.form]; if(!F||!e.target.closest('[data-k]')) return;
  if(e.target.dataset.k==='display.brightness'){ clearTimeout(brT); brT=0; }
  updForm(F); if(F.o.auto&&F.dirty) saveForm(F); });
// brightness: the touch screen follows the slider while it is dragged (not saved; saved on release)
let brT=0, brV=null;
document.addEventListener('input',e=>{ const el=e.target; if(el.dataset.k!=='display.brightness') return; brV=+el.value;
  if(!brT) brT=setTimeout(()=>{ brT=0; api('/api/brightness',{v:brV}).catch(()=>{}); },120); });
document.addEventListener('click',e=>{ const s=e.target.closest('[data-fsave]'); if(s){ saveForm(FORMS[s.dataset.fsave]); return; }
  const r=e.target.closest('[data-frev]'); if(r){ const F=FORMS[r.dataset.frev]; fillForm(F); fMsg(F,''); } });
window.addEventListener('beforeunload',e=>{ if(Object.values(FORMS).some(F=>F.dirty)||Object.keys(DRAFT).some(i=>isDirty(+i))){ e.preventDefault(); e.returnValue=''; } });

