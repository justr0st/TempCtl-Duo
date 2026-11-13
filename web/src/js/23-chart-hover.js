// ---- hover (Rost): a dotted line at the pointer, a dot on each curve, a box with the time and the values
// (mouse / pen; a finger on the live chart). Shared helpers with the CSV replay page.
function nearPt(pts,t,tol){ if(!pts||!pts.length) return null; let lo=0,hi=pts.length-1;
  while(lo<hi){ const m=(lo+hi)>>1; if(pts[m][0]<t) lo=m+1; else hi=m; }
  let b=null; for(const k of [lo-1,lo,lo+1]) if(k>=0&&k<pts.length&&pts[k][1]!=null){ const d=Math.abs(pts[k][0]-t); if(d<=tol&&(!b||d<b.d)) b={d,t:pts[k][0],v:pts[k][1]}; }
  return b; }
function segAt(segs,t){ for(const g of segs||[]){ if(g.fl&1||g.v0==null||t<g.t0||t>g.t1) continue; return {t,v:g.t1>g.t0?g.v0+(g.v1-g.v0)*(t-g.t0)/(g.t1-g.t0):g.v1}; } return null; }
function tipHtml(title,rows){ return `<div class="th">${esc(title)}</div>`+rows.map(r=>`<div class="tr"><i style="background:${r.c}${r.thin?';opacity:.6':''}"></i><span>${esc(r.n)}</span><b>${r.v}</b></div>`).join(''); }
function tipPlace(tip,box,x,top){ tip.hidden=false; const w=tip.offsetWidth, bw=box.clientWidth; tip.style.left=Math.max(0,x+14+w>bw?x-14-w:x+14)+'px'; tip.style.top=top+'px'; }
const hoverDot=(x,y,c)=>`<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="4.5" style="fill:${c};stroke:var(--panel);stroke-width:2"/>`;
let hovX=null;
function drawHover(){ const g=$('chHover'), tip=$('ctip'); if(!g||!tip) return;
  if(hovX==null||!M||drag||hovX<CL||hovX>CW-CR){ if(g.firstChild) g.textContent=''; tip.hidden=true; return; }
  const t=tAtX(hovX), lv=M.kind==='live', tol=Math.max((V1-V0)/plotW()*10,(M.bin||1)*1.5);
  let d=`<line x1="${hovX.toFixed(1)}" x2="${hovX.toFixed(1)}" y1="${PT}" y2="${PB}" style="stroke:var(--muted);stroke-dasharray:2 3"/>`; const rows=[];
  for(const i of CH){ const h=M.hist[i]||{pv:[],sp:[]};
    if(!lv||LVIS.pv[i]){ let p=nearPt(h.pv,t,tol); if(M.head&&M.head.v[i]!=null&&Math.abs(M.head.t-t)<=tol&&(!p||Math.abs(M.head.t-t)<p.d)) p={t:M.head.t,v:M.head.v[i]};
      if(p){ d+=hoverDot(X(p.t),Y(p.v),PV[i]); rows.push({c:PV[i],n:chName(i)+' PV',v:f1(p.v)+' °C'}); } }
    if(!lv||LVIS.sp[i]){ const q=nearPt(h.sp,t,tol)||(lv?null:segAt(M.segs[i],t));
      if(q){ d+=hoverDot(X(q.t),Y(q.v),SP[i]); rows.push({c:SP[i],n:chName(i)+(M.kind==='preview'||!nearPt(h.sp,t,tol)?' plan':' SP'),v:f1(q.v)+' °C',thin:true}); } } }
  if(M.hist[MON]&&(!lv||LVIS.pv[MON])){ let p=nearPt(M.hist[MON].pv,t,tol); if(M.head&&M.head.v[MON]!=null&&Math.abs(M.head.t-t)<=tol&&(!p||Math.abs(M.head.t-t)<p.d)) p={t:M.head.t,v:M.head.v[MON]};
    if(p){ d+=hoverDot(X(p.t),Y(p.v),PV[MON]); rows.push({c:PV[MON],n:M.mon||chName(MON),v:f1(p.v)+' °C'}); } }
  g.innerHTML=d;
  const title=lv?`${new Date(M.at+t*1000).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:'2-digit'})} (−${fmt(V1-t)})`:(M.kind==='preview'?'':'Run time ')+fmt(t);
  tip.innerHTML=tipHtml(title,rows.length?rows:[{c:'transparent',n:'No values here',v:''}]); tipPlace(tip,$('chartbox'),hovX,PT+2); }
$('chartbox').addEventListener('pointermove',ev=>{ const touch=ev.pointerType==='touch';
  if((touch&&!(M&&M.kind==='live'))||(!touch&&ev.buttons)||drag||pz){ if(hovX!=null){ hovX=null; drawHover(); } return; }
  if(!$('chartbox').querySelector('svg')) return; hovX=svgPt(ev).x; drawHover(); });
$('chartbox').addEventListener('pointerdown',ev=>{ if(ev.pointerType==='touch'&&M&&M.kind==='live'&&$('chartbox').querySelector('svg')){ hovX=svgPt(ev).x; drawHover(); } else if(hovX!=null){ hovX=null; drawHover(); } });
$('chartbox').addEventListener('pointerleave',()=>{ hovX=null; drawHover(); });
$('chartbox').addEventListener('pointerup',ev=>{ if(ev.pointerType==='touch'){ setTimeout(()=>{ hovX=null; drawHover(); },1500); } });
const zc=()=>M&&M.now!=null&&M.now>=V0&&M.now<=V1?M.now:(V0+V1)/2;
$('zin').onclick=()=>zoomAt(zc(),1/1.6); $('zout').onclick=()=>zoomAt(zc(),1.6); $('zfit').onclick=()=>{zv.fit=true;lastChart=0;};
$('zscroll').oninput=e=>{zv.v0=+e.target.value;afterView();};
let lastW=0; new ResizeObserver(()=>{ const w=$('chartbox').clientWidth; if(w!==lastW){ lastW=w; lastChart=0; } }).observe($('chartbox'));
function setView(v){ view=v; $('vProg').setAttribute('aria-pressed',v==='prog'); $('vLive').setAttribute('aria-pressed',v==='live'); M=null; chartAt=0; zv.fit=true; lastChart=0; renderChart(); poll(); }
$('vProg').onclick=()=>setView('prog'); $('vLive').onclick=()=>setView('live');

