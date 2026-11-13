/* ---------- USB transport (Web Serial, spec 7b) ----------
   Off the device (local launcher, GitHub Pages) the page talks JSON lines over the serial port: "api on",
   then {"id","m","p","b"} requests answered by the device's web API handlers ({"id","code","body"}, or
   text in {"id","chunk"} pieces + {"id","end":true}). The page's polling keeps the session alive (the
   device ends api mode after 15 s without a request); a PING every 5 s covers a hidden tab. */
const API_OK=[1];  // serial / HTTP API versions this page works with
let OFF=false;     // the page is not served by a device (set by start())
const DEMO=document.documentElement.hasAttribute('data-demo');  // the browser demo (/demo/: demo/demo.js runs the firmware's code here)
const SER={on:false,port:null,reader:null,writer:null,id:0,pend:new Map(),buf:'',wq:Promise.resolve(),lastTx:0,hello:null,user:false,was:false};
// in a tab of the local app (tools/launcher shell.html): the shell keeps the tabs, the page tells it
// what it shows; ?tab=<id> keeps each tab's remembered USB port apart
const FRAMED=window.parent!==window, TAB=new URLSearchParams(location.search).get('tab')||'';
const K=k=>TAB?k+'.'+TAB:k;
function toShell(m){ if(FRAMED) try{ parent.postMessage(Object.assign({tcd:1},m),'*'); }catch(e){} }
window.addEventListener('message',e=>{ if(e.source!==window.parent||!e.data||!e.data.tcd) return;
  if(e.data.close&&SER.on){ LS(K('tcd.auto'),null); serClose(true); } });
const LS=(k,v)=>{ try{ if(v===undefined) return localStorage.getItem(k); if(v===null) localStorage.removeItem(k); else localStorage.setItem(k,v); }catch(e){ return null; } };
function serWrite(t){ SER.lastTx=Date.now(); if(DEMO) return TCDemo.write(t); const enc=new TextEncoder().encode(t);
  return SER.wq=SER.wq.then(()=>SER.writer.write(enc)); }
function serReq(path,body,ms,text){
  return new Promise((res,rej)=>{
    const id=++SER.id, lim=Math.max(ms||0,15000);
    const p={chunks:[],t:0,arm(){ clearTimeout(p.t); p.t=setTimeout(()=>{ SER.pend.delete(id); rej(new Error('timeout')); },lim); },
      done(v){ clearTimeout(p.t); SER.pend.delete(id); res(v); }};
    p.arm(); SER.pend.set(id,p);
    serWrite(JSON.stringify(body===undefined?{id,p:path}:{id,m:'POST',p:path,b:body})+'\n').catch(e=>{ clearTimeout(p.t); SER.pend.delete(id); rej(e); });
  });
}
let reApi=0;
function serLine(l){
  if(l[0]!=='{'){  // the human console (banner, help): before "api on", or the device restarted (CYD)
    if(SER.on&&!SER.user&&l.trim()&&Date.now()-reApi>2000){ reApi=Date.now(); serWrite('\napi on\n').catch(()=>{}); }
    return; }
  let o; try{ o=JSON.parse(l); }catch(e){ return; }
  if(o.t==='hello'){ if(SER.hello) SER.hello(o); return; }
  if(o.t==='bye'){ if(SER.on&&!SER.user) serWrite('api on\n').catch(()=>{}); return; }  // timed out (tab asleep): start again
  if(o.t==='log'){ console.debug('[device]',o.m); return; }
  if(o.t==='error'){ console.warn('[device]',o.err); return; }
  const p=SER.pend.get(o.id); if(!p) return;
  if('body' in o) p.done(o.body);
  else if('chunk' in o){ p.chunks.push(o.chunk); p.arm(); }
  else if(o.end) p.done(p.chunks.join(''));
}
async function serReadLoop(){
  const dec=new TextDecoder();
  try{ for(;;){ const {value,done}=await SER.reader.read(); if(done) break;
    SER.buf+=dec.decode(value,{stream:true}); let i;
    while((i=SER.buf.indexOf('\n'))>=0){ const l=SER.buf.slice(0,i).replace(/\r$/,''); SER.buf=SER.buf.slice(i+1); serLine(l); } } }
  catch(e){}
  serLost();
}
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
// open the port, "api on", wait for the hello (a CYD may reset on open: up to ~8 s)
async function serConnect(port,quiet){
  const m=$('cMsg'); m.className='msg'; m.textContent='Connecting…';
  try{ await port.open({baudRate:115200,bufferSize:65536}); }
  catch(e){ m.className=quiet?'msg':'msg bad'; m.textContent=quiet?'':'The port cannot be opened - is a serial monitor (PlatformIO, Arduino) or another tab using it? Close it and try again.'; return false; }
  // DTR and RTS both on: no auto-reset (CYD), and the S3 sends only while DTR is on
  try{ await port.setSignals({dataTerminalReady:true,requestToSend:true}); }catch(e){}
  SER.port=port; SER.reader=port.readable.getReader(); SER.writer=port.writable.getWriter(); SER.buf=''; SER.wq=Promise.resolve(); SER.user=false;
  serReadLoop();
  let hello=null;
  for(let k=0;k<5&&!hello;k++){
    hello=await new Promise(r=>{ SER.hello=r; serWrite('\napi on\n').catch(()=>r(null)); setTimeout(()=>r(null),k?2000:1200); });
  }
  SER.hello=null;
  if(!hello){ m.className='msg bad'; m.textContent='No TempCtl Duo answers on this port (other device, or firmware older than 1.0.26).'; await serClose(true); return false; }
  if(!API_OK.includes(hello.api)){ m.className='msg bad';
    m.textContent=`The device (firmware ${hello.fw}) speaks API ${hello.api}; this page needs API ${API_OK.join(' / ')}. Use the page that matches the firmware.`;
    await serClose(true); return false; }
  const inf=port.getInfo?port.getInfo():{};
  LS(K('tcd.usb'),JSON.stringify({v:inf.usbVendorId,p:inf.usbProductId})); LS(K('tcd.auto'),'1');
  SER.host=hello.host||hello.name; toShell({usb:true,host:SER.host,state:'ok'});
  SER.on=true; SER.was=true; m.textContent='';
  $('conn').hidden=true; $('bSerOff').hidden=false;
  return true;
}
async function serClose(user){
  SER.user=user; const was=SER.on; SER.on=false;
  for(const p of SER.pend.values()) clearTimeout(p.t); SER.pend.clear();
  try{ if(was) await Promise.race([serWrite('api off\n'),sleep(500)]); }catch(e){}
  try{ await SER.reader.cancel(); }catch(e){} try{ SER.reader.releaseLock(); }catch(e){}
  try{ SER.writer.releaseLock(); }catch(e){} try{ await SER.port.close(); }catch(e){}
  SER.port=SER.reader=SER.writer=null;
}
function serLost(){
  if(!SER.port) return;  // closed on purpose
  const was=SER.on; serClose(false);
  if(was){ toShell({state:'lost'}); connScreen('Connection lost - the device keeps running. It reconnects by itself when the cable is back.',true); }
}
function connScreen(msg,bad){
  $('conn').hidden=false; $('bSerOff').hidden=true;
  $('linkDot').className='dot'; $('linkTxt').textContent='Not connected';
  const m=$('cMsg'); m.className='msg'+(bad?' bad':''); m.textContent=msg||'';
  if(!('serial' in navigator)){ $('cUsb').disabled=true; m.className='msg'; m.textContent=msg||'USB needs Chrome or Edge on a computer; Wi-Fi works in any browser.'; }
}
// previously granted ports (same USB ids as this tab's last one) that are plugged in
async function serKnownPorts(){
  if(!('serial' in navigator)) return [];
  let want=null; try{ want=JSON.parse(LS(K('tcd.usb'))||'null'); }catch(e){}
  const ports=await navigator.serial.getPorts();
  return ports.filter(p=>{ const i=p.getInfo?p.getInfo():{}; return !want||(i.usbVendorId===want.v&&i.usbProductId===want.p); });
}
function openNet(url,host){ LS('tcd.addr',host); if(FRAMED) toShell({open:url,host}); else location.href=url; }
async function offDevice(){  // the page did not come from a device: connect screen
  const app=document.documentElement.hasAttribute('data-launcher');  // served by the local launcher (tools/launcher)
  $('cVer').textContent=app?'Local app':'Web Serial page';
  if(app){ const alive=()=>fetch('/launcher/alive',{cache:'no-store'}).catch(()=>{}); alive(); setInterval(alive,5000);  // (it ends 30 s after the last window)
    // the launcher searches the network (mDNS) for devices: a list to click while the connect screen is shown
    const box=$('cDevs'); box.hidden=false; box.innerHTML='<span class="none">Searching the network…</span>';
    box.onclick=e=>{ const b=e.target.closest('button[data-u]'); if(b) openNet(b.dataset.u,b.dataset.h); };
    let seen='';
    const find=async()=>{ if($('conn').hidden) return;
      let r; try{ r=await (await fetch('/launcher/devices',{cache:'no-store'})).json(); }catch(e){ return; }
      const html=r.devices.length?r.devices.map(d=>`<button data-u="http://${esc(d.ip)}${d.port&&d.port!==80?':'+d.port:''}/" data-h="${esc(d.host)}"><b>${esc(d.host)}</b><small>${esc(d.ip)} · ${esc(d.board)}${d.variant?' '+esc(d.variant):''} · ${esc(d.fw)}</small></button>`).join('')
        :`<span class="none">${r.searching?'Searching the network…':'No device found on this network - enter its address below.'}</span>`;
      if(html!==seen){ seen=html; box.innerHTML=html; } };
    find(); setInterval(find,3000); }
  connDownloads();
  const a=LS('tcd.addr'); if(a) $('cAddr').value=a;
  $('cOpen').onclick=()=>{ const v=$('cAddr').value.trim().replace(/^https?:\/\//,'').replace(/\/.*$/,''); if(!v) return;
    openNet('http://'+v+'/',v); };
  $('cAddr').onkeydown=e=>{ if(e.key==='Enter') $('cOpen').click(); };
  $('cUsb').onclick=async()=>{ let port; try{ port=await navigator.serial.requestPort(); }catch(e){ return; } if(await serConnect(port)) poll(); };
  $('bSerOff').onclick=async()=>{ LS(K('tcd.auto'),null); await serClose(true); toShell({usb:false}); connScreen('Disconnected. The device keeps running.'); };
  connScreen('');
  if('serial' in navigator){
    // plugged in again (also after a device restart): reload, which connects again and reads everything fresh
    navigator.serial.addEventListener('connect',()=>{ if(!SER.on&&LS(K('tcd.auto'))) location.reload(); });
    setInterval(()=>{ if(SER.on&&Date.now()-SER.lastTx>4500) serWrite('{"id":0,"m":"PING"}\n').catch(()=>{}); },1000);
    if(LS(K('tcd.auto'))){ for(const p of await serKnownPorts()) if(await serConnect(p,true)) return poll(); }
  }
}
// the hosted page / local app: the newest desktop app and firmware files (spec 9.13)
const REL_BOARDS={'s3':'ESP32-S3 · 4.0″ screen','cyd-1xlocal-1x485':'CYD · 1 local + RS485','cyd-2xlocal':'CYD · 2 local','cyd-2xlocal-2x485':'CYD · 2 local + RS485'};
const REL_OS={windows:'Windows',macos:'macOS',linux:'Linux'};
async function connDownloads(){
  let m; try{ m=await relManifest(); }catch(e){ return; }
  const kb=n=>n>=1048576?(n/1048576).toFixed(1)+' MB':Math.round(n/1024)+' kB', u=x=>esc(REL_BASE+x);
  const d=relDate(m.date), mine=document.documentElement.getAttribute('data-version');
  $('cDlV').textContent=`Release ${m.version}${d?' · '+d:''}`+(mine&&verCmp(m.version,mine)>0?` — newer than this app (${mine}).`:'.');
  $('cDlApp').innerHTML=(m.app||[]).map(a=>`<a class="btn" href="${u(a.url)}" download>${esc(REL_OS[a.os]||a.os)}<small>${a.os==='macos'?(a.arch==='arm64'?'Apple silicon · ':'Intel · '):''}${kb(a.size)}</small></a>`).join('');
  $('cDlFw').innerHTML=Object.entries(m.firmware||{}).map(([b,f])=>`<span>${esc(REL_BOARDS[b]||b)}</span><a href="${u(f.url)}" download title="${kb(f.size)}">Update</a>`+
    (f.full?`<a href="${u(f.full.url)}" download title="${kb(f.full.size)}">Full image</a>`:'<span></span>')).join('');
  $('cDl').hidden=false; }
// CSV links over USB: fetched as text, saved as a file
const CSV_NAME={'/api/log.csv':'tempctl-duo-run.csv','/api/history.csv':'tempctl-duo-history.csv'};
document.addEventListener('click',async e=>{ const a=e.target.closest('a[href^="/api/"]'); if(!a||!SER.on) return; e.preventDefault();
  const h=a.getAttribute('href'), name=CSV_NAME[h.split('?')[0]]||'tempctl-duo.txt';
  try{ const t=await serReq(h,undefined,30000); const u=URL.createObjectURL(new Blob([t],{type:'text/csv'}));
    const d=document.createElement('a'); d.href=u; d.download=name; document.body.appendChild(d); d.click(); d.remove(); setTimeout(()=>URL.revokeObjectURL(u),2000); }
  catch(x){ note('Download failed: no answer over USB.',true); } });
function ask(text,ok){ return new Promise(res=>{ const d=$('askDlg'); $('askTxt').textContent=text; $('askOk').textContent=ok||'OK';
  $('askOk').onclick=()=>{d.close();res(true);}; $('askNo').onclick=()=>{d.close();res(false);}; d.onclose=()=>res(false); d.showModal(); }); }
function note(t,bad){ const m=$('msg'); m.textContent=t||''; m.className='msg'+(bad?' bad':''); }

