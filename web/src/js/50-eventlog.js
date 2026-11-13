/* ---------- event log ---------- */
let logUp=0;
async function updateLog(){
  if(!live) return;
  if(live.logSeq<logSeq||live.up<logUp){ logSeq=0; logList.push({seq:0,wall:Date.now()/1000-live.up,rt:null,msg:'— device restarted —'}); }  // new log
  logUp=live.up;
  if(live.logSeq===logSeq) return;
  const d=await api('/api/log?since='+logSeq); logSeq=d.seq;
  const base=Date.now()/1000-d.up;
  for(const [seq,up,rt,msg,kind] of d.log) logList.push({seq,wall:base+up,rt,msg,kind});
  logList=logList.slice(-100);
  $('log').innerHTML=logList.slice().reverse().map(l=>{ const w=new Date(l.wall*1000);
    return `<li class="k${l.kind||1}"><span title="${w.toLocaleString()}">${l.rt!=null?fmt(l.rt):w.toTimeString().slice(0,5)}</span>${esc(l.msg)}</li>`; }).join('')||'<li><span></span>No events yet.</li>';
}

