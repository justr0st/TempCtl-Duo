// Single-page web UI served from flash. Self-contained (works in AP mode
// without internet access): inline CSS, JS and SVG icons, system fonts.
// Look: dark industrial HMI style loosely following MoldControl: page tiles
// in a sidebar (slide-in panel on phones), pale cyan controls, yellow for the
// active step; the state cell and the manual setpoint tile change colour.
// The JSON API is in web.cpp.
#pragma once
#include <Arduino.h>

static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark">
<title>TempCtl Duo</title>
<style>
/* Look inspired by industrial HMIs (dark panels, tile menu, pale cyan
   controls, yellow highlight for the active item). Offline: no web fonts. */
:root{
  --bg:#1C1E21;--panel:#2A2D31;--panel2:#34383D;--inset:#222427;--line:#41464C;--line2:#585E65;
  --ink:#E9ECEE;--muted:#9BA3AB;--dim:#6C737A;
  --acc:#9FD9D5;--acc2:#C4ECE9;--acc-ink:#12302E;--acc-soft:rgba(159,217,213,.14);
  --hi:#F3C335;--hi-ink:#231C04;
  --pv:#FF8F52;--set:#7CC4F2;--plan:#D4D8DC;--ok:#5DCB8C;--bad:#F2695C;--warn:#F3C335;
  --field:#E7EAEC;--field-ink:#1B1E21;
  --txt:"Segoe UI",system-ui,-apple-system,Roboto,"Helvetica Neue",Arial,sans-serif;
  --r:6px;
}
*{box-sizing:border-box}
html,body{height:100%}
body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.4 var(--txt);
  display:grid;grid-template-columns:236px 1fr;overflow:hidden}
button,input,select{font:inherit;color:inherit}
svg{width:24px;height:24px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.hide{display:none!important}

/* ---------- sidebar with the page tiles (always visible on wide screens,
   a slide-in panel behind the menu button on narrow ones) */
#side{background:#26292D;border-right:1px solid #000;display:flex;flex-direction:column;gap:14px;padding:14px;
  overflow-y:auto;z-index:25}
.brand{display:flex;align-items:baseline;gap:8px;padding:2px 2px 4px;font-size:19px;letter-spacing:.02em}
.brand small{color:var(--muted);font-size:12px}
#scrim{position:fixed;inset:0;background:rgba(0,0,0,.4);z-index:20;display:none}
#scrim.open{display:block}
.tiles{display:grid;grid-template-columns:1fr;gap:10px}
.tile{height:60px;border:1px solid var(--acc);border-radius:var(--r);background:none;color:var(--acc2);cursor:pointer;
  display:flex;align-items:center;gap:14px;padding:0 16px;font-size:15px;text-align:left}
.tile svg{width:26px;height:26px;flex:none}
.tile:hover{background:var(--acc-soft)}
.tile[aria-current=true]{background:var(--acc);color:var(--acc-ink);border-color:var(--acc)}
.sfoot{margin-top:auto;display:flex;flex-direction:column;gap:10px;font-size:12px;color:var(--muted)}
#linkRow{display:flex;align-items:center;gap:8px;padding:8px 10px;border-radius:var(--r);border:1px solid var(--line);
  background:var(--inset);cursor:pointer;color:var(--ink);text-align:left;width:100%}
#linkRow i{width:10px;height:10px;border-radius:50%;background:var(--dim);flex:none}
#linkRow.ok i{background:var(--ok)}
#linkRow.bad{border-color:var(--bad);background:rgba(242,105,92,.15)}
#linkRow.bad i{background:var(--bad)}
#sideNet{white-space:pre-line;line-height:1.3}
.sfoot .row{justify-content:space-between}
#clock{font-variant-numeric:tabular-nums;font-size:15px;color:var(--ink)}
#bMenu{display:none}

/* ---------- top bar */
#app{display:grid;grid-template-rows:52px 1fr;min-width:0;height:100vh}
#top{display:grid;grid-template-columns:minmax(0,1fr) auto minmax(0,1fr);align-items:center;gap:12px;padding:0 14px;
  background:var(--panel);border-bottom:1px solid #000}
.tl{display:flex;align-items:center;gap:10px;min-width:0}
#segs{display:flex;gap:0;min-width:0;overflow-x:auto;scrollbar-width:none}
#segs::-webkit-scrollbar{display:none}
#segs button{flex:none;height:32px;padding:0 12px;border:1px solid var(--acc);background:none;color:var(--acc2);cursor:pointer;
  white-space:nowrap;font-size:13px;margin-left:-1px}
#segs button:first-child{border-radius:var(--r) 0 0 var(--r);margin-left:0}
#segs button:last-child{border-radius:0 var(--r) var(--r) 0}
#segs button:hover{background:var(--acc-soft)}
#title{font-size:20px;font-weight:400;letter-spacing:.01em;margin:0;text-align:center;white-space:nowrap}
#topR{display:flex;justify-content:flex-end;align-items:center;gap:10px;min-width:0}
#net{color:var(--muted);font-size:12px;white-space:pre-line;text-align:right;line-height:1.25;overflow:hidden}
.ib{width:36px;height:34px;border:1px solid var(--acc);border-radius:var(--r);display:grid;place-items:center;
  color:var(--acc2);background:none;cursor:pointer;flex:none;text-decoration:none}
.ib:hover{background:var(--acc-soft)}
.ib svg{width:20px;height:20px}

/* ---------- content */
main{overflow:auto;padding:14px;min-width:0}
section{display:none;max-width:1280px;margin:0 auto}
section.on{display:block}
.card{background:var(--panel);border:1px solid #000;border-radius:var(--r);padding:14px;margin-bottom:14px;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.04)}
.card>h2,.cardh{font-size:13px;font-weight:600;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);margin:0 0 12px}
.cardh{display:flex;align-items:center;gap:12px;flex-wrap:wrap}
.cardh .sp{flex:1}
p.note{color:var(--muted);margin:0 0 12px;max-width:75ch;font-size:13px}
.cap{color:var(--muted);font-size:12px}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.cols{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(440px,100%),1fr));gap:14px;align-items:start}
.cols>.card{margin:0}

/* buttons */
.b{height:38px;padding:0 16px;border-radius:var(--r);border:1px solid var(--acc);background:none;color:var(--acc2);
  cursor:pointer;display:inline-flex;align-items:center;justify-content:center;gap:8px;text-decoration:none;white-space:nowrap}
.b:hover{background:var(--acc-soft)}
.b.primary{background:var(--acc);color:var(--acc-ink);border-color:var(--acc);font-weight:600}
.b.primary:hover{background:var(--acc2)}
.b.danger{border-color:var(--bad);color:var(--bad)}
.b.danger:hover{background:rgba(242,105,92,.12)}
.b:disabled{opacity:.35;cursor:default;background:none}
.b.primary:disabled{background:var(--acc)}
.b svg{width:18px;height:18px}
button:focus-visible,input:focus-visible,select:focus-visible,a:focus-visible{outline:2px solid var(--hi);outline-offset:2px}

/* fields: light, like value fields on a panel */
input,select{height:36px;padding:0 10px;border:0;border-radius:4px;background:var(--field);color:var(--field-ink);
  width:100%;max-width:160px}
input[type=number]{text-align:right;font-variant-numeric:tabular-nums}
select{padding-right:6px}
input:disabled,select:disabled{opacity:.55}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(210px,1fr));gap:12px 16px}
.grid label{display:flex;flex-direction:column;gap:5px;font-size:12px;color:var(--muted)}
.grid label input,.grid label select{max-width:none}
.grid label.sw{flex-direction:row;align-items:center;justify-content:space-between;gap:10px;
  min-height:36px;align-self:end}
.inl{display:inline-flex;gap:6px;align-items:center}

/* switch (dark track, light knob with ring) */
.switch{appearance:none;-webkit-appearance:none;flex:none;width:58px;height:32px;max-width:none;border-radius:16px;
  background:#15171A;border:1px solid var(--line2);position:relative;cursor:pointer;padding:0;
  transition:background .15s}
.switch::after{content:"";position:absolute;top:3px;left:3px;width:24px;height:24px;border-radius:50%;
  background:var(--bg);border:3px solid var(--plan);transition:left .15s}
.switch:checked{background:var(--acc);border-color:var(--acc)}
.switch:checked::after{left:29px;border-color:#fff;background:var(--acc-ink)}
.switch:disabled{opacity:.4;cursor:default}

/* stepper: [-][field][+] */
.stepper{display:inline-flex;align-items:stretch;height:40px}
.stepper button{width:40px;border:0;background:var(--acc);color:var(--acc-ink);font-size:22px;line-height:1;cursor:pointer}
.stepper button:first-child{border-radius:var(--r) 0 0 var(--r)}
.stepper button:last-child{border-radius:0 var(--r) var(--r) 0}
.stepper button:hover{background:var(--acc2)}
.stepper input{height:auto;border-radius:0;max-width:120px;font-size:17px}

/* ---------- main view */
.dash{display:grid;grid-template-columns:minmax(0,1fr) 210px;gap:14px}
.dash>*{min-width:0}
.warn{border-left:4px solid var(--warn);padding:9px 12px;background:rgba(243,195,53,.1);margin-bottom:14px;
  border-radius:0 var(--r) var(--r) 0}
.warn a{color:var(--hi)}
.chartcard{padding:0;overflow:hidden}
.chartcard .cardh{margin:0;padding:10px 14px}
.legend{display:flex;gap:14px;color:var(--muted);font-size:12px;flex-wrap:wrap;align-items:center;text-transform:none;letter-spacing:0;font-weight:400}
.legend i{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:5px;vertical-align:0}
#chartSpan{color:var(--dim)}
.strip{position:relative;display:flex;height:54px;margin:0 14px 8px;gap:3px}
.strip div{position:relative;flex:1 1 0;min-width:8px;border:1px solid rgba(212,216,220,.45);border-radius:3px;
  background:linear-gradient(180deg,rgba(212,216,220,.10),rgba(212,216,220,.03));
  font-size:11px;line-height:1.2;padding:4px 5px;overflow:hidden;white-space:nowrap;color:var(--muted)}
.strip .ramp{background:linear-gradient(180deg,rgba(212,216,220,.02),rgba(212,216,220,.14))}
.strip div b{position:absolute;right:2px;bottom:2px;min-width:15px;height:15px;border:1px solid rgba(212,216,220,.6);
  border-radius:50%;font-size:9px;font-weight:600;display:grid;place-items:center;color:var(--plan)}
.strip .cur{min-width:46px;background:var(--hi);border-color:var(--hi);color:var(--hi-ink)}
.strip .cur b{border-color:var(--hi-ink);color:var(--hi-ink)}
#marker{position:absolute;top:-4px;bottom:-4px;width:2px;background:var(--hi);left:0;display:none;z-index:2}
#marker span{position:absolute;top:-2px;left:50%;transform:translate(-50%,-100%);background:var(--hi);color:var(--hi-ink);
  font-size:11px;font-weight:600;padding:2px 6px;border-radius:3px;white-space:nowrap}
.plot{background:var(--inset);border-top:1px solid #000}
canvas{width:100%;height:300px;display:block}
.facts{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));background:var(--panel);
  border:1px solid #000;border-radius:var(--r);overflow:hidden;margin-bottom:14px}
.facts>div{padding:9px 12px;min-width:0;border-right:1px solid #000;border-bottom:1px solid #000;margin:0 -1px -1px 0}
.facts b{display:block;font-size:17px;font-weight:400;font-variant-numeric:tabular-nums;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#msg{min-height:1.4em;color:var(--muted);margin:-6px 2px 10px;font-size:13px}
#msg.bad{color:var(--bad)}

/* right column: big readouts */
.reads{display:flex;flex-direction:column;gap:14px}
.read{background:var(--panel);border:1px solid #000;border-radius:var(--r);padding:10px 12px 12px;position:relative}
.read .cap{display:block}
.big{font-size:52px;font-weight:300;line-height:1;font-variant-numeric:tabular-nums;letter-spacing:-.01em;margin-top:4px}
.big small{font-size:15px;color:var(--muted);margin-left:3px;font-weight:400}
.mid{font-size:34px;font-weight:300;line-height:1.05;font-variant-numeric:tabular-nums;margin-top:4px}
#pv{color:var(--pv)}#sp{color:var(--set)}
.meter{position:absolute;right:10px;top:12px;bottom:12px;width:7px;display:flex;flex-direction:column-reverse;gap:2px}
.meter i{flex:1;background:#15171A;border-radius:1px}
.meter i.on{background:var(--ok)}
.meter i.on.hot{background:var(--warn)}
#outRead{padding-right:28px}
.man .stepper{margin:8px 0 10px}
.man .row{justify-content:space-between}
.pill{display:inline-block;padding:2px 10px;border-radius:12px;font-size:12px;border:1px solid var(--line2);color:var(--muted)}
.pill.on{background:var(--ok);border-color:var(--ok);color:#0E2A1A}

/* program run controls on the status page */
.runbar{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-bottom:14px}
.runbar select{max-width:300px;flex:1 1 170px;height:44px;background:var(--panel2);color:var(--ink);border:1px solid var(--line2)}
.runbar .b{height:44px;min-width:100px}
.runbar .b.stop{border-color:var(--bad);color:var(--bad)}
.runbar .b.stop:hover{background:rgba(242,105,92,.12)}
.runbar .b.stop:not(:disabled){background:var(--bad);color:#2A0C08;font-weight:600}

/* state cell and manual setpoint tile: colour shows what is going on */
.facts>div.st-running{background:var(--ok);color:#0B2616}
.facts>div.st-paused{background:var(--hi);color:var(--hi-ink)}
.facts>div.st-finished{background:var(--acc);color:var(--acc-ink)}
.facts>div.st-stopped{background:var(--bad);color:#2A0C08}
.facts>div.st-manual{background:var(--pv);color:#2B1204}
.facts>div[class^=st-] .cap{color:inherit;opacity:.75}
.read.man.on{background:#6B3316;border-color:var(--pv);box-shadow:inset 0 0 0 1px var(--pv)}
.read.man.on>.cap{color:#FFD9C2}
.read.man.on .pill.on{background:var(--pv);border-color:var(--pv);color:#2B1204}

/* tables */
table{border-collapse:collapse;width:100%}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line)}
th{font-weight:600;color:var(--muted);font-size:11px;letter-spacing:.05em;text-transform:uppercase}
td.n{font-variant-numeric:tabular-nums}
td input{max-width:110px;height:32px}
td .inl input{max-width:80px}td .inl select{max-width:66px}
tr.sel td{background:rgba(243,195,53,.08)}
tr.sel td:first-child{box-shadow:inset 3px 0 0 var(--hi)}
.sq{width:32px;height:32px;padding:0;border:1px solid var(--acc);border-radius:4px;background:none;color:var(--acc2);
  cursor:pointer;font-size:15px}
.sq:hover{background:var(--acc-soft)}
.sq.danger{border-color:var(--bad);color:var(--bad)}
.sq:disabled{opacity:.3;cursor:default;background:none}
.prog{display:grid;grid-template-columns:minmax(0,1.25fr) minmax(0,1fr);gap:14px;align-items:start}
.tbl{overflow:auto}
#progChart{height:240px}
.prog .plot{border:1px solid #000;border-radius:4px}

/* dialog */
#dlg{position:fixed;inset:0;background:rgba(0,0,0,.5);z-index:50;display:none;place-items:center;padding:16px}
#dlg.open{display:grid}
#dlg .box{background:#3A3D42;border:1px solid #000;border-radius:8px;width:min(420px,100%);padding:18px;
  box-shadow:0 12px 36px rgba(0,0,0,.55)}
#dlg .t{font-size:17px;margin:0 0 14px;line-height:1.35}
#dlg .row{justify-content:flex-end}

@media (max-width:980px){.dash{grid-template-columns:minmax(0,1fr)}.reads{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr))}
  .prog{grid-template-columns:1fr}}
@media (max-width:860px){body{grid-template-columns:1fr}
  #side{position:fixed;left:0;top:0;bottom:0;width:min(270px,86vw);transform:translateX(-105%);transition:transform .18s ease;
    box-shadow:6px 0 18px rgba(0,0,0,.5)}
  #side.open{transform:none}#bMenu{display:grid}#segs{display:none}}
@media (max-width:720px){#net{display:none}#top{grid-template-columns:auto 1fr auto;padding:0 10px}#title{text-align:left;font-size:18px}
  main{padding:10px}.card{padding:12px}.big{font-size:42px}canvas{height:230px}.legend .dsh{display:none}
  .runbar .b{min-width:0;flex:1;padding:0 8px}.runbar select{max-width:none;flex-basis:100%}}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
</style>
</head>
<body>
<svg style="display:none" aria-hidden="true">
  <symbol id="i-menu" viewBox="0 0 24 24"><path d="M4 6h16M4 12h16M4 18h16"/></symbol>
  <symbol id="i-close" viewBox="0 0 24 24"><path d="M6 6l12 12M18 6L6 18"/></symbol>
  <symbol id="i-home" viewBox="0 0 24 24"><path d="M3 11l9-8 9 8M5 10v10h5v-6h4v6h5V10"/></symbol>
  <symbol id="i-list" viewBox="0 0 24 24"><path d="M9 6h11M9 12h11M9 18h11M4 6h.01M4 12h.01M4 18h.01"/></symbol>
  <symbol id="i-screen" viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/></symbol>
  <symbol id="i-sliders" viewBox="0 0 24 24"><path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/></symbol>
  <symbol id="i-tool" viewBox="0 0 24 24"><path d="M14.7 6.3a4 4 0 0 0-5.4 5.4L3 18l3 3 6.3-6.3a4 4 0 0 0 5.4-5.4l-2.5 2.5-2.4-.6-.6-2.4z"/></symbol>
  <symbol id="i-play" viewBox="0 0 24 24"><path d="M8 5v14l11-7z" fill="currentColor" stroke="none"/></symbol>
  <symbol id="i-pause" viewBox="0 0 24 24"><path d="M7 5h3v14H7zM14 5h3v14h-3z" fill="currentColor" stroke="none"/></symbol>
  <symbol id="i-skip" viewBox="0 0 24 24"><path d="M5 5v14l9-7z" fill="currentColor" stroke="none"/><path d="M18 5v14"/></symbol>
  <symbol id="i-stop" viewBox="0 0 24 24"><rect x="6" y="6" width="12" height="12" rx="1" fill="currentColor" stroke="none"/></symbol>
  <symbol id="i-run" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M10 8v8l6-4z" fill="currentColor" stroke="none"/></symbol>
  <symbol id="i-runp" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M9 8h2v8H9zM13 8h2v8h-2z" fill="currentColor" stroke="none"/></symbol>
  <symbol id="i-alert" viewBox="0 0 24 24"><path d="M12 3l10 18H2z"/><path d="M13 9l-3 4h4l-3 4"/></symbol>
  <symbol id="i-dl" viewBox="0 0 24 24"><path d="M12 4v11M7 10l5 5 5-5M5 20h14"/></symbol>
  <symbol id="i-wifi" viewBox="0 0 24 24"><path d="M2 9a15 15 0 0 1 20 0M5 12.5a10 10 0 0 1 14 0M8.5 16a5 5 0 0 1 7 0M12 19.5h.01"/></symbol>
</svg>

<aside id="side" aria-label="Pages">
  <div class="brand">TempCtl Duo <small id="fw"></small></div>
  <div class="tiles">
    <button class="tile" data-v="main"><svg><use href="#i-home"/></svg>Status</button>
    <button class="tile" data-v="prog"><svg><use href="#i-list"/></svg>Programs</button>
    <button class="tile" data-v="sys"><svg><use href="#i-screen"/></svg>System</button>
    <button class="tile" data-v="ctrl"><svg><use href="#i-sliders"/></svg>Controller</button>
    <button class="tile" data-v="tools"><svg><use href="#i-tool"/></svg>Tools</button>
  </div>
  <div class="sfoot">
    <button id="linkRow" title="Controller link"><i></i><span id="linkTxt">Controller: -</span></button>
    <span id="sideNet"></span>
    <div class="row"><span id="clock"></span><a class="b" href="/api/log.csv" style="height:34px"><svg><use href="#i-dl"/></svg>Log (CSV)</a></div>
  </div>
</aside>
<div id="scrim"></div>

<div id="app">
<header id="top">
  <div class="tl"><button class="ib" id="bMenu" title="Menu" aria-expanded="false" aria-controls="side"><svg><use href="#i-menu"/></svg></button>
    <div id="segs"></div></div>
  <h1 id="title">Status</h1>
  <div id="topR"></div>
</header>
<main>

<section id="v-main" class="on">
  <div id="spWarn" class="warn hide">The setpoint register address is not set, so programs and the manual
    setpoint cannot work yet. Open <a href="#" data-go="ctrl">Controller</a> or use <a href="#" data-go="tools">Tools</a> to find it.</div>
  <div class="dash">
    <div>
      <div class="card chartcard">
        <div class="cardh">Temperature<span class="sp"></span>
          <span class="legend"><span><i style="background:var(--pv)"></i>Process value</span>
            <span><i style="background:var(--set)"></i>Setpoint</span>
            <span><i style="background:var(--plan)"></i>Program</span><span class="dsh">dashed = not active</span></span></div>
        <div class="strip" id="strip"><span id="marker"><span id="markTag"></span></span></div>
        <div class="plot"><canvas id="chart"></canvas></div>
        <div class="cap" id="chartSpan" style="padding:7px 14px"></div>
      </div>
      <div class="runbar">
        <select id="mainProg" aria-label="Program"></select>
        <button class="b primary" id="bStart"><svg><use href="#i-play"/></svg><span>Start</span></button>
        <button class="b" id="bPause"><svg><use href="#i-pause"/></svg>Pause</button>
        <button class="b" id="bSkip"><svg><use href="#i-skip"/></svg>Next phase</button>
        <button class="b stop" id="bStop"><svg><use href="#i-stop"/></svg>Stop</button>
      </div>
      <div class="facts">
        <div id="stateCell"><span class="cap">State</span><b id="state">-</b></div>
        <div><span class="cap">Step</span><b id="step">-</b></div>
        <div><span class="cap">Phase left</span><b id="phLeft">-</b></div>
        <div><span class="cap">Total left</span><b id="totLeft">-</b></div>
        <div><span class="cap">Controller mode</span><b id="mode">-</b></div>
      </div>
      <div id="msg"></div>
    </div>
    <div class="reads">
      <div class="read"><span class="cap">Process value</span><div class="big"><span id="pv">---.-</span><small>°C</small></div></div>
      <div class="read"><span class="cap">Setpoint in controller</span><div class="mid" id="sp">---.-</div></div>
      <div class="read" id="outRead"><span class="cap">Output</span><div class="mid" id="out">-</div><div class="meter" id="meter"></div></div>
      <div class="read man" id="manTile"><span class="cap">Manual setpoint</span>
        <div class="stepper"><button data-d="-1" aria-label="Lower">−</button><input id="manSp" type="number" step="0.1" aria-label="Manual setpoint"><button data-d="1" aria-label="Higher">+</button></div>
        <div class="row"><button class="b" id="bManSet">Set</button>
          <span class="inl"><span class="pill" id="manState">off</span><input type="checkbox" class="switch" id="manSw" aria-label="Manual mode on / off"></span></div>
        <p class="cap" style="margin:10px 0 0">On: writes this setpoint and switches the controller outputs on. Off: outputs off.
          Starting a program ends manual mode.</p>
      </div>
    </div>
  </div>
</section>

<section id="v-prog">
  <div class="card">
    <div class="grid">
      <label>Program<select id="progSel"></select></label>
      <label>Name<input id="progName" maxlength="23"></label>
    </div>
    <p class="note" style="margin:12px 0 0">Each step ramps from the previous temperature to its target, then holds. The first ramp starts
      at the temperature measured when the program starts. A ramp of 0 jumps directly. Up to 100 steps.</p>
  </div>
  <div class="prog">
    <div class="card"><h2>Steps</h2>
      <div class="tbl"><table><thead><tr><th>No</th><th>Target</th><th>Ramp</th><th>Hold</th><th></th></tr></thead>
        <tbody id="progRows"></tbody></table></div>
      <div class="row" style="margin-top:12px"><button class="b" id="bAddStep">+ Add step</button>
        <span class="cap" id="progTotal"></span></div>
    </div>
    <div class="card"><h2>Preview</h2>
      <div class="plot"><canvas id="progChart"></canvas></div>
      <div class="row" style="margin-top:12px"><button class="b primary" id="bSaveProg">Save program</button>
        <button class="b" id="bRevertProg">Revert</button><span id="progMsg" class="cap"></span></div>
    </div>
  </div>
</section>

<section id="v-sys">
  <div class="cols">
  <div class="card">
    <h2>Touch display</h2>
    <div class="grid">
      <label class="sw">Rotate screen 180°<input type="checkbox" class="switch" id="rotate180"></label>
      <label>Brightness (10-255)<input id="brightness" type="number" min="10" max="255"></label>
      <label>Colour theme<select id="themePalette"></select></label>
      <label class="sw">Dark mode<input type="checkbox" class="switch" id="themeDark"></label>
    </div>
    <div class="row" style="margin-top:14px"><button class="b primary" id="bSaveSys">Save display settings</button>
      <span id="sysMsg" class="cap"></span></div>
  </div>
  <div class="card">
    <h2>Wi-Fi</h2>
    <p class="note">Leave the network name empty to run as an access point only. If the device cannot join the
      network within 20 s it also opens its own access point. The host name takes effect after a restart.</p>
    <div class="grid">
      <label>Network name<input id="ssid" maxlength="32"></label>
      <label>Password (empty = keep)<input id="pass" type="password" maxlength="64"></label>
      <label>Access point password (8+ characters, empty = keep)<input id="apPass" type="password" maxlength="32"></label>
      <label>Host name (http://name.local)<input id="hostname" maxlength="23"></label>
    </div>
    <div class="row" style="margin-top:14px"><button class="b primary" id="bSaveWifi">Save and connect</button>
      <span id="wifiMsg" class="cap"></span></div>
    <p class="note" id="sysInfo" style="margin:12px 0 0"></p>
  </div>
  </div>
</section>

<section id="v-ctrl">
  <div class="cols">
  <div class="card">
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
    <div class="row" style="margin-top:14px"><button class="b" id="bDefaults">Load register defaults for this type</button></div>
  </div>
  <div class="card">
    <h2>Program behaviour</h2>
    <div class="grid">
      <label>Setpoint ramp<select id="rampMode"><option value="0">ESP32 writes setpoint steps</option>
        <option value="1">Controller ramps (r.SP), fewer writes</option></select></label>
      <label>Minimum step between writes<input id="minStep" type="number" step="0.1"></label>
      <label>Minimum seconds between writes<input id="minWriteSec" type="number"></label>
      <label class="sw">Hold only counts inside band (guaranteed soak)<input type="checkbox" class="switch" id="guaranteedSoak"></label>
      <label>Soak band ±<input id="soakBand" type="number" step="0.1"></label>
      <label>When the program ends or stops<select id="endAction"><option value="0">Keep current setpoint</option>
        <option value="1">Set end setpoint</option><option value="2">Switch controller outputs off</option></select></label>
      <label>End setpoint<input id="endSp" type="number" step="0.1"></label>
      <label class="sw">Lock front panel while running<input type="checkbox" class="switch" id="lockFront"></label>
      <label>Pause after link loss (s, 0 = never)<input id="commLossSec" type="number"></label>
      <label>Lowest allowed setpoint<input id="spMin" type="number" step="0.1"></label>
      <label>Highest allowed setpoint<input id="spMax" type="number" step="0.1"></label>
    </div>
    <div class="row" style="margin-top:14px"><button class="b primary" id="bSaveConn">Save controller settings</button>
      <span id="connMsg" class="cap"></span></div>
  </div>
  <div class="card">
    <h2>Register addresses</h2>
    <p class="note">Decimal protocol addresses (0-based, as sent on the wire). Use -1 for “not available”.
      PMA KS 4x-1 defaults use the “1dP” column of the interface description (value = register / 10),
      e.g. SP = 11372, r.SP = 11295. Saved with “Save controller settings”.</p>
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
  <div class="card">
    <h2>Controller parameters</h2>
    <p class="note" id="ctrlNote">Reads the PID and alarm parameters of the PMA KS 4x-1. Values are written one at a
      time; the controller rejects values outside its limits.</p>
    <div class="row"><button class="b" id="bReadParams">Read from controller</button></div>
    <div class="tbl"><table style="margin-top:12px"><thead><tr><th>Name</th><th>Meaning</th><th>Value</th><th></th></tr></thead>
      <tbody id="paramRows"></tbody></table></div>
  </div>
  <div class="card">
    <h2>Actions</h2>
    <div class="row" id="actions"><span class="cap">Read the parameters first.</span></div>
    <div id="actMsg" class="cap" style="margin-top:8px"></div>
  </div>
  </div>
</section>

<section id="v-tools">
  <div class="cols">
  <div class="card">
    <h2>Find the setpoint address</h2>
    <p class="note">Not needed for the PMA KS 4x-1 (preset to 11372). For other controllers: reads every register in the
      range and lists those whose value equals the controller's current effective setpoint. Make sure no ramp is active
      and the setpoint is an unusual value (e.g. 123.4) so the match is unique. Then change the setpoint on the front
      panel and read the candidate again to confirm.</p>
    <div class="row"><label class="inl cap">From <input id="scanFrom" type="number"></label>
      <label class="inl cap">To <input id="scanTo" type="number"></label>
      <button class="b" id="bScan">Scan</button></div>
    <div id="scanOut" style="margin-top:10px"></div>
  </div>
  <div class="card">
    <h2>Read registers</h2>
    <div class="row"><label class="inl cap">Address <input id="rdAddr" type="number"></label>
      <label class="inl cap">Count <input id="rdN" type="number" value="1" min="1" max="60"></label>
      <label class="inl cap">Function <select id="rdFn"><option value="0">default</option><option>3</option>
        <option>4</option></select></label>
      <button class="b" id="bRead">Read</button></div>
    <div id="rdOut" class="tbl" style="margin-top:10px"></div>
  </div>
  <div class="card">
    <h2>Write one register</h2>
    <p class="note">Writes the raw 16-bit value. Wrong addresses can change controller configuration.</p>
    <div class="row"><label class="inl cap">Address <input id="wrAddr" type="number"></label>
      <label class="inl cap">Raw value <input id="wrVal" type="number"></label>
      <button class="b danger" id="bWrite">Write</button></div>
    <div id="wrOut" class="cap" style="margin-top:10px"></div>
  </div>
  </div>
</section>
</main>

</div>

<div id="dlg"><div class="box" role="dialog" aria-modal="true" aria-labelledby="dlgT">
  <p class="t" id="dlgT"></p>
  <div class="row"><button class="b" id="dlgNo">Cancel</button><button class="b primary" id="dlgYes">OK</button></div>
</div></div>
<script>
const $=id=>document.getElementById(id);
const api=async(p,body)=>{const o=body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)};
  const r=await fetch(p,o);return r.json();};
const f1=v=>v==null?'---.-':Number(v).toFixed(1);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const dur=s=>{s=Math.max(0,Math.round(s||0));const h=Math.floor(s/3600),m=Math.floor(s/60)%60,x=s%60;
  return h?`${h}:${String(m).padStart(2,'0')}:${String(x).padStart(2,'0')}`:`${m}:${String(x).padStart(2,'0')}`;};
const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const spanFor=t=>t<=0?60:t+Math.max(t*0.1,30);
let settings=null,programs=null,status=null,view='main',edit=null,editIdx=0,dirty=false,selRow=-1;

// ---------- dialog (replaces confirm())
function ask(text,ok){return new Promise(res=>{$('dlgT').textContent=text;$('dlgYes').textContent=ok||'OK';
  const d=$('dlg');d.classList.add('open');$('dlgYes').focus();
  const done=v=>{d.classList.remove('open');$('dlgYes').onclick=$('dlgNo').onclick=d.onkeydown=null;res(v);};
  $('dlgYes').onclick=()=>done(true);$('dlgNo').onclick=()=>done(false);
  d.onkeydown=e=>{if(e.key==='Escape')done(false);};});}

// ---------- menu + navigation
const TITLES={main:'Status',prog:'Programs',sys:'System',ctrl:'Controller',tools:'Tools'};
// the sidebar is fixed on wide screens; on narrow ones the menu button slides it in
function menu(open){$('side').classList.toggle('open',open);$('scrim').classList.toggle('open',open);
  $('bMenu').setAttribute('aria-expanded',open);$('bMenu').innerHTML=`<svg><use href="#i-${open?'close':'menu'}"/></svg>`;}
$('bMenu').onclick=()=>menu(!$('side').classList.contains('open'));
$('scrim').onclick=()=>menu(false);
document.addEventListener('keydown',e=>{if(e.key==='Escape')menu(false);});
function show(v){view=v;menu(false);
  document.querySelectorAll('.tile[data-v]').forEach(b=>b.setAttribute('aria-current',b.dataset.v===v));
  document.querySelectorAll('section').forEach(s=>s.classList.toggle('on',s.id==='v-'+v));
  $('title').textContent=TITLES[v];
  // top bar segments: jump to the cards of this page
  const segs=$('segs');segs.innerHTML='';
  if(v!=='main')$('v-'+v).querySelectorAll('.card>h2').forEach(h=>{const b=document.createElement('button');
    b.textContent=h.textContent;b.onclick=()=>h.parentNode.scrollIntoView({behavior:'smooth',block:'start'});segs.appendChild(b);});
  document.querySelector('main').scrollTop=0;
  if(v==='prog')loadPrograms().then(()=>{if(!dirty)loadProgram(programs.sel);});
  if(v==='sys'||v==='ctrl'||v==='tools')loadSettings();if(v==='ctrl')loadParamsInfo();if(v==='main')pollHistory();}
document.querySelectorAll('.tile[data-v]').forEach(b=>b.onclick=()=>show(b.dataset.v));
document.querySelectorAll('[data-go]').forEach(a=>a.onclick=e=>{e.preventDefault();show(a.dataset.go);});
$('linkRow').onclick=()=>{if(status&&!status.comm.ok)ask('No answer from the controller: '+(status.comm.err||'link down'),'Close');};
const tick=()=>{const d=new Date();$('clock').textContent=`${d.getHours()}:${String(d.getMinutes()).padStart(2,'0')}`;};
tick();setInterval(tick,5000);

// ---------- steppers [-][field][+]
document.querySelectorAll('.stepper').forEach(s=>{const inp=s.querySelector('input');
  s.querySelectorAll('button').forEach(b=>b.onclick=()=>{const st=+inp.step||1;
    const v=Math.round(((+inp.value||0)+st*+b.dataset.d)/st)*st;inp.value=+v.toFixed(3);inp.focus();});});

// ---------- chart
// d: {pv,sp,plan (arrays), binSec, mode:'live'|'run', planActive, spActive}
function drawChart(cv,d){
  const dpr=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.clientHeight;cv.width=W*dpr;cv.height=H*dpr;
  const g=cv.getContext('2d');g.scale(dpr,dpr);g.clearRect(0,0,W,H);
  const n=d.plan.length,all=[...d.plan,...d.sp,...d.pv].filter(v=>v!=null);
  let lo=all.length?Math.min(...all):0,hi=all.length?Math.max(...all):100;if(hi-lo<10){lo-=5;hi+=5;}
  const raw=(hi-lo)/5,mag=10**Math.floor(Math.log10(raw)),st=[1,2,2.5,5,10].map(m=>m*mag).find(v=>v>=raw);
  lo=Math.floor(lo/st)*st;hi=Math.ceil(hi/st)*st;
  const L=48,R=14,T=12,B=26,x=i=>L+(W-L-R)*i/(n-1),y=v=>T+(H-T-B)*(1-(v-lo)/(hi-lo));
  g.font='11px '+css('--txt');g.fillStyle=css('--muted');g.strokeStyle=css('--line');g.lineWidth=1;g.setLineDash([2,4]);
  for(let v=lo;v<=hi+st/2;v+=st){const yy=Math.round(y(v))+.5;g.beginPath();g.moveTo(L,yy);g.lineTo(W-R,yy);g.stroke();
    g.textAlign='right';g.fillText(+v.toFixed(1),L-8,yy+4);}
  const span=(n-1)*d.binSec,maxLbl=Math.max(2,Math.floor((W-L-R)/80));
  const ts=[10,30,60,120,300,600,900,1800,3600,7200,10800,14400,21600,43200,86400].find(v=>span/v<=maxLbl)||86400;
  const lab=s=>{const a=Math.abs(s),h=Math.floor(a/3600),m=Math.floor(a%3600/60),x=Math.round(a%60);
    return(s<0?'-':'')+(h?`${h}:${String(m).padStart(2,'0')} h`:(a<600&&x?`${m}:${String(x).padStart(2,'0')}`:`${m} min`));};
  for(let t=0;t<=span+1;t+=ts){const i=t/d.binSec,xx=x(d.mode==='live'?(n-1)-i:i),xr=Math.round(xx)+.5;
    g.beginPath();g.moveTo(xr,T);g.lineTo(xr,H-B);g.stroke();
    g.textAlign=xx>W-R-30?'right':xx<L+30?'left':'center';g.fillText(d.mode==='live'?lab(-t):lab(t),xx,H-8);}
  g.setLineDash([]);
  const line=(arr,col,w,dash)=>{g.strokeStyle=col;g.lineWidth=w;g.setLineDash(dash?[7,5]:[]);g.beginPath();let pen=false;
    arr.forEach((v,i)=>{if(v==null){pen=false;return;}pen?g.lineTo(x(i),y(v)):g.moveTo(x(i),y(v));pen=true;});g.stroke();};
  // program curve with a light fill below, like a process profile
  if(d.plan.some(v=>v!=null)){const gr=g.createLinearGradient(0,T,0,H-B);gr.addColorStop(0,'rgba(212,216,220,.16)');
    gr.addColorStop(1,'rgba(212,216,220,0)');g.fillStyle=gr;g.beginPath();let first=-1,last=-1;
    d.plan.forEach((v,i)=>{if(v==null)return;if(first<0){first=i;g.moveTo(x(i),H-B);}g.lineTo(x(i),y(v));last=i;});
    g.lineTo(x(last),H-B);g.closePath();g.fill();}
  line(d.plan,css('--plan'),2,!d.planActive);line(d.sp,css('--set'),1.5,!d.spActive);line(d.pv,css('--pv'),2.5,false);
  g.setLineDash([]);
}

function planOf(steps,start,N){const tot=steps.reduce((a,s)=>a+s.r+s.h,0);const sp=spanFor(tot),bin=sp/(N-1);
  const at=t=>{let prev=start;for(const s of steps){if(t<s.r)return prev+(s.t-prev)*t/s.r;t-=s.r;if(t<s.h)return s.t;t-=s.h;prev=s.t;}return prev;};
  return{plan:Array.from({length:N},(_,i)=>i*bin<=tot?at(i*bin):null),binSec:bin,total:tot};}

// ---------- main view
async function loadPrograms(){programs=await api('/api/programs');fillProgSelect($('mainProg'),programs.sel);
  if(view==='prog')fillProgSelect($('progSel'),editIdx);return programs;}
function fillProgSelect(sel,cur){if(!programs)return;const keep=document.activeElement===sel;
  const html=programs.programs.map((p,i)=>`<option value="${i}">${i+1} ${esc(p.name)}${p.steps?` (${p.steps} steps, ${dur(p.total)})`:' (empty)'}</option>`).join('');
  if(sel.dataset.h!==html&&!keep){sel.innerHTML=html;sel.dataset.h=html;}if(!keep)sel.value=String(cur);}

// step boxes above the chart: one box per ramp / hold, number badge, active phase highlighted
function renderStrip(st){
  const el=$('strip');el.querySelectorAll('div').forEach(d=>d.remove());
  const steps=st.active&&runSteps?runSteps:(selSteps||[]);
  const parts=[];steps.forEach((s,i)=>[['ramp',s.r],['hold',s.h]].forEach(([k,sec])=>{if(sec>0)parts.push({k,sec,i,s});}));
  const tot=parts.reduce((a,p)=>a+p.sec,0)||1;
  parts.forEach(p=>{const d=document.createElement('div');d.className=p.k;
    d.style.flexGrow=p.sec/tot;
    d.innerHTML=`${p.k==='ramp'?'→ '+esc(p.s.t):'hold'}<br>${dur(p.sec)}<b>${p.i+1}</b>`;
    d.title=`Step ${p.i+1}: ${p.k} ${dur(p.sec)}, target ${p.s.t}`;
    if(st.active&&st.step===p.i&&st.phase===p.k)d.classList.add('cur');
    el.insertBefore(d,$('marker'));});
}
let selSteps=null,selStepsIdx=-1,runSteps=null,runStepsIdx=-1;
async function stepsOf(i){return(await api('/api/program?i='+i)).steps;}

function meter(pct){const m=$('meter');if(!m.children.length)m.innerHTML='<i></i>'.repeat(20);
  [...m.children].forEach((c,i)=>{c.className=pct!=null&&pct>i*5+2.5?(i>=17?'on hot':'on'):'';});}

async function pollStatus(){
  try{status=await api('/api/status');}catch(e){$('linkRow').className='bad';$('linkTxt').textContent='Device not reachable';return;}
  const s=status,r=s.run,active=r.state==='running'||r.state==='paused';
  $('linkRow').className=s.comm.ok?'ok':'bad';$('linkTxt').textContent=s.comm.ok?'Controller link OK':'No answer from the controller';
  $('sideNet').textContent=s.net;$('fw').textContent=s.fw?'v'+s.fw:'';
  $('pv').textContent=s.pv!=null?f1(s.pv):(s.comm.ok&&s.pvFlag!=='ok'?s.pvFlag:'---.-');
  $('sp').textContent=f1(s.spEff);
  let stTxt=r.state[0].toUpperCase()+r.state.slice(1)+(r.soakWait?' (waiting for band)':'');
  if(!active&&s.manual.on)stTxt='Manual';
  $('state').textContent=stTxt;
  $('stateCell').className=!active&&s.manual.on?'st-manual':(r.state!=='idle'?'st-'+r.state:'');
  $('manTile').classList.toggle('on',s.manual.on);
  $('step').textContent=active?`${r.step+1} of ${r.steps}, ${r.phase} to ${f1(r.target)}`:'-';
  $('phLeft').textContent=active?dur(r.phaseDur-r.phaseEl):'-';
  $('totLeft').textContent=active?`${dur(r.total-r.elapsed)} (${r.name})`:`${dur(s.plannedTotal)} planned (${s.selName})`;
  $('out').textContent=s.out!=null?Math.round(s.out)+' %':'-';meter(s.out);
  $('mode').textContent=s.mode||'-';
  $('spWarn').classList.toggle('hide',s.spConfigured);
  $('bStart').lastChild.textContent=r.state==='paused'?'Resume':'Start';
  $('bStart').disabled=r.state==='running';$('bPause').disabled=r.state!=='running';
  $('bSkip').disabled=!active;$('bStop').disabled=!active;$('mainProg').disabled=active;
  $('manState').textContent=s.manual.on?(s.manual.synced?'on':'writing...'):'off';
  $('manState').classList.toggle('on',s.manual.on);
  if(!$('manSw').busy){$('manSw').checked=s.manual.on;$('manSw').disabled=active&&!s.manual.on;}
  if(document.activeElement!==$('manSp'))$('manSp').value=s.manual.sp;
  if(programs&&programs.sel!==s.sel){programs.sel=s.sel;fillProgSelect($('mainProg'),s.sel);}
  const err=!s.comm.ok||((active||s.manual.on)&&!r.spSynced);$('msg').className=err?'bad':'';
  $('msg').textContent=err?s.comm.err:(r.msg+(active?` (setpoint writes this run: ${r.spWrites})`:''));
  if(view==='main'){
    if(selStepsIdx!==s.sel||pollStatus.n++%10===0){selStepsIdx=s.sel;selSteps=await stepsOf(s.sel);}
    if(active&&runStepsIdx!==r.prog){runStepsIdx=r.prog;runSteps=r.prog>=0?await stepsOf(r.prog):null;}
    if(!active)runStepsIdx=-1;
    renderStrip({active,step:r.step,phase:r.phase});
    // time marker inside the active box
    const m=$('marker'),cur=$('strip').querySelector('.cur');m.style.display=cur?'block':'none';
    if(cur){const f=r.phaseDur>0?Math.min(1,r.phaseEl/r.phaseDur):0;m.style.left=(cur.offsetLeft+cur.offsetWidth*f-1)+'px';
      $('markTag').textContent=dur(r.elapsed);}
  }
}
pollStatus.n=0;

async function pollHistory(){
  if(view!=='main')return;
  try{const h=await api('/api/history');drawChart($('chart'),h);
    $('chartSpan').textContent=h.mode==='live'?`Last ${dur(h.span)}. Preview of “${h.name}”: ${dur(h.planTotal)} over the full width`
      :`“${h.name}”: ${dur(h.elapsed)} of ${dur(h.planTotal)}`;
  }catch(e){}
}
const cmd=(c,extra)=>api('/api/cmd',Object.assign({cmd:c},extra||{})).then(pollStatus);
$('bStart').onclick=()=>status&&status.run.state==='paused'?cmd('resume'):cmd('start',{prog:+$('mainProg').value});
$('bPause').onclick=()=>cmd('pause');
$('bSkip').onclick=async()=>{if(await ask('Skip to the next phase?','Skip'))cmd('skip');};
$('bStop').onclick=async()=>{if(await ask('Stop the program? The end action from the Controller settings is applied.','Stop'))cmd('stop');};
$('mainProg').onchange=async()=>{await api('/api/select',{i:+$('mainProg').value});await loadPrograms();pollStatus();pollHistory();};
$('bManSet').onclick=async()=>{await api('/api/manual',{sp:+$('manSp').value});$('manSp').blur();pollStatus();};
$('manSp').onkeydown=e=>{if(e.key==='Enter')$('bManSet').click();};
$('manSw').onchange=async()=>{const sw=$('manSw');sw.busy=true;
  try{if(sw.checked)await api('/api/manual',{sp:+$('manSp').value,on:true});
    else if(await ask('Switch manual mode off? The controller outputs are switched off.','Switch off'))await api('/api/manual',{on:false});
    else sw.checked=true;}
  finally{setTimeout(()=>{sw.busy=false;pollStatus();},600);}};

// ---------- program editor
const unitOf=sec=>sec>0&&sec%60!==0?'s':'min';
async function loadProgram(i){edit=await api('/api/program?i='+i);editIdx=i;dirty=false;selRow=-1;
  fillProgSelect($('progSel'),i);$('progName').value=edit.name;renderRows();$('progMsg').textContent='';}
function timeCell(sec,label){const u=unitOf(sec);const v=u==='min'?sec/60:sec;
  return`<span class="inl"><input type="number" min="0" step="any" value="${v}" aria-label="${label}">
    <select aria-label="${label} unit"><option ${u==='min'?'selected':''}>min</option><option ${u==='s'?'selected':''}>s</option></select></span>`;}
function renderRows(){const tb=$('progRows');tb.innerHTML='';
  edit.steps.forEach((s,i)=>{const tr=document.createElement('tr');if(i===selRow)tr.className='sel';
    tr.innerHTML=`<td class="n">${i+1}</td>
      <td><input type="number" step="0.1" min="${edit.spMin}" max="${edit.spMax}" value="${s.t}" aria-label="Step ${i+1} target"></td>
      <td>${timeCell(s.r,`Step ${i+1} ramp`)}</td><td>${timeCell(s.h,`Step ${i+1} hold`)}</td>
      <td style="white-space:nowrap"><button class="sq" title="Move up" ${i?'':'disabled'}>↑</button>
        <button class="sq" title="Move down" ${i<edit.steps.length-1?'':'disabled'}>↓</button>
        <button class="sq" title="Insert a step below">+</button>
        <button class="sq danger" title="Remove step">✕</button></td>`;
    tr.querySelectorAll('input,select').forEach(el=>{el.oninput=()=>{readRows();markDirty();};
      el.onfocus=()=>{selRow=i;tb.querySelectorAll('tr').forEach((t,k)=>t.classList.toggle('sel',k===i));}});
    const [bu,bd,bi,bx]=tr.querySelectorAll('button');
    bu.onclick=()=>moveStep(i,-1);bd.onclick=()=>moveStep(i,1);bi.onclick=()=>addStep(i+1);bx.onclick=()=>{edit.steps.splice(i,1);selRow=Math.min(i,edit.steps.length-1);markDirty();renderRows();};
    tb.appendChild(tr);});
  if(!edit.steps.length)tb.innerHTML='<tr><td colspan="5" class="cap">No steps yet. Use “Add step”.</td></tr>';
  $('bAddStep').disabled=edit.steps.length>=edit.maxSteps;previewProgram();}
function readRows(){edit.steps=[...$('progRows').children].filter(tr=>tr.querySelector('input')).map(tr=>{
  const q=tr.querySelectorAll('input'),u=tr.querySelectorAll('select');
  const sec=(k)=>Math.max(0,Math.round(+q[k].value*(u[k-1].value==='min'?60:1)));
  return{t:+q[0].value,r:sec(1),h:sec(2)};});edit.name=$('progName').value;}
function markDirty(){dirty=true;$('progMsg').textContent='Unsaved changes';previewProgram();}
function moveStep(i,d){const j=i+d;if(j<0||j>=edit.steps.length)return;const a=edit.steps;[a[i],a[j]]=[a[j],a[i]];selRow=j;markDirty();renderRows();}
function addStep(at){if(edit.steps.length>=edit.maxSteps)return;const ref=edit.steps[Math.min(at,edit.steps.length)-1];
  const s=ref?{...ref}:{t:Math.min(Math.max(100,edit.spMin),edit.spMax),r:600,h:0};edit.steps.splice(at,0,s);selRow=at;markDirty();renderRows();}
function previewProgram(){const start=status&&status.pv!=null?status.pv:20;const p=planOf(edit.steps,start,300);
  $('progTotal').textContent=`${edit.steps.length}/${edit.maxSteps} steps, total ${dur(p.total)}. Preview starts at ${f1(start)}.`;
  drawChart($('progChart'),{plan:p.plan,sp:[],pv:[],binSec:p.binSec,mode:'run',planActive:true});}
$('bAddStep').onclick=()=>addStep(selRow>=0?selRow+1:edit.steps.length);
$('progName').oninput=()=>{edit.name=$('progName').value;markDirty();};
$('progSel').onchange=async()=>{const i=+$('progSel').value;
  if(dirty&&!await ask(`Discard the unsaved changes to “${edit.name}”?`,'Discard')){$('progSel').value=String(editIdx);return;}
  await api('/api/select',{i});await loadProgram(i);loadPrograms();};
$('bSaveProg').onclick=async()=>{readRows();const r=await api('/api/program',{i:editIdx,name:edit.name,steps:edit.steps});
  if(r.err){$('progMsg').textContent=r.err;return;}
  edit=r;dirty=false;renderRows();$('progName').value=edit.name;await loadPrograms();
  $('progMsg').textContent=status&&status.run.prog===editIdx&&['running','paused'].includes(status.run.state)?
    'Saved. The running program keeps its old steps until the next start.':'Saved.';};
$('bRevertProg').onclick=()=>loadProgram(editIdx);

// ---------- settings (switches are checkboxes, the API uses booleans)
const setv=(el,v)=>{if(el.type==='checkbox')el.checked=!!v;else el.value=String(v);};
const getv=el=>el.type==='checkbox'?el.checked:(v=>v==='true'?true:v==='false'?false:+v)(el.value);
const regKeys=['pv','spEff','out','status','alarm','sp','spLo','spHi','rampRate','ctrlOff','remote','scale','readFn','writeFn','offRaw'];
const topKeys=['profile','slaveId','baud','parity','stopBits','timeoutMs','pollMs','rampMode','minStep','minWriteSec',
  'guaranteedSoak','soakBand','endAction','endSp','lockFront','commLossSec','spMin','spMax'];
async function loadSettings(){settings=await api('/api/settings');
  topKeys.forEach(k=>setv($(k),settings[k]));regKeys.forEach(k=>$('r_'+k).value=settings.reg[k]);
  $('scanFrom').value=settings.scanFrom;$('scanTo').value=settings.scanTo;
  setv($('rotate180'),settings.rotate180);$('brightness').value=settings.brightness;
  $('themePalette').innerHTML=(settings.themes||[]).map((n,i)=>`<option value="${i}">${esc(n)}</option>`).join('');
  $('themePalette').value=String(settings.themePalette);setv($('themeDark'),settings.themeDark);
  $('ssid').value=settings.ssid;$('hostname').value=settings.hostname;
  $('sysInfo').textContent=status?`${status.net}  ·  firmware ${status.fw}`:'';}
function collectSettings(){const o={reg:{}};topKeys.forEach(k=>o[k]=getv($(k)));
  regKeys.forEach(k=>o.reg[k]=+$('r_'+k).value);
  if(o.profile!==settings.profile)delete o.reg; // new type: start from its defaults
  return o;}
$('bSaveConn').onclick=async()=>{await api('/api/settings',collectSettings());$('connMsg').textContent='Saved. The serial link was reopened.';loadSettings();};
$('bDefaults').onclick=async()=>{if(!await ask('Replace all register addresses with the defaults for this controller type?','Replace'))return;
  await api('/api/settings',{profile:+$('profile').value,loadDefaults:true});loadSettings();$('connMsg').textContent='Defaults loaded.';};
$('bSaveSys').onclick=async()=>{await api('/api/settings',{rotate180:$('rotate180').checked,brightness:+$('brightness').value,
  themePalette:+$('themePalette').value,themeDark:$('themeDark').checked});
  $('sysMsg').textContent='Saved.';loadSettings();};
$('bSaveWifi').onclick=async()=>{const o={ssid:$('ssid').value};if($('pass').value||!$('ssid').value)o.pass=$('pass').value;
  if($('apPass').value)o.apPass=$('apPass').value;
  await api('/api/settings',{hostname:$('hostname').value});const r=await api('/api/wifi',o);$('wifiMsg').textContent=r.msg||r.err;};

// ---------- tools
$('bScan').onclick=async()=>{const o=$('scanOut');o.textContent='Scanning, this takes a few seconds...';
  await api('/api/settings',{scanFrom:+$('scanFrom').value,scanTo:+$('scanTo').value});
  const r=await api('/api/mb/findsp',{from:+$('scanFrom').value,to:+$('scanTo').value});
  if(!r.ok){o.textContent=r.err;return;}
  if(!r.candidates.length){o.textContent=`No register in this range holds ${f1(r.reference)}. Try another range.`;return;}
  o.innerHTML=`<p class="cap">Registers holding ${f1(r.reference)}:</p><div class="row">`+r.candidates.map(a=>`<button class="b" data-a="${a}">Use ${a} as setpoint</button>`).join('')+'</div>';
  o.querySelectorAll('button').forEach(b=>b.onclick=async()=>{await api('/api/settings',{reg:{sp:+b.dataset.a}});
    o.textContent=`Setpoint address set to ${b.dataset.a}. Confirm it by changing the setpoint on the controller and reading it here.`;});};
$('bRead').onclick=async()=>{const a=+$('rdAddr').value,n=+$('rdN').value;
  const r=await api(`/api/mb/read?addr=${a}&n=${n}&fn=${$('rdFn').value}`);
  $('rdOut').innerHTML=r.ok?'<table><tr><th>Address</th><th>Raw</th><th>÷10</th><th>Hex</th></tr>'+r.values.map((v,i)=>
    `<tr><td class="n">${a+i}</td><td class="n">${v}</td><td class="n">${(v/10).toFixed(1)}</td><td class="n">0x${(v&0xffff).toString(16).padStart(4,'0')}</td></tr>`).join('')+'</table>':esc(r.err);};
$('bWrite').onclick=async()=>{if(!await ask('Write this raw value to the controller?','Write'))return;
  const r=await api('/api/mb/write',{addr:+$('wrAddr').value,value:+$('wrVal').value});$('wrOut').textContent=r.ok?'Written.':r.err;};

// ---------- controller parameters
async function loadParamsInfo(){const s=settings||await api('/api/settings');
  $('ctrlNote').textContent=s.profile==0?'Reads the PID and alarm parameters of the PMA KS 4x-1. Values are written one at a time; the controller rejects values outside its limits.'
    :'Parameter access is only built in for the PMA KS 4x-1. For other controllers use Tools with the addresses from their manual.';}
$('bReadParams').onclick=async()=>{const tb=$('paramRows');tb.innerHTML='<tr><td colspan="4">Reading...</td></tr>';
  const r=await api('/api/params');tb.innerHTML='';
  r.params.forEach(p=>{const tr=document.createElement('tr');const ro=p.name==='Sw.Op';
    tr.innerHTML=`<td>${esc(p.name)}</td><td>${esc(p.desc)}</td><td>${p.err?esc(p.err):`<input value="${esc(p.value)}" ${ro?'disabled':''}>`}</td><td>${ro||p.err?'':'<button class="b">Write</button>'}</td>`;
    const b=tr.querySelector('button');if(b)b.onclick=async()=>{const v=tr.querySelector('input').value;
      const w=await api('/api/params',{addr:p.addr,scaled:p.scaled,value:v==='off'?'off':+v});b.textContent=w.ok?'Written':'Failed';b.title=w.err||'';};
    tb.appendChild(tr);});
  const ac=$('actions');ac.innerHTML='';r.actions.forEach((l,i)=>{const b=document.createElement('button');b.className='b';b.textContent=l;
    b.onclick=async()=>{if(!await ask(l+'?',l))return;const w=await api('/api/action',{i});$('actMsg').textContent=w.ok?l+': done.':w.err;};ac.appendChild(b);});};

window.addEventListener('beforeunload',e=>{if(dirty){e.preventDefault();e.returnValue='';}});
window.addEventListener('resize',()=>{if(view==='main')pollHistory();else if(view==='prog'&&edit)previewProgram();});
show('main');
loadPrograms().then(pollStatus);
setInterval(pollStatus,1000);setInterval(pollHistory,2000);
</script>
</body>
</html>)HTML";
