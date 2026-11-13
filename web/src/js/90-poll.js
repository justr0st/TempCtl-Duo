/* ---------- polling ---------- */
let polling=false, pollTimer=0, clockSent=false, tzSent=false;
// the browser's time zone (for the device's local time in the SD run logs): its name, and its current offset
// as "UTC+02:00" for a zone the device's list does not have
function browserTz(){ let zone=''; try{ zone=Intl.DateTimeFormat().resolvedOptions().timeZone||''; }catch(e){}
  const m=-new Date().getTimezoneOffset(), a=Math.abs(m);
  return {zone,fixed:`UTC${m<0?'-':'+'}${String(Math.floor(a/60)).padStart(2,'0')}:${String(a%60).padStart(2,'0')}`}; }
async function poll(){
  if(polling) return; polling=true; clearTimeout(pollTimer);
  try{
    live=await api('/api/live'+rawLiveQuery()); rawFromLive(live); monLegend();
    $('linkDot').className='dot ok'; setTxt($('linkTxt'),DEMO?'Demo · simulated device':SER.on?'Connected over USB':'Device connected');
    toShell({state:'ok'});
    if(live.time===0&&!clockSent){ clockSent=true; api('/api/time',{t:Math.floor(Date.now()/1000)}).catch(()=>{}); }  // (for the log times)
    if(live.tz===''&&!tzSent){ tzSent=true; api('/api/time',browserTz()).catch(()=>{}); }  // time zone: once, while none is set
    setTxt($('net'),live.net); setTxt($('fw'),`v${live.fw} · ${live.board}${live.variant?' '+live.variant:''}`);
    applyPalette();
    if(!progs||progsGen!==live.progGen) await loadPrograms(); else fillProgSelect();
    await loadSaved(live.sel); if(running()) await ensureRunProg(true);
    updateRun(); updateChans(); updateEditor(); updSide();
    const active=live.run.state==='running'||live.run.state==='paused', now=Date.now();
    if(page==='status'&&(!M||now-chartAt>(active||view==='live'?2000:5000))){ chartAt=now; await refreshChart(); }
    if(page==='status'){ chartHead(); await rawUpdate(); }
    await updateLog();
    await pageUpdate();
  }catch(e){ $('linkDot').className='dot'; $('linkTxt').textContent='Device not reachable'; toShell({state:'lost'}); }
  finally{ polling=false; if(OFF&&!SER.on) return;
    if(document.hidden){ pollTimer=0; return; }  // a hidden tab does not poll (visibilitychange starts it again; over USB the PING keeps the session)
    pollTimer=setTimeout(poll,live&&live.pollMs?Math.max(250,Math.min(1000,live.pollMs/devRate())):1000); }  // (demo at 10x: more often)
}
document.addEventListener('visibilitychange',()=>{ if(!document.hidden&&!polling&&!pollTimer&&(!OFF||SER.on)) poll(); });
let clkMin=-1;  // the sidebar clock: written when the minute changes, not on every frame
function frame(){ if(lastChart===0&&!drag&&page==='status'){ renderChart(); lastChart=1; }
  const d=new Date(), m=d.getHours()*60+d.getMinutes(); if(m!==clkMin){ clkMin=m; $('clk').textContent=d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'}); }
  requestAnimationFrame(frame); }
// The demo: the simulated device answers like one on a USB cable (demo/demo.js)
async function demoStart(){
  try{ await TCDemo.start(serLine); }
  catch(e){ connScreen('The demo could not start in this browser: '+(e&&e.message||e),true); $('cVer').textContent='Demo'; return; }
  SER.on=true; SER.host='tempctl-duo-demo'; $('conn').hidden=true; poll(); }
// Served by a device: its API answers here. Otherwise (launcher on localhost, GitHub Pages): the
// connect screen (USB / Wi-Fi).
async function start(){
  OFF=!document.documentElement.hasAttribute('data-device');  // set in the device's copy (tools/web_embed.py)
  if(DEMO) return demoStart();
  if(OFF) return offDevice();
  if(FRAMED){ $('bNetOff').hidden=false; $('bNetOff').onclick=()=>toShell({disconnect:true}); }  // a device's page in a tab of the local app
  poll();
}
applyPalette(true); route(); renderChart(); start(); requestAnimationFrame(frame);
