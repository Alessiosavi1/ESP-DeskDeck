#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <math.h>

const char* ssid_casa = "YOUR-WIFI-SSID";
const char* pass_casa = "YOUR-WIFI-PASSW";
const char* ap_ssid   = "ESP32-Meteo-Config";
const char* ap_pass   = "12345678";

WebServer server(80);

String cittaNome  = "Roma";
String lat        = "41.89";
String lon        = "12.49";

float  temperatura = NAN;
int    weathercode = -1;
float  windspeed_kmh = NAN;
int    humidity = -1;
float  visibility_km = NAN;
String lastTimeISO = "";

unsigned long ultimoAggiornamento = 0;
const unsigned long intervalloMeteo = 600000; // 10 minuti

#define OLED_SDA 8
#define OLED_SCL 9
Adafruit_SSD1306 display(128, 32, &Wire, -1);

enum Modalita { METEO, ORA };
Modalita modalitaAttuale = METEO;

#define NUM_TASTI   4
#define DEBOUNCE_MS 50
const int PIN_TASTI[NUM_TASTI] = {0, 1, 2, 3};

String azioniTasti[NUM_TASTI] = {
  "display_toggle",
  "media_play",
  "media_next",
  "media_prev"
};
String urlTasti[NUM_TASTI] = {"", "", "", ""};

// Eventi tasti (ISR -> loop)
volatile unsigned long inizioPressione[NUM_TASTI] = {};
volatile unsigned long durataPressione[NUM_TASTI]  = {};
volatile bool          eventoRilascio[NUM_TASTI]   = {};
volatile bool          tastoPremenuto[NUM_TASTI]   = {};

// Pulse per UI web
bool lastPressedPulse[NUM_TASTI] = {false,false,false,false};

// Protocollo eventi verso PC
uint32_t eventCounter = 0;

struct BtnEvent {
  uint32_t id;
  uint8_t idx;
  String action;
  String url;
  uint32_t ms;
  uint32_t ts;
};

const int EVENT_Q_MAX = 12;
BtnEvent eventQ[EVENT_Q_MAX];
volatile int qHead = 0;
volatile int qTail = 0;

void queueEvent(uint32_t id, int idx, const String& action, const String& url, unsigned long ms) {
  int nextTail = (qTail + 1) % EVENT_Q_MAX;
  if (nextTail == qHead) qHead = (qHead + 1) % EVENT_Q_MAX; // drop oldest
  eventQ[qTail] = BtnEvent{ id, (uint8_t)idx, action, url, (uint32_t)ms, (uint32_t)millis() };
  qTail = nextTail;
}

// ISR
void IRAM_ATTR isr0(){ if(digitalRead(0)==LOW){inizioPressione[0]=millis();tastoPremenuto[0]=true;}else{durataPressione[0]=millis()-inizioPressione[0];tastoPremenuto[0]=false;eventoRilascio[0]=true;}}
void IRAM_ATTR isr1(){ if(digitalRead(1)==LOW){inizioPressione[1]=millis();tastoPremenuto[1]=true;}else{durataPressione[1]=millis()-inizioPressione[1];tastoPremenuto[1]=false;eventoRilascio[1]=true;}}
void IRAM_ATTR isr2(){ if(digitalRead(2)==LOW){inizioPressione[2]=millis();tastoPremenuto[2]=true;}else{durataPressione[2]=millis()-inizioPressione[2];tastoPremenuto[2]=false;eventoRilascio[2]=true;}}
void IRAM_ATTR isr3(){ if(digitalRead(3)==LOW){inizioPressione[3]=millis();tastoPremenuto[3]=true;}else{durataPressione[3]=millis()-inizioPressione[3];tastoPremenuto[3]=false;eventoRilascio[3]=true;}}

String vistaToString() { return (modalitaAttuale == METEO) ? "METEO" : "ORA"; }

void mostraMessaggio(const String& r1, const String& r2 = "", int s2 = 1) {
  display.clearDisplay();
  display.setTextSize(1); display.setCursor(0,0); display.println(r1);
  display.drawLine(0,10,128,10,SSD1306_WHITE);
  display.setTextSize(s2); display.setCursor(0,15); display.println(r2);
  display.display();
}

void mostraMeteo() {
  display.clearDisplay();
  display.setTextSize(1); display.setCursor(0,0);
  display.println(cittaNome);
  display.drawLine(0,10,128,10,SSD1306_WHITE);
  display.setTextSize(2); display.setCursor(0,15);
  if (isnan(temperatura)) display.println("--.- C");
  else { display.print(String(temperatura,1)); display.println(" C"); }
  display.display();
}

void mostraOra() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    display.clearDisplay(); display.setTextSize(1);
    display.setCursor(0,0); display.println("Ora N/D");
    display.display(); return;
  }
  char bufOra[9], bufData[12];
  strftime(bufOra,  sizeof(bufOra),  "%H:%M:%S", &timeinfo);
  strftime(bufData, sizeof(bufData), "%d/%m/%Y", &timeinfo);
  display.clearDisplay();
  display.setTextSize(1); display.setCursor(0,0); display.println(bufData);
  display.drawLine(0,10,128,10,SSD1306_WHITE);
  display.setTextSize(2); display.setCursor(0,15); display.println(bufOra);
  display.display();
}

void aggiornaDisplay() { (modalitaAttuale == METEO) ? mostraMeteo() : mostraOra(); }

bool trovaCoordinate(const String& citta) {
  mostraMessaggio("Ricerca...", citta);
  HTTPClient http;
  String url = "http://geocoding-api.open-meteo.com/v1/search?name=" + citta + "&count=1&language=it";
  url.replace(" ", "+");
  http.begin(url);
  http.setTimeout(15000);

  bool trovato = false;
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    if (!err && doc["results"].is<JsonArray>() && doc["results"].size() > 0) {
      lat       = doc["results"][0]["latitude"].as<String>();
      lon       = doc["results"][0]["longitude"].as<String>();
      cittaNome = doc["results"][0]["name"].as<String>();
      trovato   = true;
    }
  }
  http.end();
  return trovato;
}

int nearestHourlyIndex(JsonArray times, const String& targetISO) {
  struct tm tmt = {};
  if (!strptime(targetISO.c_str(), "%Y-%m-%dT%H:%M", &tmt)) strptime(targetISO.c_str(), "%Y-%m-%dT%H:%M:%S", &tmt);
  time_t target = mktime(&tmt);
  if (target == (time_t)-1) return -1;

  int bestIdx = -1;
  long bestDiff = LONG_MAX;
  for (int i = 0; i < (int)times.size(); i++) {
    const char* s = times[i];
    if (!s) continue;
    struct tm tmh = {};
    if (!strptime(s, "%Y-%m-%dT%H:%M", &tmh)) strptime(s, "%Y-%m-%dT%H:%M:%S", &tmh);
    time_t ti = mktime(&tmh);
    if (ti == (time_t)-1) continue;
    long diff = labs((long)difftime(ti, target));
    if (diff < bestDiff) { bestDiff = diff; bestIdx = i; }
  }
  return bestIdx;
}

void prendiMeteo() {
  if (WiFi.status() != WL_CONNECTED) return;
  mostraMessaggio("Aggiorno...", cittaNome);

  HTTPClient http;
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + lat +
               "&longitude=" + lon +
               "&current_weather=true&hourly=relativehumidity_2m,visibility&timezone=auto";
  http.begin(url);
  http.setTimeout(15000);

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    if (!err && doc["current_weather"].is<JsonObject>()) {
      temperatura = doc["current_weather"]["temperature"] | NAN;
      weathercode = doc["current_weather"]["weathercode"] | -1;
      windspeed_kmh = doc["current_weather"]["windspeed"] | NAN;
      lastTimeISO = doc["current_weather"]["time"].as<String>();

      humidity = -1;
      visibility_km = NAN;

      if (doc["hourly"].is<JsonObject>() && doc["hourly"]["time"].is<JsonArray>()) {
        JsonArray times = doc["hourly"]["time"].as<JsonArray>();
        int idx = nearestHourlyIndex(times, lastTimeISO);
        if (idx >= 0) {
          if (doc["hourly"]["relativehumidity_2m"].is<JsonArray>()) humidity = doc["hourly"]["relativehumidity_2m"][idx] | -1;
          if (doc["hourly"]["visibility"].is<JsonArray>()) {
            float vis_m = doc["hourly"]["visibility"][idx] | NAN;
            if (!isnan(vis_m)) visibility_km = vis_m / 1000.0f;
          }
        }
      }

      if (modalitaAttuale == METEO) mostraMeteo();
    } else mostraMessaggio("JSON Err", "");
  } else mostraMessaggio("Errore Rete", String(code));
  http.end();
}

void inviaEventoTastoSerial(uint32_t id, int idx, const String& action, const String& url, unsigned long ms) {
  JsonDocument d;
  d["type"] = "btn";
  d["id"] = id;
  d["idx"] = idx;
  d["action"] = action;
  d["url"] = url;
  d["ms"] = ms;
  d["ts"] = millis();
  String out;
  serializeJson(d, out);
  Serial.println(out);
}

void eseguiAzione(int idx, unsigned long durataMs) {
  String az = azioniTasti[idx];

  if (az == "display_toggle") {
    modalitaAttuale = (modalitaAttuale == METEO) ? ORA : METEO;
    aggiornaDisplay();
    return;
  }

  uint32_t id = ++eventCounter;
  String u = (az == "open_site") ? urlTasti[idx] : "";

  inviaEventoTastoSerial(id, idx, az, u, durataMs);
  queueEvent(id, idx, az, u, durataMs);
}

void gestisciTasti() {
  for (int i = 0; i < NUM_TASTI; i++) {
    if (eventoRilascio[i]) {
      eventoRilascio[i] = false;
      if (durataPressione[i] >= DEBOUNCE_MS) {
        lastPressedPulse[i] = true;
        eseguiAzione(i, durataPressione[i]);
      }
    }
  }
}

void handleApiMeteo() {
  JsonDocument doc;
  doc["citta"] = cittaNome;
  doc["lat"] = lat;
  doc["lon"] = lon;

  if (isnan(temperatura)) doc["temp"] = nullptr; else doc["temp"] = temperatura;
  doc["weathercode"] = weathercode;

  if (isnan(windspeed_kmh)) doc["windspeed_kmh"] = nullptr; else doc["windspeed_kmh"] = windspeed_kmh;
  if (humidity < 0) doc["humidity"] = nullptr; else doc["humidity"] = humidity;
  if (isnan(visibility_km)) doc["visibility_km"] = nullptr; else doc["visibility_km"] = visibility_km;

  doc["time"] = lastTimeISO;
  doc["vista_display"] = vistaToString();

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleApiView() {
  JsonDocument doc;
  doc["vista_display"] = vistaToString();
  doc["time"] = lastTimeISO;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

const char* ACTIONS_LIST = R"raw(
display_toggle
media_play
media_next
media_prev
media_vol_up
media_vol_down
media_mute
ctrl_c
ctrl_v
ctrl_z
win_d
screenshot
open_site
)raw";

void handleApiTasti() {
  JsonDocument doc;
  doc["actions_help"] = ACTIONS_LIST;
  for (int i = 0; i < NUM_TASTI; i++) {
    doc["az" + String(i)] = azioniTasti[i];
    doc["url" + String(i)] = urlTasti[i];
    doc["pulse" + String(i)] = lastPressedPulse[i];
    lastPressedPulse[i] = false;
  }
  String json; serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleApiEvents() {
  uint32_t after = 0;
  if (server.hasArg("after")) after = (uint32_t) server.arg("after").toInt();

  JsonDocument doc;
  JsonArray arr = doc["events"].to<JsonArray>();

  int i = qHead;
  while (i != qTail) {
    BtnEvent &e = eventQ[i];
    if (e.id > after) {
      JsonObject o = arr.add<JsonObject>();
      o["id"] = e.id;
      o["idx"] = e.idx;
      o["action"] = e.action;
      o["url"] = e.url;
      o["ms"] = e.ms;
      o["ts"] = e.ts;
    }
    i = (i + 1) % EVENT_Q_MAX;
  }

  doc["now"] = (uint32_t)millis();
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleSetCity() {
  if (!server.hasArg("city")) { server.send(400, "text/plain", "Manca city"); return; }
  String c = server.arg("city");
  server.send(200, "text/plain", "OK");
  if (trovaCoordinate(c)) prendiMeteo();
  ultimoAggiornamento = millis();
}

void handleSetButton() {
  if (!(server.hasArg("idx") && server.hasArg("az"))) { server.send(400, "text/plain", "Errore"); return; }
  int idx = server.arg("idx").toInt();
  if (idx < 0 || idx >= NUM_TASTI) { server.send(400, "text/plain", "idx invalido"); return; }
  azioniTasti[idx] = server.arg("az");
  if (server.hasArg("url")) urlTasti[idx] = server.arg("url"); else urlTasti[idx] = "";
  server.send(200, "text/plain", "OK");
}

void handleHomeMeteo() {
  server.send(200, "text/html", R"rawhtml(
<!DOCTYPE html><html lang="it"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Meteo (ESP32)</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;600;800&display=swap" rel="stylesheet">
<style>
  :root{--card-w:860px;--accent:#60a5fa;--glass:rgba(255,255,255,0.05);--border:rgba(255,255,255,0.08);}
  *{box-sizing:border-box}
  body{margin:0;font-family:Inter,system-ui,Arial;background:#071226;color:#e6eef8;min-height:100vh;}
  #anim{position:fixed;inset:0;width:100vw;height:100vh;z-index:0;pointer-events:none}
  #clouds{position:fixed;inset:0;z-index:1;pointer-events:none;overflow:hidden}
  .cloud{position:absolute;background:linear-gradient(180deg, rgba(255,255,255,0.18), rgba(255,255,255,0.06));
    border-radius:50px;opacity:0.85;filter:blur(8px);box-shadow:0 12px 40px rgba(2,6,23,0.6)}
  .wrap{position:relative;z-index:5;max-width:var(--card-w);margin:0 auto;padding:18px;display:grid;place-items:center;min-height:100vh}
  .card{width:100%;border-radius:18px;padding:22px;background:linear-gradient(180deg, rgba(255,255,255,0.06), rgba(255,255,255,0.03));
    box-shadow:0 18px 60px rgba(2,6,23,0.65);backdrop-filter: blur(12px);border:1px solid var(--border);}
  .topbar{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}
  .brand{display:flex;gap:12px;align-items:center}
  .logo{width:46px;height:46px;border-radius:14px;background:linear-gradient(135deg,var(--accent),#7c3aed);
    display:grid;place-items:center;color:#04293a;font-weight:900}
  .title{font-size:18px;font-weight:900}
  .sub{font-size:12px;color:#9fcbea}
  .navbtn{display:inline-flex;align-items:center;gap:10px;background:rgba(255,255,255,0.07);border:1px solid var(--border);
    padding:10px 14px;border-radius:999px;color:#eaf6ff;text-decoration:none;font-weight:900}
  .navbtn:hover{border-color:rgba(96,165,250,0.35)}
  .navbtn .chev{opacity:.75}
  .searchbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:12px}
  input[type="search"]{background:var(--glass);border:1px solid var(--border);padding:10px 12px;border-radius:999px;color:#eaf6ff;outline:none;min-width:260px}
  button{background:linear-gradient(90deg,#60a5fa,#7dd3fc);border:none;padding:10px 14px;border-radius:999px;color:#04293a;font-weight:900;cursor:pointer}
  .main{display:flex;gap:20px;margin-top:18px;align-items:center}
  .left{flex:1;display:flex;flex-direction:column;gap:10px}
  .temp{font-size:64px;font-weight:900;line-height:1;color:#fff;display:flex;gap:12px;align-items:center}
  .desc{color:#ccecff;font-weight:800}
  .details{display:flex;gap:10px;margin-top:6px;color:#bfe9ff;flex-wrap:wrap}
  .detail{background:rgba(255,255,255,0.03);padding:8px 12px;border-radius:999px;font-size:13px;border:1px solid rgba(255,255,255,0.06)}
  .right{width:320px;display:flex;flex-direction:column;gap:12px;align-items:center}
  .panel{width:100%;background:rgba(255,255,255,0.03);padding:12px;border-radius:14px;border:1px solid rgba(255,255,255,0.06);text-align:center}
  footer{font-size:12px;color:#9fccea;margin-top:14px;opacity:0.9}
  @media (max-width:860px){
    .main{flex-direction:column}
    .right{width:100%}
    .temp{font-size:46px}
    input[type="search"]{min-width:180px}
  }
  .clock{margin-top:10px;font-weight:900;font-size:16px;letter-spacing:.6px;color:#ffffff;}
  .clockSub{font-size:12px;color:#9fcbea;margin-top:6px;}
</style></head>
<body>
<canvas id="anim"></canvas><div id="clouds"></div>
<div class="wrap"><div class="card">
  <div class="topbar">
    <div class="brand">
      <div class="logo">M</div>
      <div>
        <div class="title">Meteo (ESP32)</div>
        <div class="sub">Sito e display usano gli stessi dati</div>
      </div>
    </div>
    <a class="navbtn" href="/tasti">Configura tasti <span class="chev">▾</span></a>
  </div>

  <div class="searchbar">
    <input id="search" type="search" placeholder="Cerca città (es: Roma)" />
    <button id="btnSearch">Cerca</button>
  </div>

  <div class="main">
    <div class="left">
      <div id="location" style="font-size:14px;color:#9fcbea">--</div>
      <div class="temp"><span id="temp">--°C</span> <span id="icon" style="font-size:20px;opacity:0.9"></span></div>
      <div class="desc" id="description">--</div>
      <div class="details">
        <div class="detail" id="humidity">Umidità: --%</div>
        <div class="detail" id="wind">Vento: -- km/h</div>
        <div class="detail" id="visibility">Visibilità: --</div>
      </div>
    </div>
    <div class="right">
      <div class="panel">
        <div style="font-size:13px;opacity:0.85">Ora (realtime)</div>
        <div class="clock" id="clock">--:--:--.--</div>
        <div class="clockSub" id="view">Vista display: --</div>
        <div class="clockSub" style="margin-top:8px;opacity:.95">Aggiornamento meteo</div>
        <div id="updated" style="margin-top:6px;font-weight:900">--</div>
      </div>
      <div class="panel" style="color:#cdeefc">Dati: Open‑Meteo • Proxy: ESP32</div>
    </div>
  </div>

  <footer>Animazioni fullscreen: Sole, Nuvole, Pioggia, Neve, Temporale, Nebbia</footer>
</div></div>

<script>
const el={
  search:document.getElementById('search'),
  btnSearch:document.getElementById('btnSearch'),
  location:document.getElementById('location'),
  temp:document.getElementById('temp'),
  description:document.getElementById('description'),
  humidity:document.getElementById('humidity'),
  wind:document.getElementById('wind'),
  visibility:document.getElementById('visibility'),
  updated:document.getElementById('updated'),
  icon:document.getElementById('icon'),
  view:document.getElementById('view'),
  clock:document.getElementById('clock'),
  cloudsLayer:document.getElementById('clouds'),
  canvas:document.getElementById('anim'),
};

const ctx=el.canvas.getContext('2d');
let W=0,H=0,DPR=1,particles=[],cloudsElems=[],animState={type:null,raf:null};
let last=performance.now(), lastCode=null;

let vistaDisplay = '--';

function resizeCanvas(){
  DPR=Math.max(1,Math.min(2,window.devicePixelRatio||1));
  W=window.innerWidth;H=window.innerHeight;
  el.canvas.width=Math.floor(W*DPR); el.canvas.height=Math.floor(H*DPR);
  el.canvas.style.width='100vw'; el.canvas.style.height='100vh';
  ctx.setTransform(DPR,0,0,DPR,0,0);
}
window.addEventListener('resize', resizeCanvas); resizeCanvas();

el.btnSearch.addEventListener('click', async ()=>{
  const q=el.search.value.trim(); if(!q) return;
  await fetch('/set?city='+encodeURIComponent(q)).catch(()=>{});
  setTimeout(fetchMeteo, 700);
});
el.search.addEventListener('keydown', e=>{ if(e.key==='Enter') el.btnSearch.click(); });

function weatherTextFromCode(code){
  if(code===0) return 'Sereno';
  if(code===1||code===2||code===3) return 'Parzialmente nuvoloso';
  if(code>=45&&code<=48) return 'Nebbia';
  if(code>=51&&code<=57) return 'Pioviggine';
  if(code>=61&&code<=67) return 'Pioggia';
  if(code>=71&&code<=77) return 'Neve';
  if(code>=80&&code<=82) return 'Pioggia intensa';
  if(code>=85&&code<=86) return 'Neve intensa';
  if(code>=95&&code<=99) return 'Temporale';
  return 'Variabile';
}
function emojiFromWeatherCode(code){
  if(code===0) return '☀️';
  if(code===1||code===2) return '🌤️';
  if(code===3) return '☁️';
  if(code>=45&&code<=48) return '🌫️';
  if((code>=51&&code<=67)||(code>=80&&code<=82)) return '🌧️';
  if((code>=71&&code<=77)||(code>=85&&code<=86)) return '❄️';
  if(code>=95&&code<=99) return '⛈️';
  return '🌤️';
}

function clearAnimations(){
  particles=[]; cloudsElems.forEach(c=>c.remove()); cloudsElems=[];
  ctx.clearRect(0,0,W,H);
  animState.type=null;
  if(animState.raf) cancelAnimationFrame(animState.raf);
  animState.raf=null;
}
function startClouds(count=3){
  for(let i=0;i<count;i++){
    const c=document.createElement('div'); c.className='cloud';
    const w=140+Math.random()*260, h=44+Math.random()*80;
    c.style.width=w+'px'; c.style.height=h+'px';
    c.style.top=(30+Math.random()*(H-160))+'px';
    c.style.left=(-w-Math.random()*400)+'px';
    c.style.opacity=0.65+Math.random()*0.30;
    el.cloudsLayer.appendChild(c); cloudsElems.push(c);
    const duration=22000+Math.random()*26000, startOffset=Math.random()*2000;
    (function loopCloud(elem,dur,offset){
      let start=performance.now()+offset;
      function step(t){
        if(!elem.isConnected) return;
        const progress=((t-start)%dur)/dur;
        elem.style.left=(-elem.clientWidth+(W+elem.clientWidth*2)*progress)+'px';
        requestAnimationFrame(step);
      }
      requestAnimationFrame(step);
    })(c,duration,startOffset);
  }
}
function startSun(){
  for(let i=0;i<90;i++){
    particles.push({x:Math.random()*W,y:Math.random()*H*0.6,r:1+Math.random()*5,vx:(Math.random()-0.5)*0.015,vy:(Math.random()-0.5)*0.020,alpha:0.05+Math.random()*0.18,type:'sun'});
  }
  particles.sun={cx:W*0.82,cy:H*0.20,baseR:42,phase:0};
}
function startRain(){
  for(let i=0;i<360;i++){
    particles.push({x:Math.random()*W,y:Math.random()*H,vx:-0.25+Math.random()*0.5,vy:5+Math.random()*7,l:10+Math.random()*20,alpha:0.35+Math.random()*0.65,type:'rain'});
  }
  startClouds(5);
}
function startSnow(){
  for(let i=0;i<220;i++){
    particles.push({x:Math.random()*W,y:Math.random()*H,vx:-0.7+Math.random()*1.4,vy:0.7+Math.random()*2.2,r:1+Math.random()*4,alpha:0.55+Math.random()*0.45,type:'snow'});
  }
  startClouds(4);
}
function startThunder(){ startRain(); particles.flashTimer=0; particles.lightning=false; }
function startFog(){
  for(let i=0;i<70;i++){
    particles.push({x:Math.random()*W,y:Math.random()*H,r:70+Math.random()*220,alpha:0.02+Math.random()*0.09,vx:-0.05+Math.random()*0.12,type:'fog'});
  }
}
function startAnimationForCode(code){
  clearAnimations();
  animState.type=code;
  if(code===0) startSun();
  else if(code===1||code===2||code===3) startClouds(4);
  else if((code>=51&&code<=67)||(code>=80&&code<=82)) startRain();
  else if((code>=71&&code<=77)||(code>=85&&code<=86)) startSnow();
  else if(code>=95&&code<=99) startThunder();
  else if(code>=45&&code<=48) startFog();
  else startClouds(3);
  last=performance.now();
  animState.raf=requestAnimationFrame(animateLoop);
}
function animateLoop(now){
  if(animState.type==null){ animState.raf=null; return; }
  const dt=Math.min(40, now-last); last=now;
  ctx.clearRect(0,0,W,H);

  for(const p of particles){
    if(p.type==='fog'){
      p.x += p.vx*dt;
      const g=ctx.createRadialGradient(p.x,p.y,p.r*0.1,p.x,p.y,p.r);
      g.addColorStop(0,`rgba(200,230,255,${p.alpha})`);
      g.addColorStop(1,'rgba(200,230,255,0)');
      ctx.fillStyle=g;
      ctx.fillRect(p.x-p.r,p.y-p.r,p.r*2,p.r*2);
      if(p.x<-p.r) p.x=W+p.r;
      if(p.x>W+p.r) p.x=-p.r;
    }
  }

  if(particles.sun){
    const s=particles.sun;
    s.phase += dt*0.0012;
    const haloR=s.baseR*(3.2+0.25*Math.sin(s.phase*1.6));
    const halo=ctx.createRadialGradient(s.cx,s.cy,s.baseR*0.2,s.cx,s.cy,haloR);
    halo.addColorStop(0,'rgba(255,245,180,0.35)');
    halo.addColorStop(0.5,'rgba(255,210,120,0.16)');
    halo.addColorStop(1,'rgba(255,210,120,0)');
    ctx.fillStyle=halo;
    ctx.fillRect(s.cx-haloR,s.cy-haloR,haloR*2,haloR*2);

    ctx.save(); ctx.translate(s.cx,s.cy); ctx.rotate(s.phase*0.7);
    for(let i=0;i<12;i++){
      const a=(i/12)*Math.PI*2, rayLen=34+10*Math.sin(s.phase*2+i);
      ctx.beginPath(); ctx.strokeStyle='rgba(255,235,150,0.22)'; ctx.lineWidth=3;
      ctx.moveTo(Math.cos(a)*(s.baseR+10),Math.sin(a)*(s.baseR+10));
      ctx.lineTo(Math.cos(a)*(s.baseR+10+rayLen),Math.sin(a)*(s.baseR+10+rayLen));
      ctx.stroke();
    }
    ctx.restore();

    ctx.beginPath(); ctx.fillStyle='rgba(255,240,165,0.82)';
    ctx.arc(s.cx,s.cy,s.baseR,0,Math.PI*2); ctx.fill();
  }

  for(const p of particles){
    if(p.type==='sun'){
      p.x += p.vx*dt; p.y += p.vy*dt;
      if(p.x<-50) p.x=W+50; if(p.x>W+50) p.x=-50;
      if(p.y<-50) p.y=H*0.6; if(p.y>H*0.7) p.y=0;
      const gr=ctx.createRadialGradient(p.x,p.y,p.r*0.1,p.x,p.y,p.r*10);
      gr.addColorStop(0,`rgba(255,248,190,${p.alpha})`);
      gr.addColorStop(1,'rgba(255,200,90,0)');
      ctx.fillStyle=gr; ctx.fillRect(p.x-p.r*10,p.y-p.r*10,p.r*20,p.r*20);
    }
  }

  for(const p of particles){
    if(p.type==='rain'){
      p.x+=p.vx*dt; p.y+=p.vy*dt;
      ctx.beginPath(); ctx.strokeStyle=`rgba(200,220,255,${p.alpha})`; ctx.lineWidth=1;
      ctx.moveTo(p.x,p.y); ctx.lineTo(p.x-p.vx*3,p.y-p.l); ctx.stroke();
      if(p.y>H+20){ p.y=-10; p.x=Math.random()*W; }
      if(p.x<-50) p.x=W+50;
    } else if(p.type==='snow'){
      p.x+=p.vx*dt*0.6; p.y+=p.vy*dt*0.6;
      ctx.beginPath(); ctx.fillStyle=`rgba(255,255,255,${p.alpha})`;
      ctx.arc(p.x,p.y,p.r,0,Math.PI*2); ctx.fill();
      if(p.y>H+10){ p.y=-10; p.x=Math.random()*W; }
      if(p.x<-50) p.x=W+50; if(p.x>W+50) p.x=-50;
    }
  }

  if(particles.lightning!==undefined){
    particles.flashTimer=(particles.flashTimer||0)-dt;
    if(particles.flashTimer<=0 && Math.random()<0.03){
      particles.lightning=true; particles.flashTimer=1200+Math.random()*2200;
    }
    if(particles.lightning){
      ctx.fillStyle='rgba(255,255,255,0.14)';
      ctx.fillRect(0,0,W,H);
      if(Math.random()<0.06) particles.lightning=false;
    }
  }

  animState.raf=requestAnimationFrame(animateLoop);
}

function pad2(n){ return String(n).padStart(2,'0'); }
function pad3(n){ return String(n).padStart(3,'0'); }
function updateClock50ms(){
  const d = new Date();
  const t = pad2(d.getHours())+":"+pad2(d.getMinutes())+":"+pad2(d.getSeconds())+"."+pad3(d.getMilliseconds());
  el.clock.textContent = t;
  // richiesto: aggiornare la scritta "Vista display" ogni 50ms
  el.view.textContent = "Vista display: " + (vistaDisplay || '--');
}

async function fetchView(){
  try{
    const r = await fetch('/api/view', {cache:'no-store'});
    const d = await r.json();
    if(d && d.vista_display) vistaDisplay = d.vista_display;
  }catch(e){}
}

async function fetchMeteo(){
  try{
    const r=await fetch('/api/meteo',{cache:'no-store'});
    const d=await r.json();
    el.location.textContent=d.citta||'--';
    el.temp.textContent=(d.temp==null)?'--°C':(Math.round(d.temp)+'°C');

    const code=(d.weathercode==null)?-1:d.weathercode;
    el.description.textContent=weatherTextFromCode(code);
    el.icon.textContent=emojiFromWeatherCode(code);

    el.wind.textContent='Vento: '+(d.windspeed_kmh==null?'--':d.windspeed_kmh)+' km/h';
    el.humidity.textContent='Umidità: '+(d.humidity==null?'--':d.humidity)+'%';
    el.visibility.textContent='Visibilità: '+(d.visibility_km==null?'--':Number(d.visibility_km).toFixed(1)+' km');
    el.updated.textContent=d.time?new Date(d.time).toLocaleString():'--';

    if(lastCode!==code){ lastCode=code; startAnimationForCode(code); }
  }catch(e){}
}

fetchMeteo();
fetchView();

setInterval(updateClock50ms, 50);

setInterval(fetchView, 250);

setInterval(fetchMeteo, 8000);
</script>
</body></html>
)rawhtml");
}

void handleTastiPage() {
  server.send(200, "text/html", R"rawhtml(
<!DOCTYPE html><html lang="it"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Configura Tasti (ESP32)</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;600;800&display=swap" rel="stylesheet">
<style>
  :root{
    --bg:#070f1f;
    --bg2:#050b16;
    --glass: rgba(255,255,255,.06);
    --border: rgba(255,255,255,.10);
    --text: #eaf3ff;
    --muted:#a8c7e6;
    --accent:#6ea8ff;
    --accent2:#a78bfa;
    --ok:#2be7c9;
    --warn:#ffd28a;
    --shadow: 0 26px 85px rgba(0,0,0,.55);
  }
  *{box-sizing:border-box}
  body{
    margin:0;font-family:Inter,system-ui,Arial;color:var(--text);min-height:100vh;
    background:
      radial-gradient(1200px 800px at 16% 14%, rgba(110,168,255,.14), transparent 60%),
      radial-gradient(900px 700px at 86% 12%, rgba(167,139,250,.12), transparent 55%),
      linear-gradient(180deg, var(--bg), var(--bg2));
  }
  .wrap{max-width:980px;margin:0 auto;padding:18px;min-height:100vh;display:grid;place-items:center;}
  .shell{
    width:100%;border-radius:22px;padding:18px;background:rgba(255,255,255,.04);
    border:1px solid rgba(255,255,255,.07);box-shadow:var(--shadow);backdrop-filter: blur(18px);
  }
  .top{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;padding:6px 6px 10px 6px;}
  .brand{display:flex;align-items:center;gap:12px}
  .logo{width:46px;height:46px;border-radius:16px;background:linear-gradient(135deg,var(--accent),var(--accent2));
    display:grid;place-items:center;color:#071226;font-weight:900}
  .title{font-weight:900;font-size:18px;letter-spacing:.2px}
  .sub{font-size:12px;color:var(--muted);margin-top:2px}
  .navbtn{
    display:inline-flex;align-items:center;gap:10px;padding:10px 14px;border-radius:999px;
    color:var(--text);text-decoration:none;font-weight:900;border:1px solid rgba(255,255,255,.10);
    background:rgba(255,255,255,.06);backdrop-filter: blur(16px);
  }
  .navbtn:hover{border-color:rgba(110,168,255,.30)}
  .navbtn .dot{width:8px;height:8px;border-radius:999px;background:rgba(110,168,255,.95)}

  .grid{margin-top:12px;display:grid;grid-template-columns:1fr;gap:12px;}
  .card{border-radius:18px;padding:14px;background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.09);backdrop-filter: blur(18px);}
  .hrow{display:flex;align-items:baseline;justify-content:space-between;gap:12px}
  .h1{font-weight:900;letter-spacing:.2px}
  .hint{font-size:12px;color:var(--muted)}

  .btnGrid{margin-top:12px;display:grid;grid-template-columns:1fr 1fr;gap:12px;}
  @media (max-width:760px){.btnGrid{grid-template-columns:1fr}}

  .kcard{border-radius:18px;padding:14px;background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.09);
    backdrop-filter: blur(18px);transition:border-color .15s ease, transform .15s ease;}
  .kcard:hover{transform: translateY(-1px);border-color: rgba(110,168,255,.24);}
  .topRow{display:flex;align-items:center;justify-content:space-between;gap:10px}
  .pill{display:inline-flex;align-items:center;gap:8px;padding:6px 10px;border-radius:999px;font-size:12px;font-weight:900;
    background:rgba(0,0,0,.18);border:1px solid rgba(255,255,255,.10);}
  .name{font-weight:900}
  .dirtyTag{display:none;margin-left:8px;font-size:12px;color:var(--warn);font-weight:900}
  .dirty .dirtyTag{display:inline}

  .lbl{margin-top:12px;font-size:12px;color:var(--muted);letter-spacing:1px;text-transform:uppercase}

  input{
    width:100%;margin-top:8px;padding:12px 12px;border-radius:16px;border:1px solid rgba(255,255,255,.10);
    outline:none;color:var(--text);background:rgba(10,20,40,.28);backdrop-filter: blur(12px);
  }
  input:focus{border-color: rgba(110,168,255,.38);}
  .urlBox{display:none}

  /* custom action picker */
  .picker{
    margin-top:8px;
    display:flex;
    align-items:center;
    justify-content:space-between;
    gap:10px;
    padding:12px 12px;
    border-radius:16px;
    border:1px solid rgba(255,255,255,.10);
    background:rgba(10,20,40,.28);
    cursor:pointer;
    user-select:none;
  }
  .picker:hover{border-color: rgba(110,168,255,.22)}
  .picker .left{display:flex;align-items:center;gap:10px;min-width:0}
  .dotMini{width:8px;height:8px;border-radius:999px;background:rgba(110,168,255,.95)}
  .picker .txt{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-weight:900}
  .picker .chev{opacity:.7}

  .modal{
    position:fixed; inset:0;
    display:none;
    z-index:999;
    background: rgba(0,0,0,.55);
    backdrop-filter: blur(10px);
    padding: 18px;
  }
  .modal.open{display:grid;place-items:center;}
  .sheet{
    width: min(920px, 100%);
    max-height: min(82vh, 740px);
    overflow:hidden;
    border-radius: 22px;
    background: rgba(255,255,255,.06);
    border:1px solid rgba(255,255,255,.10);
    box-shadow: 0 30px 110px rgba(0,0,0,.6);
    backdrop-filter: blur(18px);
  }
  .sheetTop{
    padding: 14px 14px 10px 14px;
    display:flex; align-items:center; justify-content:space-between; gap:10px;
    border-bottom: 1px solid rgba(255,255,255,.10);
  }
  .sheetTop .st{font-weight:900}
  .sheetTop .close{
    border:none; cursor:pointer;
    border-radius:999px;
    padding:10px 12px;
    color:var(--text);
    background: rgba(255,255,255,.06);
    border:1px solid rgba(255,255,255,.10);
  }

  .searchRow{padding: 0 14px 12px 14px;}
  .searchRow input{margin-top:10px}

  .list{
    padding: 10px 10px 14px 10px;
    overflow:auto;
    max-height: calc(82vh - 120px);
    display:grid;
    grid-template-columns: 1fr 1fr;
    gap: 10px;
  }
  @media (max-width:760px){ .list{grid-template-columns:1fr;} }

  .opt{
    border-radius: 18px;
    padding: 12px;
    background: rgba(255,255,255,.05);
    border:1px solid rgba(255,255,255,.10);
    cursor:pointer;
    transition: transform .12s ease, border-color .12s ease;
  }
  .opt:hover{transform: translateY(-1px);border-color: rgba(110,168,255,.26);}
  .opt .l1{font-weight:900}
  .opt .l2{margin-top:6px;font-size:12px;color:var(--muted)}
  .opt.selected{border-color: rgba(110,168,255,.45); box-shadow: 0 0 0 1px rgba(110,168,255,.15) inset;}

  .previewRow{margin-top:10px;display:flex;gap:10px;flex-wrap:wrap;align-items:center;}
  .chip{display:inline-flex;align-items:center;gap:8px;padding:6px 10px;border-radius:999px;font-size:12px;
    border:1px solid rgba(255,255,255,.12);background:rgba(255,255,255,.05);}
  .miniDot{width:7px;height:7px;border-radius:999px;background:rgba(110,168,255,.95)}

  .ctaRow{margin-top:12px;display:flex;gap:10px}
  .btn{
    flex:1;border:none;cursor:pointer;border-radius:16px;padding:12px 14px;font-weight:900;color:#071226;
    background: linear-gradient(90deg, rgba(110,168,255,1), rgba(125,211,252,1));
  }
  .btn:active{transform:scale(.99)}
  .btn.ghost{color:var(--text);background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.10);}

  .pulse{
    border-color: rgba(43,231,201,.55) !important;
    box-shadow: 0 0 26px rgba(43,231,201,.14);
    animation: pulse 420ms ease-out;
  }
  @keyframes pulse{0%{transform:scale(1)}35%{transform:scale(1.01)}100%{transform:scale(1)}}
  footer{margin-top:12px;font-size:12px;color:var(--muted);padding:0 6px 4px 6px;}
</style>
</head>
<body>
<div class="wrap">
  <div class="shell">
    <div class="top">
      <div class="brand">
        <div class="logo">T</div>
        <div>
          <div class="title">Configura Tasti</div>
          <div class="sub">No tendina • picker custom • dark super minimal</div>
        </div>
      </div>
      <a class="navbtn" href="/"><span class="dot"></span> Meteo <span style="opacity:.75">▾</span></a>
    </div>

    <div class="grid">
      <div class="card">
        <div class="hrow">
          <div class="h1">Scelta tasti</div>
          <div class="hint">tap per scegliere azione</div>
        </div>

        <div class="btnGrid">
          <div class="kcard" id="c0">
            <div class="topRow">
              <span class="pill">PIN 0</span>
              <div class="name">Tasto 0 <span class="dirtyTag">• non salvato</span></div>
            </div>

            <div class="lbl">Azione</div>
            <div class="picker" id="pick0" onclick="openPicker(0)">
              <div class="left"><span class="dotMini"></span><div class="txt" id="pickTxt0">--</div></div>
              <div class="chev">▾</div>
            </div>

            <div class="urlBox" id="u0">
              <div class="lbl">URL (solo open_site)</div>
              <input id="url0" type="text" placeholder="https://...">
            </div>

            <div class="previewRow">
              <span class="chip"><span class="miniDot"></span><span id="p0">--</span></span>
            </div>

            <div class="ctaRow">
              <button class="btn" onclick="salva(0)">Salva</button>
              <button class="btn ghost" onclick="annulla(0)">Annulla</button>
            </div>
          </div>

          <div class="kcard" id="c1">
            <div class="topRow">
              <span class="pill">PIN 1</span>
              <div class="name">Tasto 1 <span class="dirtyTag">• non salvato</span></div>
            </div>

            <div class="lbl">Azione</div>
            <div class="picker" id="pick1" onclick="openPicker(1)">
              <div class="left"><span class="dotMini"></span><div class="txt" id="pickTxt1">--</div></div>
              <div class="chev">▾</div>
            </div>

            <div class="urlBox" id="u1">
              <div class="lbl">URL (solo open_site)</div>
              <input id="url1" type="text" placeholder="https://...">
            </div>

            <div class="previewRow">
              <span class="chip"><span class="miniDot"></span><span id="p1">--</span></span>
            </div>

            <div class="ctaRow">
              <button class="btn" onclick="salva(1)">Salva</button>
              <button class="btn ghost" onclick="annulla(1)">Annulla</button>
            </div>
          </div>

          <div class="kcard" id="c2">
            <div class="topRow">
              <span class="pill">PIN 2</span>
              <div class="name">Tasto 2 <span class="dirtyTag">• non salvato</span></div>
            </div>

            <div class="lbl">Azione</div>
            <div class="picker" id="pick2" onclick="openPicker(2)">
              <div class="left"><span class="dotMini"></span><div class="txt" id="pickTxt2">--</div></div>
              <div class="chev">▾</div>
            </div>

            <div class="urlBox" id="u2">
              <div class="lbl">URL (solo open_site)</div>
              <input id="url2" type="text" placeholder="https://...">
            </div>

            <div class="previewRow">
              <span class="chip"><span class="miniDot"></span><span id="p2">--</span></span>
            </div>

            <div class="ctaRow">
              <button class="btn" onclick="salva(2)">Salva</button>
              <button class="btn ghost" onclick="annulla(2)">Annulla</button>
            </div>
          </div>

          <div class="kcard" id="c3">
            <div class="topRow">
              <span class="pill">PIN 3</span>
              <div class="name">Tasto 3 <span class="dirtyTag">• non salvato</span></div>
            </div>

            <div class="lbl">Azione</div>
            <div class="picker" id="pick3" onclick="openPicker(3)">
              <div class="left"><span class="dotMini"></span><div class="txt" id="pickTxt3">--</div></div>
              <div class="chev">▾</div>
            </div>

            <div class="urlBox" id="u3">
              <div class="lbl">URL (solo open_site)</div>
              <input id="url3" type="text" placeholder="https://...">
            </div>

            <div class="previewRow">
              <span class="chip"><span class="miniDot"></span><span id="p3">--</span></span>
            </div>

            <div class="ctaRow">
              <button class="btn" onclick="salva(3)">Salva</button>
              <button class="btn ghost" onclick="annulla(3)">Annulla</button>
            </div>
          </div>
        </div>

        <footer>Tip: se non vedi il pulse verde quando premi un tasto fisico, controlla pin/interrupt/cablaggio.</footer>
      </div>
    </div>
  </div>
</div>

<!-- MODAL PICKER -->
<div class="modal" id="modal" onclick="closePickerIfBackdrop(event)">
  <div class="sheet" onclick="event.stopPropagation()">
    <div class="sheetTop">
      <div class="st" id="sheetTitle">Scegli azione</div>
      <button class="close" onclick="closePicker()">Chiudi</button>
    </div>
    <div class="searchRow">
      <input id="q" type="text" placeholder="Cerca (es: volume, copia, sito)..." oninput="renderList()">
    </div>
    <div class="list" id="list"></div>
  </div>
</div>

<script>
const AZ=[
  {v:"display_toggle",l:"🖥 Cambia Vista Display (locale)", d:"Cambia vista su display ESP (METEO/ORA)"},
  {v:"media_play",    l:"⏯ Play / Pausa", d:"Tasto multimediale Windows"},
  {v:"media_next",    l:"⏭ Traccia Successiva", d:"Tasto multimediale Windows"},
  {v:"media_prev",    l:"⏮ Traccia Precedente", d:"Tasto multimediale Windows"},
  {v:"media_vol_up",  l:"🔊 Volume Su", d:"Tasto multimediale Windows"},
  {v:"media_vol_down",l:"🔉 Volume Giù", d:"Tasto multimediale Windows"},
  {v:"media_mute",    l:"🔇 Muto", d:"Tasto multimediale Windows"},
  {v:"ctrl_c",        l:"📋 Copia (Ctrl+C)", d:"Shortcut Windows"},
  {v:"ctrl_v",        l:"📌 Incolla (Ctrl+V)", d:"Shortcut Windows"},
  {v:"ctrl_z",        l:"↩ Annulla (Ctrl+Z)", d:"Shortcut Windows"},
  {v:"win_d",         l:"🗔 Mostra Desktop (Win+D)", d:"Shortcut Windows"},
  {v:"screenshot",    l:"📷 Screenshot", d:"Win+Shift+S"},
  {v:"open_site",     l:"🌐 Apri Sito Web", d:"Apre un URL sul PC"},
];

const isEditing=[false,false,false,false];
const lastServer={ az:["","","",""], url:["","","",""] };
const selectedAz=["","","",""];

let pickerIdx = -1;

function labelForValue(v){
  const a = AZ.find(x=>x.v===v);
  return a ? a.l : v;
}

function toggleUrl(i){
  const az=selectedAz[i];
  document.getElementById('u'+i).style.display = (az==='open_site') ? 'block' : 'none';
}

function updatePreview(i){
  const az=selectedAz[i];
  const url=(document.getElementById('url'+i).value||"").trim();
  const text = (az==='open_site')
    ? (labelForValue(az) + " → " + (url || "(URL vuoto)"))
    : labelForValue(az);
  document.getElementById('p'+i).textContent = text;
  document.getElementById('pickTxt'+i).textContent = labelForValue(az) || '--';
}

function markDirty(i, dirty){
  const c = document.getElementById('c'+i);
  if(dirty) c.classList.add('dirty');
  else c.classList.remove('dirty');
}

function setSelected(i, az){
  selectedAz[i] = az;
  isEditing[i] = true;
  markDirty(i, true);
  toggleUrl(i);
  updatePreview(i);
}

/* input URL editing */
for(let i=0;i<4;i++){
  const u=document.getElementById('url'+i);
  u.addEventListener('input', ()=>{
    isEditing[i]=true;
    markDirty(i,true);
    updatePreview(i);
  });
  u.addEventListener('focus', ()=>{ isEditing[i]=true; });
}

function applyFromServer(i, az, url){
  lastServer.az[i]=az||'media_play';
  lastServer.url[i]=url||'';
  if(!isEditing[i]){
    selectedAz[i] = lastServer.az[i];
    document.getElementById('url'+i).value = lastServer.url[i];
    toggleUrl(i);
    updatePreview(i);
    markDirty(i,false);
  }
}

async function salva(i){
  const az=selectedAz[i] || 'media_play';
  const url=document.getElementById('url'+i).value.trim();
  await fetch('/setbutton?idx='+i+'&az='+encodeURIComponent(az)+'&url='+encodeURIComponent(url));
  isEditing[i]=false;
  markDirty(i,false);
  await refresh(true);
}

function annulla(i){
  isEditing[i]=false;
  selectedAz[i] = lastServer.az[i] || 'media_play';
  document.getElementById('url'+i).value = lastServer.url[i] || '';
  toggleUrl(i);
  updatePreview(i);
  markDirty(i,false);
}

/* Modal picker */
function openPicker(i){
  pickerIdx = i;
  document.getElementById('sheetTitle').textContent = 'Scegli azione (Tasto ' + i + ')';
  document.getElementById('q').value = '';
  document.getElementById('modal').classList.add('open');
  renderList();
  setTimeout(()=>document.getElementById('q').focus(), 50);
}
function closePicker(){
  document.getElementById('modal').classList.remove('open');
  pickerIdx = -1;
}
function closePickerIfBackdrop(e){ closePicker(); }

function renderList(){
  const q = (document.getElementById('q').value || '').trim().toLowerCase();
  const list = document.getElementById('list');
  list.innerHTML = '';

  const items = AZ.filter(a=>{
    if(!q) return true;
    const hay = (a.v + ' ' + a.l + ' ' + (a.d||'')).toLowerCase();
    return hay.includes(q);
  });

  items.forEach(a=>{
    const div = document.createElement('div');
    div.className = 'opt' + ((pickerIdx>=0 && selectedAz[pickerIdx]===a.v) ? ' selected' : '');
    div.innerHTML = '<div class="l1">'+escapeHtml(a.l)+'</div><div class="l2">'+escapeHtml(a.d||a.v)+'</div>';
    div.onclick = ()=>{
      if(pickerIdx>=0) setSelected(pickerIdx, a.v);
      closePicker();
    };
    list.appendChild(div);
  });
}

function escapeHtml(s){
  return String(s).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;');
}

async function refresh(force=false){
  const r = await fetch('/api/tasti', {cache:'no-store'});
  const d = await r.json();

  for(let i=0;i<4;i++){
    const az=d['az'+i] || 'media_play';
    const url=d['url'+i] || '';
    if(force){ isEditing[i]=false; }
    applyFromServer(i, az, url);

    const card=document.getElementById('c'+i);
    if(d['pulse'+i]){
      card.classList.add('pulse');
      setTimeout(()=>card.classList.remove('pulse'), 420);
    }
  }
}

refresh();
setInterval(()=>refresh(false), 1500);

// init previews
for(let i=0;i<4;i++){
  selectedAz[i] = 'media_play';
  updatePreview(i);
}
</script>
</body></html>
)rawhtml");
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_TASTI; i++) pinMode(PIN_TASTI[i], INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(0), isr0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(1), isr1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(2), isr2, CHANGE);
  attachInterrupt(digitalPinToInterrupt(3), isr3, CHANGE);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;){} }
  display.setTextColor(SSD1306_WHITE);
  mostraMessaggio("Avvio...", "Connessione...");

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);

  IPAddress dns(8,8,8,8);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns);

  WiFi.begin(ssid_casa, pass_casa);
  WiFi.softAP(ap_ssid, ap_pass);

  int count = 0;
  while (WiFi.status() != WL_CONNECTED && count < 20) { delay(500); count++; }

  if (WiFi.status() == WL_CONNECTED) {
    mostraMessaggio("WiFi OK!", WiFi.localIP().toString());
    delay(1200);
    configTime(3600, 3600, "pool.ntp.org");
    prendiMeteo();
  } else {
    mostraMessaggio("WiFi OFF", "AP: 192.168.4.1");
  }

  server.on("/", handleHomeMeteo);
  server.on("/tasti", handleTastiPage);

  server.on("/api/meteo", handleApiMeteo);
  server.on("/api/view", handleApiView);
  server.on("/api/tasti", handleApiTasti);
  server.on("/api/events", handleApiEvents);

  server.on("/set", handleSetCity);
  server.on("/setbutton", handleSetButton);

  server.begin();
  ultimoAggiornamento = millis();
}

void loop() {
  gestisciTasti();
  server.handleClient();

  static unsigned long ultimoTickOra = 0;
  if (modalitaAttuale == ORA && millis() - ultimoTickOra >= 1000) {
    ultimoTickOra = millis();
    mostraOra();
  }

  if (millis() - ultimoAggiornamento >= intervalloMeteo) {
    ultimoAggiornamento = millis();
    if (WiFi.status() == WL_CONNECTED) prendiMeteo();
    else WiFi.reconnect();
  }
}
