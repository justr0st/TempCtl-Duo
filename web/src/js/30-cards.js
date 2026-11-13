/* ---------- channel cards ---------- */
let chansKey='';
function buildChans(){
  $('chans').innerHTML=CH.map(i=>`
  <section class="panel chan" id="card${i}" style="--c:${PV[i]}" aria-label="${esc(chName(i))}">
    <span class="fwarn" id="fwn${i}" hidden title="Fault: acknowledge it below"><svg viewBox="0 0 24 24"><path d="M10.3 3.9L1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><path d="M12 9v4M12 17h.01"/></svg></span>
    <div class="hd"><span class="nm"><span id="nm${i}"></span><span class="ty" id="ty${i}"></span></span><span class="state" id="st${i}"></span></div>
    <div class="read">
      <div><small>PV</small><b id="pv${i}">–</b></div>
      <div><small>SP</small><b class="sp" id="sp${i}">–</b></div>
      <div><small>Output <span class="o" id="ov${i}"></span></small><div class="meter"><i id="out${i}"></i></div></div>
    </div>
    <div class="why" id="why${i}"></div>
    <div class="row flt" id="flt${i}" hidden><span id="fltT${i}"></span><button data-ack="${i}">Acknowledge</button></div>
    <div class="row" id="atRow${i}" hidden><span class="lbl">Autotune</span><button data-atx="stop" data-ch="${i}">Stop autotune</button><button data-atx="review" data-ch="${i}" class="pri">Review the result…</button><button data-atx="clear" data-ch="${i}">Clear</button></div>
    <div id="runRows${i}" hidden>
      <div class="row"><span class="lbl">Target</span>
        <button data-dt="${i}" data-v="-5" aria-label="Target minus 5 °C">−5</button><button data-dt="${i}" data-v="-1" aria-label="Target minus 1 °C">−1</button>
        <input type="number" id="tg${i}" step="1" aria-label="Target °C"><button data-dt="${i}" data-v="1" aria-label="Target plus 1 °C">+1</button><button data-dt="${i}" data-v="5" aria-label="Target plus 5 °C">+5</button><button data-set="${i}">Set</button></div>
      <div class="row"><span class="lbl">Extend hold</span><button data-ex="${i}" data-v="10">+10 s</button><button data-ex="${i}" data-v="30">+30 s</button><button data-ex="${i}" data-v="60">+1 min</button></div>
    </div>
    <div class="row" id="manRow${i}" hidden><span class="lbl">Manual</span><input type="checkbox" class="switch" id="man${i}" aria-label="Manual setpoint on / off">
      <input type="number" id="manSp${i}" step="1" aria-label="Manual setpoint °C"> °C <button data-mset="${i}">Set</button></div>
  </section>`).join('');
  for(const i of CH){
    $('man'+i).onchange=async e=>{ const on=e.target.checked; e.target.busy=Date.now();
      if(on) await api('/api/manual',{ch:i,sp:+$('manSp'+i).value,on:true});
      else await api('/api/manual',{ch:i,on:false});  // off at once (no question)
      poll(); };
    $('manSp'+i).onkeydown=e=>{ if(e.key==='Enter') document.querySelector(`[data-mset="${i}"]`).click(); };
    $('tg'+i).onkeydown=e=>{ if(e.key==='Enter') document.querySelector(`[data-set="${i}"]`).click(); };
  }
}
$('chans').addEventListener('click',e=>{ const b=e.target.closest('[data-atx]'); if(b) tuneAction(+b.dataset.ch,b.dataset.atx); });
// the autotune rule (setting local.tuneFast): saved at once
document.addEventListener('change',async e=>{ const s=e.target.closest('[data-atfast]'); if(!s) return; const i=+s.dataset.atfast, v=s.checked;
  s.dataset.busy='1'; const r=await postCfg({channels:CH.map(k=>k===i?{local:{tuneFast:v}}:null)}); delete s.dataset.busy;
  if(r&&r.err){ s.checked=!v; setMsg('atMsg'+i,r.err,true); return; } CFG.channels[i].local.tuneFast=v; setMsg('atMsg'+i,''); });
