/*
  ESP32 Space Farm Kit – SoftAP + REST + PWM + Embedded HTML (cleaned) + Persistent State (NVS)

  Notes:
  - Uses a simpler, robust HTML/CSS block to avoid any layout jumbling on captive-portal browsers.
  - Serves HTML from PROGMEM with raw string literal R"HTML( ... )HTML" (no escaping issues).
  - Persists Red/Blue LED levels and Pump Mode in NVS; restores on refresh or reboot.
  - Adds /api/state so the page restores UI on load.
  - Adds debug prints for each endpoint.

  Pin map (DHT must be on an output-capable GPIO):
    DHTPIN = 27 (NOT 35)
    SOIL_PIN = 34
    RED_LED = 14
    BLUE_LED = 13
    PUMP_PIN = 4
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Edit this to update the wifi

const int uniqueID = 100;   // change this per device

// ---------- Pins ----------
#define DHTPIN   27
#define DHTTYPE  DHT11
#define SOIL_PIN 34
#define RED_LED  14
#define BLUE_LED 13
#define PUMP_PIN 4

// ---------- Wi-Fi AP ----------
String AP_SSID = String("GenexFarmkit_") + uniqueID;
String AP_PASS = String("12345678") + uniqueID;  // password also includes ID
const int   WIFI_TX_PWR = 70; // 0.25 dBm steps (70 ≈ 17.5 dBm). Set 78 for ≈20 dBm.

WebServer server(80);
Preferences prefs;

// ---------- Sensors ----------
DHT dht(DHTPIN, DHTTYPE);
float lastTempC = NAN;
float lastHumidity = NAN;
bool dhtOk = false;

// ---------- LEDC ----------
const int LEDC_FREQ = 5000;
const int LEDC_RES  = 8; // 0..255
const int CH_RED    = 0;
const int CH_BLUE   = 1;

// ---------- Pump logic ----------
enum PumpMode { PM_OFF=0, PM_AUTO=1, PM_ON=2 };
PumpMode pumpMode = PM_OFF;
bool pumpOn = false;
const int SOIL_ON_THR  = 30; // % turn ON below
const int SOIL_OFF_THR = 55; // % turn OFF above
const uint32_t PUMP_MIN_ON_MS = 10UL * 1000UL;
uint32_t pumpLastOnMs = 0;

// ---------- Cadence ----------
uint32_t lastReadMs = 0;
const uint32_t READ_INTERVAL_MS = 2000;

// ---------- Persisted state ----------
uint8_t redLevel  = 0;
uint8_t blueLevel = 0;

// ---------- Utils ----------
int clamp255(int v){ return v<0?0:(v>255?255:v); }
int soilPercentFromADC(int raw){
  const int RAW_DRY = 3000; // tune to your probe
  const int RAW_WET = 1100;
  int pct = map(raw, RAW_DRY, RAW_WET, 0, 100);
  if(pct<0)pct=0; if(pct>100)pct=100; return pct;
}

// ---------- NVS helpers ----------
void loadState(){
  prefs.begin("spacefarm", true);
  redLevel  = prefs.getUChar("red", 0);
  blueLevel = prefs.getUChar("blue", 0);
  pumpMode  = (PumpMode)prefs.getUChar("pump", (uint8_t)PM_OFF);
  prefs.end();
}
void saveRed(uint8_t v){ prefs.begin("spacefarm", false); prefs.putUChar("red", v); prefs.end(); }
void saveBlue(uint8_t v){ prefs.begin("spacefarm", false); prefs.putUChar("blue", v); prefs.end(); }
void savePump(PumpMode m){ prefs.begin("spacefarm", false); prefs.putUChar("pump", (uint8_t)m); prefs.end(); }

// ---------- Embedded HTML ----------
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Genex Space Farm Kit</title>
<style>
  :root{--bg:#0c1220;--card:#141c2c;--muted:#93a7c0;--line:#1f2a44;--accent:#79bfff;--ok:#58d39c;--warn:#ffd35a;--bad:#ff6b6b}
  *{box-sizing:border-box;font-family:system-ui,Segoe UI,Roboto,Arial,sans-serif}
  body{margin:0;background:var(--bg);color:#eaf2ff}
  header{padding:16px 20px;background:#101a2e;border-bottom:1px solid var(--line)}
  h1{margin:0;font-size:18px;font-weight:700}
  .wrap{max-width:960px;margin:0 auto;padding:16px}
  .grid{display:grid;gap:16px;grid-template-columns:1fr}
  @media(min-width:820px){.grid{grid-template-columns:1fr 1fr}}
  .card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px}
  .title{font-size:13px;color:var(--muted);margin-bottom:8px}
  .kpi{display:flex;align-items:center;justify-content:space-between;padding:8px 0;border-bottom:1px dashed var(--line)}
  .kpi:last-child{border-bottom:0}
  .kpi .val{font-size:22px;font-weight:700}
  .row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
  .btn{padding:10px 14px;border-radius:10px;border:1px solid var(--line);background:#17233a;color:#eaf2ff;cursor:pointer}
  .btn.active{outline:2px solid var(--accent)}
  .slider{width:100%}
  .badge{display:inline-block;padding:4px 10px;border-radius:999px;border:1px solid var(--line);color:#cfe6ff}
  canvas{width:100%;height:200px;background:#0c1426;border:1px solid var(--line);border-radius:10px}
</style>
</head>
<body>
<header><h1>🌱 Genex Space Farm Kit</h1></header>
<div class="wrap">
  <div class="grid">
    <div class="card">
      <div class="row" style="justify-content:space-between">
        <div class="title">Live Telemetry</div>
        <div id="sensorOk" class="badge">DHT: checking…</div>
      </div>
      <div class="kpi"><div class="title">Temperature</div><div class="val" id="tempVal">-- °C</div></div>
      <div class="kpi"><div class="title">Humidity</div><div class="val" id="humVal">-- %</div></div>
      <div class="kpi"><div class="title">Soil Moisture</div><div class="val" id="soilVal">-- %</div></div>
    </div>

    <div class="card">
      <div class="title">Trends (last ~2 min)</div>
      <canvas id="chart"></canvas>
    </div>

    <div class="card">
      <div class="title">Red LED (0–255)</div>
      <input id="redSlider" type="range" min="0" max="255" value="0" class="slider">
      <div class="title">Blue LED (0–255)</div>
      <input id="blueSlider" type="range" min="0" max="255" value="0" class="slider">
    </div>

    <div class="card">
      <div class="title">Pump Control</div>
      <div class="row">
        <button class="btn" id="pumpOff">OFF</button>
        <button class="btn" id="pumpAuto">AUTO</button>
        <button class="btn" id="pumpOn">ON</button>
        <span id="pumpStatus" class="badge">Pump: --</span>
      </div>
      <div class="title" style="margin-top:10px">AUTO: ON ≤ 30% • OFF ≥ 55% • min 10s ON</div>
    </div>
  </div>
</div>
<footer>&copy; Genex Space</footer>
<script>
const $ = s=>document.querySelector(s);
const tempEl=$('#tempVal'), humEl=$('#humVal'), soilEl=$('#soilVal'), okEl=$('#sensorOk');
const redSlider=$('#redSlider'), blueSlider=$('#blueSlider');
const pumpOff=$('#pumpOff'), pumpAuto=$('#pumpAuto'), pumpOn=$('#pumpOn'), pumpStatus=$('#pumpStatus');


const canvas=$('#chart'), ctx=canvas.getContext('2d');
let W,H; function resize(){ W=canvas.clientWidth; H=canvas.clientHeight; canvas.width=W; canvas.height=H; } resize(); addEventListener('resize',resize);
const maxPts=60; let histT=[], histH=[], histS=[];
function draw(){ ctx.clearRect(0,0,W,H); ctx.strokeStyle='#1f2a44'; ctx.lineWidth=1; for(let v=0; v<=100; v+=25){ let y=H-(v/100)*H; ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(W,y); ctx.stroke(); ctx.fillStyle='#93a7c0'; ctx.fillText(v,W-20,y-2);} line(histT,0,50,'#79bfff'); line(histH,0,100,'#58d39c'); line(histS,0,100,'#ffd35a'); }
function line(data,yMin,yMax,col){ if(data.length<2)return; ctx.beginPath(); for(let i=0;i<data.length;i++){ let x=i/(maxPts-1)*W; let y=H-((data[i]-yMin)/(yMax-yMin))*H; i?ctx.lineTo(x,y):ctx.moveTo(x,y);} ctx.strokeStyle=col; ctx.lineWidth=2; ctx.stroke(); }


async function jget(u){ const r=await fetch(u,{cache:'no-store'}); return r.json(); }
async function sensors(){ try{ const j=await jget('/api/sensors'); okEl.textContent=j.sensor_ok===false?'DHT: fault':'DHT: OK'; okEl.style.borderColor=j.sensor_ok===false?'#ff6b6b':'#58d39c'; if(j.temp!=null) tempEl.textContent=j.temp.toFixed(1)+' °C'; if(j.humidity!=null) humEl.textContent=j.humidity.toFixed(1)+' %'; soilEl.textContent=(j.soil!=null?j.soil:'--')+' %'; if(j.temp!=null){histT.push(j.temp); if(histT.length>maxPts)histT.shift();} if(j.humidity!=null){histH.push(j.humidity); if(histH.length>maxPts)histH.shift();} if(j.soil!=null){histS.push(j.soil); if(histS.length>maxPts)histS.shift();} draw(); }catch(e){ okEl.textContent='DHT: --'; } }
async function loadState(){ const s=await jget('/api/state'); redSlider.value=s.red; blueSlider.value=s.blue; await fetch('/api/led/red?value='+s.red); await fetch('/api/led/blue?value='+s.blue); await pumpRefresh(); }
async function setRed(v){ await fetch('/api/led/red?value='+v); }
async function setBlue(v){ await fetch('/api/led/blue?value='+v); }
async function pumpSet(m){ await fetch('/api/pump/set?mode='+m); await pumpRefresh(); }
async function pumpRefresh(){ const j=await jget('/api/pump/get'); pumpStatus.textContent='Pump: '+j.status+' ('+j.mode+')'; [pumpOff,pumpAuto,pumpOn].forEach(b=>b.classList.remove('active')); if(j.mode==='OFF')pumpOff.classList.add('active'); if(j.mode==='AUTO')pumpAuto.classList.add('active'); if(j.mode==='ON')pumpOn.classList.add('active'); }


redSlider.addEventListener('input', e=>setRed(e.target.value));
blueSlider.addEventListener('input', e=>setBlue(e.target.value));
pumpOff.addEventListener('click', ()=>pumpSet('OFF'));
pumpAuto.addEventListener('click', ()=>pumpSet('AUTO'));
pumpOn.addEventListener('click', ()=>pumpSet('ON'));


setInterval(sensors,2000); sensors(); loadState();
</script>
</body>
</html>
)HTML";

// ---------- HTTP Handlers ----------
void handleRoot(){
  Serial.println("HTTP /");
  server.sendHeader("Cache-Control","no-store");
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleSensors(){
  float t=dht.readTemperature();
  float h=dht.readHumidity();
  bool ok=!(isnan(t)||isnan(h));
  if(ok){ lastTempC=t; lastHumidity=h; dhtOk=true; } else { dhtOk=false; }
  int soilRaw=analogRead(SOIL_PIN);
  int soilPct=soilPercentFromADC(soilRaw);
  String out="{";
  out += "\"temp\":" + String(isnan(lastTempC)?String("null"):String(lastTempC,1));
  out += ",\"humidity\":" + String(isnan(lastHumidity)?String("null"):String(lastHumidity,1));
  out += ",\"soil\":" + String(soilPct);
  out += ",\"sensor_ok\":"; out += (ok?"true":"false");
  out += "}";
  server.send(200,"application/json",out);
}

void handleSetLed(const String& which){
  if(!server.hasArg("value")){ server.send(400,"application/json","{\"error\":\"missing value\"}"); return; }
  int v=clamp255(server.arg("value").toInt());
  if(which=="red"){ redLevel=v; ledcWrite(CH_RED,v); saveRed(redLevel);} 
  if(which=="blue"){ blueLevel=v; ledcWrite(CH_BLUE,v); saveBlue(blueLevel);} 
  String out=String("{\"ok\":true,\"")+which+"\":"+v+"}";
  server.send(200,"application/json",out);
}

void handlePumpSet(){
  if(!server.hasArg("mode")){ server.send(400,"application/json","{\"error\":\"missing mode\"}"); return; }
  String m=server.arg("mode"); m.toUpperCase();
  if(m=="OFF") pumpMode=PM_OFF; else if(m=="AUTO") pumpMode=PM_AUTO; else if(m=="ON") pumpMode=PM_ON; else { server.send(400,"application/json","{\"error\":\"mode must be OFF|AUTO|ON\"}"); return; }
  savePump(pumpMode);
  server.send(200,"application/json",String("{\"ok\":true,\"mode\":\"")+m+"\"}");
}

void handlePumpGet(){
  String modeStr=(pumpMode==PM_OFF?"OFF":pumpMode==PM_AUTO?"AUTO":"ON");
  String out=String("{\"mode\":\"")+modeStr+"\",\"status\":\""+(pumpOn?"ON":"OFF")+"\"}";
  server.send(200,"application/json",out);
}

void handleStateGet(){
  String out=String("{\"red\":")+redLevel+",\"blue\":"+blueLevel+",\"pump\":"+(int)pumpMode+"}";
  server.send(200,"application/json",out);
}

void handleNotFound(){ server.send(404,"application/json","{\"error\":\"not found\"}"); }

// ---------- Setup / Loop ----------
void setup(){
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  loadState();

  pinMode(PUMP_PIN,OUTPUT); digitalWrite(PUMP_PIN,LOW);
  analogReadResolution(12);
  ledcSetup(CH_RED,LEDC_FREQ,LEDC_RES); ledcSetup(CH_BLUE,LEDC_FREQ,LEDC_RES);
  ledcAttachPin(RED_LED,CH_RED); ledcAttachPin(BLUE_LED,CH_BLUE);
  ledcWrite(CH_RED,redLevel); ledcWrite(CH_BLUE,blueLevel);

  dht.begin();
  Serial.begin(115200); delay(200);
  Serial.println("ESP32 Space Farm – Clean HTML + Persistent State");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID.c_str(), AP_PASS.c_str());
  delay(200);
  esp_wifi_set_max_tx_power(WIFI_TX_PWR);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  server.on("/",HTTP_GET,handleRoot);
  server.on("/api/sensors",HTTP_GET,handleSensors);
  server.on("/api/led/red",HTTP_GET,[](){ Serial.println("HTTP /api/led/red"); handleSetLed("red"); });
  server.on("/api/led/blue",HTTP_GET,[](){ Serial.println("HTTP /api/led/blue"); handleSetLed("blue"); });
  server.on("/api/pump/set",HTTP_GET,[](){ Serial.println("HTTP /api/pump/set"); handlePumpSet(); });
  server.on("/api/pump/get",HTTP_GET,[](){ Serial.println("HTTP /api/pump/get"); handlePumpGet(); });
  server.on("/api/state",HTTP_GET,[](){ Serial.println("HTTP /api/state"); handleStateGet(); });
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

void loop(){
  server.handleClient();

  uint32_t now=millis();
  if(now-lastReadMs>=READ_INTERVAL_MS){
    lastReadMs=now;
    int soilRaw=analogRead(SOIL_PIN);
    int soilPct=soilPercentFromADC(soilRaw);
    if(pumpMode==PM_AUTO){
      if(!pumpOn && soilPct<=SOIL_ON_THR){ pumpOn=true; pumpLastOnMs=now; digitalWrite(PUMP_PIN,HIGH); }
      else if(pumpOn){ bool minOnDone=(now-pumpLastOnMs)>=PUMP_MIN_ON_MS; if(minOnDone && soilPct>=SOIL_OFF_THR){ pumpOn=false; digitalWrite(PUMP_PIN,LOW);} }
    } else if(pumpMode==PM_ON){ if(!pumpOn){ pumpOn=true; digitalWrite(PUMP_PIN,HIGH);} }
    else { if(pumpOn){ pumpOn=false; digitalWrite(PUMP_PIN,LOW);} }
  }
}
