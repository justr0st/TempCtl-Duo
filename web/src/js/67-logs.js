/* ---------- system log (System page): saved events of earlier starts + everything of this start ---------- */
const SLK=['Tech','Event','Run','Warning','Error'], SLC=['var(--muted)','var(--text)','var(--acc)','var(--amber)','var(--danger)'];
const SL={seq:0,boot:-1,lastBoot:-1,busy:false,at:0,n:0,find:''};
/* ---------- SD run logs (System page, S3: one CSV per run, spec 9.9) ---------- */
const SD={list:null,key:'',busy:false};
function sdName(n){  // "2026-10-12_143205_Lead-free_BGA.csv" (the device's local time) / "start12_run3_x.csv"
  let m=/^(\d{4})-(\d\d)-(\d\d)_(\d\d)(\d\d)(\d\d)_(.*?)(?:_(\d+))?\.csv$/.exec(n);
  if(m){ const d=new Date(+m[1],m[2]-1,+m[3],+m[4],+m[5],+m[6]);  // (shown as written: no conversion)
    return {prog:m[7].replace(/_/g,' '),when:d.toLocaleString([], {year:'numeric',month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit'})}; }
  m=/^start(\d+)_run(\d+)_(.*?)\.csv$/.exec(n);
  if(m) return {prog:m[3].replace(/_/g,' '),when:`start ${m[1]}, run ${m[2]} (clock not set)`};
  return {prog:n,when:''}; }
const kB=k=>k>=1048576?(k/1048576).toFixed(1)+' GB':k>=1024?(k/1024).toFixed(1)+' MB':k+' kB';
const fsize=b=>b>=1048576?(b/1048576).toFixed(1)+' MB':b>=1024?Math.round(b/1024)+' kB':b+' B';
async function sdLoad(){
  if(SD.busy) return; SD.busy=true; $('sdRef').disabled=true; setMsg('sdMsg','');
  try{ SD.list=await api('/api/sd',undefined,20000); }catch(e){ SD.list={err:'No answer from the device'}; }
  SD.busy=false; $('sdRef').disabled=false; sdRender(); }
function sdRender(){
  const L=SD.list, sd=live&&live.sd; if(!sd) return;
  const files=L&&L.files||[], rec=sd.logging?sd.file.replace(/^runs\//,''):'';
  const why=(L&&L.err)||sd.err||'';
  let st=sd.card===0||(L&&L.card===false)?(/^no card/.test(why)||!why?'No card found':esc(why))+' - insert a FAT32 card: the next run is logged.'
    :L&&L.card?`${files.length} run log${files.length===1?'':'s'} · ${L.freeKb?kB(L.freeKb)+' free of '+kB(L.totalKb):''}`:'Card not read yet.';
  if(rec) st+=` · Recording “${esc(sdName(rec).prog)}”.`;
  else if(sd.err&&L&&L.card) st+=` · Last problem: ${esc(sd.err)}`;
  $('sdSt').innerHTML=st+' A file per program run, a row every second, readable in a spreadsheet; times are the device\'s local time (Clock).';
  $('sdList').innerHTML=files.map(([n,size])=>{ const x=sdName(n), now=n===rec;
    return `<div class="sdrow"><span class="pn">${esc(x.prog)}${now?'<span class="state run">Recording</span>':''}</span>
      <span class="pm"><span>${esc(x.when)}</span><span>${fsize(size)}</span></span>
      <span class="pa"><button data-sd="view" data-n="${esc(n)}" title="Open it on the CSV replay page">Replay</button><button data-sd="get" data-n="${esc(n)}">Download</button><button data-sd="del" data-n="${esc(n)}" class="danger" ${now?'disabled':''}>Delete</button></span></div>`; }).join('');
}
function sdUpdate(){
  const sd=live&&live.sd; $('sdP').hidden=!sd; if(!sd) return;
  const key=sd.logging+'|'+sd.file;  // a run started / ended: read the list again
  if(SD.list===null||key!==SD.key){ SD.key=key; sdLoad(); } else sdRender(); }
$('sdRef').onclick=()=>sdLoad();
$('sdList').addEventListener('click',async e=>{ const b=e.target.closest('button[data-sd]'); if(!b) return; const n=b.dataset.n;
  if(b.dataset.sd==='del'){ if(!await ask(`Delete the run log “${sdName(n).prog}” (${sdName(n).when}) from the SD card?`,'Delete')) return;
    const r=await api('/api/sd/delete',{name:n},20000).catch(()=>({err:'No answer from the device'})); if(r.err){ setMsg('sdMsg',r.err,true); return; } sdLoad(); return; }
  b.disabled=true; setMsg('sdMsg','Reading the file…');
  try{ const path='/api/sd/file?name='+encodeURIComponent(n); let t;
    if(SER.on){ t=await serReq(path,undefined,120000); if(typeof t!=='string') throw new Error(t&&t.err||'not readable'); }
    else { const r=await fetch(path,{cache:'no-store'}); if(!r.ok){ let m='not readable'; try{ m=(await r.json()).err||m; }catch(x){} throw new Error(m); } t=await r.text(); }
    if(b.dataset.sd==='view'){ setMsg('sdMsg',''); b.disabled=false; location.hash='#/replay'; rpLoad(t,'tempctl-duo-'+n); return; }
    const a=document.createElement('a'); a.href=URL.createObjectURL(new Blob([t],{type:'text/csv'})); a.download='tempctl-duo-'+n;
    document.body.appendChild(a); a.click(); setTimeout(()=>{ URL.revokeObjectURL(a.href); a.remove(); },500); setMsg('sdMsg','');
  }catch(err){ setMsg('sdMsg','Download failed: '+err.message,true); }
  b.disabled=false; });

async function apiText(path){ if(SER.on) return serReq(path,undefined,30000); const r=await fetch(path,{cache:'no-store'}); return r.ok?r.text():''; }
function slTime(l){ if(l.epoch){ const d=new Date(l.epoch*1000), t=d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit',second:'2-digit'});
    return d.toDateString()===new Date().toDateString()?t:d.toLocaleDateString([], {day:'2-digit',month:'2-digit'})+' '+t; }
  return '+'+fmt(l.up); }  // (the clock was not set yet: time since the start)
function slAdd(l,box){
  if(l.boot!==SL.lastBoot){ SL.lastBoot=l.boot; const b=document.createElement('div'); b.className='bt';
    b.textContent=`Start ${l.boot}${l.boot===SL.boot?' (now)':''}`; box.appendChild(b); }
  const d=document.createElement('div'); d.className='l k'+l.kind;
  d.innerHTML=`<span class="t">${esc(slTime(l))}</span><span class="k">${SLK[l.kind]||''}</span><span class="m">${esc(l.text)}</span>`;
  d._t=(l.text+' '+(SLK[l.kind]||'')).toLowerCase(); if(SL.find&&!d._t.includes(SL.find)) d.classList.add('nf');
  box.appendChild(d); ++SL.n;
}
async function slLoad(){  // (again after a device restart)
  const box=$('slog'); box.innerHTML=''; SL.seq=0; SL.lastBoot=-1; SL.n=0; SL.boot=live.boot;
  try{ const t=await apiText('/api/eventlog.txt');  // events of earlier starts (saved in flash)
    for(const ln of t.split('\n')){ const p=ln.split('\t'); if(p.length<5) continue; const b=+p[0]; if(b>=SL.boot) continue;
      slAdd({boot:b,epoch:+p[1],up:+p[2],kind:+p[3],text:p.slice(4).join('\t')},box); } }catch(e){}
  await slMore(true);
}
async function slMore(first){
  const box=$('slog'), stick=box.scrollHeight-box.scrollTop-box.clientHeight<40;
  for(let k=0;k<40;k++){  // pieces of about 12 kB
    const d=await api('/api/syslog?since='+SL.seq); if(!d||!d.lines) break;
    if(first&&!SL.seq&&d.first>1){ const n=document.createElement('div'); n.className='bt'; n.textContent=`Start ${d.boot}: the ${d.first-1} oldest lines of this start were dropped (the device keeps the newest)`; box.appendChild(n); SL.lastBoot=d.boot; }
    for(const [seq,up,epoch,kind,text] of d.lines) slAdd({boot:d.boot,seq,up,epoch,kind,text},box);
    SL.seq=d.lines.length?d.lines[d.lines.length-1][0]:d.seq; if(!d.more) break; }
  while(SL.n>12000&&box.firstChild){ if(box.firstChild.classList.contains('l')) --SL.n; box.firstChild.remove(); }
  if(!SL.n) box.innerHTML='<div class="none">No log lines yet.</div>';
  if(stick||first) box.scrollTop=box.scrollHeight;
}
async function slUpdate(){
  if(SL.busy||!live) return;
  if(live.boot!==SL.boot){ SL.busy=true; try{ await slLoad(); }finally{ SL.busy=false; } return; }
  if(live.sysSeq>SL.seq&&Date.now()-SL.at>1500){ SL.busy=true; SL.at=Date.now(); try{ await slMore(false); }finally{ SL.busy=false; } }
}
(function(){ const bar=$('slBar'), box=$('slog');
  [4,3,2,1,0].forEach(k=>{ const b=document.createElement('button'); b.className='kf'; b.setAttribute('aria-pressed','true');
    b.innerHTML=`<i style="background:${SLC[k]}"></i>${['Technical','Events','Runs','Warnings','Errors'][k]}`;
    b.title=['Technical lines of the serial log ([wifi], [lcd], [mem] ...)','Events','Program runs','Warnings','Errors'][k];
    b.onclick=()=>{ const on=b.getAttribute('aria-pressed')!=='true'; b.setAttribute('aria-pressed',on); box.classList.toggle('h'+k,!on); };
    bar.insertBefore(b,$('slFind')); });
  $('slFind').oninput=e=>{ SL.find=e.target.value.trim().toLowerCase();
    for(const d of box.querySelectorAll('.l')) d.classList.toggle('nf',!!SL.find&&!d._t.includes(SL.find)); };
  $('slDl').onclick=()=>{ const out=[...box.children].map(d=>d.classList.contains('bt')?'\n== '+d.textContent+' ==':
      d.classList.contains('l')?[...d.children].map(x=>x.textContent).join('  '):'').join('\n');
    const a=document.createElement('a'); a.href=URL.createObjectURL(new Blob([out+'\n'],{type:'text/plain'}));
    a.download='tempctl-duo-log.txt'; document.body.appendChild(a); a.click(); setTimeout(()=>{ URL.revokeObjectURL(a.href); a.remove(); },500); };
})();

