/* ---------- System page ---------- */
// time zone (the device's local time: SD run logs): the device's list, the browser's offset and the current value
async function buildTimeForm(){
  let zones=[]; try{ zones=(await api('/api/timezones')).zones||[]; }catch(e){}
  const b=browserTz(), cur=CFG&&CFG.time?CFG.time.zone:'';
  const o=[['','Not set (UTC)'],...zones.map(z=>[z,z.replace(/_/g,' ')])];
  for(const extra of [b.fixed,cur]) if(extra&&!o.some(x=>x[0]===extra)) o.push([extra,extra+' (no summer time)']);
  mkForm('time',$('sTime'),[{h:'',fields:[
    {k:'time.zone',l:'Time zone',t:'sel',o,h:`For the times in the SD run logs (local time). This browser: ${esc(b.zone||b.fixed)}.`}]}],{title:'Clock',auto:true}); }
/* ---------- releases on the hosted page (spec 9.13): /dl/manifest.json, built by .github/workflows/release.yml ---------- */
// the release page (spec 9.13, 9.22): GitHub Pages, a project page under /TempCtl-Duo (the files: REL_BASE + /dl/…)
const REL_HOST='justr0st.github.io', REL_BASE='https://'+REL_HOST+'/TempCtl-Duo';
const verCmp=(a,b)=>{ const x=String(a||'').split('.'), y=String(b||'').split('.');
  for(let i=0;i<Math.max(x.length,y.length);i++){ const d=(parseInt(x[i],10)||0)-(parseInt(y[i],10)||0); if(d) return d<0?-1:1; } return 0; };
const relDate=d=>{ const t=d?new Date(d):null; return t&&!isNaN(t)?t.toLocaleDateString(undefined,{day:'numeric',month:'short',year:'numeric'}):''; };
async function relManifest(){ const r=await fetch(REL_BASE+'/dl/manifest.json',{cache:'no-store'}); if(!r.ok) throw new Error('HTTP '+r.status); return r.json(); }
// a release file with progress (0..1); the size from the manifest when the server sends none
async function relDownload(url,size,prog){
  const r=await fetch(url,{cache:'no-store'}); if(!r.ok) throw new Error('HTTP '+r.status);
  const tot=+r.headers.get('content-length')||size||0;
  if(!r.body||!r.body.getReader) return r.blob();
  const rd=r.body.getReader(), parts=[]; let got=0;
  for(;;){ const {done,value}=await rd.read(); if(done) break; parts.push(value); got+=value.length; if(tot) prog(Math.min(1,got/tot)); }
  return new Blob(parts,{type:'application/octet-stream'}); }

