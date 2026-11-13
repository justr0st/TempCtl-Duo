/* ---------- Tools page ---------- */
function updTools(){
  if(!CFG) return; const mb=CH.filter(i=>CFG.channels[i].type==='modbus');
  const h=mb.map(i=>`<option value="${i}">${esc(CFG.channels[i].name)} (address ${CFG.channels[i].modbus.addr})</option>`).join(''), s=$('tCh');
  if(s.dataset.h!==h){ const v=s.value; s.innerHTML=h; s.dataset.h=h; if(mb.includes(+v)) s.value=v; }
  $('tNone').hidden=mb.length>0; $('tBox').hidden=!mb.length; s.disabled=!mb.length;
  if(document.activeElement!==$('scFrom')&&!$('scFrom').value) $('scFrom').value=CFG.tools.scanFrom;
  if(document.activeElement!==$('scTo')&&!$('scTo').value) $('scTo').value=CFG.tools.scanTo;
}
const tch=()=>+$('tCh').value;
$('bFind').onclick=async()=>{ const from=+$('scFrom').value, to=+$('scTo').value, ch=tch();
  if(!(to>=from)||to-from>2000){ setMsg('scOut','Range: “To” ≥ “From”, at most 2000 registers.',true); return; }
  setMsg('scOut','Scanning, this takes up to 20 s…'); $('bFind').disabled=true;
  try{ await postCfg({tools:{scanFrom:from,scanTo:to}}); let r=await api('/api/mb/findsp',{ch,from,to},20000);
    for(const t0=Date.now();r&&r.busy&&Date.now()-t0<60000;){ await sleep(700); r=await api('/api/mb/findsp',undefined,20000); }  // (the scan runs in the background)
    if(!r||r.busy||r.none){ setMsg('scOut',r&&r.busy?'No result after 60 s.':'The scan result was lost - scan again.',true); return; }
    if(!r.ok){ setMsg('scOut',r.err||'Failed',true); return; }
    const part=r.stoppedAt!=null?` Scanned ${from} … ${r.stoppedAt} (the device scans at most 300 registers / 20 s at a time): scan again from ${r.stoppedAt+1} for the rest.`:'';
    if(r.stoppedAt!=null) $('scFrom').value=r.stoppedAt+1;
    if(!r.candidates.length){ setMsg('scOut',`No register in this range holds ${f1(r.reference)}.${part||' Try another range.'}`); return; }
    $('scOut').className='msg'; $('scOut').innerHTML=`Registers holding ${f1(r.reference)}:${esc(part)}<div class="row">${r.candidates.map(a=>`<button data-usesp="${a}">Use ${a} as setpoint address</button>`).join('')}</div>`;
  }catch(e){ setMsg('scOut','Device not reachable.',true); } finally{ $('bFind').disabled=false; } };
$('scOut').onclick=async e=>{ const b=e.target.closest('button[data-usesp]'); if(!b) return; const ch=tch(), a=+b.dataset.usesp;
  const body={}; sp(body,`channels.${ch}.modbus.reg.sp`,a); const r=await postCfg(body); if(r.err){ setMsg('scOut',r.err,true); return; } CFG=r;
  setMsg('scOut',`Setpoint address of ${CFG.channels[ch].name} set to ${a}. Confirm it by changing the setpoint on the controller and reading the register here.`); };
$('bRd').onclick=async()=>{ const a=+$('rdA').value, n=Math.max(1,Math.min(60,+$('rdN').value||1)), ch=tch();
  try{ const r=await api(`/api/mb/read?ch=${ch}&addr=${a}&n=${n}&fn=${$('rdFn').value}`,undefined,20000);
    if(!r.ok){ setMsg('rdOut',r.err||'Failed',true); return; }
    $('rdOut').className='msg'; $('rdOut').innerHTML=`<div class="ed-wrap"><table class="t"><thead><tr><th>Address</th><th>Raw</th><th>÷ 10</th><th>Hex</th></tr></thead><tbody>${r.values.map((v,k)=>
      `<tr><td class="n">${a+k}</td><td class="n">${v}</td><td class="n">${(v/10).toFixed(1)}</td><td class="n">0x${(v&0xffff).toString(16).padStart(4,'0')}</td></tr>`).join('')}</tbody></table></div>`;
  }catch(e){ setMsg('rdOut','Device not reachable.',true); } };
$('bWr').onclick=async()=>{ const a=+$('wrA').value, v=+$('wrV').value, ch=tch(); if($('wrA').value===''||$('wrV').value==='') return;
  if(!await ask(`Write raw value ${v} to register ${a} of ${CFG.channels[ch].name}?`,'Write')) return;
  try{ const r=await api('/api/mb/write',{ch,addr:a,value:v},20000); setMsg('wrOut',r.ok?'Written.':r.err||'Failed',!r.ok); }catch(e){ setMsg('wrOut','Device not reachable.',true); } };
$('bCfgDl').onclick=async()=>{ try{ const c=await api('/api/config'); delete c.limits; if(c.wifi){ delete c.wifi.passSet; delete c.wifi.apPassSet; }
  download(`tempctl-duo-settings-${(c.wifi&&c.wifi.hostname)||'device'}.json`,c); setMsg('cfgMsg',''); }catch(e){ setMsg('cfgMsg','Device not reachable.',true); } };
$('bCfgUp').onclick=async()=>{ const t=await pickFile(); if(t==null) return; let o=parseJson(t);
  if(o&&o.kind==='tempctl-duo-backup') o=o.settings;  // (an SD card backup: its settings)
  if(!o||typeof o!=='object'||!Array.isArray(o.channels)||!o.bus){ setMsg('cfgMsg','This is not a TempCtl Duo settings file.',true); return; }
  if(!await ask('Replace the settings with the ones from the file? Channel types, names and all other settings change; a running program may be stopped.','Restore')) return;
  delete o.limits; const r=await postCfg(o); if(r.err){ setMsg('cfgMsg','Restore failed: '+r.err,true); return; } CFG=r;
  for(const F of Object.values(FORMS)) fillForm(F); setMsg('cfgMsg','Settings restored.'); poll(); };
$('bPrDl').onclick=async()=>{ try{ if(!progs) await loadPrograms(); const out=[];
  for(let i=0;i<progs.programs.length;i++) if(progs.programs[i].used){ const p=await storedProg(i); if(p) out.push({i,program:p}); }
  download('tempctl-duo-programs.json',{fmt:1,kind:'tempctl-duo-programs',programs:out}); setMsg('prMsg',`${out.length} programs saved to the file.`); }catch(e){ setMsg('prMsg','Device not reachable.',true); } };
$('bPrUp').onclick=async()=>{ const t=await pickFile(); if(t==null) return; const o=parseJson(t);
  if(!o||!Array.isArray(o.programs)){ setMsg('prMsg',o&&o.type?'This is a single program: import it on the Programs page.':'This is not a TempCtl Duo programs file.',true); return; }
  const list=o.programs.filter(x=>x&&Number.isInteger(x.i)&&x.i>=0&&x.i<20&&x.program);
  if(!list.length){ setMsg('prMsg','The file holds no programs.',true); return; }
  if(!await ask(`Restore ${list.length} program(s) into slot(s) ${list.map(x=>x.i+1).join(', ')}? The programs in these slots are replaced.`,'Restore')) return;
  const bad=[]; let n=0;
  for(const x of list){ if(runActive()&&live.run.prog===x.i){ bad.push(`slot ${x.i+1}: running`); continue; }
    try{ const r=await api('/api/program',{i:x.i,program:x.program}); if(r.err) bad.push(`slot ${x.i+1}: ${r.err}`); else { n++; delete DRAFT[x.i]; delete SAVED[x.i]; savedGen[x.i]=-1; } }catch(e){ bad.push(`slot ${x.i+1}: no answer`); } }
  await loadPrograms(); setMsg('prMsg',`${n} program(s) restored.${bad.length?' Not restored: '+bad.join('; '):''}`,bad.length>0); };

/* ---------- backups on the SD card (Tools, S3: settings + all programs in one file, spec 9.12) ---------- */
const BK={list:null,busy:false};
function bkName(n){  // "2026-10-14_153012.json" (the device's local time) / "start12_1.json"
  let m=/^(\d{4})-(\d\d)-(\d\d)_(\d\d)(\d\d)(\d\d)(?:_(\d+))?\.json$/.exec(n);
  if(m){ const d=new Date(+m[1],m[2]-1,+m[3],+m[4],+m[5],+m[6]);
    return d.toLocaleString([], {year:'numeric',month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',second:'2-digit'})+(m[7]?` (${m[7]})`:''); }
  m=/^start(\d+)_(\d+)\.json$/.exec(n); if(m) return `start ${m[1]}, backup ${m[2]} (no clock)`;
  return n.replace(/\.json$/,''); }
async function bkLoad(){
  if(BK.busy) return; BK.busy=true; $('bkRef').disabled=true;
  try{ BK.list=await api('/api/backup',undefined,20000); }catch(e){ BK.list={err:'No answer from the device'}; }
  BK.busy=false; $('bkRef').disabled=false; bkRender(); masonry(); }
function bkRender(){
  const L=BK.list; if(!L){ $('bkSt').textContent='Card not read yet.'; $('bkList').innerHTML=''; return; }
  const files=L.files||[], why=L.err||'';
  $('bkSt').innerHTML=L.card===false||why?(/^no card/.test(why)||!why?'No card found - insert a FAT32 card.':esc(why))
    :(files.length?`${files.length} backup${files.length===1?'':'s'}, newest first`:'No backups on the card yet')+(L.freeKb?` · ${kB(L.freeKb)} free of ${kB(L.totalKb)}`:'')+'.';
  $('bkList').innerHTML=files.map(([n,size])=>`<div class="sdrow bk"><span class="pn">${esc(bkName(n))}</span><span class="pm"><span>${fsize(size)}</span></span>
    <span class="pa"><button data-bk="res" data-n="${esc(n)}">Restore…</button><button data-bk="get" data-n="${esc(n)}">Download</button><button data-bk="del" data-n="${esc(n)}" class="danger">Delete</button></span></div>`).join(''); }
function bkUpdate(){ const sd=!!(live&&live.sd); $('bkSd').hidden=!sd; if(sd&&BK.list===null) bkLoad(); }
$('bkRef').onclick=()=>bkLoad();
$('bkNew').onclick=async()=>{ const b=$('bkNew'); b.disabled=true; setMsg('bkMsg','Writing the backup…');
  try{ const r=await api('/api/backup',{},30000);
    if(r.err) setMsg('bkMsg','Backup failed: '+r.err,true); else { setMsg('bkMsg',`Saved: ${bkName(r.name)} (backup/${r.name}).`); await bkLoad(); } }
  catch(e){ setMsg('bkMsg','No answer from the device.',true); }
  b.disabled=false; };
$('bkList').addEventListener('click',async e=>{ const b=e.target.closest('button[data-bk]'); if(!b) return; const n=b.dataset.n, act=b.dataset.bk;
  if(act==='del'){ if(!await ask(`Delete the backup of ${bkName(n)} from the SD card?`,'Delete')) return;
    const r=await api('/api/backup/delete',{name:n},20000).catch(()=>({err:'No answer from the device'})); if(r.err){ setMsg('bkMsg',r.err,true); return; }
    setMsg('bkMsg',''); bkLoad(); return; }
  if(act==='get'){ b.disabled=true; setMsg('bkMsg','Reading the file…');
    try{ const path='/api/backup/file?name='+encodeURIComponent(n); let t;
      if(SER.on){ t=await serReq(path,undefined,60000); if(typeof t!=='string') throw new Error(t&&t.err||'not readable'); }
      else { const r=await fetch(path,{cache:'no-store'}); if(!r.ok){ let m='not readable'; try{ m=(await r.json()).err||m; }catch(x){} throw new Error(m); } t=await r.text(); }
      const a=document.createElement('a'); a.href=URL.createObjectURL(new Blob([t],{type:'application/json'})); a.download='tempctl-duo-backup-'+n;
      document.body.appendChild(a); a.click(); setTimeout(()=>{ URL.revokeObjectURL(a.href); a.remove(); },500); setMsg('bkMsg','');
    }catch(err){ setMsg('bkMsg','Download failed: '+err.message,true); }
    b.disabled=false; return; }
  if(runActive()){ setMsg('bkMsg','A program is running - stop it first.',true); return; }
  b.disabled=true; setMsg('bkMsg','Reading the backup…'); let i;
  try{ i=await api('/api/backup/info?name='+encodeURIComponent(n),undefined,20000); }catch(x){ i={err:'No answer from the device'}; }
  b.disabled=false; if(i.err){ setMsg('bkMsg',i.err,true); return; } setMsg('bkMsg','');
  const pr=`${i.programs} program${i.programs===1?'':'s'}`+(i.programs?` (slot${i.programs===1?'':'s'} ${i.slots})`:'');
  const opts=[]; if(i.settings&&i.programs) opts.push(['all','Restore both',true]);
  if(i.settings) opts.push(['set','Settings only',!i.programs]); if(i.programs) opts.push(['prog','Programs only',!i.settings]);
  if(!opts.length){ setMsg('bkMsg','This backup holds nothing to restore.',true); return; } opts.push(['cancel','Cancel']);
  const a=await choose(`Restore from the backup of ${bkName(n)}${i.fw?` (firmware ${i.fw})`:''}?\nIt holds ${i.settings?'the settings and ':''}${pr}.\n`+
    `Settings: all of them are replaced (the stored Wi-Fi passwords stay). Programs: the slots in the backup are replaced, the other slots stay as they are.`,opts);
  if(a==='cancel') return;
  setMsg('bkMsg','Restoring…');
  let r; try{ r=await api('/api/backup/restore',{name:n,settings:a!=='prog',programs:a!=='set'},30000); }catch(x){ r={err:'No answer from the device'}; }
  if(r.err) setMsg('bkMsg','Restore: '+r.err,true); else setMsg('bkMsg',r.msg);
  if(a!=='set'){ for(let k=0;k<20;k++) if(i.mask&(1<<k)){ delete DRAFT[k]; delete SAVED[k]; savedGen[k]=-1; } await loadPrograms().catch(()=>{}); }
  if(a!=='prog'){ cfgGen=-1; await loadCfg(); for(const F of Object.values(FORMS)) fillForm(F); }
  poll(); });

