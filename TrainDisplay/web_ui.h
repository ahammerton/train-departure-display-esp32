// The settings web page served at /
#pragma once

static const char HTML_INDEX[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Train Display</title>
<style>
:root{--bg:#f4f4f2;--card:#fff;--fg:#1d1d1b;--muted:#6b6b66;--line:#deded9;--accent:#c98a00;--bad:#b3261e;--good:#2e7d32;--board:#111;--amber:#ffb000}
@media (prefers-color-scheme:dark){:root{--bg:#161615;--card:#21211f;--fg:#ececea;--muted:#a3a39e;--line:#363633;--accent:#ffb000;--bad:#ff6b5e;--good:#7bc67e}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:720px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 2px}h2{font-size:16px;margin:0 0 12px}
.sub{color:var(--muted);margin:0 0 16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin:0 0 16px}
.board{background:var(--board);color:var(--amber);font:13px/1.6 ui-monospace,Menlo,monospace;border-radius:6px;padding:10px 12px;overflow-x:auto}
.board div{display:flex;gap:12px;white-space:nowrap}.board .d{flex:1;overflow:hidden;text-overflow:ellipsis}
dl{display:grid;grid-template-columns:max-content 1fr;gap:4px 14px;margin:12px 0 0}dt{color:var(--muted)}dd{margin:0;overflow-wrap:anywhere}
label{display:block;margin:0 0 12px}label span{display:block;font-weight:600;margin-bottom:4px}
label small{display:block;color:var(--muted);font-weight:400}
input[type=text],input[type=password],input[type=number],select{width:100%;padding:8px 10px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);font:inherit}
.check{display:flex;gap:8px;align-items:flex-start}.check input{margin-top:4px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:0 14px}@media (max-width:520px){.grid{grid-template-columns:1fr}}
fieldset{border:0;border-top:1px solid var(--line);margin:8px 0 0;padding:12px 0 0}legend{font-weight:700;padding:0 6px 0 0}
button{font:inherit;font-weight:600;padding:8px 14px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:#1d1d1b}
button:disabled{opacity:.5;cursor:default}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.msg{margin:10px 0 0;min-height:1.4em}.msg.bad{color:var(--bad)}.msg.good{color:var(--good)}
progress{width:100%;height:10px}
.hidden{display:none}
</style></head><body><main>
<h1>Train Departure Display</h1>
<p class="sub" id="sub">Loading…</p>

<section class="card">
  <h2>Now showing</h2>
  <div class="board" id="board"><div><span class="d">…</span></div></div>
  <dl id="status"></dl>
</section>

<section class="card" id="loginCard">
  <h2>Admin login</h2>
  <form id="loginForm" class="row">
    <input type="password" id="loginPass" placeholder="Admin password" autocomplete="current-password" style="flex:1;min-width:180px">
    <button class="primary">Log in</button>
  </form>
  <p class="msg" id="loginMsg"></p>
</section>

<section class="card hidden" id="settingsCard">
  <h2>Settings</h2>
  <form id="settingsForm">
    <fieldset><legend>Departures</legend>
      <div class="grid">
        <label><span>Station</span><input type="text" name="station" maxlength="3" required><small>3-letter code, e.g. PAD</small></label>
        <label><span>Calling at</span><input type="text" name="destinations" maxlength="23"><small>Only trains calling at these, e.g. WAT,CLJ</small></label>
        <label><span>Platform</span><input type="text" name="platform" maxlength="3"><small>Only this platform; blank for all</small></label>
        <label><span>Screen 2 platform</span><input type="text" name="platform2" maxlength="3"><small>For the second display; blank for all</small></label>
        <label><span>Refresh every (seconds)</span><input type="number" name="refreshTime" min="30" max="3600"></label>
        <label><span>Time offset (minutes)</span><input type="number" name="timeOffset" min="-120" max="119"><small>Show the board this far ahead</small></label>
      </div>
      <label class="check"><input type="checkbox" name="numericPlatformsOnly"><span>Numbered platforms only<small>Hide buses and lettered platforms</small></span></label>
      <label class="check"><input type="checkbox" name="individualTimes"><span>Times for each calling point</span></label>
    </fieldset>
    <fieldset><legend>Hours</legend>
      <div class="grid">
        <label><span>Operating hours</span><input type="text" name="operatingHours" maxlength="5" placeholder="e.g. 8-22"><small>Blank for always</small></label>
        <label><span>Out-of-hours name</span><input type="text" name="outOfHoursName" maxlength="40"><small>Shown on the welcome screen</small></label>
        <label><span>Screen off hours</span><input type="text" name="screenBlankHours" maxlength="5" placeholder="e.g. 1-6"><small>Blank for never</small></label>
      </div>
    </fieldset>
    <fieldset><legend>Display</legend>
      <div class="grid">
        <label><span>Rotation</span><select name="rotation"><option value="0">0°</option><option value="2">180°</option></select></label>
        <label><span>Brightness</span><input type="number" name="contrast" min="0" max="255"><small>0-255</small></label>
      </div>
      <label class="check"><input type="checkbox" name="firstBold"><span>First departure in bold</span></label>
      <label class="check"><input type="checkbox" name="showNumbers"><span>Number the departures (1st, 2nd, 3rd)</span></label>
      <label class="check"><input type="checkbox" name="dualScreen"><span>Second display<small>Connected to GPIO 7 (CS), sharing the other wires</small></span></label>
      <div class="grid"><label><span>Debug screen</span><input type="number" name="debug" min="0" max="3600"><small>0 = off, 1 = always, 2 or more = seconds at start-up</small></label></div>
    </fieldset>
    <fieldset><legend>Network and keys</legend>
      <div class="grid">
        <label><span>Wi-Fi network</span><input type="text" name="wifiSsid" maxlength="32" required></label>
        <label><span>Wi-Fi password</span><input type="password" name="wifiPass" maxlength="63" autocomplete="new-password"><small id="wifiPassHint">Leave blank to keep</small></label>
        <label><span>Rail Data Marketplace key</span><input type="password" name="apiKey" maxlength="80" autocomplete="off"><small id="apiKeyHint">Leave blank to keep</small></label>
        <label><span>New admin password</span><input type="password" name="adminPass" maxlength="64" autocomplete="new-password"><small>Leave blank to keep</small></label>
      </div>
      <label class="check"><input type="checkbox" name="wifiOpen"><span>Open Wi-Fi network (no password)</span></label>
    </fieldset>
    <div class="row"><button class="primary">Save</button><small id="saveHint" class="sub" style="margin:0">Changing Wi-Fi restarts the display</small></div>
    <p class="msg" id="saveMsg"></p>
  </form>
</section>

<section class="card hidden" id="actionsCard">
  <h2>Actions</h2>
  <div class="row">
    <button data-act="refresh">Refresh now</button>
    <button data-act="debug">Show debug screen</button>
    <button data-act="restart">Restart</button>
    <button data-act="reset">Reset settings</button>
    <button id="logout">Log out</button>
  </div>
  <p class="msg" id="actMsg"></p>
</section>

<section class="card hidden" id="fwCard">
  <h2>Firmware update</h2>
  <p class="sub" id="fwInfo"></p>
  <form id="fwForm" class="row">
    <input type="file" id="fwFile" accept=".bin" style="flex:1;min-width:180px">
    <button class="primary">Upload</button>
  </form>
  <progress id="fwProg" class="hidden" max="100" value="0"></progress>
  <p class="msg" id="fwMsg"></p>
  <p class="sub">Use <code>TrainDisplay.ino.bin</code> from the build folder. If new firmware can't reach the network within 10 minutes, the display goes back to the previous version.</p>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
let auth=null;try{auth=sessionStorage.getItem('tdd-auth')}catch(e){}
function setMsg(el,text,cls){el.textContent=text||'';el.className='msg'+(cls?' '+cls:'')}
async function api(method,url,body){
  const h={'X-TDD':'1'};if(auth)h.Authorization='Basic '+auth;
  if(body!==undefined)h['Content-Type']='application/json';
  const r=await fetch(url,{method,headers:h,body:body===undefined?undefined:JSON.stringify(body)});
  if(r.status===401){loggedOut();throw new Error('Please log in again')}
  if(!r.ok)throw new Error((await r.text())||('HTTP '+r.status));
  const t=r.headers.get('content-type')||'';return t.includes('json')?r.json():r.text();
}
function esc(s){return String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function status(s){
  if(s==='On time'||s==='Cancelled'||s==='Delayed')return s;return 'Exp '+s;
}
function ago(sec){if(sec<60)return sec+' s ago';if(sec<3600)return Math.round(sec/60)+' min ago';return Math.round(sec/3600)+' h ago'}
function uptime(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'d ':'')+h+'h '+m+'m'}
async function loadStatus(){
  try{
    const s=await (await fetch('/api/status',{cache:'no-store'})).json();
    $('sub').textContent=(s.station||'No data yet')+' · firmware '+s.fw;
    const b=$('board');
    b.innerHTML=s.departures.length?s.departures.map(d=>`<div><span>${esc(d.std)}</span><span class="d">${esc(d.destination)}</span><span>${d.platform?'Plat '+esc(d.platform):''}</span><span>${esc(d.std===d.etd?'On time':status(d.etd))}</span></div>`).join(''):'<div><span class="d">No departures loaded</span></div>';
    const f=s.fetch;
    const rows=[
      ['Time',s.time||'not set yet'],
      ['Network',s.mode==='setup'?'Setup hotspot '+s.ssid:s.ssid+(s.rssi?' ('+s.rssi+' dBm)':'')],
      ['Address',s.ip],
      ['Last update',f.ever_ok?ago(f.age)+(f.ok?'':' (latest try failed: '+f.error+')'):(f.error||'waiting')],
      ['Reply',f.bytes?f.bytes+' bytes in '+f.ms+' ms':'-'],
      ['Up',uptime(s.uptime)],
      ['Display',s.screens+(s.screens>1?' screens':' screen')+', '+s.fps+' fps'],
      ['Memory',Math.round(s.heap/1024)+' KB free, PSRAM '+Math.round(s.psram/1024)+' KB'],
    ];
    $('status').innerHTML=rows.map(r=>`<dt>${r[0]}</dt><dd>${esc(r[1])}</dd>`).join('');
    $('fwInfo').textContent='Running '+s.fw+', built '+s.built+' ('+s.ota+')'+(s.rolled_back?'. A previous update was rolled back.':'');
  }catch(e){$('sub').textContent='Display not reachable'}
}
const form=$('settingsForm');
async function loadSettings(){
  const s=await api('GET','/api/settings');
  for(const [k,v] of Object.entries(s)){
    const el=form.elements[k];if(!el)continue;
    if(el.type==='checkbox')el.checked=!!v;else el.value=v;
  }
  form.elements.wifiPass.value='';form.elements.apiKey.value='';form.elements.adminPass.value='';
  form.elements.wifiOpen.checked=false;
  $('wifiPassHint').textContent=s.wifiPassSet?'Leave blank to keep':'Not set';
  $('apiKeyHint').textContent=s.apiKeySet?'Leave blank to keep':'Not set: no departures until it is';
}
function loggedIn(){
  for(const id of ['settingsCard','actionsCard','fwCard'])$(id).classList.remove('hidden');
  $('loginCard').classList.add('hidden');
}
function loggedOut(){
  auth=null;try{sessionStorage.removeItem('tdd-auth')}catch(e){}
  for(const id of ['settingsCard','actionsCard','fwCard'])$(id).classList.add('hidden');
  $('loginCard').classList.remove('hidden');
}
$('loginForm').onsubmit=async e=>{
  e.preventDefault();
  auth=btoa('admin:'+$('loginPass').value);
  try{await loadSettings();try{sessionStorage.setItem('tdd-auth',auth)}catch(e){}
    $('loginPass').value='';setMsg($('loginMsg'),'');loggedIn();}
  catch(err){auth=null;setMsg($('loginMsg'),err.message==='Please log in again'?'Wrong password':err.message,'bad')}
};
form.onsubmit=async e=>{
  e.preventDefault();
  const body={};
  for(const el of form.elements){
    if(!el.name)continue;
    if(el.type==='checkbox')body[el.name]=el.checked;
    else if(el.type==='number'||el.name==='rotation')body[el.name]=parseInt(el.value,10);
    else body[el.name]=el.value;
  }
  setMsg($('saveMsg'),'Saving…');
  try{
    const r=await api('POST','/api/settings',body);
    if(body.adminPass){auth=btoa('admin:'+body.adminPass);try{sessionStorage.setItem('tdd-auth',auth)}catch(e){}}
    setMsg($('saveMsg'),r.restart?'Saved. The display is restarting; reload this page in a few seconds.':'Saved. Fetching new departures…','good');
    if(!r.restart){await loadSettings();setTimeout(loadStatus,3000)}
  }catch(err){setMsg($('saveMsg'),err.message,'bad')}
};
document.querySelectorAll('[data-act]').forEach(b=>b.onclick=async()=>{
  const act=b.dataset.act;
  if(act==='reset'&&!confirm('Reset all settings to the built-in defaults and restart?'))return;
  if(act==='restart'&&!confirm('Restart the display?'))return;
  try{await api('POST','/api/'+act);setMsg($('actMsg'),{refresh:'Fetching now…',debug:'Showing the debug screen for 30 seconds'}[act]||'Restarting…','good');
    if(act==='refresh')setTimeout(loadStatus,3000)}
  catch(err){setMsg($('actMsg'),err.message,'bad')}
});
$('logout').onclick=loggedOut;
$('fwForm').onsubmit=e=>{
  e.preventDefault();
  const file=$('fwFile').files[0];if(!file){setMsg($('fwMsg'),'Choose the .bin file first','bad');return}
  const fd=new FormData();fd.append('firmware',file,file.name);
  const x=new XMLHttpRequest();x.open('POST','/api/firmware');
  x.setRequestHeader('X-TDD','1');if(auth)x.setRequestHeader('Authorization','Basic '+auth);
  const p=$('fwProg');p.classList.remove('hidden');p.value=0;
  x.upload.onprogress=ev=>{if(ev.lengthComputable)p.value=ev.loaded*100/ev.total};
  x.onload=()=>{if(x.status===200)setMsg($('fwMsg'),'Uploaded. The display is restarting with the new firmware.','good');
    else if(x.status===401){loggedOut()}else setMsg($('fwMsg'),x.responseText||('HTTP '+x.status),'bad')};
  x.onerror=()=>setMsg($('fwMsg'),'Upload failed: connection lost','bad');
  setMsg($('fwMsg'),'Uploading…');x.send(fd);
};
loadStatus();setInterval(loadStatus,15000);
if(auth)loadSettings().then(loggedIn).catch(()=>loggedOut());
</script></body></html>)HTML";
