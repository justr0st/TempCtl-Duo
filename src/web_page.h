// Single-page web UI served from flash. Self-contained (works in AP mode
// without internet access).
#pragma once
#include <Arduino.h>

static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Temperature program</title>
<style>
:root{
  --bg:#E6EAEE;--panel:#FAFBFC;--ink:#17202B;--muted:#5A6675;--rule:#C9D1DA;
  --heat:#D9480F;--set:#1B64A8;--plan:#7FA7CF;--ok:#2B8A57;--warn:#B7791F;--bad:#C53030;
  --num:"Bahnschrift","DIN Alternate","DIN 2014","Roboto Condensed","Arial Narrow",sans-serif;
  --txt:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
}
@media (prefers-color-scheme:dark){:root{
  --bg:#12171D;--panel:#1A2129;--ink:#E4E9EE;--muted:#93A0AE;--rule:#2C3642;
  --heat:#FF7A3D;--set:#5EA3E6;--plan:#3F6B96;--ok:#4CC38A;--warn:#E2B04A;--bad:#F07070;}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 var(--txt)}
header{display:flex;align-items:center;gap:12px;padding:12px 18px;flex-wrap:wrap}
header h1{font:600 20px/1 var(--num);margin:0;letter-spacing:.01em}
#link{width:10px;height:10px;border-radius:50%;background:var(--muted)}
#net{margin-left:auto;color:var(--muted);font-size:13px;white-space:pre-line;text-align:right}
nav{display:flex;gap:2px;padding:0 12px;border-bottom:1px solid var(--rule);overflow-x:auto}
nav button{background:none;border:0;border-bottom:3px solid transparent;padding:10px 12px;color:var(--muted);
  font:500 15px var(--txt);cursor:pointer;white-space:nowrap}
nav button[aria-selected=true]{color:var(--ink);border-bottom-color:var(--set)}
main{max-width:980px;margin:0 auto;padding:16px}
section{display:none}section.on{display:block}
.panel{background:var(--panel);border:1px solid var(--rule);border-radius:6px;padding:16px;margin-bottom:16px}
h2{font:600 17px var(--num);margin:0 0 10px}
p.note{color:var(--muted);margin:4px 0 12px;max-width:70ch}
.readout{display:grid;grid-template-columns:auto 1fr;gap:4px 24px;align-items:end}
.pv{font:600 clamp(56px,11vw,96px)/.9 var(--num);color:var(--heat);font-variant-numeric:tabular-nums}
.sp{font:500 clamp(28px,5vw,40px)/1 var(--num);color:var(--set);font-variant-numeric:tabular-nums}
.cap{color:var(--muted);font-size:13px}
.facts{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px 18px;margin-top:14px}
.facts b{display:block;font:500 20px var(--num);font-variant-numeric:tabular-nums}
.strip{position:relative;display:flex;height:46px;margin:18px 0 6px;border-radius:4px;overflow:hidden;
  border:1px solid var(--rule)}
.strip div{position:relative;border-right:1px solid var(--rule);font:500 12px/1.2 var(--num);padding:5px 6px;
  overflow:hidden;white-space:nowrap}
.strip .ramp{background:linear-gradient(90deg,transparent,color-mix(in srgb,var(--set) 18%,transparent))}
.strip .hold{background:color-mix(in srgb,var(--set) 18%,transparent)}
.strip .cur{outline:2px solid var(--set);outline-offset:-2px}
#marker{position:absolute;top:0;bottom:0;width:3px;background:var(--heat);left:0;display:none}
canvas{width:100%;height:280px;display:block}
.legend{display:flex;gap:18px;color:var(--muted);font-size:13px;margin-top:6px;flex-wrap:wrap}
.legend i{display:inline-block;width:18px;height:3px;vertical-align:middle;margin-right:6px}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
button.b,a.b{font:500 15px var(--txt);padding:9px 16px;border-radius:5px;border:1px solid var(--rule);
  background:var(--panel);color:var(--ink);cursor:pointer}
button.b.primary{background:var(--set);border-color:var(--set);color:#fff}
a.b{text-decoration:none;display:inline-block}
button.b.danger{color:var(--bad);border-color:var(--bad)}
button.b:disabled{opacity:.45;cursor:default}
button:focus-visible,input:focus-visible,select:focus-visible{outline:3px solid var(--set);outline-offset:2px}
#msg{margin-top:10px;min-height:1.4em;color:var(--muted)}
#msg.bad{color:var(--bad)}
table{border-collapse:collapse;width:100%}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--rule)}
th{font-weight:500;color:var(--muted);font-size:13px}
td.n{font-variant-numeric:tabular-nums}
input,select{font:15px var(--txt);padding:7px 8px;border:1px solid var(--rule);border-radius:4px;
  background:var(--bg);color:var(--ink);width:100%;max-width:140px}
input[type=checkbox]{width:20px;height:20px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:12px 18px}
.grid label{display:flex;flex-direction:column;gap:4px;font-size:13px;color:var(--muted)}
.grid label input,.grid label select{max-width:none}
.warn{border-left:4px solid var(--warn);padding:8px 12px;background:color-mix(in srgb,var(--warn) 12%,transparent);
  margin-bottom:14px;border-radius:0 4px 4px 0}
.hide{display:none}
code{font-size:13px}
@media (max-width:560px){.readout{grid-template-columns:1fr}main{padding:10px}.panel{padding:12px}}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
</style>
</head>
<body>
<header>
  <span id="link" title="Controller link"></span>
  <h1>Temperature program</h1>
  <span id="net"></span>
</header>
<nav role="tablist">
  <button role="tab" data-v="run" aria-selected="true">Run</button>
  <button role="tab" data-v="prog">Program</button>
  <button role="tab" data-v="ctrl">Controller</button>
  <button role="tab" data-v="conn">Connection</button>
  <button role="tab" data-v="tools">Tools</button>
  <button role="tab" data-v="wifi">Wi-Fi</button>
</nav>
<main>

<section id="v-run" class="on">
  <div id="spWarn" class="warn hide">The setpoint register address is not set, so programs cannot run yet.
    Open <a href="#" data-go="conn">Connection</a> or use <a href="#" data-go="tools">Tools</a> to find it.</div>
  <div class="panel">
    <div class="readout">
      <div><div class="cap">Process value</div><div class="pv" id="pv">---.-</div></div>
      <div><div class="cap">Setpoint in controller</div><div class="sp" id="sp">---.-</div></div>
    </div>
    <div class="strip" id="strip"><span id="marker"></span></div>
    <div class="facts">
      <div><span class="cap">State</span><b id="state">-</b></div>
      <div><span class="cap">Step</span><b id="step">-</b></div>
      <div><span class="cap">Phase left</span><b id="phLeft">-</b></div>
      <div><span class="cap">Total left</span><b id="totLeft">-</b></div>
      <div><span class="cap">Output</span><b id="out">-</b></div>
      <div><span class="cap">Controller mode</span><b id="mode">-</b></div>
    </div>
  </div>
  <div class="panel">
    <canvas id="chart"></canvas>
    <div class="legend"><span><i style="background:var(--plan)"></i>Planned</span>
      <span><i style="background:var(--set)"></i>Setpoint</span>
      <span><i style="background:var(--heat)"></i>Process value</span>
      <span id="chartSpan"></span></div>
  </div>
  <div class="row">
    <button class="b primary" id="bStart">Start program</button>
    <button class="b" id="bPause">Pause</button>
    <button class="b" id="bSkip">Skip to next phase</button>
    <button class="b danger" id="bStop">Stop program</button>
    <a class="b" href="/api/log.csv">Download log (CSV)</a>
  </div>
  <div id="msg"></div>
</section>

<section id="v-prog">
  <div class="panel">
    <h2>Program steps</h2>
    <p class="note">Each step ramps from the previous temperature to its target, then holds. The first ramp starts
      at the temperature measured when you press Start. Times are in minutes; a ramp of 0 jumps directly.</p>
    <table><thead><tr><th>Step</th><th>On</th><th>Target</th><th>Ramp (min)</th><th>Hold (min)</th></tr></thead>
      <tbody id="progRows"></tbody></table>
    <p class="note" id="progTotal"></p>
    <canvas id="progChart" style="height:200px"></canvas>
    <div class="row" style="margin-top:12px"><button class="b primary" id="bSaveProg">Save program</button>
      <span id="progMsg" class="cap"></span></div>
  </div>
</section>

<section id="v-ctrl">
  <div class="panel">
    <h2>Controller parameters</h2>
    <p class="note" id="ctrlNote">Reads the PID and alarm parameters of the PMA KS 4x-1. Values are written one at a
      time; the controller rejects values outside its limits.</p>
    <div class="row"><button class="b" id="bReadParams">Read from controller</button></div>
    <table style="margin-top:12px"><thead><tr><th>Name</th><th>Meaning</th><th>Value</th><th></th></tr></thead>
      <tbody id="paramRows"></tbody></table>
  </div>
  <div class="panel">
    <h2>Actions</h2>
    <div class="row" id="actions"></div>
    <div id="actMsg" class="cap" style="margin-top:8px"></div>
  </div>
</section>

<section id="v-conn">
  <div class="panel">
    <h2>Serial link</h2>
    <div class="grid">
      <label>Controller type<select id="profile"><option value="0">PMA KS 40-1 / 41-1 / 42-1</option>
        <option value="1">Generic Modbus RTU (e.g. LCTC TE-M226)</option></select></label>
      <label>Slave address<input id="slaveId" type="number" min="1" max="247"></label>
      <label>Baud rate<select id="baud"><option>2400</option><option>4800</option><option>9600</option>
        <option>19200</option><option>38400</option></select></label>
      <label>Parity<select id="parity"><option value="0">None</option><option value="1">Even</option>
        <option value="2">Odd</option></select></label>
      <label>Stop bits (no parity only)<select id="stopBits"><option>1</option><option>2</option></select></label>
      <label>Response timeout (ms)<input id="timeoutMs" type="number"></label>
      <label>Poll interval (ms)<input id="pollMs" type="number"></label>
    </div>
    <div class="row" style="margin-top:12px"><button class="b" id="bDefaults">Load register defaults for this type</button></div>
  </div>
  <div class="panel">
    <h2>Register addresses</h2>
    <p class="note">Decimal protocol addresses (0-based, as sent on the wire). Use -1 for “not available”.
      PMA KS 4x-1 defaults use the “1dP” column of the interface description (value = register / 10),
      e.g. SP = 11372, r.SP = 11295.</p>
    <div class="grid">
      <label>Process value (read)<input id="r_pv" type="number"></label>
      <label>Effective setpoint (read)<input id="r_spEff" type="number"></label>
      <label>Output % (read)<input id="r_out" type="number"></label>
      <label>Status word (read)<input id="r_status" type="number"></label>
      <label>Alarm word (read)<input id="r_alarm" type="number"></label>
      <label>Setpoint SP (write, required)<input id="r_sp" type="number"></label>
      <label>Setpoint lower limit (read)<input id="r_spLo" type="number"></label>
      <label>Setpoint upper limit (read)<input id="r_spHi" type="number"></label>
      <label>Setpoint gradient r.SP (write)<input id="r_rampRate" type="number"></label>
      <label>Outputs off (write 1)<input id="r_ctrlOff" type="number"></label>
      <label>Front panel lock (write 1)<input id="r_remote" type="number"></label>
      <label>Divide values by<input id="r_scale" type="number" step="any"></label>
      <label>Read function<select id="r_readFn"><option>3</option><option>4</option></select></label>
      <label>Write function<select id="r_writeFn"><option>6</option><option>16</option></select></label>
      <label>“Off” raw value<input id="r_offRaw" type="number"></label>
    </div>
  </div>
  <div class="panel">
    <h2>Program behaviour</h2>
    <div class="grid">
      <label>Setpoint ramp<select id="rampMode"><option value="0">ESP32 writes setpoint steps</option>
        <option value="1">Controller ramps (r.SP), fewer writes</option></select></label>
      <label>Minimum step between writes<input id="minStep" type="number" step="0.1"></label>
      <label>Minimum seconds between writes<input id="minWriteSec" type="number"></label>
      <label>Hold only counts inside band<select id="guaranteedSoak"><option value="false">No</option>
        <option value="true">Yes (guaranteed soak)</option></select></label>
      <label>Soak band ±<input id="soakBand" type="number" step="0.1"></label>
      <label>When the program ends or stops<select id="endAction"><option value="0">Keep current setpoint</option>
        <option value="1">Set end setpoint</option><option value="2">Switch controller outputs off</option></select></label>
      <label>End setpoint<input id="endSp" type="number" step="0.1"></label>
      <label>Lock front panel while running<select id="lockFront"><option value="false">No</option>
        <option value="true">Yes</option></select></label>
      <label>Pause after link loss (s, 0 = never)<input id="commLossSec" type="number"></label>
      <label>Lowest allowed setpoint<input id="spMin" type="number" step="0.1"></label>
      <label>Highest allowed setpoint<input id="spMax" type="number" step="0.1"></label>
      <label>Display brightness (10-255)<input id="brightness" type="number"></label>
    </div>
    <div class="row" style="margin-top:12px"><button class="b primary" id="bSaveConn">Save connection settings</button>
      <span id="connMsg" class="cap"></span></div>
  </div>
</section>

<section id="v-tools">
  <div class="panel">
    <h2>Find the setpoint address</h2>
    <p class="note">Not needed for the PMA KS 4x-1 (preset to 11372). For other controllers: reads every register in the range and lists those whose value equals the controller's current
      effective setpoint. Make sure no ramp is active and the setpoint is an unusual value (e.g. 123.4) so the match
      is unique. Then change the setpoint on the front panel and read the candidate again to confirm.</p>
    <div class="row"><label class="cap">From <input id="scanFrom" type="number"></label>
      <label class="cap">To <input id="scanTo" type="number"></label>
      <button class="b" id="bScan">Scan</button></div>
    <div id="scanOut" style="margin-top:10px"></div>
  </div>
  <div class="panel">
    <h2>Read registers</h2>
    <div class="row"><label class="cap">Address <input id="rdAddr" type="number"></label>
      <label class="cap">Count <input id="rdN" type="number" value="1" min="1" max="60"></label>
      <label class="cap">Function <select id="rdFn"><option value="0">default</option><option>3</option>
        <option>4</option></select></label>
      <button class="b" id="bRead">Read</button></div>
    <div id="rdOut" style="margin-top:10px"></div>
  </div>
  <div class="panel">
    <h2>Write one register</h2>
    <p class="note">Writes the raw 16-bit value. Wrong addresses can change controller configuration.</p>
    <div class="row"><label class="cap">Address <input id="wrAddr" type="number"></label>
      <label class="cap">Raw value <input id="wrVal" type="number"></label>
      <button class="b danger" id="bWrite">Write</button></div>
    <div id="wrOut" class="cap" style="margin-top:10px"></div>
  </div>
</section>

<section id="v-wifi">
  <div class="panel">
    <h2>Wi-Fi</h2>
    <p class="note">Leave the network name empty to run as an access point only. The device restarts after saving.
      If it cannot join the network within 20 s it also opens its own access point.</p>
    <div class="grid">
      <label>Network name<input id="ssid" maxlength="32"></label>
      <label>Password<input id="pass" type="password" maxlength="64"></label>
      <label>Access point password (8+ characters)<input id="apPass" type="password" maxlength="32"></label>
      <label>Host name (http://name.local)<input id="hostname" maxlength="23"></label>
    </div>
    <div class="row" style="margin-top:12px"><button class="b primary" id="bSaveWifi">Save and restart</button>
      <span id="wifiMsg" class="cap"></span></div>
  </div>
</section>
</main>

<script>
const $=id=>document.getElementById(id);
const api=async(p,body)=>{const o=body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)};
  const r=await fetch(p,o);return r.json();};
const f1=v=>v==null?'---.-':Number(v).toFixed(1);
const dur=s=>{s=Math.max(0,Math.round(s||0));const h=Math.floor(s/3600),m=Math.floor(s/60)%60,x=s%60;
  return h?`${h}:${String(m).padStart(2,'0')}:${String(x).padStart(2,'0')}`:`${m}:${String(x).padStart(2,'0')}`;};
const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
let settings=null,program=null,status=null,view='run';

// ---------- navigation
function show(v){view=v;document.querySelectorAll('nav button').forEach(b=>b.setAttribute('aria-selected',b.dataset.v===v));
  document.querySelectorAll('section').forEach(s=>s.classList.toggle('on',s.id==='v-'+v));
  if(v==='prog')loadProgram();if(v==='conn'||v==='tools'||v==='wifi')loadSettings();if(v==='ctrl')loadParamsInfo();}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>show(b.dataset.v));
document.querySelectorAll('[data-go]').forEach(a=>a.onclick=e=>{e.preventDefault();show(a.dataset.go);});

// ---------- chart
function drawChart(cv,d){
  const dpr=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.clientHeight;cv.width=W*dpr;cv.height=H*dpr;
  const g=cv.getContext('2d');g.scale(dpr,dpr);g.clearRect(0,0,W,H);
  const n=d.plan.length,all=[...d.plan,...d.sp,...d.pv].filter(v=>v!=null);
  let lo=all.length?Math.min(...all):0,hi=all.length?Math.max(...all):100;if(hi-lo<10){lo-=5;hi+=5;}
  const raw=(hi-lo)/5,mag=10**Math.floor(Math.log10(raw)),st=[1,2,2.5,5,10].map(m=>m*mag).find(v=>v>=raw);
  lo=Math.floor(lo/st)*st;hi=Math.ceil(hi/st)*st;
  const L=48,R=12,T=10,B=26,x=i=>L+(W-L-R)*i/(n-1),y=v=>T+(H-T-B)*(1-(v-lo)/(hi-lo));
  g.font='12px '+css('--txt');g.fillStyle=css('--muted');g.strokeStyle=css('--rule');g.lineWidth=1;
  for(let v=lo;v<=hi+st/2;v+=st){const yy=Math.round(y(v))+.5;g.beginPath();g.moveTo(L,yy);g.lineTo(W-R,yy);g.stroke();
    g.textAlign='right';g.fillText(+v.toFixed(1),L-6,yy+4);}
  const span=(n-1)*d.binSec,maxLbl=Math.max(2,Math.floor((W-L-R)/80));
  const ts=[60,300,600,900,1800,3600,7200,10800,14400,21600,43200,86400].find(v=>span/v<=maxLbl)||86400;
  const lab=s=>{const a=Math.abs(s),h=Math.floor(a/3600),m=Math.round(a%3600/60);
    return(s<0?'-':'')+(h?`${h}:${String(m).padStart(2,'0')} h`:`${m} min`);};
  for(let t=0;t<=span+1;t+=ts){const i=t/d.binSec,xx=x(d.mode==='live'?(n-1)-i:i);
    g.textAlign=xx>W-R-30?'right':xx<L+30?'left':'center';g.fillText(d.mode==='live'?lab(-t):lab(t),xx,H-8);}
  const line=(arr,col,w,dash)=>{g.strokeStyle=col;g.lineWidth=w;g.setLineDash(dash||[]);g.beginPath();let pen=false;
    arr.forEach((v,i)=>{if(v==null){pen=false;return;}pen?g.lineTo(x(i),y(v)):g.moveTo(x(i),y(v));pen=true;});g.stroke();};
  line(d.plan,css('--plan'),2,[6,4]);line(d.sp,css('--set'),1.5);line(d.pv,css('--heat'),2.5);g.setLineDash([]);
}

function planOf(steps,start,N){const tot=steps.reduce((a,s)=>a+(s.on?(s.r+s.h)*60:0),0);const bin=Math.max(1,tot/N);
  const at=t=>{let prev=start;for(const s of steps){if(!s.on)continue;const r=s.r*60,h=s.h*60;
    if(t<r)return prev+(s.t-prev)*t/r;t-=r;if(t<h)return s.t;t-=h;prev=s.t;}return prev;};
  return{plan:Array.from({length:N},(_,i)=>at((i+.5)*bin)),binSec:bin,total:tot};}

// ---------- run view
function renderStrip(steps,st){
  const el=$('strip');el.querySelectorAll('div').forEach(d=>d.remove());
  const tot=steps.reduce((a,s)=>a+(s.on?s.r+s.h:0),0)||1;
  steps.forEach((s,i)=>{if(!s.on)return;[['ramp',s.r],['hold',s.h]].forEach(([k,m])=>{if(m<=0)return;
    const d=document.createElement('div');d.className=k;d.style.width=(100*m/tot)+'%';
    d.textContent=k==='ramp'?`${i+1} → ${s.t}`:`${i+1} hold`;d.title=`Step ${i+1}: ${k} ${m} min, target ${s.t}`;
    if(st&&st.step===i&&st.phase===k&&(st.state==='running'||st.state==='paused'))d.classList.add('cur');
    el.insertBefore(d,$('marker'));});});
}

async function pollStatus(){
  try{status=await api('/api/status');}catch(e){$('link').style.background=css('--bad');return;}
  const s=status,r=s.run,active=r.state==='running'||r.state==='paused';
  $('link').style.background=s.comm.ok?css('--ok'):css('--bad');
  $('net').textContent=s.net;
  $('pv').textContent=s.pv!=null?f1(s.pv):(s.comm.ok&&s.pvFlag!=='ok'?s.pvFlag:'---.-');
  $('sp').textContent=f1(s.spEff);
  $('state').textContent=r.state[0].toUpperCase()+r.state.slice(1)+(r.soakWait?' (waiting for band)':'');
  $('step').textContent=active?`${r.step+1} of 6, ${r.phase} to ${f1(r.target)}`:'-';
  $('phLeft').textContent=active?dur(r.phaseDur-r.phaseEl):'-';
  $('totLeft').textContent=active?dur(r.total-r.elapsed):dur(s.plannedTotal)+' planned';
  $('out').textContent=s.out!=null?Math.round(s.out)+' %':'-';
  $('mode').textContent=s.mode||'-';
  $('spWarn').classList.toggle('hide',s.spConfigured);
  $('bStart').textContent=r.state==='paused'?'Resume program':'Start program';
  $('bStart').disabled=r.state==='running';$('bPause').disabled=r.state!=='running';
  $('bSkip').disabled=!active;$('bStop').disabled=!active;
  const m=$('marker');m.style.display=active?'block':'none';if(r.total>0)m.style.left=(100*r.elapsed/r.total)+'%';
  const err=!s.comm.ok||(active&&!r.spSynced);$('msg').className=err?'bad':'';
  $('msg').textContent=err?s.comm.err:(r.msg+(active?` (setpoint writes this run: ${r.spWrites})`:''));
  if(program)renderStrip(program.steps,r);
}

async function pollHistory(){
  if(view!=='run')return;
  try{const h=await api('/api/history');drawChart($('chart'),h);
    $('chartSpan').textContent=h.mode==='live'?`Last ${dur(h.binSec*h.plan.length)}`:`Program run, ${dur(h.total)}`;}catch(e){}
}
const cmd=c=>api('/api/cmd',{cmd:c}).then(pollStatus);
$('bStart').onclick=()=>cmd(status&&status.run.state==='paused'?'resume':'start');
$('bPause').onclick=()=>cmd('pause');
$('bSkip').onclick=()=>confirm('Skip to the next phase?')&&cmd('skip');
$('bStop').onclick=()=>confirm('Stop the program? The end action from Connection settings is applied.')&&cmd('stop');

// ---------- program
async function loadProgram(){
  program=await api('/api/program');const tb=$('progRows');tb.innerHTML='';
  program.steps.forEach((s,i)=>{const tr=document.createElement('tr');
    tr.innerHTML=`<td class="n">${i+1}</td><td><input type="checkbox" ${s.on?'checked':''} aria-label="Step ${i+1} on"></td>
      <td><input type="number" step="0.1" min="${program.spMin}" max="${program.spMax}" value="${s.t}" aria-label="Step ${i+1} target"></td>
      <td><input type="number" step="0.1" min="0" value="${s.r}" aria-label="Step ${i+1} ramp minutes"></td>
      <td><input type="number" step="0.1" min="0" value="${s.h}" aria-label="Step ${i+1} hold minutes"></td>`;
    tr.querySelectorAll('input').forEach(inp=>inp.oninput=previewProgram);tb.appendChild(tr);});
  previewProgram();
}
function readProgram(){return[...$('progRows').children].map(tr=>{const q=tr.querySelectorAll('input');
  return{on:q[0].checked,t:+q[1].value,r:+q[2].value,h:+q[3].value};});}
function previewProgram(){const steps=readProgram();const start=status&&status.pv!=null?status.pv:20;
  const p=planOf(steps,start,240);$('progTotal').textContent=`Total ${dur(p.total)}. Preview starts at ${f1(start)}.`;
  drawChart($('progChart'),{plan:p.plan,sp:[],pv:[],binSec:p.binSec,mode:'run'});}
$('bSaveProg').onclick=async()=>{program=await api('/api/program',{steps:readProgram()});
  $('progMsg').textContent='Program saved. A running program keeps its old steps until the next start.';loadProgram();};

// ---------- settings
const regKeys=['pv','spEff','out','status','alarm','sp','spLo','spHi','rampRate','ctrlOff','remote','scale','readFn','writeFn','offRaw'];
const topKeys=['profile','slaveId','baud','parity','stopBits','timeoutMs','pollMs','rampMode','minStep','minWriteSec',
  'guaranteedSoak','soakBand','endAction','endSp','lockFront','commLossSec','spMin','spMax','brightness'];
async function loadSettings(){settings=await api('/api/settings');
  topKeys.forEach(k=>$(k).value=String(settings[k]));regKeys.forEach(k=>$('r_'+k).value=settings.reg[k]);
  $('scanFrom').value=settings.scanFrom;$('scanTo').value=settings.scanTo;
  $('ssid').value=settings.ssid;$('hostname').value=settings.hostname;}
function collectSettings(){const o={reg:{}};topKeys.forEach(k=>{const v=$(k).value;o[k]=v==='true'?true:v==='false'?false:+v;});
  regKeys.forEach(k=>o.reg[k]=+$('r_'+k).value);
  if(o.profile!==settings.profile)delete o.reg; // new type: start from its defaults
  return o;}
$('bSaveConn').onclick=async()=>{await api('/api/settings',collectSettings());$('connMsg').textContent='Saved. The serial link was reopened.';loadSettings();};
$('bDefaults').onclick=async()=>{if(!confirm('Replace all register addresses with the defaults for this controller type?'))return;
  await api('/api/settings',{profile:+$('profile').value,loadDefaults:true});loadSettings();$('connMsg').textContent='Defaults loaded.';};

// ---------- tools
$('bScan').onclick=async()=>{const o=$('scanOut');o.textContent='Scanning, this takes a few seconds...';
  await api('/api/settings',{scanFrom:+$('scanFrom').value,scanTo:+$('scanTo').value});
  const r=await api('/api/mb/findsp',{from:+$('scanFrom').value,to:+$('scanTo').value});
  if(!r.ok){o.textContent=r.err;return;}
  if(!r.candidates.length){o.textContent=`No register in this range holds ${f1(r.reference)}. Try another range.`;return;}
  o.innerHTML=`Registers holding ${f1(r.reference)}: `+r.candidates.map(a=>`<button class="b" data-a="${a}">Use ${a} as setpoint</button>`).join(' ');
  o.querySelectorAll('button').forEach(b=>b.onclick=async()=>{await api('/api/settings',{reg:{sp:+b.dataset.a}});
    o.textContent=`Setpoint address set to ${b.dataset.a}. Confirm it by changing the setpoint on the controller and reading it here.`;});};
$('bRead').onclick=async()=>{const a=+$('rdAddr').value,n=+$('rdN').value;
  const r=await api(`/api/mb/read?addr=${a}&n=${n}&fn=${$('rdFn').value}`);
  $('rdOut').innerHTML=r.ok?'<table><tr><th>Address</th><th>Raw</th><th>÷10</th><th>Hex</th></tr>'+r.values.map((v,i)=>
    `<tr><td class="n">${a+i}</td><td class="n">${v}</td><td class="n">${(v/10).toFixed(1)}</td><td class="n">0x${(v&0xffff).toString(16).padStart(4,'0')}</td></tr>`).join('')+'</table>':r.err;};
$('bWrite').onclick=async()=>{if(!confirm('Write this raw value to the controller?'))return;
  const r=await api('/api/mb/write',{addr:+$('wrAddr').value,value:+$('wrVal').value});$('wrOut').textContent=r.ok?'Written.':r.err;};

// ---------- controller parameters
async function loadParamsInfo(){const s=settings||await api('/api/settings');
  $('ctrlNote').textContent=s.profile==0?'Reads the PID and alarm parameters of the PMA KS 4x-1. Values are written one at a time; the controller rejects values outside its limits.'
    :'Parameter access is only built in for the PMA KS 4x-1. For other controllers use Tools with the addresses from their manual.';}
$('bReadParams').onclick=async()=>{const tb=$('paramRows');tb.innerHTML='<tr><td colspan="4">Reading...</td></tr>';
  const r=await api('/api/params');tb.innerHTML='';
  r.params.forEach(p=>{const tr=document.createElement('tr');const ro=p.name==='Sw.Op';
    tr.innerHTML=`<td>${p.name}</td><td>${p.desc}</td><td>${p.err?p.err:`<input value="${p.value}" ${ro?'disabled':''}>`}</td><td>${ro||p.err?'':'<button class="b">Write</button>'}</td>`;
    const b=tr.querySelector('button');if(b)b.onclick=async()=>{const v=tr.querySelector('input').value;
      const w=await api('/api/params',{addr:p.addr,scaled:p.scaled,value:v==='off'?'off':+v});b.textContent=w.ok?'Written':'Failed';b.title=w.err||'';};
    tb.appendChild(tr);});
  const ac=$('actions');ac.innerHTML='';r.actions.forEach((l,i)=>{const b=document.createElement('button');b.className='b';b.textContent=l;
    b.onclick=async()=>{if(!confirm(l+'?'))return;const w=await api('/api/action',{i});$('actMsg').textContent=w.ok?l+': done.':w.err;};ac.appendChild(b);});};

// ---------- wifi
$('bSaveWifi').onclick=async()=>{const o={ssid:$('ssid').value,pass:$('pass').value};if($('apPass').value)o.apPass=$('apPass').value;
  await api('/api/settings',{hostname:$('hostname').value});const r=await api('/api/wifi',o);$('wifiMsg').textContent=r.msg||r.err;};

api('/api/program').then(p=>{program=p;pollStatus();});
setInterval(pollStatus,1000);setInterval(pollHistory,5000);pollHistory();
</script>
</body>
</html>)HTML";
