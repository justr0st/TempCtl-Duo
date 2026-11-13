'use strict';
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const fmt=t=>{t=Math.max(0,Math.round(t));const h=Math.floor(t/3600),m=Math.floor(t/60)%60,s=String(t%60).padStart(2,'0');return h?`${h}:${String(m).padStart(2,'0')}:${s}`:`${m}:${s}`;};
const f1=v=>v==null?'–':(+v).toFixed(1);
const CH=[0,1], KEY=['B','T'], PV=['var(--pvB)','var(--pvT)','var(--acc)'], SP=['var(--spB)','var(--spT)'];
// the monitor channel (a third thermocouple, S3: shown and logged only): index MON of the chart's PV rows and the
// raw readings; its colour is the accent (PV[MON])
const MON=2, monLive=()=>!!(live&&live.mon);
let tabsHtml=''; const TAB_R=12, TAB_TR=12;  // channel tabs (shapeTabs)
const EV={PHASE:1,STEP:2,RAMP_END:3,HOLD_END:4,PAUSE:5,CONT:6,END:7,STOP:8};
let live=null, progs=null, progsGen=-1, runProg=null, runProgAt=0, chartAt=0, view='live', showLast=true, edKey='';  // view: the live chart first (Rost); a run switches to its program view
let logSeq=0, logList=[], prevState='idle', askedSave=false;

async function api(path,body,ms){
  if(SER.on) return serReq(path,body,ms);
  if(OFF) throw new Error('not connected');
  const o=body===undefined?{cache:'no-store'}:{method:'POST',headers:{'Content-Type':'application/json','X-TempCtl':'1'},body:JSON.stringify(body)};  // (the device refuses POSTs without X-TempCtl)
  const ctl=new AbortController(), to=setTimeout(()=>ctl.abort(),ms||8000); o.signal=ctl.signal;
  try{const r=await fetch(path,o); return await r.json();} finally{clearTimeout(to);}
}
