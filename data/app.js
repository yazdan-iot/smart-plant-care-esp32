/* ============================================================
   API CONTRACT — implement these on the ESP32
   ------------------------------------------------------------
   GET /api/status
     -> 200 application/json
     {
       "soilPercent": 42,
       "temperature": 26.1,
       "humidity": 41.3,
       "relayOn": false,
       "wateringActive": false,
       "cooldownRemainingMs": 3600000,
       "moistureThreshold": 30,
       "cooldownPeriodMs": 21600000,
       "wateringDurationMs": 5000,
       "decisionIntervalMs": 1800000,
       "lastWateredMsAgo": 5400000,
       "uptimeMs": 123456
     }

   POST /api/settings   (body: application/json)
     {
       "moistureThreshold": 30,
       "cooldownPeriodMs": 21600000,
       "wateringDurationMs": 5000,
       "decisionIntervalMs": 1800000
     }
     -> 200 on success (echo updated status, or just {"ok":true})

   POST /api/water   (no body needed)
     -> triggers a manual watering cycle override
     -> 200 on success
   ============================================================ */

const state = {
  connected:false,
  soilPercent:55, temperature:24.5, humidity:45,
  relayOn:false, wateringActive:false,
  cooldownRemainingMs:0,
  moistureThreshold:30, cooldownPeriodMs:6*3600*1000,
  wateringDurationMs:5000, decisionIntervalMs:30*60*1000,
  lastWateredMsAgo:0, uptimeMs:0
};

let history = [];
const MAX_HISTORY = 40;
let consecutiveFailures = 0;
let userEditingControls = false;

const el = id => document.getElementById(id);

/* ---------- number tween ---------- */
function animateNumber(node, to, decimals){
  const from = parseFloat(node.dataset.raw || node.textContent) || 0;
  const start = performance.now();
  const dur = 650;
  function step(now){
    const t = Math.min(1, (now-start)/dur);
    const eased = 1 - Math.pow(1-t, 3);
    const val = from + (to-from)*eased;
    node.textContent = val.toFixed(decimals);
    if(t<1) requestAnimationFrame(step);
    else node.dataset.raw = to;
  }
  requestAnimationFrame(step);
}

function fmtDuration(ms){
  if(ms<=0) return "0m";
  const totalMin = Math.round(ms/60000);
  const h = Math.floor(totalMin/60);
  const m = totalMin%60;
  if(h>0) return h+"h "+m+"m";
  const totalSec = Math.round(ms/1000);
  if(totalMin>0) return m+"m "+(totalSec%60)+"s";
  return totalSec+"s";
}

function fmtAgo(ms){
  if(ms<60000) return Math.round(ms/1000)+"s ago";
  if(ms<3600000) return Math.round(ms/60000)+"m ago";
  return (ms/3600000).toFixed(1)+"h ago";
}

/* ---------- verdict + plant figure ---------- */
function computeVerdict(){
  if(state.wateringActive) return {text:"Watering now", mood:"watering"};
  const dry = state.soilPercent < state.moistureThreshold;
  if(dry && state.cooldownRemainingMs>0) return {text:"Thirsty — resting", mood:"droopy"};
  if(dry) return {text:"Thirsty", mood:"droopy"};
  if(state.soilPercent < state.moistureThreshold+15) return {text:"Comfortable", mood:"perky"};
  return {text:"Thriving", mood:"perky"};
}

function updatePlantFigure(mood){
  const fig = el('plantFigure');
  fig.classList.remove('perky','droopy','watering');
  if(mood==="watering"){ fig.classList.add('perky','watering'); }
  else fig.classList.add(mood);
  const drop = fig.querySelector('.drop');
  drop.style.opacity = mood==="watering" ? "1" : "0";
}

/* ---------- chart ---------- */
function redrawChart(){
  if(history.length<2) return;
  const w=400,h=150;
  const max=100,min=0;
  const step = w/(MAX_HISTORY-1);
  const offset = MAX_HISTORY-history.length;
  const pts = history.map((d,i)=>{
    const x = (offset+i)*step;
    const y = h - ((d.soil-min)/(max-min))*h;
    return x.toFixed(1)+","+y.toFixed(1);
  });
  el('chartLine').setAttribute('points', pts.join(' '));
  el('chartFill').setAttribute('points', pts.join(' ')+` ${(offset+history.length-1)*step},${h} ${offset*step},${h}`);
}

/* ---------- render ---------- */
function render(){
  animateNumber(el('soilValue'), state.soilPercent, 0);
  animateNumber(el('tempValue'), state.temperature, 1);
  animateNumber(el('humValue'), state.humidity, 0);
  el('vialFill').style.height = Math.max(4,Math.min(100,state.soilPercent))+"%";
  el('uptimeValue').textContent = fmtDuration(state.uptimeMs);

  const verdict = computeVerdict();
  el('verdictText').textContent = verdict.text;
  updatePlantFigure(verdict.mood);

  el('pumpDot').classList.toggle('on', state.relayOn);
  el('pumpText').textContent = state.relayOn ? "Active" : "Idle";
  el('wateringStatus').textContent = state.wateringActive ? "Running" : (state.cooldownRemainingMs>0 ? "Cooldown" : "Idle");
  el('cooldownValue').textContent = state.cooldownRemainingMs>0 ? fmtDuration(state.cooldownRemainingMs) : "Ready";
  el('lastWateredValue').textContent = state.lastWateredMsAgo!=null ? fmtAgo(state.lastWateredMsAgo) : "—";

  if(!userEditingControls){
    el('thresholdSlider').value = state.moistureThreshold;
    el('thresholdReadout').textContent = state.moistureThreshold;
    el('cooldownSlider').value = (state.cooldownPeriodMs/3600000).toFixed(1);
    el('cooldownReadout').textContent = (state.cooldownPeriodMs/3600000).toFixed(1);
    el('durationSlider').value = Math.round(state.wateringDurationMs/1000);
    el('durationReadout').textContent = Math.round(state.wateringDurationMs/1000);
    el('intervalSlider').value = Math.round(state.decisionIntervalMs/60000);
    el('intervalReadout').textContent = Math.round(state.decisionIntervalMs/60000);
  }

  redrawChart();
  el('lastUpdated').textContent = "updated " + new Date().toLocaleTimeString();
}

function pushLog(){
  const list = el('logList');
  const li = document.createElement('li');
  const time = new Date().toLocaleTimeString([], {hour:'2-digit',minute:'2-digit',second:'2-digit'});
  li.innerHTML = `<span class="tag">${time}</span> soil ${state.soilPercent.toFixed(0)}% &middot; temp ${state.temperature.toFixed(1)}&deg;C &middot; humidity ${state.humidity.toFixed(0)}%`;
  list.prepend(li);
  while(list.children.length>1) list.removeChild(list.lastChild);
}

function pushHistory(){
  history.push({soil:state.soilPercent, temp:state.temperature, hum:state.humidity, t:Date.now()});
  if(history.length>MAX_HISTORY) history.shift();
}

/* ---------- connection state ---------- */
function setConnection(live){
  const pill = el('statusPill');
  pill.classList.toggle('live', live);
  pill.classList.toggle('demo', !live);
  el('statusText').textContent = live ? "Connected" : "Demo mode";
  el('demoBanner').classList.toggle('show', !live);
  state.connected = live;
}

/* ---------- demo simulation ---------- */
function simulateStep(){
  const drift = (v,amt,min,max)=>Math.max(min,Math.min(max, v + (Math.random()-0.5)*amt));
  if(state.wateringActive){
    state.soilPercent = Math.min(95, state.soilPercent + 4);
  } else {
    state.soilPercent = drift(state.soilPercent, 2.4, 8, 96);
  }
  state.temperature = drift(state.temperature, 0.4, 18, 32);
  state.humidity = drift(state.humidity, 1.6, 28, 65);

  if(state.wateringActive){
    state.wateringActiveTicks = (state.wateringActiveTicks||0) - 1;
    if(state.wateringActiveTicks<=0){
      state.wateringActive = false;
      state.relayOn = false;
      state.cooldownRemainingMs = state.cooldownPeriodMs;
      state.lastWateredMsAgo = 0;
    }
  } else if(state.soilPercent < state.moistureThreshold && state.cooldownRemainingMs<=0){
    state.wateringActive = true;
    state.relayOn = true;
    state.wateringActiveTicks = Math.max(1, Math.round(state.wateringDurationMs/4000));
  } else if(state.cooldownRemainingMs>0){
    state.cooldownRemainingMs = Math.max(0, state.cooldownRemainingMs - 4000);
    state.lastWateredMsAgo = (state.lastWateredMsAgo||0) + 4000;
  } else {
    state.lastWateredMsAgo = (state.lastWateredMsAgo||0) + 4000;
  }
  state.uptimeMs += 4000;
}

/* ---------- polling ---------- */
async function poll(){
  try{
    const res = await fetch('/api/status', {cache:'no-store', signal: AbortSignal.timeout(2500)});
    if(!res.ok) throw new Error('bad response');
    const data = await res.json();
    Object.assign(state, data);
    setConnection(true);
    consecutiveFailures = 0;
  }catch(e){
    consecutiveFailures++;
    simulateStep();
    setConnection(false);
  }
  pushHistory();
  pushLog();
  render();
}

/* ---------- countdown ticking between polls ---------- */
setInterval(()=>{
  if(state.cooldownRemainingMs>0){
    state.cooldownRemainingMs = Math.max(0, state.cooldownRemainingMs-1000);
    el('cooldownValue').textContent = fmtDuration(state.cooldownRemainingMs);
  }
}, 1000);

/* ---------- controls wiring ---------- */
function bindSlider(sliderId, readoutId, onChange){
  const slider = el(sliderId);
  slider.addEventListener('input', ()=>{
    userEditingControls = true;
    el(readoutId).textContent = slider.value;
    onChange(slider.value);
  });
  slider.addEventListener('pointerup', ()=>{ setTimeout(()=>userEditingControls=false, 3000); });
}
bindSlider('thresholdSlider','thresholdReadout', v=>{});
bindSlider('cooldownSlider','cooldownReadout', v=>{});
bindSlider('durationSlider','durationReadout', v=>{});
bindSlider('intervalSlider','intervalReadout', v=>{});

el('saveBtn').addEventListener('click', async ()=>{
  const payload = {
    moistureThreshold: Number(el('thresholdSlider').value),
    cooldownPeriodMs: Math.round(Number(el('cooldownSlider').value)*3600000),
    wateringDurationMs: Number(el('durationSlider').value)*1000,
    decisionIntervalMs: Number(el('intervalSlider').value)*60000
  };
  const fb = el('saveFeedback');
  try{
    const res = await fetch('/api/settings', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify(payload),
      signal: AbortSignal.timeout(3000)
    });
    if(!res.ok) throw new Error();
    Object.assign(state, payload);
    fb.textContent = "Saved to device";
  }catch(e){
    Object.assign(state, payload);
    fb.textContent = "Saved locally (device unreachable)";
  }
  fb.classList.add('show');
  setTimeout(()=>fb.classList.remove('show'), 2600);
  userEditingControls = false;
});

el('waterNowBtn').addEventListener('click', async ()=>{
  const btn = el('waterNowBtn');
  btn.disabled = true;
  try{
    await fetch('/api/water', {method:'POST', signal: AbortSignal.timeout(3000)});
  }catch(e){
    state.wateringActive = true;
    state.relayOn = true;
    state.wateringActiveTicks = Math.max(1, Math.round(state.wateringDurationMs/4000));
    render();
  }
  setTimeout(()=>btn.disabled=false, 2000);
});

/* ---------- boot ---------- */
poll();
setInterval(poll, 4000);
