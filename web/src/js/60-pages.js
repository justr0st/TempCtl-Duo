/* ---------- pages (hash routes: #/programs, #/replay, #/channels, #/log, #/settings, #/tools; anything else = Status) ---------- */
const PAGES={status:'Dashboard',programs:'Programs',replay:'CSV replay',channels:'Channels',log:'Log',settings:'Settings',tools:'Tools'};
const PAGE_ALIAS={system:'settings',dashboard:'status'};  // (old links)
let page='';
function route(){
  const h=location.hash, m0=h.startsWith('#/')?h.slice(2):'', m=PAGE_ALIAS[m0]||m0, pg=PAGES[m]?m:'status', changed=pg!==page;
  page=pg;
  for(const k in PAGES) $('pg-'+k).hidden=k!==pg;
  document.querySelectorAll('nav a[data-pg]').forEach(a=>{ const on=a.dataset.pg===pg; a.classList.toggle('on',on); if(on) a.setAttribute('aria-current','page'); else a.removeAttribute('aria-current'); });
  $('title').textContent=PAGES[pg]; $('jump').hidden=pg!=='status'; $('jumpS').hidden=pg!=='settings'; const app=DEMO?'TempCtl Duo DEMO':'TempCtl Duo'; document.title=pg==='status'?app:PAGES[pg]+' – '+app;
  $('side').classList.remove('open'); $('menu').setAttribute('aria-expanded','false');
  if(changed){ window.scrollTo(0,0); if(pg==='status'){ lastChart=0; chartAt=0; } if(pg==='replay') rpDraw(); pageUpdate(true); requestAnimationFrame(()=>{ shapeTabs(); topFade(); }); }
}
window.addEventListener('hashchange',route);
document.addEventListener('click',e=>{ const a=e.target.closest('a[data-jump]'); if(!a) return; e.preventDefault(); const t=$(a.dataset.jump); if(t) t.scrollIntoView({behavior:'smooth',block:'start'}); });
// Masonry: each tile spans as many 4 px rows as it is tall (+ the 12 px gap), so a short tile
// moves up into the space next to a tall one. One column: normal flow.
const masRO=new ResizeObserver(()=>requestAnimationFrame(masonry));
function masonry(){ document.querySelectorAll('.cards').forEach(c=>{
  if(!c.dataset.mo){ c.dataset.mo=1; masRO.observe(c); }
  for(const el of c.children) if(!el.dataset.mo){ el.dataset.mo=1; masRO.observe(el); }
  const cols=getComputedStyle(c).gridTemplateColumns.split(' ').filter(Boolean).length, on=cols>1&&c.offsetParent!==null;
  c.classList.toggle('mas',on);
  for(const el of c.children){ const v=on&&!el.hidden?'span '+Math.ceil((el.getBoundingClientRect().height+12)/4):''; if(el.style.gridRowEnd!==v) el.style.gridRowEnd=v; }
}); }
async function pageUpdate(force){
  if(!live) return;
  if((page==='channels'||page==='settings'||page==='tools')&&(!CFG||cfgGen!==live.cfgGen)) await loadCfg();
  if(page==='programs') renderProgs(force);
  else if(page==='channels') updChanPage();
  else if(page==='settings') updSystem();
  else if(page==='log'){ sdUpdate(); slUpdate(); }
  else if(page==='tools'){ updTools(); bkUpdate(); }
  masonry();
}
function download(name,obj){ const a=document.createElement('a'); a.href=URL.createObjectURL(new Blob([JSON.stringify(obj,null,1)],{type:'application/json'}));
  a.download=name; document.body.appendChild(a); a.click(); setTimeout(()=>{ URL.revokeObjectURL(a.href); a.remove(); },500); }
function pickFile(){ return new Promise(res=>{ const f=$('fileIn'); f.value=''; f.onchange=()=>{ const x=f.files[0]; if(!x) return res(null); x.text().then(t=>res(t),()=>res(null)); }; f.click(); }); }
function parseJson(t){ try{ return JSON.parse(t); }catch(e){ return null; } }
const fileName=s=>(s||'program').replace(/[^\w\-. ]+/g,'_').trim().replace(/\s+/g,'-').slice(0,40)||'program';
function setMsg(id,t,bad){ const m=$(id); m.textContent=t||''; m.className='msg'+(bad?' bad':''); }

