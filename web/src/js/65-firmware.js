/* ---------- firmware update (spec 9.11): upload of a .bin (Wi-Fi only), firmware.bin on the SD card (S3), release page (9.13) ---------- */
const FW={info:null,at:0,file:null,img:null,expect:null,phase:'',sd:null,rel:null,relAt:0,relErr:'',relBusy:false};
// what the .bin is: ESP image header (0xE9, chip id at 12: 0 ESP32, 9 ESP32-S3) and the TempCtl Duo id string
async function fwInspect(file){
  const b=new Uint8Array(await file.arrayBuffer()); const r={size:b.length,chip:b.length>13?b[12]|(b[13]<<8):-1,magic:b[0]===0xE9,board:'',version:''};
  const key=[...'TCDUO-IMAGE|'].map(c=>c.charCodeAt(0));
  outer: for(let i=0;i+key.length<b.length;i++){ if(b[i]!==84) continue; for(let k=0;k<key.length;k++) if(b[i+k]!==key[k]) continue outer;
    let s=''; for(let j=i+key.length;j<Math.min(b.length,i+key.length+80)&&b[j];j++) s+=String.fromCharCode(b[j]);
    const m=/^([^|]+)\|([^|]+)\|end/.exec(s); if(m){ r.board=m[1]; r.version=m[2]; break; } }
  return r; }
function fwProblem(r){ const I=FW.info; if(!I) return 'no answer from the device';
  if(r.size<65536) return 'the file is too small for a firmware image';
  if(!r.magic) return 'not an ESP32 firmware image (use .pio/build/<env>/firmware.bin)';
  const chip=I.board==='s3'?9:0; if(r.chip!==chip) return r.chip===9?'this firmware is for the ESP32-S3, not for this board':'this firmware is for another ESP32 chip than this board';
  if(!r.board) return 'not a TempCtl Duo firmware';
  if(r.board!==I.board) return `this firmware is for “${r.board}”, this device is “${I.board}”`;
  if(I.slotKb&&r.size>I.slotKb*1024) return `the file is larger than the firmware slot (${I.slotKb} kB)`;
  return ''; }
async function fwLoad(force){ if(!force&&FW.info&&Date.now()-FW.at<5000) return; FW.at=Date.now();
  try{ FW.info=await api('/api/ota'); }catch(e){ return; } fwShow(); }
function fwShow(){ const I=FW.info; if(!I) return; const wifi=!OFF&&!SER.on;
  if(DEMO){ $('fwHint').textContent=`Running ${I.version} in the browser demo: there is no firmware to update here.`; $('fwUp').hidden=$('fwSd').hidden=$('fwNet').hidden=true; return; }
  $('fwHint').textContent=`Running ${I.version} (board ${I.board}, slot ${I.slot}).`+(I.slotKb?` Drop or choose the firmware.bin of this board (PlatformIO: .pio/build/${I.board}/firmware.bin); the device restarts and checks the new firmware for 30 s — if it does not run, the previous one starts again.`:` ${I.why||'No second firmware slot.'}`)+(wifi?'':' The upload works over Wi-Fi only (open the page from the device’s address).');
  $('fwUp').hidden=!I.slotKb||!wifi; $('fwSd').hidden=!I.sd||!I.slotKb;
  $('fwGo').disabled=!FW.img||!!fwProblem(FW.img)||!I.canStart||FW.phase==='upload';
  $('fwGo').title=I.canStart?'':I.why||''; fwNetShow(); }
// the newest release on the hosted page: checked when Settings opens (at most every 10 min) or with the button
async function fwNetCheck(auto){ if(FW.relBusy||(auto&&FW.relAt&&Date.now()-FW.relAt<600000)) return;
  FW.relBusy=true; $('fwNetCheck').disabled=true; if(!auto) $('fwNetName').textContent='Asking the release page…';
  try{ FW.rel=await relManifest(); FW.relErr=''; }
  catch(e){ FW.rel=null; FW.relErr=`The release page (${REL_HOST}) cannot be reached — this computer or phone needs internet access.`; }
  FW.relAt=Date.now(); FW.relBusy=false; $('fwNetCheck').disabled=false; fwNetShow(); }
function fwNetShow(){ const I=FW.info, m=FW.rel, nm=$('fwNetName'), go=$('fwNetGo'); go.hidden=true;
  if(!I||!m){ if(!FW.relBusy) nm.textContent=FW.relErr; return; }
  const f=m.firmware&&m.firmware[I.board], d=relDate(m.date), dt=d?` (${d})`:'';
  if(!f){ nm.textContent=`Release ${m.version}${dt} has no firmware for this board (${I.board}).`; return; }
  const c=verCmp(m.version,I.version);
  if(c<=0){ nm.textContent=c===0?`${I.version} is the newest release.`:`The newest release is ${m.version}${dt}; this device runs ${I.version}.`; return; }
  const wifi=!OFF&&!SER.on;
  if(wifi&&I.slotKb){ nm.textContent=`Firmware ${m.version}${dt} is available (${Math.round(f.size/1024)} kB).`;
    go.hidden=false; go.disabled=!I.canStart||fwBusy(); go.title=I.canStart?'':I.why||''; }
  else nm.innerHTML=`Firmware ${esc(m.version)}${esc(dt)} is available: <a href="${esc(REL_BASE+f.url)}" download>download</a> it, or open the page from the device’s address to install it from here.`; }
$('fwNetCheck').onclick=()=>fwNetCheck(false);
$('fwNetGo').onclick=async()=>{ const m=FW.rel, I=FW.info, f=m&&I&&m.firmware&&m.firmware[I.board]; if(!f||fwBusy()) return;
  if(!await ask(`Install firmware ${m.version} (now ${I.version})? This browser downloads it from the release page and uploads it to the device (about a minute); the device then restarts. The outputs stay off.`,'Install')) return;
  FW.phase='upload'; fwShow(); setMsg('fwMsg','Downloading from the release page…'); fwBar(0,'download 0 %');
  let blob; try{ blob=await relDownload(REL_BASE+f.url,f.size,q=>fwBar(q*100,`download ${Math.round(q*100)} %`)); }
  catch(e){ FW.phase=''; fwBar(null); setMsg('fwMsg','The download failed ('+e.message+'). Nothing was changed.',true); fwShow(); return; }
  const file=new File([blob],f.url.split('/').pop(),{type:'application/octet-stream'}), r=await fwInspect(file);
  const p=blob.size!==f.size?'the download is incomplete':fwProblem(r)||(r.version!==m.version?`the file holds firmware ${r.version||'?'}, not ${m.version}`:'');
  if(p){ FW.phase=''; fwBar(null); setMsg('fwMsg','Not installed: '+p+'.',true); fwShow(); return; }
  fwUpload(file,r,f.md5); };
$('fwPick').onclick=()=>{ $('fwFile').value=''; $('fwFile').click(); };
$('fwFile').onchange=()=>fwSelect($('fwFile').files[0]);
async function fwSelect(f,note){ FW.file=f||null; FW.img=null; setMsg('fwMsg','');
  if(!f){ $('fwName').textContent=''; fwShow(); return; }
  $('fwName').textContent=f.name+' …'; FW.img=await fwInspect(f); const p=fwProblem(FW.img);
  $('fwName').textContent=`${f.name}: ${FW.img.version?'firmware '+FW.img.version+' for '+FW.img.board:Math.round(f.size/1024)+' kB'}`;
  if(p) setMsg('fwMsg',p,true); else if(!FW.info.canStart) setMsg('fwMsg',FW.info.why||'',true); else if(note) setMsg('fwMsg',note);
  fwShow(); }
// drag and drop of the .bin onto the box (files only; a file dropped elsewhere on the page is not opened by the browser)
const fwHasFiles=e=>!!e.dataTransfer&&[...e.dataTransfer.types].includes('Files');
const fwBusy=()=>FW.phase==='upload'||FW.phase==='sd'||!!FW.expect;
let fwDrag=0;
for(const [ev,fn] of Object.entries({
  dragenter:e=>{ if(!fwHasFiles(e)) return; e.preventDefault(); if(!fwBusy()&&++fwDrag===1) $('fwUp').classList.add('over'); },
  dragover:e=>{ if(!fwHasFiles(e)) return; e.preventDefault(); e.dataTransfer.dropEffect=fwBusy()?'none':'copy'; },
  dragleave:e=>{ if(!fwHasFiles(e)) return; if(--fwDrag<=0){ fwDrag=0; $('fwUp').classList.remove('over'); } },
  drop:e=>{ if(!fwHasFiles(e)) return; e.preventDefault(); fwDrag=0; $('fwUp').classList.remove('over'); if(fwBusy()) return;
    const fs=[...e.dataTransfer.files], f=fs.find(x=>/\.bin$/i.test(x.name))||fs[0]; if(!f) return;
    $('fwFile').value=''; fwSelect(f,fs.length>1?`${fs.length} files dropped: using ${f.name}.`:''); }}))
  $('fwUp').addEventListener(ev,fn);
for(const ev of ['dragover','drop']) window.addEventListener(ev,e=>{ if(fwHasFiles(e)&&!e.defaultPrevented){ e.preventDefault(); if(ev==='dragover') e.dataTransfer.dropEffect='none'; } });
function fwBar(pct,text){ $('fwProg').hidden=pct==null; if(pct!=null){ $('fwBarI').style.width=Math.max(0,Math.min(100,pct))+'%'; $('fwPct').textContent=text||Math.round(pct)+' %'; } }
// after the restart: wait for the device, then compare the version
function fwExpect(v){ FW.expect={v,from:FW.info.version,at:Date.now()}; }
function fwWatch(){ const E=FW.expect; if(!E||!live) return; if(Date.now()-E.at<4000) return;
  if(live.fw===E.v){ const pr=live.ota&&live.ota.probation;
    setMsg('fwMsg',pr?`Firmware ${E.v} runs — checking it for 30 s…`:`Firmware ${E.v} installed and confirmed.`); fwBar(null);
    if(!pr){ FW.expect=null; FW.phase=''; fwLoad(true); } }
  else if(live.fw===E.from&&live.up<120&&Date.now()-E.at>15000){ setMsg('fwMsg',`The new firmware did not start correctly: ${E.from} runs again (see the system log).`,true); FW.expect=null; FW.phase=''; fwBar(null); fwLoad(true); } }
$('fwGo').onclick=async()=>{ const f=FW.file, r=FW.img; if(!f||!r) return; const p=fwProblem(r); if(p){ setMsg('fwMsg',p,true); return; }
  if(!await ask(`Install firmware ${r.version} (now ${FW.info.version})? The upload takes about a minute; the device then restarts. The outputs stay off.`,'Install')) return;
  fwUpload(f,r); };
// the upload (a chosen / dropped file, or a release downloaded by this browser: with its MD5, checked by the device)
function fwUpload(f,r,md5){
  FW.phase='upload'; fwShow(); setMsg('fwMsg','Uploading…'); fwBar(0);
  const fd=new FormData(); fd.append('fw',f,f.name); const x=new XMLHttpRequest();
  x.open('POST','/api/ota?size='+f.size+(md5&&/^[0-9a-f]{32}$/i.test(md5)?'&md5='+md5:'')); x.setRequestHeader('X-TempCtl','1'); x.timeout=300000;
  x.upload.onprogress=e=>{ if(e.lengthComputable) fwBar(e.loaded/e.total*100,`${Math.round(e.loaded/e.total*100)} % (${Math.round(e.loaded/1024)} kB)`); };
  x.onload=()=>{ let j={}; try{ j=JSON.parse(x.responseText); }catch(e){}
    if(j.ok){ fwBar(100,'written'); setMsg('fwMsg',`Firmware ${j.version||r.version} written — the device restarts, the page reconnects by itself…`); fwExpect(j.version||r.version); }
    else{ FW.phase=''; fwBar(null); setMsg('fwMsg','Update failed: '+(j.err||('HTTP '+x.status)),true); fwLoad(true); } };
  x.onerror=x.ontimeout=()=>{ FW.phase=''; fwBar(null); setMsg('fwMsg','The upload was interrupted (connection lost). Nothing was changed if the device did not restart.',true); fwLoad(true); };
  x.send(fd); }
$('fwSdCheck').onclick=async()=>{ const b=$('fwSdCheck'); if(b.disabled) return; b.disabled=true;
  setMsg('fwMsg','Reading firmware.bin on the SD card…'); $('fwSdGo').hidden=true; $('fwSdName').textContent='';
  let r; const t0=Date.now();
  try{ r=await api('/api/ota/sd?start=1',undefined,20000);  // the device reads the file in the background: ask until done
    while(r&&r.busy&&Date.now()-t0<90000){ await new Promise(z=>setTimeout(z,600));
      setMsg('fwMsg',`Reading firmware.bin on the SD card… ${Math.round((Date.now()-t0)/1000)} s`); r=await api('/api/ota/sd',undefined,20000); } }
  catch(e){ r={err:'No answer from the device.'}; }
  b.disabled=false;
  if(r&&r.busy) r={err:'The SD card check takes too long - try again.'};
  if(!r||r.err||r.none){ setMsg('fwMsg',(r&&r.err)||'The check did not start - try again.',true); return; } FW.sd=r;
  if(!r.found){ setMsg('fwMsg','No firmware.bin in the root folder of the SD card.',true); return; }
  $('fwSdName').textContent=`firmware.bin: ${r.version?'firmware '+r.version+' for '+r.board:'?'} (${r.sizeKb} kB)`;
  if(r.problem){ setMsg('fwMsg',r.problem,true); return; }
  setMsg('fwMsg',FW.info&&!FW.info.canStart?FW.info.why||'':'',FW.info&&!FW.info.canStart); $('fwSdGo').hidden=false; };
$('fwSdGo').onclick=async()=>{ const r=FW.sd; if(!r||!r.found) return;
  if(!await ask(`Install firmware ${r.version} from the SD card (now ${FW.info.version})? It takes about 30 s; the device then restarts. The outputs stay off.`,'Install')) return;
  let j; try{ j=await api('/api/ota/sd',{}); }catch(e){ setMsg('fwMsg','No answer from the device.',true); return; }
  if(j.err){ setMsg('fwMsg',j.err,true); return; } FW.phase='sd'; $('fwSdGo').hidden=true; setMsg('fwMsg','Installing from the SD card…'); fwBar(0); fwExpect(r.version); FW.expect.at=Date.now()+60000; };
// progress of an update that the device reports (SD card, or one started elsewhere)
function fwLive(){ const o=live&&live.ota; if(!o) return;
  if(o.phase==='running'&&FW.phase!=='upload'){ const pct=o.total?o.done/o.total*100:null; fwBar(pct==null?50:pct,o.total?`${Math.round(pct)} %${o.source==='sd'?' (SD card)':''}`:`${Math.round(o.done/1024)} kB`); }
  if(o.phase==='done'&&FW.phase==='sd'){ fwBar(100,'written'); setMsg('fwMsg',`Firmware ${o.version} written — the device restarts, the page reconnects by itself…`); if(FW.expect) FW.expect.at=Date.now(); }
  if(o.phase==='failed'&&FW.phase==='sd'){ FW.phase=''; FW.expect=null; fwBar(null); setMsg('fwMsg','Update failed: '+o.msg,true); } }

let sysBuilt=false, wDirty=false;
const upTxt=s=>{ const d=Math.floor(s/86400); return (d?d+' d ':'')+fmt(s%86400); };
function updSystem(){
  if(!CFG||!live) return;
  if(!sysBuilt){ sysBuilt=true; buildTimeForm();
    const L=CFG.limits||{};
    mkForm('safe',$('sSafe'),[{h:'',fields:[
      {k:'safety.manualIdle',l:'End manual mode without activity',t:'sw',w:true,h:'A forgotten manual setpoint: it is switched off after this long without a touch, a web action or a serial command (a beep and a message 1 min before).'},
      {k:'safety.manualIdleMin',l:'… after',t:'num',min:2,max:1440,u:'min',def:30,show:g=>g('safety.manualIdle')},
      {k:'safety.estop',l:'E-stop input',t:'sw',w:true,show:()=>!!CFG.limits.estop,h:'Normally closed contact between the E-stop pin and GND. Open contact or a broken wire: all outputs and the SSR supply relay off, a running program stops.'},
      {k:'safety.relay',l:'SSR supply relay',t:'sw',def:true,w:true,show:()=>!!CFG.limits.relay,h:'An active-high DC solid-state relay in the common +5 V supply of both SSR inputs: on while a local channel heats, off at once on a fault or E-stop (covers a stuck SSR input or driver; a shorted SSR output needs a contactor). Switch it off only when no relay is fitted: the relay pin then stays low — with a relay fitted, the heaters would get no power.'},
      {k:'safety.sound',l:'Alarm sound',t:'sw',def:true,w:true,show:()=>!!CFG.limits.sound,h:'Faults and E-stop: three long beeps every 3 s. “Action required” and the manual-mode warning: two short beeps every 10 s. Run finished: three short beeps. A touch or a web action silences the current alarm.'},
      {k:'safety.volume',l:'Sound volume',t:'sel',num:true,def:3,w:true,o:[[1,'1 (quietest)'],[2,'2'],[3,'3'],[4,'4'],[5,'5 (loudest)']],show:()=>!!CFG.limits.sound,h:'Alarms (faults, E-stop) always play at full volume.'},
      {k:'safety.keyBeep',l:'Key beep',t:'sw',def:true,w:true,show:()=>!!CFG.limits.sound,h:'A short beep on the touch-screen buttons.'},
      {k:'safety.chipWarn',l:'Warn when the chip gets hot',t:'sw',def:true,w:true,show:()=>!!L.chipTemp,h:'The ESP32-S3’s internal temperature sensor, read every 30 s: a warning in the log above the limit.'},
      {k:'safety.chipMaxC',l:'… above',t:'num',min:40,max:120,u:'°C',def:80,show:g=>!!L.chipTemp&&g('safety.chipWarn')}],
      extra:`<div class="row" id="beepRow"><button id="bBeep">Test beep</button><span class="msg" id="beepMsg"></span></div>`},
     {h:'Optional pins',show:()=>!!(CFG.limits.ioGpio&&CFG.limits.ioGpio.length),note:'What each spare pin of the board does. A function can be on one pin only.',
      fields:(L.ioGpio||[]).map((pin,i)=>({k:'io.fn.'+i,l:'GPIO '+pin,t:'sel',w:true,o:IO_FN}))},
     {h:'Cooling fan',show:()=>!!CFG.limits.fan,fields:[
      {k:'fan.afterRun',l:'Run the fan after a program',t:'sw',def:true,w:true,h:'The fan keeps running after the program ends, until every channel is cool.'},
      {k:'fan.offBelow',l:'… until every channel is below',t:'num',min:30,max:200,step:1,u:'°C',def:60,w:true,show:g=>g('fan.afterRun')},
      {k:'fan.maxMin',l:'… at most',t:'num',min:1,max:240,step:1,u:'min',def:30,w:true,show:g=>g('fan.afterRun')},
      {k:'fan.speed',l:'Fan speed (PWM fan)',t:'num',min:20,max:100,step:1,u:'%',def:100,w:true,show:()=>((CFG.io&&CFG.io.fn)||[]).includes('fanPwm')}]}],
      {title:'Safety',auto:true,top:'<p class="live1" id="safeLive"></p>'});
    if($('bBeep')) $('bBeep').onclick=async()=>{ try{ const r=await api('/api/beep',{}); setMsg('beepMsg',r.err||'',!!r.err); }catch(e){ setMsg('beepMsg','No answer from the device.',true); } };
    mkForm('disp',$('sDisp'),[{h:'',fields:[
      {k:'display.accent',l:'Accent colour',t:'accent',h:'Used on the touch screen and on this page; the channel colours follow from it.'},
      {k:'display.themeDark',l:'Touch screen theme',t:'theme',def:true},
      {k:'display.brightness',l:'Touch screen brightness',t:'range',min:10,max:255,w:true,fmt:v=>(v>=255?100:Math.max(1,Math.min(99,Math.round(1+(v-10)*99/245))))+' %'}  /* 10 = 1 %, 255 = 100 % (only fully on: no PWM) */,
      {k:'display.rotate180',l:'Rotate the screen by 180°',t:'sw'},
      {k:'display.screenCheck',l:'Restart a hanging screen by itself',t:'sw',def:true,h:'Checks the display and the touch controller every second and restarts one that hangs (logged as an event).'},
      {k:'display.spiMhz',l:'Display SPI clock',t:'sel',num:true,o:[[20,'20 MHz (long wires)'],[40,'40 MHz'],[80,'80 MHz (short wires)']],h:'How fast the picture goes to the touch screen. 80 MHz draws faster but needs short wires; if pixels go wrong, set 40 MHz again. The touch screen’s Setup › Display › Frame rate meter (serial: perf on) shows the effect.'}]}],{title:'Display',auto:true}); }
  const sf=live.safety||{}, L=CFG.limits||{}, sl=[];
  if(L.estop) sl.push(sf.estop?'<span class="msg bad" style="margin:0">E-stop active</span>':`E-stop contact ${sf.contact||'–'}${CFG.safety&&CFG.safety.estop?'':' (input not watched)'}`);
  if(sf.chipC!=null) sl.push(`chip ${f1(sf.chipC)} °C${sf.chipHot?' <span class="msg bad" style="margin:0">hot</span>':''}`);
  if($('beepRow')) $('beepRow').hidden=!L.sound;
  if(live.fan) sl.push('fan '+(live.fan.on?'on':'off')+(live.fan.rpm!=null?' · '+live.fan.rpm+' rpm':''));
  if(live.relay!=null) sl.push('SSR supply relay '+(live.relay?'on':'off'));
  if(sf.muted) sl.push('alarm silenced');
  const slh=sl.join(' · ')||'No E-stop input on this board.';
  if($('safeLive')&&$('safeLive').innerHTML!==slh) $('safeLive').innerHTML=slh;
  fwLoad(); fwLive(); fwWatch(); if(!DEMO) fwNetCheck(true);
  const host=CFG.wifi.hostname;
  const kv=[['Firmware','TempCtl Duo '+live.fw],['Board',live.board+(live.variant?' · '+live.variant:'')],['Network',live.net],['Host name',`<a href="http://${esc(host)}.local/">${esc(host)}.local</a>`],['Uptime',upTxt(live.up)]]
    .map(([k,v])=>`<dt>${k}</dt><dd>${k==='Host name'?v:esc(v)}</dd>`).join('');
  if($('devInfo').innerHTML!==kv) $('devInfo').innerHTML=kv;
  if(!wDirty){ const w=CFG.wifi; $('wSsid').value=w.ssid; $('wHost').value=host; $('wStatic').checked=!!w.staticIp;
    $('wIp').value=w.ip||''; $('wMask').value=w.subnet||'255.255.255.0'; $('wGw').value=w.gateway||''; $('wDns1').value=w.dns1||''; $('wDns2').value=w.dns2||''; $('wIpBox').hidden=!w.staticIp; }
  $('wPassH').textContent=CFG.wifi.passSet?'Set — leave empty to keep it':'Not set';
  const wOn=CFG.wifi.enabled!==false; if(!$('wOn').dataset.busy) $('wOn').checked=wOn; $('wBody').hidden=!wOn;
  $('wHostH').textContent=`http://${$('wHost').value||host}.local`;
}
['wSsid','wPass','wAp','wHost','wIp','wMask','wGw','wDns1','wDns2','wStatic'].forEach(id=>$(id).addEventListener('input',()=>{ wDirty=true; if(id==='wHost') $('wHostH').textContent=`http://${$('wHost').value}.local`; }));
document.addEventListener('click',e=>{ const b=e.target.closest('button[data-eye]'); if(!b) return; const i=$(b.dataset.eye), show=i.type==='password';
  i.type=show?'text':'password'; b.setAttribute('aria-pressed',show); b.setAttribute('aria-label',show?'Hide password':'Show password'); b.title=b.getAttribute('aria-label'); i.focus(); });
// Wi-Fi on / off (wifi.enabled): off cuts this page's connection unless it runs over USB
$('wOn').onchange=async()=>{ const el=$('wOn'), on=el.checked;
  if(!on&&!SER.on&&!(await ask('Switch Wi-Fi off? The device leaves the network and stops its access point: this page loses its connection. Switch it on again on the touch screen (Setup › Network) or over USB (serial: wifi on).','Switch off'))){ el.checked=true; return; }
  el.dataset.busy=1; setMsg('wOnMsg',on?'Switching Wi-Fi on…':'Switching Wi-Fi off…');
  const r=await postCfg({wifi:{enabled:on}}); delete el.dataset.busy;
  if(r.err){ setMsg('wOnMsg',r.err,true); el.checked=!on; return; }
  CFG=r; $('wBody').hidden=!on; setMsg('wOnMsg',on?'Wi-Fi on: the device joins its network (or opens its access point).':'Wi-Fi off.'); };
$('wStatic').onchange=()=>{ const on=$('wStatic').checked; $('wIpBox').hidden=!on;
  if(on&&!$('wIp').value){ const l1=((live&&live.net)||'').split('\n')[0], m=/^AP/.test(l1)?null:/(\d+\.\d+\.\d+\.\d+)/.exec(l1); if(m){ $('wIp').value=m[1]; if(!$('wGw').value) $('wGw').value=m[1].replace(/\.\d+$/,'.1'); } } };
const ipOk=v=>/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.test(v)&&v.split('.').every(x=>+x<=255);
const ipN=v=>v.split('.').reduce((a,x)=>a*256+(+x),0);
$('bScan').onclick=async()=>{ const n=$('nets'); n.hidden=false; n.textContent='Scanning…'; $('bScan').disabled=true;
  try{ let r=await api('/api/wifiscan?start=1');
    for(let k=0;k<25&&r.busy;k++){ await new Promise(z=>setTimeout(z,1000)); r=await api('/api/wifiscan'); }
    if(r.busy){ n.textContent='The scan takes too long — try again.'; return; }
    n.innerHTML=(r.networks||[]).length?r.networks.map(x=>`<button type="button" data-net="${esc(x)}">${esc(x)}</button>`).join(''):esc(r.err||'No networks found.');
  }catch(e){ n.textContent='No answer during the scan (in access point mode the page may lose the connection for a moment) — try again.'; }
  finally{ $('bScan').disabled=false; } };
$('nets').onclick=e=>{ const b=e.target.closest('button[data-net]'); if(!b) return; $('wSsid').value=b.dataset.net; wDirty=true; $('nets').hidden=true; $('wPass').focus(); };
$('bWifi').onclick=async()=>{ if(!CFG) return; const o={ssid:$('wSsid').value.trim()}, p=$('wPass').value, ap=$('wAp').value, host=$('wHost').value.trim();
  if(p||!o.ssid) o.pass=p;
  if(ap){ if(ap.length<8){ setMsg('wMsg','The access point password needs 8 or more characters.',true); return; } o.apPass=ap; }
  if(!/^[A-Za-z0-9-]{1,23}$/.test(host)){ setMsg('wMsg','Host name: letters, digits and “-” only.',true); return; }
  const st=$('wStatic').checked, w={hostname:host,staticIp:st};
  if(st){ const f={ip:$('wIp').value.trim(),subnet:$('wMask').value.trim(),gateway:$('wGw').value.trim(),dns1:$('wDns1').value.trim(),dns2:$('wDns2').value.trim()};
    for(const [k,l] of [['ip','IP address'],['subnet','Subnet mask'],['gateway','Gateway']]) if(!ipOk(f[k])){ setMsg('wMsg',`${l}: four numbers 0–255 with dots, e.g. 192.168.1.50.`,true); return; }
    for(const [k,l] of [['dns1','DNS server'],['dns2','Second DNS server']]) if(f[k]&&!ipOk(f[k])){ setMsg('wMsg',`${l}: four numbers 0–255 with dots, or empty.`,true); return; }
    const m=ipN(f.subnet); if((ipN(f.ip)-ipN(f.ip)%(2**32-m))!==(ipN(f.gateway)-ipN(f.gateway)%(2**32-m))){ setMsg('wMsg','The gateway must be in the same network as the IP address (check the subnet mask).',true); return; }
    Object.assign(w,f); }
  const chg=Object.keys(w).filter(k=>w[k]!==CFG.wifi[k]), body=chg.length?{wifi:Object.fromEntries(chg.map(k=>[k,w[k]]))}:null;
  const where=st?`http://${w.ip}/`:`http://${host}.local/`;
  if(!await ask(o.ssid?`Join “${o.ssid}”${st?` with the fixed address ${w.ip}`:''} now? Afterwards open the page at ${where}${st?'':' or the new address shown on the touch screen'}.`:'Run as access point only?','Save and connect')) return;
  try{ if(body){ const r=await postCfg(body); if(r.err){ setMsg('wMsg',r.err,true); return; } CFG=r; }
    const r=await api('/api/wifi',o); setMsg('wMsg',r.err||(st?`Saved. Connecting… then open ${where}`:r.msg),!!r.err); if(!r.err){ wDirty=false; $('wPass').value=''; $('wAp').value=''; } }
  catch(e){ setMsg('wMsg','Saved; the device is changing its network connection.'); wDirty=false; } };
$('bScrRst').onclick=async()=>{ try{ const r=await api('/api/screen/restart',{}); setMsg('rsMsg',r.err||'Restarting the screen (display and touch controller) - the device keeps running.',!!r.err); }catch(e){ setMsg('rsMsg','No answer from the device.',true); } };
$('bRestart').onclick=async()=>{ if(!await ask('Restart the device now? Outputs are switched off during the restart.','Restart')) return;
  try{ const r=await api('/api/restart',{}); setMsg('rsMsg',r.err||'Restarting… the page reconnects by itself.',!!r.err); }catch(e){ setMsg('rsMsg','Restarting…'); } };

