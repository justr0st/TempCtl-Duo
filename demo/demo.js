// TempCtl Duo - browser demo (demo/README.md). Runs the firmware's control code (tempctl-demo.wasm, built by
// demo/build.sh) with simulated hardware and connects the normal web page to it like a USB cable: the page
// writes its request lines, the firmware's web API (webApiRequest) answers. The file system (settings,
// programs, event log) is kept in this browser; "Reset demo" starts from scratch.
'use strict';
window.TCDemo = (() => {
  const P = 'tcdemo.';
  const get = k => { try { return localStorage.getItem(P + k); } catch (e) { return null; } };
  const put = (k, v) => { try { if (v == null) localStorage.removeItem(P + k); else localStorage.setItem(P + k, v); } catch (e) {} };
  const dec = new TextDecoder(), enc = new TextEncoder();
  let mod = null, ex = null, onLine = null, down = true, queue = [], timer = 0, last = 0, rest = 0;
  let speed = +get('speed') || 1, sound = get('sound') !== '0', fsSaved = -1, heatAt = 0, logLine = '', st = null;

  // ---- WASI: the few calls the module makes (log output, clocks)
  function wasi() {
    const dv = () => new DataView(ex.memory.buffer);
    return {
      environ_get: () => 0,
      environ_sizes_get: (c, s) => { dv().setUint32(c, 0, true); dv().setUint32(s, 0, true); return 0; },
      clock_time_get: (id, _prec, out) => {
        const ms = id === 0 ? (ex && ex.demo_wall_ms ? ex.demo_wall_ms() : Date.now()) : performance.now();
        dv().setBigUint64(out, BigInt(Math.round(ms * 1e6)), true); return 0; },
      fd_close: () => 0,
      fd_seek: () => 70,
      fd_fdstat_get: (_fd, p) => { const d = dv(); d.setUint8(p, 2); d.setUint16(p + 2, 0, true); d.setBigUint64(p + 8, 0n, true); d.setBigUint64(p + 16, 0n, true); return 0; },
      fd_write: (fd, iov, n, nw) => {
        const d = dv(); let k = 0;
        for (let i = 0; i < n; i++) {
          const p = d.getUint32(iov + i * 8, true), l = d.getUint32(iov + i * 8 + 4, true);
          logLine += dec.decode(new Uint8Array(ex.memory.buffer, p, l)); k += l;
        }
        let j; while ((j = logLine.indexOf('\n')) >= 0) { const t = logLine.slice(0, j).replace(/\r$/, ''); logLine = logLine.slice(j + 1); if (t.trim()) console.debug('[demo]', t); }
        d.setUint32(nw, k, true); return 0; },
      proc_exit: c => { throw new Error('demo stopped (' + c + ')'); },
    };
  }
  const bytes = (p, n) => new Uint8Array(ex.memory.buffer, p, n);
  function cstr(s) { const b = enc.encode(s + '\0'), p = ex.demo_alloc(b.length); bytes(p, b.length).set(b); return p; }
  const b64 = u => { let s = ''; for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768)); return btoa(s); };

  // ---- the module: start, keep, restart
  async function boot(restarted) {
    if (!mod) {
      const r = fetch('tempctl-demo.wasm');
      try { mod = await WebAssembly.compileStreaming(r); }
      catch (e) { const x = await fetch('tempctl-demo.wasm'); if (!x.ok) throw new Error('tempctl-demo.wasm: HTTP ' + x.status); mod = await WebAssembly.compile(await x.arrayBuffer()); }
    }
    const inst = await WebAssembly.instantiate(mod, { wasi_snapshot_preview1: wasi() });
    ex = inst.exports; ex._initialize();
    const fs = get('fs');
    if (fs) try {
      const u = Uint8Array.from(atob(fs), c => c.charCodeAt(0)), p = ex.demo_alloc(u.length);
      bytes(p, u.length).set(u); ex.demo_fs_load(p, u.length); ex.demo_free(p);
    } catch (e) { console.warn('[demo] stored file system not readable - starting fresh', e); }
    let h = null; try { h = JSON.parse(get('heat') || 'null'); } catch (e) {}
    if (h && h.v && h.v.length === 7) {
      new Float32Array(ex.memory.buffer, ex.demo_heat(), 7).set(h.v);
      if (!restarted) ex.demo_coast(Math.max(0, (Date.now() - h.t) / 1000));
    }
    ex.demo_set(3, 0, speed);
    ex.demo_boot(Date.now(), restarted ? 1 : 0);
    fsSaved = -1; save();
    down = false;
    const q = queue; queue = []; q.forEach(request);
  }
  function save() {
    if (!ex) return;
    const g = ex.demo_fs_gen();
    if (g !== fsSaved) { const p = ex.demo_fs_dump(); put('fs', b64(bytes(p, ex.demo_fs_size()).slice())); fsSaved = g; }
    put('heat', JSON.stringify({ t: Date.now(), v: Array.from(new Float32Array(ex.memory.buffer, ex.demo_heat(), 7)) }));
  }
  function restart() {  // the firmware asked for a restart: what the board would do (about 1.5 s)
    down = true; save(); ex = null;
    setTimeout(() => boot(true).catch(e => console.error('[demo]', e)), 1500);
  }
  function step() {
    const now = performance.now(), dt = Math.min(now - last, 60000); last = now;
    if (down || !ex) return;
    rest += dt * speed;
    const ms = Math.min(Math.floor(rest), 600000); rest -= ms;
    if (ms > 0 && ex.demo_run(ms) === 1) return restart();
    if (now - heatAt > 3000) { heatAt = now; save(); }
    if (now - (step.ui || 0) > 250) { step.ui = now; st = JSON.parse(dec.decode(cbytes(ex.demo_state()))); ui(); }
  }
  const cbytes = p => { const u = new Uint8Array(ex.memory.buffer); let e = p; while (u[e]) e++; return u.subarray(p, e); };

  // ---- the page's "serial port"
  function request(line) {
    if (down) { queue.push(line); return; }
    const p = cstr(line), r = ex.demo_request(p), n = ex.demo_request_len();
    const out = dec.decode(bytes(r, n).slice()); ex.demo_free(p);
    for (const l of out.split('\n')) if (l) setTimeout(() => onLine(l), 0);
  }
  function write(t) {
    for (const l of String(t).split('\n')) if (l.trim().startsWith('{')) request(l.trim());
    return Promise.resolve();
  }

  // ---- the demo bar: speed, what the simulated hardware does, faults, reset
  let bar = null, audio = null, osc = null;
  const SPEEDS = [1, 2, 5, 10];
  function beep(on) {
    if (on && sound && !audio) return;  // (needs a click on the page first)
    if (!audio) return;
    if (on && sound) {
      if (!osc) { osc = audio.createOscillator(); const g = audio.createGain(); g.gain.value = 0.04; osc.type = 'square'; osc.frequency.value = 2700; osc.connect(g).connect(audio.destination); osc.start(); }
    } else if (osc) { osc.stop(); osc = null; }
  }
  addEventListener('pointerdown', () => { if (!audio) try { audio = new (window.AudioContext || window.webkitAudioContext)(); } catch (e) {} }, { once: true });
  const f1 = v => (Math.round(v * 10) / 10).toFixed(1);
  function ui() {
    if (!bar || !st) return;
    const q = s => bar.querySelector(s);
    const put = (s, t) => { const e = q(s); if (e.textContent !== t) e.textContent = t; };  // (only changes: no repaint)
    put('.dm-chip b', speed + '×');
    if (!q('.dm-p').hidden) {
      put('[data-v=e0]', f1(st.emit[0]) + ' °C');
      put('[data-v=e1]', f1(st.emit[1]) + ' °C');
      put('[data-v=p0]', Math.round(st.pow[0]) + ' W');
      put('[data-v=p1]', Math.round(st.pow[1]) + ' W');
      put('[data-v=b]', f1(st.board) + ' °C');
      put('[data-v=g]', f1(st.gpu) + ' °C');
      put('[data-v=r]', st.relay ? 'on' : 'off');
      put('[data-v=z]', st.buzz ? 'sounding' : 'quiet');
    }
    q('.dm-chip').classList.toggle('hot', st.pow[0] > 1 || st.pow[1] > 1);
    q('.dm-chip').classList.toggle('alarm', !!st.buzz);
    beep(!!st.buzz);
  }
  function makeBar() {
    const css = document.createElement('style');
    css.textContent = `
.dm{position:fixed;right:14px;bottom:14px;z-index:25;font:14px var(--sans);color:var(--text);display:flex;flex-direction:column;align-items:flex-end;gap:8px;max-width:calc(100vw - 28px)}
.dm-chip{position:relative;z-index:1;display:inline-flex;align-items:center;gap:8px;padding:14px 26px 14px 20px;border-radius:999px;border:3px solid var(--acc);background:var(--glass,var(--panel));-webkit-backdrop-filter:blur(18px) saturate(1.4);backdrop-filter:blur(18px) saturate(1.4);box-shadow:var(--glass-shadow,0 6px 20px rgba(0,0,0,.2));color:var(--text);font:600 24px var(--sans);gap:14px;cursor:pointer}
.dm-chip .lamp{width:16px;height:16px;border-radius:50%;background:var(--muted);flex:none}
.dm-chip.hot .lamp{background:#E8711C;box-shadow:0 0 12px #E8711C}
.dm-chip.alarm .lamp{background:var(--danger);animation:dmb .5s steps(2) infinite}
@keyframes dmb{50%{opacity:.2}}
.dm-chip b{font-weight:700;color:var(--acc)}
.dm-p{width:340px;max-width:100%;padding:14px 16px;border-radius:16px;border:1px solid var(--glass-border,var(--line));background:var(--glass,var(--panel));-webkit-backdrop-filter:blur(22px) saturate(1.4);backdrop-filter:blur(22px) saturate(1.4);box-shadow:var(--glass-shadow,0 10px 30px rgba(0,0,0,.25))}
.dm-p[hidden]{display:none}
.dm-p h4{margin:0 0 4px;font:700 15px var(--cond);letter-spacing:.02em}
.dm-p p{margin:0 0 10px;color:var(--muted);font-size:13px;line-height:1.35}
.dm-p .row{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin:8px 0}
.dm-p .lbl{color:var(--muted);font-size:13px;min-width:56px}
.dm-sp{display:inline-flex;border:1px solid var(--pill-border,var(--line));border-radius:999px;overflow:hidden}
.dm-sp button{border:0;border-radius:0;background:transparent;color:var(--text);padding:4px 12px;font:600 13px var(--sans);cursor:pointer;min-height:0}
.dm-sp button[aria-pressed=true]{background:var(--acc);color:var(--acc-ink)}
.dm-t{width:100%;border-collapse:collapse;font-size:13px;margin:4px 0 6px}
.dm-t td{padding:3px 0;border-top:1px solid var(--line)}
.dm-t td:nth-child(n+2){text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap;padding-left:10px}
.dm-f{display:flex;flex-direction:column;gap:6px;margin:6px 0 10px}
.dm-f label{display:flex;align-items:center;gap:8px;font-size:13px;cursor:pointer}
.dm-f small{color:var(--muted)}
.dm-p .end{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-top:6px}
.dm-p .end button{font:600 13px var(--sans);padding:5px 12px;border-radius:999px;border:1px solid var(--pill-border,var(--line));background:var(--pill,var(--sunk));color:var(--text);cursor:pointer}
.dm-p .end button.bad{color:var(--danger)}
/* breathing glow under the button: an element of its own, only its opacity / size animate (no repaints) */
html:root .dm .dm-chip,html:root .dm .dm-chip:hover{border:3px solid var(--acc)}  /* (over the page's button style) */
.dm-cw{position:relative;display:inline-flex}
.dm-glow{position:absolute;left:0;right:0;top:20%;bottom:-26px;border-radius:999px;background:var(--acc);filter:blur(24px);
  opacity:.45;transform:scale(.92);animation:dmglow 2.6s ease-in-out infinite;will-change:opacity,transform;pointer-events:none}
@keyframes dmglow{50%{opacity:1;transform:scale(1.3)}}
@media (prefers-reduced-motion:reduce){.dm-glow{animation:none;opacity:.6;transform:none}}
.brand b i.demo{font-style:normal;font-size:.72em;font-weight:700;letter-spacing:.06em;color:var(--acc-ink);background:var(--acc);
  border-radius:6px;padding:1px 6px;margin-left:4px;vertical-align:2px}
@media (max-width:720px){.dm{right:10px;bottom:10px}}`;
    document.head.appendChild(css);
    bar = document.createElement('div');
    bar.className = 'dm';
    bar.innerHTML = `
<div class="dm-p" hidden role="dialog" aria-label="Demo simulation">
  <h4>Demo — simulated device</h4>
  <p>The real firmware's control code runs in this browser. Its hardware is simulated: an 1100 W IR preheater under a medium-sized GPU board and a 450 W IR head above the GPU. Thermocouples: bottom on the board's underside, top on the GPU.</p>
  <div class="row"><span class="lbl">Speed</span><span class="dm-sp" role="group" aria-label="Simulation speed">${SPEEDS.map(s => `<button data-s="${s}">${s}×</button>`).join('')}</span></div>
  <table class="dm-t"><tbody>
    <tr><td>Bottom emitter</td><td data-v="e0"></td><td data-v="p0"></td></tr>
    <tr><td>Top emitter</td><td data-v="e1"></td><td data-v="p1"></td></tr>
    <tr><td>Board (true)</td><td data-v="b"></td><td></td></tr>
    <tr><td>GPU (true)</td><td data-v="g"></td><td></td></tr>
    <tr><td>SSR supply relay</td><td data-v="r"></td><td></td></tr>
    <tr><td>Buzzer</td><td data-v="z"></td><td></td></tr>
  </tbody></table>
  <div class="dm-f">
    <label><input type="checkbox" data-f="estop"> E-stop pressed <small>(Settings › Safety › E-stop must be on)</small></label>
    <label><input type="checkbox" data-f="tc0"> Bottom thermocouple broken</label>
    <label><input type="checkbox" data-f="tc1"> Top thermocouple broken</label>
    <label><input type="checkbox" data-f="snd"> Buzzer sound</label>
  </div>
  <div class="end"><button class="bad" data-a="reset" title="Settings, programs and logs back to the first start">Reset demo</button><button data-a="close">Close</button></div>
</div>
<div class="dm-cw"><span class="dm-glow" aria-hidden="true"></span><button class="dm-chip" aria-expanded="false" title="Demo: speed, simulated hardware, faults"><span class="lamp"></span>Demo <b>1×</b></button></div>`;
    document.body.appendChild(bar);
    const panel = bar.querySelector('.dm-p'), chip = bar.querySelector('.dm-chip');
    const pressed = () => bar.querySelectorAll('.dm-sp button').forEach(b => b.setAttribute('aria-pressed', +b.dataset.s === speed));
    pressed();
    chip.onclick = () => { panel.hidden = !panel.hidden; chip.setAttribute('aria-expanded', !panel.hidden); ui(); };
    bar.querySelector('.dm-sp').onclick = e => { const b = e.target.closest('button'); if (!b) return;
      speed = +b.dataset.s; put('speed', speed); if (ex) ex.demo_set(3, 0, speed); pressed(); ui(); };
    const fe = k => bar.querySelector(`[data-f=${k}]`);
    fe('snd').checked = sound;
    bar.querySelector('.dm-f').onchange = e => {
      const k = e.target.dataset.f, v = e.target.checked;
      if (k === 'snd') { sound = v; put('sound', v ? '1' : '0'); if (!v) beep(false); return; }
      if (!ex) return;
      if (k === 'estop') ex.demo_set(1, 0, v ? 1 : 0);
      else ex.demo_set(2, k === 'tc0' ? 0 : 1, v ? 1 : 0);
    };
    bar.querySelector('.end').onclick = async e => {
      const a = e.target.closest('button'); if (!a) return;
      if (a.dataset.a === 'close') { panel.hidden = true; chip.setAttribute('aria-expanded', false); return; }
      const ok = typeof window.ask === 'function' ? await window.ask('Reset the demo? Settings, programs and the logs go back to the first start.', 'Reset')
        : confirm('Reset the demo?');
      if (!ok) return;
      clearInterval(timer); down = true; ex = null;
      put('fs', null); put('heat', null);
      location.reload();
    };
  }

  // the page calls this instead of opening a serial port; lineFn gets the answer lines (as serLine)
  async function start(lineFn) {
    onLine = lineFn;
    makeBar();
    await boot(false);
    last = performance.now();
    timer = setInterval(step, 50);
    addEventListener('pagehide', save);
    document.addEventListener('visibilitychange', () => { if (document.hidden) save(); });
  }
  return { start, write, speed: () => speed };
})();
