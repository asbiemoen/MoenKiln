#pragma once

const char WEB_UI[] = R"rawliteral(
<!DOCTYPE html>
<html lang="no">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Moen Kiln</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#111;color:#eee;font-family:system-ui,sans-serif;padding:12px;max-width:500px;margin:0 auto}
h1{color:#ff7700;font-size:1.2em;margin-bottom:10px}
.card{background:#222;border-radius:10px;padding:14px;margin-bottom:10px}
.bigrow{display:flex;align-items:baseline;gap:10px}
.big{font-size:3.2em;font-weight:700;color:#ff7700;line-height:1.1}
.trend{font-size:2em;font-weight:700;transition:color .5s}
.phase{font-size:.85em;margin-top:6px;transition:color .5s}
.segtl{display:flex;gap:3px;margin-top:10px}
.sg{flex:1;min-width:0;padding:5px 4px;border-radius:5px;background:#333;font-size:.72em;text-align:center;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#aaa;cursor:pointer;transition:filter .15s;-webkit-tap-highlight-color:transparent;position:relative}
.sg:active{filter:brightness(1.3)}
.sg.done{background:#1a4020;color:#55bb55}
.sg.active{background:#7a3a00;color:#ff9933;font-weight:700;box-shadow:0 0 6px #ff7700}
.sg.open::after{content:'';display:block;position:absolute;bottom:-5px;left:50%;transform:translateX(-50%);width:0;height:0;border-left:5px solid transparent;border-right:5px solid transparent;border-top:5px solid #555}
.sg.done.open::after{border-top-color:#2a6030}
.sg.active.open::after{border-top-color:#aa5500}
.segdetail{background:#1a1a1a;border-radius:7px;padding:0;margin-top:8px;overflow:hidden;max-height:0;transition:max-height .25s ease,padding .25s ease}
.segdetail.show{max-height:120px;padding:10px 12px}
.sdtitle{font-size:.78em;font-weight:700;color:#ff9933;margin-bottom:7px}
.sdgrid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:4px}
.sdc{background:#252525;border-radius:5px;padding:6px 8px}
.sclabel{font-size:.68em;color:#666;margin-bottom:2px}
.scval{font-size:.9em;font-weight:700;color:#eee}
.pbar{background:#333;border-radius:6px;height:10px;margin-top:10px;overflow:hidden}
.pfill{background:#ff7700;height:10px;border-radius:6px;width:0%;transition:width .8s}
.pills{display:flex;gap:8px;margin-top:8px;flex-wrap:wrap}
.pill{background:#333;border-radius:6px;padding:3px 10px;font-size:.85em}
.donebox{text-align:center;padding:18px 0 6px;font-size:3em;display:none}
canvas{display:block;width:100%}
label{display:block;font-size:.8em;color:#888;margin-bottom:5px}
select{width:100%;padding:9px;border-radius:7px;border:none;background:#333;color:#eee;font-size:.95em;margin-bottom:8px}
.btns{display:flex;gap:8px}
button{flex:1;padding:11px;border:none;border-radius:7px;font-size:.9em;cursor:pointer;font-weight:600;touch-action:manipulation;transition:transform .08s,filter .08s;-webkit-tap-highlight-color:transparent}
button:active{transform:scale(0.94);filter:brightness(0.75)}
button:disabled{opacity:0.35;cursor:not-allowed;transform:none!important;filter:none!important}
.go{background:#2e7d32;color:#fff}
.stop{background:#b71c1c;color:#fff}
.rst{background:#444;color:#eee}
.inp{width:100%;padding:8px;border-radius:6px;border:none;background:#333;color:#eee;font-size:.88em;margin-bottom:6px;display:block}
.shead{color:#888;font-size:.82em;margin:10px 0 8px}
details summary{cursor:pointer;color:#ff7700;font-size:.95em;touch-action:manipulation}
#overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.8);z-index:100;align-items:center;justify-content:center}
#overlay.show{display:flex}
#modal{background:#222;border-radius:14px;padding:28px 24px;max-width:300px;width:90%;text-align:center}
#modal h2{color:#ff7700;margin-bottom:8px}
#modal .ip{font-size:1.5em;font-weight:700;margin:12px 0 20px;letter-spacing:.05em}
#modal p{color:#888;font-size:.85em;margin-bottom:16px}
</style>
</head>
<body>
<div id="overlay">
  <div id="modal">
    <h2>Moen Kiln</h2>
    <p>Ready. Connect at</p>
    <div class="ip" id="modal-ip"></div>
    <button class="go" style="width:100%;padding:13px" onclick="dismiss()">OK</button>
  </div>
</div>
<h1>Moen Kiln</h1>
<div class="card">
  <div class="bigrow">
    <div class="big" id="T">--°C</div>
    <div class="trend" id="trend">→</div>
  </div>
  <div class="phase" id="ph">---</div>
  <div id="segtl" class="segtl"></div>
  <div id="segdetail" class="segdetail"></div>
  <div class="pbar" style="margin-top:8px"><div class="pfill" id="pg"></div></div>
  <div class="pills">
    <div class="pill">Rate: <b id="rate">--</b>°C/h</div>
    <div class="pill">Duty cycle: <b id="pid">--</b>%</div>
    <div class="pill" id="pill-stgt" style="display:none">Segment target: <b id="stgt">--</b>°C</div>
    <div class="pill" id="pill-seta" style="display:none">Time to target: <b id="seta">--</b></div>
    <div class="pill" id="rem"></div>
    <div class="pill" id="tot"></div>
    <div class="pill" id="et"></div>
    <div class="pill" id="pill-nosensor" style="display:none;background:#5a1a1a;color:#ff6666">&#9888; No sensor</div>
  </div>
  <div class="donebox" id="donebox">👍</div>
</div>
<div class="card" style="padding:8px">
  <canvas id="cv" height="160"></canvas>
  </div>
</div>
<div class="card" id="logcard" style="padding:10px 8px">
  <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
    <span style="color:#ff7700;font-size:.88em;font-weight:700">Log</span>
    <button class="rst" style="flex:0;padding:3px 10px;font-size:.78em" onclick="dlCsv('/api/log.csv','firing')">↓ Full log</button>
    <button class="rst" style="flex:0;padding:3px 10px;font-size:.78em;margin-left:4px" onclick="dlCsv('/api/detail.csv','detail')">↓ Detail</button>
    <button class="rst" style="flex:0;padding:3px 10px;font-size:.78em;margin-left:4px" onclick="refreshLog()">↻</button>
  </div>
  <div id="evtsec" style="margin-bottom:8px;display:none">
    <div style="color:#666;font-size:.75em;margin-bottom:4px">Events</div>
    <div id="evtlist"></div>
  </div>
  <div style="overflow-x:auto;max-height:280px;overflow-y:auto;-webkit-overflow-scrolling:touch">
    <table style="width:100%;border-collapse:collapse;font-size:.78em;min-width:260px">
      <thead><tr style="color:#666;background:#222;position:sticky;top:0">
        <th style="text-align:left;padding:5px 6px;font-weight:normal;border-bottom:1px solid #333">Time</th>
        <th style="text-align:right;padding:5px 6px;font-weight:normal;border-bottom:1px solid #333">Temp</th>
        <th style="text-align:right;padding:5px 6px;font-weight:normal;border-bottom:1px solid #333">SP</th>
        <th style="text-align:right;padding:5px 6px;font-weight:normal;border-bottom:1px solid #333">PID%</th>
        <th style="text-align:center;padding:5px 6px;font-weight:normal;border-bottom:1px solid #333">Relay</th>
        <th style="text-align:left;padding:5px 6px;font-weight:normal;border-bottom:1px solid #333">Segment</th>
      </tr></thead>
      <tbody id="logtbody"><tr><td colspan="6" style="padding:12px 6px;color:#555;text-align:center">No data</td></tr></tbody>
    </table>
  </div>
</div>
<div class="card">
  <label>Firing profile</label>
  <select id="pr">
    <option value="0">Glaze</option>
    <option value="1">Bisque</option>
    <option value="2">Config Test</option>
  </select>
  <input class="inp" id="maxtemp" type="number" min="100" max="1400" placeholder="Max temp °C (default 1300)">
  <div class="btns">
    <button class="go" id="btn-go" onclick="go()">&#9654; Start</button>
    <button class="stop" id="btn-stop" onclick="stp()">&#9632; Stop</button>
    <button class="rst" id="btn-rst" onclick="rst()">&#8635; Reset</button>
  </div>
</div>
<div class="card">
<details>
<summary>Settings</summary>
<div style="margin-top:10px">
<p class="shead">Email (Resend)</p>
<input class="inp" id="eto" type="email" placeholder="To: asbjorn@moenmedia.no">
<input class="inp" id="ecc" type="email" placeholder="CC (optional)">
<input class="inp" id="efrom" type="email" placeholder="From: miln@your-domain.com">
<input class="inp" id="ekey" type="password" placeholder="Resend API key (leave blank to keep existing)">
<button class="go" style="width:100%" onclick="saveSet()">Save settings</button>
</div>
</details>
</div>
<script>
var cv=document.getElementById('cv'),cx=cv.getContext('2d'),T=[],SP=[];
var sc={RAMP:'#ff7700',HOLD:'#44bb44',STOP:'#ff4444',ERR:'#ff4444',DONE:'#44bb44',IDLE:'#666',COOL:'#4499ff'};
var sn={RAMP:'Heating up',HOLD:'Holding temperature',COOL:'Cooling down',DONE:'Complete',IDLE:'Ready',STOP:'Stopped',ERR:'Error'};
var modalDismissed=false,lastFiringId=-1,segsData=[],openSeg=-1,logSegNames=[];
function dlCsv(url,prefix){fetch(url).then(function(r){return r.blob();}).then(function(b){var a=document.createElement('a');a.href=URL.createObjectURL(b);a.download=prefix+'-'+new Date().toISOString().slice(0,10)+'.csv';a.click();URL.revokeObjectURL(a.href);});}
function fmt(s){var h=Math.floor(s/3600),m=Math.floor((s%3600)/60);return h>0?h+'h '+m+'m':m+'m';}
function dismiss(){
  document.getElementById('overlay').classList.remove('show');
  modalDismissed=true;
  fetch('/api/dismiss',{method:'POST'});
}
function poll(){
fetch('/api/status').then(function(r){return r.json();}).then(function(d){
  if(d.firingId!==undefined&&d.firingId!==lastFiringId){
    T=[];SP=[];lastFiringId=d.firingId;
    segsData=[];openSeg=-1;
    document.getElementById('segtl').innerHTML='';
    document.getElementById('segdetail').classList.remove('show');
    document.getElementById('donebox').style.display='none';
    document.getElementById('pg').style.width='0%';
  }

  document.getElementById('T').textContent=d.sensorMissing?'No sensor':d.temp.toFixed(1)+'°C';
  var rateVal=(d.delta*3600);
  document.getElementById('rate').textContent=(rateVal>=0?'+':'')+rateVal.toFixed(0);
  document.getElementById('pid').textContent=d.pid.toFixed(0);

  var tr=document.getElementById('trend');
  if(d.delta>0.005){tr.textContent='↑';tr.style.color='#ff7700';}
  else if(d.delta<-0.005){tr.textContent='↓';tr.style.color='#4499ff';}
  else{tr.textContent='→';tr.style.color='#666';}

  var ph=document.getElementById('ph');
  var label=sn[d.state]||d.state;
  ph.textContent=label+(d.segment?' – '+d.segment:(d.state==='DONE'&&d.profile?' – '+d.profile:''));
  ph.style.color=sc[d.state]||'#888';

  if(d.segNames&&d.segNames.length){
    logSegNames=d.segNames;
    if(d.segs) segsData=d.segs;
    var tl=document.getElementById('segtl');
    if(tl.children.length!==d.segNames.length){
      tl.innerHTML='';
      d.segNames.forEach(function(n,i){
        var el=document.createElement('div');
        el.className='sg';el.textContent=n;el.title=n;
        (function(idx){el.onclick=function(){toggleSeg(idx);};})(i);
        tl.appendChild(el);
      });
    }
    var kids=tl.children;
    for(var i=0;i<kids.length;i++){
      var base='sg'+(i<d.segIdx?' done':i===d.segIdx?' active':'');
      kids[i].className=base+(i===openSeg?' open':'');
    }
  }

  if(d.progress!==undefined)
    document.getElementById('pg').style.width=(d.progress*100).toFixed(0)+'%';

  var inRamp=(d.state==='RAMP');
  var hasTarget=(d.segTarget!==undefined&&(d.state==='RAMP'||d.state==='HOLD'));
  document.getElementById('pill-stgt').style.display=hasTarget?'':'none';
  document.getElementById('pill-seta').style.display=(inRamp&&d.remaining>0)?'':'none';
  if(hasTarget) document.getElementById('stgt').textContent=d.segTarget;
  if(inRamp&&d.remaining>0) document.getElementById('seta').textContent=fmt(d.remaining);

  document.getElementById('rem').textContent=(d.remaining&&d.remaining>0&&!inRamp)?'Segment remaining: '+fmt(d.remaining):'';
  document.getElementById('tot').textContent=(d.totalRemaining&&d.totalRemaining>0)?'Total remaining: '+fmt(d.totalRemaining):'';
  document.getElementById('et').textContent=d.elapsed?fmt(d.elapsed)+' elapsed':'';

  document.getElementById('donebox').style.display=(d.state==='DONE')?'block':'none';

  var active=(d.state==='RAMP'||d.state==='HOLD'||d.state==='COOL');
  document.getElementById('btn-go').disabled=active||(d.sensorMissing===true);
  document.getElementById('pill-nosensor').style.display=d.sensorMissing?'':'none';
  document.getElementById('btn-stop').disabled=!active;
  document.getElementById('btn-rst').disabled=(d.state==='IDLE'||active);

  if(d.showingIP&&!modalDismissed){
    document.getElementById('modal-ip').textContent=window.location.hostname;
    document.getElementById('overlay').classList.add('show');
  }

  T.push(d.temp);SP.push(d.setpoint);
  if(T.length>120){T.shift();SP.shift();}
  draw();
}).catch(function(){});
setTimeout(poll,5000);
}
function draw(){
var w=cv.offsetWidth||300,h=160;
cv.width=w;cv.height=h;
if(T.length<2)return;
var all=T.concat(SP),mn=Math.min.apply(null,all)-20,mx=Math.max.apply(null,all)+20;
if(mx-mn<80){mn-=40;mx+=40;}
var px=function(i){return i/(T.length-1)*w;};
var py=function(v){return h-(v-mn)/(mx-mn)*h;};
cx.clearRect(0,0,w,h);
if(mn<573&&mx>573){
  cx.strokeStyle='#ff3333';cx.setLineDash([3,6]);cx.lineWidth=1;
  cx.beginPath();cx.moveTo(0,py(573));cx.lineTo(w,py(573));cx.stroke();
  cx.fillStyle='#ff3333';cx.font='10px sans-serif';cx.fillText('573°C',3,py(573)-3);
}
cx.strokeStyle='#555';cx.setLineDash([5,5]);cx.lineWidth=1.5;
cx.beginPath();SP.forEach(function(v,i){i?cx.lineTo(px(i),py(v)):cx.moveTo(px(i),py(v));});cx.stroke();
cx.strokeStyle='#ff7700';cx.setLineDash([]);cx.lineWidth=2.5;
cx.beginPath();T.forEach(function(v,i){i?cx.lineTo(px(i),py(v)):cx.moveTo(px(i),py(v));});cx.stroke();
}
function toggleSeg(i){
  var det=document.getElementById('segdetail');
  var kids=document.getElementById('segtl').children;
  if(openSeg===i){
    openSeg=-1;det.classList.remove('show');
    for(var j=0;j<kids.length;j++) kids[j].classList.remove('open');
    return;
  }
  openSeg=i;
  for(var j=0;j<kids.length;j++) kids[j].classList.toggle('open',j===i);
  var s=segsData[i];
  if(!s){det.classList.remove('show');return;}
  var rateStr=s.r>0?s.r+'°C/h':'Free cool';
  var holdStr=s.h>0?s.h+' min':'—';
  det.innerHTML='<div class="sdtitle">'+(kids[i]?kids[i].textContent:'')+'</div>'
    +'<div class="sdgrid">'
    +'<div class="sdc"><div class="sclabel">Target temp</div><div class="scval">'+s.t+'°C</div></div>'
    +'<div class="sdc"><div class="sclabel">Rate</div><div class="scval">'+rateStr+'</div></div>'
    +'<div class="sdc"><div class="sclabel">Hold</div><div class="scval">'+holdStr+'</div></div>'
    +'</div>';
  det.classList.add('show');
}
var evColors={0:'#44bb44',1:'#ff9933',2:'#4499ff',3:'#ff4444',4:'#ff4444',5:'#ff4444',6:'#44bb44',7:'#888'};
function fmtSec(s){var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sc=s%60;return h>0?h+'h '+(m<10?'0':'')+m+'m':m+'m '+(sc<10?'0':'')+sc+'s';}
function refreshLog(){
  fetch('/api/fulllog').then(function(r){return r.json();}).then(function(rows){
    var tb=document.getElementById('logtbody');
    if(!rows.length){tb.innerHTML='<tr><td colspan="6" style="padding:12px 6px;color:#555;text-align:center">No data</td></tr>';return;}
    var html='';
    rows.forEach(function(r,idx){
      var s=r[0],t=r[1],sp=r[2],si=r[3]&0x7F,relay=(r[3]&0x80)?1:0,pid=r[4]||0;
      var seg=logSegNames[si]||('Seg '+(si+1));
      var bg=idx%2?'background:#1a1a1a':'';
      var relCol=relay?'#ff7700':'#444';
      html+='<tr style="'+bg+'">'
        +'<td style="padding:4px 6px;color:#888">'+fmtSec(s)+'</td>'
        +'<td style="padding:4px 6px;text-align:right;color:#ff7700;font-weight:700">'+t+'°</td>'
        +'<td style="padding:4px 6px;text-align:right;color:#555">'+sp+'°</td>'
        +'<td style="padding:4px 6px;text-align:right;color:#aaa">'+pid+'%</td>'
        +'<td style="padding:4px 6px;text-align:center;color:'+relCol+'">'+( relay?'●':'○')+'</td>'
        +'<td style="padding:4px 6px;color:#888">'+seg+'</td>'
        +'</tr>';
    });
    tb.innerHTML=html;
  }).catch(function(){});
  fetch('/api/events').then(function(r){return r.json();}).then(function(evts){
    var sec=document.getElementById('evtsec'),el=document.getElementById('evtlist');
    if(!evts.length){sec.style.display='none';return;}
    sec.style.display='';
    var html='';
    evts.forEach(function(e){
      var col=evColors[e.type]||'#888';
      html+='<div style="display:flex;gap:8px;padding:3px 0;border-bottom:1px solid #1a1a1a;align-items:baseline">'
        +'<span style="color:#555;font-size:.75em;min-width:52px">'+fmtSec(e.sec)+'</span>'
        +'<span style="color:'+col+';font-size:.8em;font-weight:700">'+e.txt+'</span>'
        +'<span style="color:#555;font-size:.75em;margin-left:auto">'+e.temp+'°C</span>'
        +'</div>';
    });
    el.innerHTML=html;
  }).catch(function(){});
}
function go(){var mt=document.getElementById('maxtemp').value;var body='profile='+document.getElementById('pr').value+(mt?'&maxtemp='+parseInt(mt):'');fetch('/api/start',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body}).then(function(r){return r.json();}).then(function(d){if(!d.ok)alert('Cannot start: '+(d.error||'unknown error'));}).catch(function(){});}
function stp(){if(!confirm('Stop the firing?'))return;fetch('/api/stop',{method:'POST'});}
function rst(){fetch('/api/reset',{method:'POST'});}
fetch('/api/settings').then(function(r){return r.json();}).then(function(d){
  document.getElementById('eto').value=d.to||'';
  document.getElementById('ecc').value=d.cc||'';
  document.getElementById('efrom').value=d.from||'';
}).catch(function(){});
function saveSet(){
  var b='to='+encodeURIComponent(document.getElementById('eto').value)
    +'&cc='+encodeURIComponent(document.getElementById('ecc').value)
    +'&from='+encodeURIComponent(document.getElementById('efrom').value)
    +'&key='+encodeURIComponent(document.getElementById('ekey').value);
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b})
    .then(function(){alert('Saved!');});
}
poll();
refreshLog();
setInterval(refreshLog, 300000);
</script>
</body>
</html>
)rawliteral";
