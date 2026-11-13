/* ---------- CSV replay (Rost): a CSV of the device (history / last run export, SD run log) as a chart to zoom,
   crop and save again; hover shows the values. Columns: time_s / run_s / time (wall clock), <ch>_pv, _sp,
   _min, _max, _plan / _prog, _out; SD logs also kind, event (shown as marks). ---------- */
const RP={lines:null,head:null,name:'',T:[],cols:{},ev:[],clock:false,t0:0,crop:null,v0:0,v1:1,hide:{},sel:null,hov:null,W:1000,H:420};
const RP_CH=[['bottom',0],['top',1],['monitor',MON]];
function csvSplit(l){ const out=[]; let cur='',q=false;
  for(let k=0;k<l.length;k++){ const c=l[k];
    if(q){ if(c==='"'){ if(l[k+1]==='"'){ cur+='"'; k++; } else q=false; } else cur+=c; }
    else if(c==='"') q=true; else if(c===','){ out.push(cur); cur=''; } else cur+=c; }
  out.push(cur); return out; }
function rpLoad(text,name){
  const lines=text.replace(/\r/g,'').split('\n').filter(l=>l.length);
  if(lines.length<2) return setMsg('rpMsg','No rows in this file.',true);
  const head=csvSplit(lines[0]).map(h=>h.trim()), ix=k=>head.indexOf(k);
  const iT=ix('time'), iTs=ix('time_s'), iRun=ix('run_s');
  const clock=iT>=0&&!isNaN(Date.parse(csvSplit(lines[1])[iT]?.replace(' ','T')));
  if(!clock&&iTs<0&&iRun<0) return setMsg('rpMsg','Not a TempCtl Duo CSV: no time column (time, time_s or run_s).',true);
  const T=[], cols={}, ev=[];
  for(const [p] of RP_CH) for(const f of ['pv','sp','min','max','plan','prog','out']) if(ix(p+'_'+f)>=0) cols[p+'_'+f]=[];
  const iK=ix('kind'), iE=ix('event'); let t0=null;
  for(let r=1;r<lines.length;r++){ const v=csvSplit(lines[r]);
    let t=clock?Date.parse(v[iT].replace(' ','T'))/1000:parseFloat(v[iTs>=0?iTs:iRun]);
    if(isNaN(t)) t=T.length?T[T.length-1]:0;
    if(clock){ if(t0==null) t0=t; t-=t0; }
    T.push(t);
    for(const k in cols){ const x=v[ix(k)]; cols[k].push(x===''||x==null||isNaN(+x)?null:+x); }
    if(iE>=0&&v[iE]) ev.push({t,kind:iK>=0?v[iK]:'',text:v[iE]}); }
  Object.assign(RP,{lines,head,name,T,cols,ev,clock,t0:clock?t0:0,crop:null,sel:null,hov:null});
  rpFit(); $('rpName').textContent=`${name} · ${lines.length-1} rows · ${fmt(T[T.length-1]-T[0])}`; setMsg('rpMsg','');
  $('rpBar').hidden=false; $('rpHint').hidden=true; rpLegend(); rpDraw(); }
const rpRange=()=>{ const T=RP.T; let a=T[0],b=T[T.length-1]; for(const t of T){ if(t<a) a=t; if(t>b) b=t; } if(RP.crop){ a=Math.max(a,RP.crop[0]); b=Math.min(b,RP.crop[1]); } return [a,b>a?b:a+1]; };
function rpFit(){ if(!RP.T.length) return; [RP.v0,RP.v1]=rpRange(); }
function rpClamp(){ const [a,b]=rpRange(), span=Math.min(b-a,Math.max(5,RP.v1-RP.v0)); if(RP.v0<a) RP.v0=a; RP.v1=RP.v0+span; if(RP.v1>b){ RP.v1=b; RP.v0=b-span; } }
// series: [key, label, colour, kind]; channels by the current names when connected
function rpSeries(){ const s=[], C={}; for(const k in RP.cols) if(RP.cols[k].some(v=>v!=null)) C[k]=RP.cols[k];  // (columns with values only)
  for(const [p,i] of RP_CH){ const nm=live||i===MON?chName(i):(p==='bottom'?'Bottom':'Top');
    if(C[p+'_min']&&C[p+'_max']) s.push({k:p+'_band',n:nm+' lowest / highest',c:PV[i],kind:'band',lo:C[p+'_min'],hi:C[p+'_max']});
    if(C[p+'_pv']) s.push({k:p+'_pv',n:nm+' PV',c:PV[i],kind:'pv',v:C[p+'_pv']});
    if(C[p+'_sp']) s.push({k:p+'_sp',n:nm+' SP',c:SP[i],kind:'sp',v:C[p+'_sp']});
    const pl=C[p+'_plan']||C[p+'_prog']; if(pl) s.push({k:p+'_plan',n:nm+' program',c:SP[i],kind:'plan',v:pl});
    if(C[p+'_out']) s.push({k:p+'_out',n:nm+' output',c:PV[i],kind:'out',v:C[p+'_out']}); }
  return s; }
function rpLegend(){ $('rpLeg').innerHTML='<span class="lh">Show:</span>'+rpSeries().filter(x=>x.kind!=='out').map(x=>`<button class="lt" data-rp="${x.k}" aria-pressed="${!RP.hide[x.k]}"><i class="sw" style="background:${x.c}${x.kind==='pv'?'':';height:4px'}${x.kind==='band'?';opacity:.35':''}"></i>${esc(x.n)}</button>`).join('')
  +(RP.ev.length?`<button class="lt" data-rp="ev" aria-pressed="${!RP.hide.ev}"><i class="sw" style="background:var(--muted);height:4px"></i>Events (${RP.ev.length})</button>`:''); }
$('rpLeg').addEventListener('click',e=>{ const b=e.target.closest('[data-rp]'); if(!b) return; RP.hide[b.dataset.rp]=!RP.hide[b.dataset.rp]; rpLegend(); rpDraw(); });
const RL=46, RR=14, RT=14, RB=30;
function rpX(t){ return RL+(RP.W-RL-RR)*(t-RP.v0)/(RP.v1-RP.v0); }
function rpT(x){ return RP.v0+(x-RL)/(RP.W-RL-RR)*(RP.v1-RP.v0); }
function rpTick(){ const px=RP.W-RL-RR, span=RP.v1-RP.v0; for(const st of [1,2,5,10,15,30,60,120,300,600,900,1800,3600,7200,10800,14400,21600,43200,86400]) if(px/(span/st)>=70) return st; return 172800; }
const rpTimeTxt=(t,sec)=>{ if(RP.clock){ const d=new Date((RP.t0+t)*1000); return d.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:sec?'2-digit':undefined}); } return (t<0?'−':'')+fmt(Math.abs(t)); };
// index range of the rows in [a, b] (rows sorted by time)
function rpIdx(t){ const T=RP.T; let lo=0,hi=T.length; while(lo<hi){ const m=(lo+hi)>>1; if(T[m]<t) lo=m+1; else hi=m; } return lo; }
function rpPath(v,y){ const i0=Math.max(0,rpIdx(RP.v0)-1), i1=Math.min(RP.T.length,rpIdx(RP.v1)+2), W=RP.W-RL-RR; let d='',pen=false,col=null,g=[];
  const put=p=>{ d+=(pen?'L':'M')+p[0].toFixed(1)+','+p[1].toFixed(1); pen=true; };
  const flush=()=>{ if(!g.length) return; let lo=g[0],hi=g[0]; for(const p of g){ if(p[1]<lo[1]) lo=p; if(p[1]>hi[1]) hi=p; } [g[0],lo,hi,g[g.length-1]].filter((p,k,a)=>a.indexOf(p)===k).sort((a,b)=>a[0]-b[0]).forEach(put); g=[]; };
  const many=i1-i0>W*3;
  for(let k=i0;k<i1;k++){ if(v[k]==null){ flush(); pen=false; continue; } const x=rpX(RP.T[k]), p=[x,y(v[k])];
    if(!many){ put(p); continue; } const c=Math.floor(x); if(c!==col){ flush(); col=c; } g.push(p); }
  flush(); return d; }
function rpBand(lo,hi,y){ const i0=Math.max(0,rpIdx(RP.v0)-1), i1=Math.min(RP.T.length,rpIdx(RP.v1)+2), step=Math.max(1,Math.floor((i1-i0)/(RP.W*2)));
  let top=[],bot=[],d=''; const end=()=>{ if(top.length>1) d+='M'+top.join('L')+'L'+bot.reverse().join('L')+'Z'; top=[]; bot=[]; };
  for(let k=i0;k<i1;k+=step){ if(lo[k]==null||hi[k]==null){ end(); continue; } const x=rpX(RP.T[k]).toFixed(1); top.push(x+','+y(hi[k]).toFixed(1)); bot.push(x+','+y(lo[k]).toFixed(1)); }
  end(); return d; }
function rpDraw(){ const box=$('rpBox'); if(page!=='replay') return;
  if(!RP.T.length){ box.innerHTML=''; return; }
  RP.W=Math.max(280,Math.floor(box.clientWidth)||1000); RP.H=RP.W<600?320:Math.round(Math.min(480,Math.max(360,RP.W*0.42)));
  const S=rpSeries().filter(x=>!RP.hide[x.k]&&x.kind!=='out'), i0=Math.max(0,rpIdx(RP.v0)), i1=Math.min(RP.T.length,rpIdx(RP.v1)+1);
  let mn=Infinity,mx=-Infinity; for(const x of S) for(const a of x.kind==='band'?[x.lo,x.hi]:[x.v]) for(let k=i0;k<i1;k++){ const v=a[k]; if(v!=null){ if(v<mn) mn=v; if(v>mx) mx=v; } }
  if(!isFinite(mn)){ mn=0; mx=100; } const pad=Math.max(1,(mx-mn)*0.06); let lo=mn-pad, hi=mx+pad;
  const ys=[0.5,1,2,5,10,20,25,50,100,200].find(st=>(hi-lo)/st<=8)||500; lo=Math.floor(lo/ys)*ys; hi=Math.ceil(hi/ys)*ys;
  const PBr=RP.H-RB, y=v=>PBr-(PBr-RT)*(v-lo)/(hi-lo);
  let s=`<svg width="${RP.W}" height="${RP.H}" viewBox="0 0 ${RP.W} ${RP.H}" role="img" aria-label="CSV replay chart"><defs><clipPath id="rpclip"><rect x="${RL}" y="0" width="${RP.W-RL-RR}" height="${RP.H}"/></clipPath></defs>`;
  for(let v=lo;v<=hi+1e-9;v+=ys) s+=`<line x1="${RL}" x2="${RP.W-RR}" y1="${y(v).toFixed(1)}" y2="${y(v).toFixed(1)}" style="stroke:var(--line)"/><text x="${RL-6}" y="${(y(v)+4).toFixed(1)}" text-anchor="end" style="fill:var(--muted);font-size:12px">${+v.toFixed(1)}</text>`;
  const st=rpTick(), base=RP.clock?RP.t0:0;
  for(let t=Math.ceil((RP.v0+base)/st)*st-base;t<=RP.v1+1e-6;t+=st){ const x=rpX(t).toFixed(1);
    s+=`<line x1="${x}" x2="${x}" y1="${RT}" y2="${PBr}" style="stroke:var(--line)"/><text x="${x}" y="${PBr+18}" text-anchor="middle" style="fill:var(--muted);font-size:12px">${rpTimeTxt(t,st<60)}</text>`; }
  s+=`<g clip-path="url(#rpclip)">`;
  if(!RP.hide.ev) for(const e of RP.ev){ if(e.t<RP.v0||e.t>RP.v1) continue; const x=rpX(e.t).toFixed(1); s+=`<line x1="${x}" x2="${x}" y1="${RT}" y2="${PBr}" style="stroke:${e.kind==='error'?'var(--danger)':e.kind==='warning'?'var(--amber)':'var(--muted)'};stroke-dasharray:2 4;opacity:.8"/>`; }
  for(const x of S){ if(x.kind==='band') s+=`<path d="${rpBand(x.lo,x.hi,y)}" style="fill:${x.c};opacity:.16;stroke:none"/>`; }
  for(const x of S){ if(x.kind==='band') continue; const st=x.kind==='pv'?'stroke-width:2.2':x.kind==='sp'?'stroke-width:1.6;opacity:.6':'stroke-width:1.8;stroke-dasharray:6 4;opacity:.9';
    s+=`<path d="${rpPath(x.v,y)}" style="fill:none;stroke:${x.c};stroke-linejoin:round;${st}"/>`; }
  if(RP.sel) s+=`<rect x="${Math.min(RP.sel[0],RP.sel[1])}" y="${RT}" width="${Math.abs(RP.sel[1]-RP.sel[0])}" height="${PBr-RT}" style="fill:var(--acc);opacity:.15"/>`;
  s+=`<g id="rpHov"></g></g></svg><div class="ctip" id="rpTip" hidden></div>`;
  box.innerHTML=s; RP.y=y; RP.PB=PBr;
  $('rpRange').textContent=`${rpTimeTxt(RP.v0,true)} – ${rpTimeTxt(RP.v1,true)}`+(RP.crop?' (cropped)':'');
  $('rpUncrop').hidden=!RP.crop; rpHover(); }
function rpHover(){ const g=$('rpHov'), tip=$('rpTip'); if(!g||!tip) return;
  const x=RP.hov; if(x==null||x<RL||x>RP.W-RR||RP.drag){ g.textContent=''; tip.hidden=true; return; }
  const t=rpT(x), k=(()=>{ const i=rpIdx(t); return i>0&&(i>=RP.T.length||t-RP.T[i-1]<RP.T[i]-t)?i-1:i; })(), tol=(RP.v1-RP.v0)/(RP.W-RL-RR)*12;
  let d=`<line x1="${x.toFixed(1)}" x2="${x.toFixed(1)}" y1="${RT}" y2="${RP.PB}" style="stroke:var(--muted);stroke-dasharray:2 3"/>`; const rows=[];
  if(k<RP.T.length&&Math.abs(RP.T[k]-t)<=Math.max(tol,(RP.v1-RP.v0)/50)){ const tk=RP.T[k];
    for(const s of rpSeries()){ if(RP.hide[s.k]&&s.kind!=='out') continue;
      if(s.kind==='band'){ if(s.lo[k]!=null) rows.push({c:s.c,n:s.n,v:`${f1(s.lo[k])} … ${f1(s.hi[k])} °C`,thin:true}); continue; }
      const v=s.v[k]; if(v==null) continue;
      if(s.kind==='out'){ rows.push({c:s.c,n:s.n,v:Math.round(v)+' %',thin:true}); continue; }
      d+=hoverDot(rpX(tk),RP.y(v),s.c); rows.push({c:s.c,n:s.n,v:f1(v)+' °C',thin:s.kind!=='pv'}); }
    if(!RP.hide.ev) for(const e of RP.ev) if(Math.abs(e.t-t)<=tol) rows.push({c:e.kind==='error'?'var(--danger)':e.kind==='warning'?'var(--amber)':'var(--muted)',n:e.text.slice(0,70),v:'',thin:true});
    g.innerHTML=d; tip.innerHTML=tipHtml(rpTimeTxt(tk,true)+(RP.clock?'':''),rows.length?rows:[{c:'transparent',n:'No values',v:''}]); tipPlace(tip,$('rpBox'),x,RT+2); }
  else { g.innerHTML=d; tip.hidden=true; } }
function rpPt(ev){ const svg=$('rpBox').querySelector('svg'); if(!svg) return null; const r=svg.getBoundingClientRect(); return (ev.clientX-r.left)*RP.W/r.width; }
function rpZoom(t,f){ const span=Math.max(5,(RP.v1-RP.v0)*f), r=(t-RP.v0)/(RP.v1-RP.v0); RP.v0=t-r*span; RP.v1=RP.v0+span; rpClamp(); rpDraw(); }
const rpP=new Map(); let rpPz=null;
$('rpBox').addEventListener('wheel',e=>{ if(!RP.T.length) return; e.preventDefault(); const x=rpPt(e); if(x==null) return;
  if(Math.abs(e.deltaX)>Math.abs(e.deltaY)){ const dt=e.deltaX/(RP.W-RL-RR)*(RP.v1-RP.v0); RP.v0+=dt; RP.v1+=dt; rpClamp(); rpDraw(); }
  else rpZoom(rpT(x),e.deltaY>0?1.25:0.8); },{passive:false});
$('rpBox').addEventListener('pointerdown',e=>{ const x=rpPt(e); if(x==null) return; if(e.pointerType==='mouse'&&e.button!==0) return;
  try{ $('rpBox').setPointerCapture(e.pointerId); }catch(_){} rpP.set(e.pointerId,x);
  if(e.shiftKey){ RP.sel=[x,x]; RP.drag='sel'; } else { RP.drag='pan'; rpPz={v0:RP.v0,v1:RP.v1,pts:new Map(rpP)}; }
  if(e.pointerType==='touch'){ RP.hov=x; } rpHover(); });
$('rpBox').addEventListener('pointermove',e=>{ const x=rpPt(e); if(x==null) return;
  if(!RP.drag){ if(e.pointerType!=='touch'){ RP.hov=x; rpHover(); } return; }
  rpP.set(e.pointerId,x);
  if(RP.drag==='sel'){ RP.sel[1]=x; rpDraw(); return; }
  const ids=[...rpPz.pts.keys()], span0=rpPz.v1-rpPz.v0, W=RP.W-RL-RR;
  if(rpP.size>=2&&ids.length>=2){ const a0=rpPz.pts.get(ids[0]),b0=rpPz.pts.get(ids[1]),a1=rpP.get(ids[0]),b1=rpP.get(ids[1]); if(a1==null||b1==null) return;
    const span=Math.max(5,span0*Math.max(10,Math.abs(b0-a0))/Math.max(10,Math.abs(b1-a1))), tm=rpPz.v0+((a0+b0)/2-RL)/W*span0; RP.v0=tm-((a1+b1)/2-RL)/W*span; RP.v1=RP.v0+span; }
  else { const x0=rpPz.pts.get(e.pointerId); if(x0==null) return; RP.v0=rpPz.v0-(x-x0)/W*span0; RP.v1=RP.v0+span0; }
  RP.hov=null; rpClamp(); rpDraw(); });
const rpUp=e=>{ rpP.delete(e.pointerId);
  if(RP.drag==='sel'&&RP.sel){ const a=rpT(Math.min(RP.sel[0],RP.sel[1])), b=rpT(Math.max(RP.sel[0],RP.sel[1])); RP.sel=null; if(b-a>=2){ RP.v0=a; RP.v1=b; rpClamp(); } }
  if(!rpP.size){ RP.drag=null; rpPz=null; } else rpPz={v0:RP.v0,v1:RP.v1,pts:new Map(rpP)};
  if(e.pointerType==='touch') setTimeout(()=>{ RP.hov=null; rpHover(); },1500); rpDraw(); };
$('rpBox').addEventListener('pointerup',rpUp); $('rpBox').addEventListener('pointercancel',rpUp);
$('rpBox').addEventListener('pointerleave',e=>{ if(e.pointerType!=='touch'){ RP.hov=null; rpHover(); } });
$('rpBox').addEventListener('dblclick',()=>{ rpFit(); rpDraw(); });
$('rpIn').onclick=()=>rpZoom((RP.v0+RP.v1)/2,1/1.6); $('rpOut').onclick=()=>rpZoom((RP.v0+RP.v1)/2,1.6); $('rpFit').onclick=()=>{ rpFit(); rpDraw(); };
$('rpCrop').onclick=()=>{ if(!RP.T.length) return; RP.crop=[RP.v0,RP.v1]; rpDraw(); setMsg('rpMsg',`Cropped to ${rpTimeTxt(RP.v0,true)} – ${rpTimeTxt(RP.v1,true)}; Save CSV writes only these rows.`); };
$('rpUncrop').onclick=()=>{ RP.crop=null; rpFit(); rpDraw(); setMsg('rpMsg',''); };
$('rpSave').onclick=()=>{ if(!RP.lines) return; const [a,b]=RP.crop||[-Infinity,Infinity], out=[RP.lines[0]];
  for(let k=0;k<RP.T.length;k++) if(RP.T[k]>=a&&RP.T[k]<=b) out.push(RP.lines[k+1]);
  const nm=RP.name.replace(/\.csv$/i,'')+(RP.crop?'_crop':'')+'.csv', el=document.createElement('a');
  el.href=URL.createObjectURL(new Blob([out.join('\n')+'\n'],{type:'text/csv'})); el.download=nm; document.body.appendChild(el); el.click(); setTimeout(()=>{ URL.revokeObjectURL(el.href); el.remove(); },500);
  setMsg('rpMsg',`Saved ${out.length-1} rows as ${nm}.`); };
$('rpOpen').onclick=()=>$('rpFile').click();
$('rpFile').onchange=async()=>{ const f=$('rpFile').files[0]; if(!f) return; rpLoad(await f.text(),f.name); $('rpFile').value=''; };
async function rpFetch(path,name){ if(OFF&&!SER.on) return setMsg('rpMsg','Not connected to a device.',true); setMsg('rpMsg','Loading…');
  try{ const t=await apiText(path); if(!t) return setMsg('rpMsg','The device sent nothing.',true); rpLoad(t,name); }catch(e){ setMsg('rpMsg','Device not reachable.',true); } }
$('rpHist').onclick=()=>rpFetch('/api/history.csv','tempctl-duo-history.csv');
$('rpRun').onclick=()=>rpFetch('/api/log.csv','tempctl-duo-run.csv');
$('pg-replay').addEventListener('dragover',e=>{ if([...e.dataTransfer.types].includes('Files')){ e.preventDefault(); e.dataTransfer.dropEffect='copy'; } });
$('pg-replay').addEventListener('drop',async e=>{ const f=e.dataTransfer.files&&e.dataTransfer.files[0]; if(!f) return; e.preventDefault(); rpLoad(await f.text(),f.name); });
new ResizeObserver(()=>{ if(page==='replay'&&RP.T.length&&Math.floor($('rpBox').clientWidth)!==RP.W) rpDraw(); }).observe($('rpBox'));
