/* ---------- Channels page ---------- */
let chSel=0, chBuilt=false, chNamesKey='', xKey=['',''];
const TYPE_TXT={off:'off',modbus:'Modbus',local:'local'};
// (a third tab for the monitor channel on boards that have it: same look, the accent colour)
const chTabList=()=>CFG&&CFG.limits&&CFG.limits.monitor&&CFG.monitor?[0,1,MON]:CH;
function chTabs(){ if(!CFG) return; const L=chTabList(); if(!L.includes(chSel)) chSel=0;
  const tab=(i,name,sub)=>`<button role="tab" data-cht="${i}" aria-pressed="${i===chSel}" aria-selected="${i===chSel}" style="--c:${PV[i]}"><span class="bg" aria-hidden="true"></span><svg class="tabsh" aria-hidden="true"><path class="ol"/></svg><span class="arc" aria-hidden="true"></span><i></i>${esc(name)} <small>${sub}</small></button>`;
  const h=L.map(i=>i===MON?tab(i,CFG.monitor.name,CFG.monitor.on?'monitor':'off'):tab(i,CFG.channels[i].name,TYPE_TXT[CFG.channels[i].type]||'')).join('');
  if(tabsHtml!==h){ tabsHtml=h; $('chTabs').innerHTML=h; requestAnimationFrame(shapeTabs); } document.querySelectorAll('[data-chp]').forEach(d=>d.hidden=+d.dataset.chp!==chSel); }
$('chTabs').onclick=e=>{ const b=e.target.closest('button[data-cht]'); if(!b) return; chSel=+b.dataset.cht; chTabs(); updChanPage(); };
/* Channel tabs (spec 7, "Look"): css-tip "rounded tab" shapes. lb / rb = left / right foot: 'flat' (first tab, flush with the card's
   left edge), 'flare' (curves out into the card's top edge), 'under' (inactive tab tucked under its right neighbour or the selected one: its
   edge stops where the neighbour's foot starts, its fill follows that curve). Outline coordinates are 1px-stroke centres on
   the card border's pixel grid; the tabs overlap by TAB_R+1 px. The selected tab and the card share one frosted background
   (.tabbg = union of both shapes), so there is no seam; the card's top edge is open under the selected tab (--g0 / --g1). */
function tabShape(W,H,lb,rb,ox,oy,yb){
  const r=TAB_R, TR=TAB_TR, e=.5, P=(x,y)=>(x+ox)+' '+(y+oy), A=(rad,sw,x,y)=>' A'+rad+' '+rad+' 0 0 '+sw+' '+P(x,y);
  const xl=lb==='flare'?r+e:e, xr=rb==='flare'?W-r-e:W-e;
  let ol, fs, oe, fe;
  if(lb==='flare'){ ol='M'+P(e,H-e)+A(r,0,r+e,H-e-r); fs=ol; }
  else if(lb==='under'){ ol='M'+P(e,H-e-r); fs='M'+P(r+e,H-e)+A(r,1,e,H-e-r); }
  else { ol='M'+P(e,H); fs='M'+P(0,yb)+' L'+P(0,TR+e); }
  const top=' L'+P(xl,TR+e)+A(TR,1,xl+TR,e)+' L'+P(xr-TR,e)+A(TR,1,xr,TR+e);
  if(rb==='flare'){ oe=' L'+P(xr,H-e-r)+A(r,0,W-e,H-e); fe=oe+' L'+P(W,yb); }
  else { oe=' L'+P(xr,H-e-r); fe=oe+A(r,1,W-e-r,H-e)+' L'+P(W-e-r,yb); }
  return {ol:ol+top+oe, fill:fs+top+fe+' L'+P(lb==='under'?r+e:0,yb)+' Z'};
}
function shapeTabs(){
  const tabs=[...document.querySelectorAll('#chTabs button[data-cht]')], sel=tabs.findIndex(t=>t.getAttribute('aria-pressed')==='true');
  if(!tabs.length||!tabs[0].offsetWidth) return;  // page not shown
  tabs.forEach((b,i)=>{
    // an inactive tab's right foot always tucks under its right neighbour (selected, or inactive and drawn later
    // on top of it): no gap between two inactive tabs (three tabs with the monitor channel)
    const W=b.offsetWidth, H=b.offsetHeight, on=i===sel, lb=i===0?'flat':(!on&&sel===i-1?'under':'flare'), rb=!on&&i<tabs.length-1?'under':'flare';
    const sh=tabShape(W,H,lb,rb,0,0,on?H:H-1);
    b.querySelector('.bg').style.clipPath=`path('${sh.fill}')`; b.querySelector('.ol').setAttribute('d',sh.ol);
    b.style.setProperty('--ax',(lb==='flare'?TAB_R:0)+'px'); b._shape=[W,H,lb,rb];
  });
  const t=tabs[sel], pg=$('pg-channels'), box=document.querySelector('[data-chp]:not([hidden]) .ctab-panel'); if(!t||!box) return;
  let bgEl=pg.querySelector('.tabbg'); if(!bgEl){ bgEl=document.createElement('div'); bgEl.className='tabbg'; bgEl.setAttribute('aria-hidden','true'); pg.insertBefore(bgEl,pg.firstChild); }
  const gr=pg.getBoundingClientRect(), tr=t.getBoundingClientRect(), pr=box.getBoundingClientRect();
  Object.assign(bgEl.style,{left:(pr.left-gr.left)+'px',top:(tr.top-gr.top)+'px',width:pr.width+'px',height:(pr.bottom-tr.top)+'px'});
  const s=t._shape, tab=tabShape(s[0],s[1],s[2],s[3],tr.left-pr.left,0,s[1]).fill, cy=pr.top-tr.top, cw=pr.width, ch=pr.height, R=18;
  const card=`M0 ${cy} L${cw-R} ${cy} A${R} ${R} 0 0 1 ${cw} ${cy+R} L${cw} ${cy+ch-R} A${R} ${R} 0 0 1 ${cw-R} ${cy+ch} L${R} ${cy+ch} A${R} ${R} 0 0 1 0 ${cy+ch-R} Z`;
  bgEl.style.clipPath=`path('${tab} ${card}')`;
  box.style.setProperty('--g0',(sel===0?1:tr.left-pr.left+1)+'px'); box.style.setProperty('--g1',(tr.right-pr.left-1)+'px');
}
if(window.ResizeObserver) new ResizeObserver(()=>shapeTabs()).observe($('cChan'));
window.addEventListener('resize',shapeTabs);
if(document.fonts&&document.fonts.ready) document.fonts.ready.then(shapeTabs);
/* title bar (spec 7, "Look"): its backdrop band is transparent at the top of the page and fully there once the first card reaches the top */
let tfRaf=0;
function topFade(){ tfRaf=0; const top=document.querySelector('.top'), pg=document.querySelector('.pg:not([hidden])'), first=pg&&pg.querySelector('.panel,.ctabs,.cards');
  const y0=first?first.getBoundingClientRect().top+scrollY:80; if(top) top.style.setProperty('--tf',Math.max(0,Math.min(1,scrollY/Math.max(1,y0))).toFixed(3)); }
addEventListener('scroll',()=>{ if(!tfRaf) tfRaf=requestAnimationFrame(topFade); },{passive:true});
// lowest polling interval for the saved channels (settings.cpp pollMinMs)
const pollMin=()=>CFG&&CFG.channels.every(c=>c.type==='off'||(c.type==='local'&&c.local.sensor==='max31855'))?100:250;
function commonGroups(){ const bn=CFG.channels[0].name;
  return [{h:'',fields:[
    {k:'bus.pollMs',l:'Polling interval',t:'num',get min(){ return pollMin(); },max:10000,step:50,u:'ms',h:'How often PV, SP and output are read: at least 100 ms when every channel is local with a MAX31855, else 250 ms (a MAX6675 or Modbus channel; raised by itself). With two Modbus controllers each one is polled every 2 × this interval.'},
    {k:'bus.commLossSec',l:'Pause the run after no answer for',t:'num',min:0,max:3600,u:'s',h:'0 = never pause'},
    {k:'run.bottomBand',l:`“${bn} reached” band ±`,t:'num',min:0.5,max:50,step:0.5,u:'°C',h:`Used by “Wait for ${bn}” in Sequence phases.`},
    {k:'run.spMargin',l:'Setpoint margin below the cut-off',t:'num',min:0,max:100,step:1,u:'°C',h:'A local channel’s highest setpoint is at most its max. temperature (cut-off) minus this: room for an overshoot before the outputs are cut off.'}]},
  ]; }
// RS485 bus: in the tile of each Modbus channel (one bus, shared by both: a change shows in both tiles)
function busGroup(P){ return {h:'RS485 bus',beta:true,show:g=>CFG.limits.rs485&&g(P+'type')==='modbus',note:'Shared by both Modbus channels. Modbus is a beta feature: not tested with a real controller yet.',fields:[
    {k:'bus.baud',l:'Baud rate',t:'sel',num:true,o:[2400,4800,9600,19200,38400].map(b=>[b,b])},
    {k:'bus.parity',l:'Parity',t:'sel',num:true,o:[[0,'None'],[1,'Even'],[2,'Odd']]},
    {k:'bus.stopBits',l:'Stop bits',t:'sel',num:true,o:[[1,'1'],[2,'2']]},
    {k:'bus.timeoutMs',l:'Response timeout',t:'num',min:50,max:2000,step:10,u:'ms'}]}; }
const REGS=[['pv','Process value (read)'],['spEff','Effective setpoint (read)'],['out','Output % (read)'],['status','Status word (read)'],['alarm','Alarm word (read)'],
  ['sp','Setpoint SP (write, required)'],['spLo','Setpoint lower limit (read)'],['spHi','Setpoint upper limit (read)'],['rampRate','Setpoint gradient r.SP (write)'],
  ['ctrlOff','Outputs off (write 1)'],['remote','Front panel lock (write 1)']];
function runawayFields(P,ty){ const R=`${P}${ty}.runaway.`, sh=g=>g(P+'type')===ty, sh2=g=>sh(g)&&g(R+'enable');
  return [{k:R+'enable',l:'Runaway detection',t:'sw',show:sh,sent:true,rh:'Heating failed: the sensor is not on its heater, or the heater / SSR is broken. Then (second line): the PV leaves the setpoint after it was reached.'},
    {k:R+'minOut',l:'Heating failed: output at least',pre:'heating failed: output at least',t:'num',min:0,max:100,u:'%',show:sh2,j:true},
    {k:R+'heatSec',l:'… for',pre:'for',t:'num',min:10,max:3600,u:'s',show:sh2,j:true},
    {k:R+'minRise',l:'… with a rise below',pre:'with a rise below',t:'num',min:0.5,max:100,step:0.5,u:'°C',show:sh2,j:true},
    {k:R+'holdBand',l:'Holding: PV off the setpoint by more than ±',rl:'',pre:'or: PV off the setpoint by more than ±',t:'num',min:1,max:200,u:'°C',show:sh2,sent:true},
    {k:R+'holdSec',l:'… for longer than',pre:'for longer than',t:'num',min:5,max:3600,u:'s',show:sh2,j:true},
    {k:R+'offEnable',l:'Heating while off (stuck SSR / controller)',rl:'Heating while off',t:'sw',def:true,show:sh,sent:true,h:'A stuck SSR or controller. Checked from 2 min after the outputs went off, not while the other channel heats.'},
    {k:R+'offRise',l:'… PV rises by',pre:'outputs off, but the PV rises by',t:'num',min:2,max:100,step:0.5,u:'°C',def:10,show:g=>sh(g)&&g(R+'offEnable'),j:true},
    {k:R+'offSec',l:'… within',pre:'within',t:'num',min:10,max:3600,u:'s',def:60,show:g=>sh(g)&&g(R+'offEnable'),j:true},
    {k:R+'frozenEnable',l:'Reading frozen',t:'sw',def:true,show:sh,sent:true,h:'A stuck converter or controller value.'},
    {k:R+'frozenSec',l:'… for',pre:'outputs on, but the reading does not change at all for',t:'num',min:10,max:3600,u:'s',def:120,show:g=>sh(g)&&g(R+'frozenEnable'),j:true}]; }
function chanGroups(i){ const P=`channels.${i}.`, ty=t=>g=>g(P+'type')===t;
  return [
   {h:'General',c:'var(--acc)',fields:[{k:P+'name',l:'Name',t:'text',max:16,h:'Shown on all screens'},
     {k:P+'type',l:'Type',t:'sel',o:[['off','Off (not used)'],...(CFG.limits.rs485?[['modbus','Modbus controller (beta)']]:[]),['local','Local (thermocouple + SSR)']]},
     {k:P+'endAction',l:'When the program ends or stops',t:'sel',o:[['keep','Keep the last setpoint'],['sp','Go to the end setpoint'],['off','Switch the outputs off']],show:g=>g(P+'type')!=='off'},
     {k:P+'endSp',l:'End setpoint',t:'num',min:-200,max:1500,step:0.1,u:'°C',show:g=>g(P+'type')!=='off'&&g(P+'endAction')==='sp'},
     {k:P+'spMin',l:'Lowest setpoint',rl:'Setpoint range',t:'num',min:-200,max:1500,step:0.1,show:g=>g(P+'type')!=='off',rh:'Programs, manual setpoints and run edits stay inside. Local: the highest setpoint is at most the max. temperature − 10 °C.'},
     {k:P+'spMax',l:'Highest setpoint',pre:'…',t:'num',min:-200,max:1500,step:0.1,u:'°C',show:g=>g(P+'type')!=='off',j:true}]},
   {h:'Modbus controller',beta:true,c:'var(--acc)',show:ty('modbus'),extra:`<div class="row"><button data-defs="${i}">Load register defaults for this controller type</button></div>`,fields:[
     {k:P+'modbus.addr',l:'Slave address',t:'num',min:1,max:247},
     {k:P+'modbus.profile',l:'Controller type',t:'sel',o:[['pma','PMA KS 40-1 / 41-1 / 42-1'],['generic','Generic Modbus RTU']],h:'A new type loads its register defaults'},
     {k:P+'modbus.rampMode',l:'Setpoint ramps',t:'sel',o:[['host','ESP32 writes setpoint steps'],['controller','Controller ramps (r.SP), fewer writes']]},
     {k:P+'modbus.minStep',l:'Min. step between writes',t:'num',min:0.1,max:100,step:0.1,u:'°C',show:g=>g(P+'modbus.rampMode')==='host'},
     {k:P+'modbus.minWriteSec',l:'Min. time between writes',t:'num',min:1,max:3600,u:'s',show:g=>g(P+'modbus.rampMode')==='host'},
     {k:P+'modbus.lockFront',l:'Lock the front panel while running',t:'sw'}]},
   Object.assign(busGroup(P),{c:'var(--acc)',full:true,c2:true}),
   {h:'Register addresses',det:true,c2:true,c:'var(--muted)',show:ty('modbus'),note:'Decimal protocol addresses (0-based, as sent on the wire); −1 = not available. The PMA KS 4x-1 defaults use the “1dP” column of its interface description (value = register / 10), e.g. SP = 11372.',
    fields:REGS.map(([k,l])=>({k:P+'modbus.reg.'+k,l,t:'num',min:-1,max:65534})).concat([
     {k:P+'modbus.reg.scale',l:'Divide values by',t:'num',min:1,max:1000,step:'any'},
     {k:P+'modbus.reg.readFn',l:'Read function',t:'sel',num:true,o:[[3,'3 (holding)'],[4,'4 (input)']]},
     {k:P+'modbus.reg.writeFn',l:'Write function',t:'sel',num:true,o:[[6,'6 (single)'],[16,'16 (multiple)']]},
     {k:P+'modbus.reg.offRaw',l:'“Off” raw value',t:'num',min:-32768,max:32767}])},
   {h:'Thermocouple',c:'var(--ok)',show:ty('local'),extra:`<div class="calbox" id="calBox${i}" hidden>${calHtml(i)}</div>`,fields:[
     {k:P+'local.sensor',l:'Converter',t:'sel',o:[['max31855','MAX31855'],['max6675','MAX6675']]},
     {k:P+'local.readMs',l:'Read every',t:'num',min:100,max:1000,step:10,u:'ms',def:250,h:'MAX6675: at least 250 ms (its conversion time); MAX31855 converts in up to 100 ms'},
     {k:P+'local.median',l:'Median filter',t:'sw',def:true,h:'Removes single-reading spikes; a change shows about half the window later'},
     {k:P+'local.medianN',l:'… over the last',t:'sel',num:true,def:3,o:[[3,'3 readings'],[5,'5 readings'],[7,'7 readings'],[9,'9 readings']],show:g=>g(P+'type')==='local'&&g(P+'local.median'),j:true},     {k:P+'local.jumpMax',l:'Largest change per reading',t:'num',min:0,max:500,step:1,u:'°C',def:25,h:'A bigger jump between two readings counts as a bad reading (held back, the output stays). 0 = not checked.'},     {k:P+'local.badSec',l:'Sensor fault after',t:'num',min:1,max:30,step:1,u:'s',def:3,h:'Bad readings (a converter fault or a jump) for this long while heating give a sensor fault.'},
     {k:P+'local.calGain',l:'Calibration gain',rl:'Calibration',pre:'×',t:'num',min:0.5,max:2,step:0.0001,rh:'Calibrated = raw × gain + offset. “Two-point…” works them out from one or two reference temperatures.'},
     {k:P+'local.calOffset',l:'Calibration offset',pre:'+',t:'num',min:-50,max:50,step:0.01,u:'°C',j:true,btn:`<button type="button" data-calt="${i}">Two-point…</button>`}]},
   {h:'PID control',c:'var(--manual)',show:ty('local'),fields:[
     {k:P+'local.kp',l:'Kp',rl:'Kp · Ki · Kd',t:'num',min:0,max:1000,step:0.01,rh:'Kp in % / °C, Ki in % / (°C·s), Kd in %·s / °C. The PID autotune finds them.'},
     {k:P+'local.ki',l:'Ki',t:'num',min:0,max:100,step:0.0001,j:true},
     {k:P+'local.kd',l:'Kd',t:'num',min:0,max:10000,step:0.1,j:true},
     {k:P+'local.ff',l:'Feed-forward',t:'sw',def:false,h:'Adds the output the heater model expects for the setpoint ahead (holding + ramp power); the PID corrects the rest. The autotune measures the model.'},
     {k:P+'local.ffK',l:'°C per % output',rl:'Heater model',t:'num',min:0.1,max:20,step:0.01,rh:'°C per % output · time constant · dead time (the setpoint is looked at this far ahead). The autotune measures them.'},
     {k:P+'local.ffTau',l:'Time constant',t:'num',min:5,max:3600,step:1,u:'s',j:true},
     {k:P+'local.ffDead',l:'Dead time',t:'num',min:0,max:120,step:0.5,u:'s',j:true},
     {k:P+'local.windowSec',l:'SSR window',t:'num',min:0.2,max:10,step:0.1,u:'s',h:'The SSR is on for the output share of every window'},
     {k:P+'local.maxOut',l:'Max. output',t:'num',min:0,max:100,u:'%'},
     {k:P+'local.maxTemp',l:'Max. temperature (outputs off above)',t:'num',min:50,max:600,u:'°C'}]},
   {h:'PID autotune',sub:'Tyreus–Luyben, about 10–30 min',c:'var(--violet)',show:ty('local'),extra:autotuneHtml(i),fields:[
     {k:P+'local.tuneMaxMin',l:'Autotune time limit',rl:'Time limit',t:'num',min:10,max:240,u:'min',def:70,h:'The autotune gives up after this time'}]},
   {h:'Protection',full:true,c:'var(--danger)',sub:'a tripped check switches the outputs off, stops a run and waits for Acknowledge',show:g=>g(P+'type')!=='off',
    fields:runawayFields(P,'modbus').concat(runawayFields(P,'local'),[
     {k:P+'local.cjEnable',l:'Converter too hot',t:'sw',def:true,show:ty('local'),sent:true,h:'A hot enclosure makes the readings drift.'},
     {k:P+'local.cjMax',l:'… above',pre:'the MAX31855’s own (cold-junction) temperature above',t:'num',min:30,max:125,u:'°C',def:70,show:g=>g(P+'type')==='local'&&g(P+'local.cjEnable'),j:true}])}];
}
// the monitor channel's tab: a third thermocouple, shown on the chart and logged only
function monGroups(){ const on=g=>g('monitor.on');
  return [
   {h:'General',c:'var(--acc)',note:'A third thermocouple that is only measured: the chart shows it (accent colour, its reading in the legend), the CSV exports and SD run logs include it. No output, no alarm. Wiring: its converter’s CS on GPIO 3; SCK and SO shared with the channels.',fields:[
     {k:'monitor.on',l:'Converter fitted',t:'sw',h:'Read, charted and logged while on'},
     {k:'monitor.name',l:'Name',t:'text',max:16,h:'Chart legend, CSV, touch screen'}]},
   {h:'Thermocouple',c:'var(--ok)',show:on,extra:`<div class="calbox" id="calBox${MON}" hidden>${calHtml(MON)}</div>`,fields:[
     {k:'monitor.sensor',l:'Converter',t:'sel',o:[['max31855','MAX31855'],['max6675','MAX6675']]},
     {k:'monitor.readMs',l:'Read every',t:'num',min:100,max:1000,step:10,u:'ms',def:1000,h:'MAX6675: at least 250 ms (its conversion time)'},
     {k:'monitor.median',l:'Median filter',t:'sw',def:true,h:'Removes single-reading spikes; a change shows about half the window later'},
     {k:'monitor.medianN',l:'… over the last',t:'sel',num:true,def:3,o:[[3,'3 readings'],[5,'5 readings'],[7,'7 readings'],[9,'9 readings']],show:g=>on(g)&&g('monitor.median'),j:true},
     {k:'monitor.calGain',l:'Calibration gain',rl:'Calibration',pre:'×',t:'num',min:0.5,max:2,step:0.0001,rh:'Calibrated = raw × gain + offset. “Two-point…” works them out from one or two reference temperatures.'},
     {k:'monitor.calOffset',l:'Calibration offset',pre:'+',t:'num',min:-50,max:50,step:0.01,u:'°C',j:true,btn:`<button type="button" data-calt="${MON}">Two-point…</button>`}]}];
}
function calHtml(i){ return `<h4>Two-point calibration</h4><p class="hint">Measure one or two temperatures with a reference thermometer. “Now” takes the current raw reading (without calibration). With one point only the offset changes.</p>
  <div class="cal"><span></span><span>Raw reading</span><span>True temperature</span>
  <span>Point 1</span><span><input type="number" step="0.1" id="cr1_${i}" aria-label="Point 1 raw"> <button data-now="${i}" data-p="1">Now</button></span><span><input type="number" step="0.1" id="ct1_${i}" aria-label="Point 1 true"> °C</span>
  <span>Point 2</span><span><input type="number" step="0.1" id="cr2_${i}" aria-label="Point 2 raw"> <button data-now="${i}" data-p="2">Now</button></span><span><input type="number" step="0.1" id="ct2_${i}" aria-label="Point 2 true"> °C</span></div>
  <div class="row"><button data-calc="${i}">Calculate offset and gain</button><span class="msg" id="calMsg${i}"></span></div>`; }
function rawNow(i){ if(i===MON) return live&&live.mon&&live.mon.raw!=null?live.mon.raw:null;
  const c=live&&live.ch[i], l=CFG.channels[i].local; if(!c||c.pv==null) return null; return (c.pv-l.calOffset)/(l.calGain||1); }
function buildChanPage(){
  mkForm('common',$('cCommon'),commonGroups(),{title:'All channels'});
  for(const i of CH) mkForm('ch'+i,$('cf'+i),chanGroups(i),{cards:true,sticky:true,save:'Save channel settings',top:`<p class="live1" id="chLive${i}"></p>`,
    confirm:async(F,body)=>{ const c=body.channels&&body.channels[i]; if(c&&c.type&&runActive()) return ask(`Changing the type of ${CFG.channels[i].name} stops the running program. Continue?`,'Change type'); return true; }});
  if(CFG.monitor) mkForm('ch'+MON,$('cf'+MON),monGroups(),{cards:true,sticky:true,save:'Save monitor settings',top:`<p class="live1" id="chLive${MON}"></p>`});
  chBuilt=true; chNamesKey=CFG.channels[0].name;
}
$('pg-channels').addEventListener('click',async e=>{
  const b=e.target.closest('button'); if(!b||!CFG) return; const d=b.dataset;
  if(d.defs!=null){ const i=+d.defs, F=FORMS['ch'+i], prof=readIn(F,F.fields.find(f=>f.k===`channels.${i}.modbus.profile`));
    if(!await ask('Replace all register addresses of this channel with the defaults for the selected controller type? Other unsaved changes of this channel are saved too.','Load defaults')) return;
    const body={}; for(const f of changedFields(F)) sp(body,f.k,readIn(F,f)); sp(body,`channels.${i}.modbus.profile`,prof); sp(body,`channels.${i}.modbus.loadDefaults`,true);
    const r=await postCfg(body); if(r.err){ fMsg(F,r.err,true); return; } CFG=r; fillForm(F); fMsg(F,'Register defaults loaded and saved.'); return; }
  if(d.calt!=null){ const x=$('calBox'+d.calt); x.hidden=!x.hidden; b.classList.toggle('on',!x.hidden); return; }
  if(d.now!=null){ const i=+d.now, v=rawNow(i); if(v==null){ setMsg('calMsg'+i,'No reading right now.',true); return; } $(`cr${d.p}_${i}`).value=Math.round(v*100)/100; setMsg('calMsg'+i,''); return; }
  if(d.calc!=null){ const i=+d.calc, F=FORMS['ch'+i], n=id=>parseFloat($(id+'_'+i).value), r1=n('cr1'), t1=n('ct1'), r2=n('cr2'), t2=n('ct2');
    const fo=F.fields.find(f=>/(local|monitor)\.calOffset$/.test(f.k)), fg=F.fields.find(f=>/(local|monitor)\.calGain$/.test(f.k));
    if(isNaN(r1)||isNaN(t1)){ setMsg('calMsg'+i,'Enter at least point 1 (raw and true).',true); return; }
    let g=readIn(F,fg); if(isNaN(g)) g=1;
    if(!isNaN(r2)&&!isNaN(t2)){ if(Math.abs(r2-r1)<5){ setMsg('calMsg'+i,'The two raw readings must be at least 5 °C apart.',true); return; } g=(t2-t1)/(r2-r1); }
    const o=t1-g*r1;
    if(g<0.5||g>2||Math.abs(o)>50){ setMsg('calMsg'+i,`Result out of range (gain ${g.toFixed(4)}, offset ${o.toFixed(2)}): check the points.`,true); return; }
    fEl(F,fg).value=(Math.round(g*10000)/10000).toString(); fEl(F,fo).value=(Math.round(o*100)/100).toString(); updForm(F);
    setMsg('calMsg'+i,'Offset and gain filled in — save the channel settings to use them.'); return; }
  if(d.at!=null){ const i=+d.ch, cmd=d.at;
    if(cmd==='start'){ const sp=parseFloat($('atSp'+i).value); if(isNaN(sp)) return; setMsg('atMsg'+i,'Starting…'); const r=await api('/api/autotune',{ch:i,cmd,sp}); setMsg('atMsg'+i,r.err||(r.ok?'':'Not started: see the message on the Dashboard.'),!!r.err||!r.ok); }
    else if(cmd==='stop'||cmd==='discard'){ const r=await api('/api/autotune',{ch:i,cmd}); setMsg('atMsg'+i,r.err||'',!!r.err); }
    else if(cmd==='accept'){ const r=await api('/api/autotune',{ch:i,cmd}); if(r.err){ setMsg('atMsg'+i,r.err,true); return; } await loadCfg(); setMsg('atMsg'+i,r.model?'New PID values and heater model saved.':'New PID values saved.'); }
    poll(); return; }
  if(d.pread!=null){ readParams(+d.pread); return; }
});
function autotuneHtml(i){ const c=CFG.channels[i];
  return `<div class="crow"><span class="cl">Test temperature <button type="button" class="ci-i" title="Relay test around this temperature: the heater is switched between more and less power until the oscillation is steady. At least 10 °C above the current temperature and 15 °C below the max. temperature." aria-label="Explain">i</button></span>
    <span class="cc"><label class="ci"><input type="number" id="atSp${i}" step="1" value="${Math.min(200,c.local.maxTemp-15)}" aria-label="Test temperature"><span class="u">°C</span></label></span>
    <small class="ch" hidden>Relay test around this temperature: the heater is switched between more and less power until the oscillation is steady. At least 10 °C above the current temperature and 15 °C below the max. temperature. The proposed values can be accepted or discarded.</small></div>
    <div class="crow"><span class="cl">Result <button type="button" class="ci-i" title="Slow: no overshoot, but the last few degrees to the setpoint take minutes. Faster: the same gain, a stronger integral part; reaches the setpoint about 2–3 times sooner with a little overshoot. Chosen before the start; it cannot be changed while the autotune runs." aria-label="Explain">i</button></span>
    <span class="cc atr"><span>Slow, no overshoot</span><input type="checkbox" class="switch acc" id="atFast${i}" data-atfast="${i}" ${c.local.tuneFast?'checked':''} aria-label="Faster autotune result"><span>Faster</span></span>
    <small class="ch" hidden>Slow: no overshoot, but the last few degrees to the setpoint take minutes. Faster: the same gain, a stronger integral part; reaches the setpoint about 2–3 times sooner with a little overshoot. Chosen before the start; it cannot be changed while the autotune runs.</small></div>
    <div class="crow"><span class="cl">State: <span class="u" id="atSt${i}">idle</span></span><span class="cc"><button class="pri" data-at="start" data-ch="${i}" id="atGo${i}">Start autotune</button><button data-at="stop" data-ch="${i}" id="atStop${i}">Stop</button></span></div>
    <div class="msg" id="atMsg${i}"></div><div id="atRes${i}"></div>`; }
function extraHtml(i){ const c=CFG.channels[i];
  if(c.type==='modbus') return `<section class="panel"><h2>Controller parameters</h2>${c.modbus.profile==='pma'?`<p class="hint">Reads the PID and alarm parameters of the PMA KS 4x-1. Values are written one at a time; the controller rejects values outside its limits.</p>
    <div class="row"><button data-pread="${i}">Read from controller</button></div><div class="ed-wrap" id="pTbl${i}"></div><h3>Actions</h3><div class="row" id="pAct${i}"><span class="hint" style="margin:0">Read the parameters first.</span></div><div class="msg" id="pMsg${i}"></div>`
    :'<p class="hint">Parameter access is built in only for the PMA KS 4x-1. For other controllers use the Modbus tools (Tools page) with the addresses from their manual.</p>'}</section>`;
  return ''; }
async function readParams(i){ const tb=$('pTbl'+i); tb.textContent='Reading… (up to a few seconds per parameter)';
  // the device reads them in the background: ask until all are there
  let r; try{ r=await api('/api/params?ch='+i,undefined,20000);
    for(const t0=Date.now();r&&r.busy&&Date.now()-t0<150000;){ tb.textContent=`Reading… ${r.done||0} of ${r.total||'?'} parameters`; await sleep(600); r=await api('/api/params?ch='+i,undefined,20000); }
  }catch(e){ tb.textContent='Device not reachable.'; return; }
  if(!r||r.busy||!r.params){ tb.textContent=r&&r.err?r.err:'No answer from the device.'; return; }
  tb.innerHTML=`<table class="t"><thead><tr><th>Name</th><th>Meaning</th><th>Value</th><th></th></tr></thead><tbody>${r.params.map((p,k)=>{ const ro=p.name==='Sw.Op';
    return `<tr><td>${esc(p.name)}</td><td>${esc(p.desc)}</td><td>${p.err?`<span class="msg bad">${esc(p.err)}</span>`:`<input type="text" value="${esc(p.value)}" ${ro?'disabled':''} id="pv${i}_${k}" aria-label="${esc(p.name)}">`}</td><td>${ro||p.err?'':`<button data-pw="${k}">Write</button>`}</td></tr>`; }).join('')}</tbody></table>`;
  tb.querySelectorAll('button[data-pw]').forEach(b=>b.onclick=async()=>{ const p=r.params[+b.dataset.pw], v=$(`pv${i}_${b.dataset.pw}`).value.trim();
    if(v!=='off'&&isNaN(parseFloat(v))){ b.textContent='Number or off'; return; }
    const w=await api('/api/params',{ch:i,addr:p.addr,scaled:p.scaled,value:v==='off'?'off':+v}); b.textContent=w.ok?'Written':'Failed'; b.title=w.err||''; });
  const ac=$('pAct'+i); ac.innerHTML=r.actions.map((l,k)=>`<button data-pact="${k}">${esc(l)}</button>`).join('')||'<span class="hint">No actions.</span>';
  ac.querySelectorAll('button').forEach(b=>b.onclick=async()=>{ const l=r.actions[+b.dataset.pact]; if(!await ask(l+'?',l)) return;
    const w=await api('/api/action',{ch:i,i:+b.dataset.pact}); setMsg('pMsg'+i,w.ok?l+': done.':w.err||'Failed',!w.ok); }); }
function updChanPage(){
  if(!CFG||!live) return;
  if(!chBuilt) buildChanPage();
  else if(chNamesKey!==CFG.channels[0].name&&!FORMS.common.dirty){ mkForm('common',$('cCommon'),commonGroups(),{title:'All channels'}); chNamesKey=CFG.channels[0].name; }
  chTabs();
  for(const i of CH){ const c=live.ch[i], cc=CFG.channels[i];
    const k=cc.type+cc.modbus.profile; if(xKey[i]!==k){ xKey[i]=k; $('cx'+i).innerHTML=extraHtml(i); }
    const L=$('chLive'+i);
    if(L) L.innerHTML=cc.type==='off'?'Channel off: not polled.':`${c.ok?'':'<span class="msg bad" style="margin:0">'+esc('No answer'+(c.err?': '+c.err:''))+'</span> · '}PV <b>${c.pv!=null?f1(c.pv)+' °C':'–'}</b>${cc.type==='local'&&c.pv!=null?` (raw ${f1(rawNow(i))} °C)`:''} · SP ${c.sp!=null?f1(c.sp)+' °C':'–'} · output ${c.out!=null?Math.round(c.out)+' %':'–'}${c.cold!=null?` · cold junction ${f1(c.cold)} °C`:''}${c.mode?' · '+esc(c.mode):''}${c.fault?` · <span class="msg bad" style="margin:0">Fault: ${esc(c.fault)}</span>`:''}`;
    if(cc.type==='local'&&$('atRes'+i)){ const a=c.autotune, busy=a&&a.state==='running', blocked=runActive()&&c.inProgram;
      $('atGo'+i).disabled=busy||blocked||!!c.fault; $('atStop'+i).disabled=!busy;
      const fs=$('atFast'+i); if(fs){ fs.disabled=busy; if(!fs.dataset.busy) fs.checked=busy?!!a.fast:!!cc.local.tuneFast; }  // (while it runs: the rule of the run)
      let h='';
      if(blocked&&!busy) h='<p class="hint">Not available while the program uses this channel.</p>';
      if(a){ if(a.state==='running') h=`<p>Running at ${f1(a.sp)} °C (${a.fast?'faster':'slow, no overshoot'}): cycle ${a.cycle}, ${fmt(a.elapsed)} — ${esc(a.msg)}</p>`;
        else if(a.state==='done'){ const l=cc.local; h=`<p>${esc(a.msg)} (Ku ${(+a.ku).toFixed(2)}, Pu ${Math.round(a.pu)} s)</p><table class="t"><thead><tr><th></th><th>Kp</th><th>Ki</th><th>Kd</th></tr></thead><tbody>
          <tr><td>Proposed</td><td class="n">${(+a.kp).toFixed(3)}</td><td class="n">${(+a.ki).toFixed(5)}</td><td class="n">${(+a.kd).toFixed(2)}</td></tr><tr><td>Current</td><td class="n">${(+l.kp).toFixed(3)}</td><td class="n">${(+l.ki).toFixed(5)}</td><td class="n">${(+l.kd).toFixed(2)}</td></tr></tbody></table>
          <p class="hint">${tuneModelText(a,l)}</p>
          <div class="row"><button class="pri" data-at="accept" data-ch="${i}">Accept</button><button data-at="discard" data-ch="${i}">Discard</button></div>`; }
        else if(a.state==='failed') h=`<p class="msg bad">Autotune failed: ${esc(a.msg)}</p><div class="row"><button data-at="discard" data-ch="${i}">Clear</button></div>`; }
      if($('atRes'+i).dataset.h!==h){ $('atRes'+i).innerHTML=h; $('atRes'+i).dataset.h=h; }
      $('atSt'+i).textContent=a?({running:'running',done:'done — review the values below',failed:'failed'}[a.state]||'idle'):blocked?'not available during the run':'idle'; }
  }
  const ML=$('chLive'+MON);
  if(ML&&CFG.monitor){ const m=live.mon;
    ML.innerHTML=!CFG.monitor.on?'Monitor channel off: not read.':!m?'Starting…':m.pv!=null?`PV <b>${f1(m.pv)} °C</b>${m.raw!=null?` (raw ${f1(m.raw)} °C)`:''}${m.cold!=null?` · cold junction ${f1(m.cold)} °C`:''} · shown and logged only`
      :`<span class="msg bad" style="margin:0">No reading${m.err?': '+esc(m.err):''}</span>`; }
}

