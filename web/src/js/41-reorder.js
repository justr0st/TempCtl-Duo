// ---- reorder phases / steps: drag a row by its grip (pointer events: mouse, touch and pen alike - the browser's
// own drag and drop has no touch), or Alt+Up / Alt+Down on the grip. One list: the Sequence table or its phone
// cards, or one channel's Timeline table. While a row is dragged the editor is not rebuilt (updateEditor); the
// move itself is one edit (undo / redo, "Unsaved changes"), not while a program runs (no grips then).
const rowList=item=>[...item.parentElement.children].filter(x=>x.dataset.i!=null&&(x.dataset.k||'')===(item.dataset.k||''));
function moveRow(k,from,to){ if(from===to) return false; const q=beginEdit(); if(!q) return false;
  const a=k?q[k]:q.phases; if(!a||from<0||to<0||from>=a.length||to>=a.length){ afterEdit(); return false; }
  a.splice(to,0,a.splice(from,1)[0]); afterEdit(); return true; }
function focusGrip(k,i){ const r=$('editor').querySelector(`[data-i="${i}"]${k?`[data-k="${k}"]`:''}`), g=r&&r.querySelector('.grip'); if(g) g.focus({preventScroll:true}); }
function rowDragAt(d,y){  // the dragged row follows the pointer; the rows it passes make room
  d.y=y; const dy=y+scrollY-d.y0-d.s0; d.item.style.transform=`translateY(${dy}px)`;
  const at=y+scrollY; let to=0; d.mids.forEach((m,j)=>{ if(j!==d.from&&m<at) to++; });  // (where the finger / pointer is: tall phone cards)
  if(to===d.to) return; d.to=to;
  d.list.forEach((x,j)=>{ if(j===d.from) return; const s=d.from<to&&j>d.from&&j<=to?-d.h:d.from>to&&j>=to&&j<d.from?d.h:0; x.style.transform=s?`translateY(${s}px)`:''; }); }
function rowDragGo(d){  // over the threshold: lifted
  d.on=true; document.body.classList.add('drgOn');
  d.mids=d.list.map(x=>{ const r=x.getBoundingClientRect(); return r.top+scrollY+r.height/2; });
  const r=d.item.getBoundingClientRect(), nx=d.list[d.from+1], pv=d.list[d.from-1];
  d.h=r.height+(nx?nx.getBoundingClientRect().top-r.bottom:pv?r.top-pv.getBoundingClientRect().bottom:0);  // (+ the gap)
  d.list.forEach(x=>x.classList.add(x===d.item?'drg':'mvR'));
  const tick=()=>{ if(rowDrag!==d) return; const e=56, v=d.y<e?-(e-d.y):d.y>innerHeight-e?d.y-innerHeight+e:0;  // near an edge: scroll
    if(v){ const s0=scrollY; scrollBy(0,Math.round(v/3)); if(scrollY!==s0) rowDragAt(d,d.y); } d.raf=requestAnimationFrame(tick); };
  d.raf=requestAnimationFrame(tick); }
function rowDragEnd(ok){ const d=rowDrag; if(!d) return; rowDrag=null; cancelAnimationFrame(d.raf); document.body.classList.remove('drgOn');
  d.list.forEach(x=>{ x.style.transform=''; x.classList.remove('mvR','drg'); });
  if(d.on&&ok) moveRow(d.item.dataset.k,d.from,d.to);
  updateEditor(); }  // (a rebuild held back meanwhile)
$('editor').addEventListener('pointerdown',e=>{
  const g=e.target.closest('.grip'); if(!g||e.button!==0||rowDrag||running()||!live) return;
  const item=g.closest('[data-i]'); if(!item) return;
  e.preventDefault();  // (no text selection; the grip takes no focus: no focus ring after a drag)
  const list=rowList(item);
  rowDrag={g,item,list,from:list.indexOf(item),to:list.indexOf(item),id:e.pointerId,y0:e.clientY,s0:scrollY,y:e.clientY,on:false,raf:0};
  try{ g.setPointerCapture(e.pointerId); }catch(_){}
});
document.addEventListener('pointermove',e=>{ const d=rowDrag; if(!d||e.pointerId!==d.id) return;
  if(!d.on){ if(Math.abs(e.clientY-d.y0)<4) return; rowDragGo(d); } e.preventDefault(); rowDragAt(d,e.clientY); });
document.addEventListener('pointerup',e=>{ if(rowDrag&&e.pointerId===rowDrag.id) rowDragEnd(true); });
document.addEventListener('pointercancel',e=>{ if(rowDrag&&e.pointerId===rowDrag.id) rowDragEnd(false); });
document.addEventListener('keydown',e=>{ if(rowDrag&&e.key==='Escape'){ rowDragEnd(false); e.preventDefault(); } },true);
$('editor').addEventListener('keydown',e=>{
  const g=e.target.closest&&e.target.closest('.grip'); if(!g||!e.altKey||(e.key!=='ArrowUp'&&e.key!=='ArrowDown')||rowDrag) return;
  e.preventDefault(); const item=g.closest('[data-i]'), list=rowList(item), from=list.indexOf(item), to=from+(e.key==='ArrowUp'?-1:1);
  if(to<0||to>=list.length) return; if(moveRow(item.dataset.k,from,to)) focusGrip(item.dataset.k,to); });

// chart handles: target (square, drag up / down), ramp end (diamond) and hold end (circle, drag left / right)
let handles=[], drag=null;
function buildHandles(){
  const H=[]; if(!M||(M.kind!=='preview'&&M.kind!=='run')||!M.prog) return H;
  const p=M.prog, seq=p.type==='seq', now=M.kind==='run'?M.now-.01:-1;
  for(const e of M.ev){ if(e.past||e.t<now) continue;
    if(e.type===EV.HOLD_END){ let v; if(seq) v=refTarget(p,e.ch,e.idx); else { const x=p[KEY[e.ch]][e.idx]; if(!x||x.kind==='pause') continue; v=x.kind==='idle'?null:x.t; }
      H.push({type:'hold',dir:'h',ch:e.ch,idx:e.idx,t:e.t,v:v==null?YMIN+8:v}); }
    else if(e.type===EV.RAMP_END){ if(rampLocked(e.ch,e.idx)) continue; const v=refTarget(p,e.ch,e.idx); if(v==null) continue;
      if(seq&&e.ch===0&&p.phases[e.idx].kind==='BT'&&!p.phases[e.idx].t.idle) continue; H.push({type:'ramp',dir:'h',ch:e.ch,idx:e.idx,t:e.t,v}); } }
  for(const i of CH){ const groups=[]; let g=null;
    for(const s of M.segs[i]){ if(s.t1<=now) continue; const ok=s.fl===0&&s.src>=0;
      if(ok&&g&&g.src===s.src&&Math.abs(g.t1-s.t0)<.01){ g.t1=s.t1; } else { if(g) groups.push(g); g=ok?{src:s.src,t0:Math.max(s.t0,now),t1:s.t1}:null; } }
    if(g) groups.push(g);
    const seen=new Set();
    for(const q of groups){ if(seen.has(q.src)) continue; seen.add(q.src); const v=refTarget(p,i,q.src); if(v==null) continue;
      H.push({type:'target',dir:'v',ch:i,idx:q.src,t:(q.t0+q.t1)/2,v,w:X(q.t1)-X(q.t0)}); }
    for(const h of H.filter(h=>h.type==='ramp'&&h.ch===i)){ if(seen.has(h.idx)) continue; seen.add(h.idx); H.push({type:'target',dir:'v',ch:i,idx:h.idx,t:h.t,v:h.v,lift:-22}); } }
  for(const h of H) if(h.type==='target'&&!h.lift&&h.w<16) h.lift=-22;
  for(const h of H.filter(h=>h.type==='hold')) if(H.some(o=>o!==h&&o.type!=='hold'&&o.ch===h.ch&&Math.abs(X(o.t)-X(h.t))<14&&Math.abs(Y(o.v)-Y(h.v))<14)) h.lift=22;
  return H.filter(h=>h.t>=V0-1&&h.t<=V1+1);
}
function drawHandles(H){ let s=''; const seq=M.prog.type==='seq';
  H.forEach((h,k)=>{ const col=PV[h.ch], x=X(h.t), y0=Y(h.v), y=y0+(h.lift||0), where=seq?`phase ${h.idx+1}`:`${chName(h.ch)} step ${h.idx+1}`;
    if(h.lift) s+=`<line x1="${x}" y1="${y0}" x2="${x}" y2="${y}" style="stroke:${col};stroke-width:1.5"/>`;
    if(h.type==='hold') s+=`<circle data-h="${k}" cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="7" style="fill:var(--panel);stroke:${col};stroke-width:2.5;cursor:ew-resize"><title>Drag left or right: hold time of ${where}</title></circle>`;
    else if(h.type==='ramp') s+=`<rect data-h="${k}" x="${(x-6).toFixed(1)}" y="${(y-6).toFixed(1)}" width="12" height="12" transform="rotate(45 ${x.toFixed(1)} ${y.toFixed(1)})" style="fill:var(--panel);stroke:${col};stroke-width:2.5;cursor:ew-resize"><title>Drag left or right: ramp time of ${where}</title></rect>`;
    else s+=`<rect data-h="${k}" x="${(x-7).toFixed(1)}" y="${(y-7).toFixed(1)}" width="14" height="14" rx="3" style="fill:${col};stroke:var(--panel);stroke-width:2;cursor:ns-resize"><title>Drag up or down: ${esc(chName(h.ch))} target of ${where}</title></rect>`; });
  return s; }
function dragMove(ev){ const p=svgPt(ev), h=drag.h, g=$('chartbox').querySelector('#ghost'); if(!g) return;
  if(h.dir==='h'){ let t=h.t+(p.x-drag.px)/plotW()*(V1-V0); t=Math.max(M.kind==='run'?M.now:0,Math.round(t)); drag.t=t; const d=Math.round(t-h.t), y=Y(h.v)+(h.lift||0);
    g.innerHTML=`<line x1="${X(t)}" x2="${X(t)}" y1="${PT}" y2="${PB}" style="stroke:var(--acc);stroke-width:2"/><circle cx="${X(t)}" cy="${y}" r="6" style="fill:var(--acc)"/><text x="${X(t)+10}" y="${y-12}" style="fill:var(--text);font-size:13px;font-weight:600;paint-order:stroke;stroke:var(--panel);stroke-width:3px">${h.type==='ramp'?'ramp':'hold'} ${d>=0?'+':''}${d} s</text>`; }
  else { const c=live.ch[h.ch]; let v=h.v-(p.y-drag.py)/(PB-PT)*(YMAX-YMIN); v=Math.max(c.spMin,Math.min(c.spMax,Math.round(v))); drag.v=v; const x=X(h.t);
    g.innerHTML=`<line x1="${CL}" x2="${CW-CR}" y1="${Y(v)}" y2="${Y(v)}" style="stroke:var(--acc);stroke-width:2"/><rect x="${x-6}" y="${Y(v)-6}" width="12" height="12" rx="3" style="fill:var(--acc)"/><text x="${x+12}" y="${Y(v)-10}" style="fill:var(--text);font-size:13px;font-weight:600;paint-order:stroke;stroke:var(--panel);stroke-width:3px">${v} °C</text>`; } }
async function dragEnd(){ const h=drag.h, D=Math.round(drag.t-h.t), V=drag.v; drag=null; lastChart=0;
  if(h.type==='target'?V===h.v:D===0) return;
  const p=M.prog, seq=p.type==='seq';
  if(M.kind==='run'){
    if(h.type==='target') await runCmd({cmd:'target',ch:h.ch,idx:h.idx,t:V});
    else if(h.type==='hold'){ const x=seq?p.phases[h.idx]:p[KEY[h.ch]][h.idx], old=seq?(bDrv(x)?x.b.h:x.t.h):x.h; await runCmd({cmd:'hold',ch:h.ch,idx:h.idx,sec:Math.max(0,old+D)}); }
    else { const x=seq?p.phases[h.idx]:p[KEY[h.ch]][h.idx], old=seq?(h.ch===0?x.b.r:x.t.r):x.r; await runCmd({cmd:'ramp',ch:h.ch,idx:h.idx,sec:Math.max(0,old+D)}); }
    poll(); return; }
  const q=beginEdit(); if(!q) return;
  if(seq){ const x=q.phases[h.idx], part=h.type==='target'?(h.ch===0?x.b:x.t):h.type==='hold'?(bDrv(x)?x.b:x.t):(h.ch===0?x.b:x.t);
    if(h.type==='target') part.t=V; else if(h.type==='hold') part.h=clampS(part.h+D); else part.r=clampS(part.r+D); }
  else { const x=q[KEY[h.ch]][h.idx]; if(h.type==='target') x.t=V; else if(h.type==='hold') x.h=clampS(x.h+D); else x.r=clampS(x.r+D); }
  blurEditor(); afterEdit(); }

