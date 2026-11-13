/* ---------- theme + palette (spec 6: channel colours from the accent) ---------- */
const hex2rgb=h=>{h=h.replace('#','').trim();if(h.length===3)h=h.replace(/./g,c=>c+c);return[0,2,4].map(i=>parseInt(h.substr(i,2),16)/255);};
const rgb2hex=c=>'#'+c.map(v=>Math.round(Math.max(0,Math.min(1,v))*255).toString(16).padStart(2,'0')).join('').toUpperCase();
function rgb2hsl([r,g,b]){const mx=Math.max(r,g,b),mn=Math.min(r,g,b),l=(mx+mn)/2;let h=0,s=0;
  if(mx!==mn){const d=mx-mn;s=l>.5?d/(2-mx-mn):d/(mx+mn);h=mx===r?(g-b)/d+(g<b?6:0):mx===g?(b-r)/d+2:(r-g)/d+4;h*=60;}return[h,s,l];}
function hsl2rgb(h,s,l){const k=n=>(n+h/30)%12,a=s*Math.min(l,1-l);return[0,8,4].map(n=>l-a*Math.max(-1,Math.min(k(n)-3,9-k(n),1)));}
const lum=c=>{const f=v=>v<=.03928?v/12.92:Math.pow((v+.055)/1.055,2.4);const[r,g,b]=c.map(f);return .2126*r+.7152*g+.0722*b;};
const contrast=(a,b)=>{const x=lum(a),y=lum(b);return(Math.max(x,y)+.05)/(Math.min(x,y)+.05);};
function fitC(h,s,l,bg,min,dark){ for(let i=0;i<120;i++){ const c=hsl2rgb(h,s,l); if(contrast(c,bg)>=min||l<=0||l>=1) return[c,l]; l=Math.max(0,Math.min(1,l+(dark?.01:-.01))); } return[hsl2rgb(h,s,l),l]; }
function isDark(){ const t=document.documentElement.dataset.theme; return t?t==='dark':matchMedia('(prefers-color-scheme: dark)').matches; }
let palKey='';
function applyPalette(force){
  const acc=(live&&live.accent)||'#2196F3', dark=isDark(), key=acc+dark; if(key===palKey&&!force) return; palKey=key;
  const st=document.documentElement.style; ['--acc','--acc-ink','--pvB','--spB','--pvT','--spT','--vB','--vT'].forEach(p=>st.removeProperty(p));
  // contrast reference: the frosted card (glass over the backdrop) of the current theme
  const bg=hex2rgb(dark?'#1A2329':'#E4EBEF');
  const [h,s,l]=rgb2hsl(hex2rgb(acc));
  for(const [ch,dh] of [['B',-150],['T',150]]){ const hh=(h+dh+360)%360, [pv,lp]=fitC(hh,s,l,bg,4.5,dark), [sp]=fitC(hh,s,Math.min(1,lp+.12),bg,3,dark);
    st.setProperty('--pv'+ch,rgb2hex(pv)); st.setProperty('--sp'+ch,rgb2hex(sp));
    // lit colour (strips, glows, meters, tab arc; not text): same hue, full saturation, only lightly fitted in light mode
    st.setProperty('--v'+ch,rgb2hex(dark?pv:fitC(hh,1,l,bg,1.6,false)[0])); }
  const [a]=fitC(h,s,l,bg,3,dark); st.setProperty('--acc',rgb2hex(a));
  st.setProperty('--acc-ink',contrast(a,[1,1,1])>=contrast(a,[.06,.08,.09])?'#FFFFFF':'#0F1417');
}
// theme change with a short colour fade (.theming)
let themingT=0;
function fadeTheme(f){ const r=document.documentElement; r.classList.add('theming'); void r.offsetWidth; f(); clearTimeout(themingT); themingT=setTimeout(()=>r.classList.remove('theming'),450); }
const sysDark=()=>matchMedia('(prefers-color-scheme: dark)').matches;
// the switch stores a choice only while it differs from the system setting (otherwise: follow the system)
function setTheme(dark){ fadeTheme(()=>{ const r=document.documentElement;
  if(dark===sysDark()){ delete r.dataset.theme; try{localStorage.removeItem('tcd-theme');}catch(e){} }
  else { r.dataset.theme=dark?'dark':'light'; try{localStorage.setItem('tcd-theme',r.dataset.theme);}catch(e){} }
  applyPalette(true); }); }
$('theme').onchange=e=>setTheme(e.target.checked);
try{ const m=localStorage.getItem('tcd-theme'); if(m==='dark'||m==='light') document.documentElement.dataset.theme=m; }catch(e){}
$('theme').checked=isDark();
matchMedia('(prefers-color-scheme: dark)').addEventListener('change',()=>fadeTheme(()=>{ applyPalette(true); $('theme').checked=isDark(); }));
$('menu').onclick=()=>{const o=$('side').classList.toggle('open');$('menu').setAttribute('aria-expanded',o);};
document.addEventListener('click',e=>{ if($('side').classList.contains('open')&&!e.target.closest('#side,#menu')){$('side').classList.remove('open');$('menu').setAttribute('aria-expanded','false');} });

