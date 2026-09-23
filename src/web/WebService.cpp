#include "WebService.h"

#include <ArduinoJson.h>

namespace {

constexpr size_t kStatusBufferSize = 512;
constexpr size_t kJsonCapacity = 512;

String effectName(uint8_t effect) {
  static const char* const names[kSaberEffectCount] = {"solid", "pulse", "rainbow", "scanner"};
  return names[effect < kSaberEffectCount ? effect : 0];
}

String eyeName(uint8_t pattern) {
  static const char* const names[kEyePatternCount] = {"normal", "sleep", "angry"};
  return names[pattern < kEyePatternCount ? pattern : 0];
}

const char INDEX_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="zh-CN"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#000000">
<title>ESABER 控制台</title>
<style>
:root{color-scheme:dark;--bg:#000;--card:rgba(28,28,30,.72);--line:rgba(255,255,255,.12);--text:#f5f5f7;--muted:#9d9da5;--blue:#0a84ff;--green:#30d158}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;min-height:100vh;background:radial-gradient(circle at 20% 0,#25314d,#000 55%);background-attachment:fixed;font-family:-apple-system,BlinkMacSystemFont,"SF Pro Text","Segoe UI",system-ui,sans-serif;color:var(--text);display:flex;justify-content:center;padding:calc(24px + env(safe-area-inset-top)) 16px calc(24px + env(safe-area-inset-bottom))}
.wrap{width:100%;max-width:430px}
.title{font-size:30px;font-weight:700;letter-spacing:-.6px;margin:10px 0 6px}
.sub{color:var(--muted);font-size:13px;margin:0 0 20px}
.card{backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);background:var(--card);border:1px solid var(--line);border-radius:22px;padding:20px;margin-bottom:16px;box-shadow:0 18px 40px rgba(0,0,0,.32)}
.card h2{font-size:17px;font-weight:600;margin:0 0 16px;letter-spacing:-.2px}
.row{display:flex;justify-content:space-between;align-items:center;gap:12px;margin:14px 0}
label{font-size:14px;color:var(--muted)}
input,select{width:100%;border:1px solid var(--line);background:rgba(255,255,255,.08);color:var(--text);border-radius:14px;padding:13px;font-size:16px;outline:none;font-family:inherit}
input:focus{border-color:var(--blue)}
input[type=color]{height:58px;padding:4px;border-radius:16px}
input[type=range]{padding:0;background:transparent;border:0;accent-color:var(--blue)}
.segment{display:grid;grid-auto-flow:column;grid-auto-columns:1fr;gap:4px;background:rgba(120,120,128,.16);border-radius:13px;padding:3px}
.segment button{border:0;background:transparent;color:var(--muted);border-radius:10px;padding:9px 4px;font-size:13px;font-family:inherit;font-weight:500;transition:background .18s,color .18s}
.segment button.active{background:#fff;color:#000;font-weight:600}
.primary{width:100%;border:0;border-radius:15px;background:var(--blue);color:#fff;font-size:16px;font-weight:600;padding:14px;font-family:inherit}
.ghost{background:rgba(255,255,255,.14)}
.muted{color:var(--muted);font-size:13px;line-height:1.5;margin:14px 0 0}
.hidden{display:none}
.pill{display:inline-flex;align-items:center;gap:7px;padding:6px 12px;border-radius:999px;background:rgba(255,255,255,.12);font-size:12px;font-weight:500}
.dot{width:7px;height:7px;border-radius:50%;background:var(--muted)}
.dot.on{background:var(--green);box-shadow:0 0 8px var(--green)}
.value{font-size:14px;color:var(--text);font-variant-numeric:tabular-nums}
</style></head><body><main class="wrap">
<h1 class="title">ESABER</h1>
<p class="sub" id="subtitle">光剑控制台</p>

<section id="wifiCard" class="card"><h2>Wi-Fi 连接</h2>
<input id="ssid" placeholder="Wi-Fi 名称" autocomplete="off" autocapitalize="none"><div style="height:12px"></div>
<input id="pass" type="password" placeholder="Wi-Fi 密码"><div style="height:16px"></div>
<button class="primary" id="wifiBtn" onclick="saveWifi()">连接</button>
<p id="wifiMsg" class="muted">连接成功后会自动进入控制页。</p></section>

<section id="control" class="hidden">
<div class="card"><div class="row" style="margin-top:0"><h2 style="margin:0">光剑电源</h2><span id="powerPill" class="pill"><i class="dot"></i>OFF</span></div>
<div style="height:16px"></div>
<div id="powerSeg" class="segment"></div></div>

<div class="card"><h2>外观</h2>
<div class="row" style="margin-top:0"><label>颜色</label><span id="colorValue" class="value"></span></div>
<input id="color" type="color" value="#ff0000" onchange="saveSettings()">
<div class="row"><label>亮度</label><span id="brightnessValue" class="value">100</span></div>
<input id="brightness" type="range" min="0" max="255" value="100" oninput="onBrightnessInput()" onchange="saveSettings()">
<div class="row"><label>特效</label></div><div id="effects" class="segment"></div>
<div class="row"><label>眼睛图案</label></div><div id="eyes" class="segment"></div></div>

<div class="card"><h2>Blender 动捕</h2>
<input id="blenderIp" placeholder="电脑 IP，例如 192.168.1.10" inputmode="numeric" autocapitalize="none">
<div style="height:12px"></div>
<button class="primary" onclick="saveBlender()">保存 IP</button>
<p class="muted">ESP32 以 10 Hz 将姿态数据发送到该地址的 5005 端口。</p></div>

<div class="card"><h2>设备</h2>
<div class="row" style="margin:0"><label>访问地址</label><span id="deviceUrl" class="value">-</span></div>
<div class="row" style="margin-bottom:0"><label>当前网络</label><span id="deviceSsid" class="value">-</span></div></div>
</section></main>
<script>
var status={};
var effectLabels=['常亮','呼吸','彩虹','扫描'];
var eyeLabels=['正常','睡觉','生气'];
var powerLabels=['关闭','开启'];
var effectNames=['solid','pulse','rainbow','scanner'];
var eyeNames=['normal','sleep','angry'];
var effects=document.getElementById('effects');
var eyes=document.getElementById('eyes');
var powerSeg=document.getElementById('powerSeg');
var wifiCard=document.getElementById('wifiCard');
var control=document.getElementById('control');

function el(id){return document.getElementById(id);}
function api(path,body){return fetch(path,{method:'POST',body:new URLSearchParams(body||{})}).then(function(r){return r.json();});}
function buildSegments(container,labels,onPick){container.innerHTML='';labels.forEach(function(label,index){var b=document.createElement('button');b.textContent=label;b.onclick=function(){onPick(index);};container.appendChild(b);});}
function markActive(container,index){for(var i=0;i<container.children.length;i++){container.children[i].className=(i===index)?'active':'';}}

async function load(){try{var r=await fetch('/api/status');status=await r.json();render();}catch(e){}}

function render(){
  var online=status.wifi&&status.wifi.connected;
  wifiCard.classList.toggle('hidden',!!online);
  control.classList.toggle('hidden',!online);
  el('subtitle').textContent=online?(status.wifi.ssid||'已连接'):'光剑控制台';

  if(!online){
    el('wifiBtn').textContent=status.wifi&&status.wifi.connecting?'连接中…':'连接';
    el('wifiMsg').textContent=status.wifi&&status.wifi.connecting?'正在尝试连接，请稍候…':'连接成功后会自动进入控制页。';
    return;
  }

  if(status.wifi.ssid)el('ssid').value=status.wifi.ssid;
  if(status.blenderIp)el('blenderIp').value=status.blenderIp;
  if(status.color)el('color').value=status.color;
  el('colorValue').textContent=(status.color||'').toUpperCase();
  el('brightness').value=status.brightness;
  el('brightnessValue').textContent=Math.round(status.brightness*100/255)+'%';
  el('deviceUrl').textContent=status.wifi.url||'-';
  el('deviceSsid').textContent=status.wifi.ssid||'-';

  markActive(effects,status.effect);
  markActive(eyes,status.eye);
  markActive(powerSeg,status.power?1:0);
  var pill=el('powerPill');
  pill.innerHTML='<i class="dot'+(status.power?' on':'')+'"></i>'+(status.power?'ON':'OFF');
}

function onBrightnessInput(){el('brightnessValue').textContent=Math.round(el('brightness').value*100/255)+'%';}

async function saveWifi(){el('wifiBtn').textContent='连接中…';await api('/api/wifi',{ssid:el('ssid').value,password:el('pass').value});setTimeout(load,4000);}

async function saveSettings(patch){
  var hex=el('color').value;
  var body=Object.assign({r:parseInt(hex.substr(1,2),16),g:parseInt(hex.substr(3,2),16),b:parseInt(hex.substr(5,2),16),brightness:el('brightness').value,effect:status.effect,eye:status.eye},patch||{});
  status=Object.assign(status,patch||{});
  render();
  await api('/api/settings',body);
  load();
}

async function setPower(on){await api('/api/power',{state:on?1:0});load();}
async function saveBlender(){await api('/api/blender',{ip:el('blenderIp').value});load();}

buildSegments(effects,effectLabels,function(i){saveSettings({effect:i});});
buildSegments(eyes,eyeLabels,function(i){saveSettings({eye:i});});
buildSegments(powerSeg,powerLabels,function(i){setPower(i===1);});
load();
setInterval(load,3000);
</script></body></html>)html";

}  // namespace

void WebService::begin(WebServer* server, SaberController* saber, WifiService* wifi,
                       SettingsStore* settings, MotionTelemetry* telemetry) {
  server_ = server;
  saber_ = saber;
  wifi_ = wifi;
  settings_ = settings;
  telemetry_ = telemetry;

  server_->on("/", HTTP_GET, [this]() { handleRoot(); });
  server_->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  server_->on("/api/wifi", HTTP_POST, [this]() { handleWifi(); });
  server_->on("/api/settings", HTTP_POST, [this]() { handleSettings(); });
  server_->on("/api/power", HTTP_POST, [this]() { handlePower(); });
  server_->on("/api/blender", HTTP_POST, [this]() { handleBlender(); });
  server_->onNotFound([this]() { handleNotFound(); });
  server_->begin();
}

void WebService::handleRoot() {
  server_->send_P(200, "text/html", INDEX_HTML);
}

void WebService::handleStatus() {
  JsonDocument json;
  const SaberSettings& saber = saber_->settings();

  char colorHex[8];
  snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X", saber.red, saber.green, saber.blue);

  json["power"] = saber.power;
  json["color"] = colorHex;
  json["brightness"] = saber.brightness;
  json["effect"] = static_cast<uint8_t>(saber.effect);
  json["eye"] = static_cast<uint8_t>(saber.eyePattern);
  json["effectName"] = effectName(static_cast<uint8_t>(saber.effect));
  json["eyeName"] = eyeName(static_cast<uint8_t>(saber.eyePattern));
  json["blenderIp"] = settings_->blenderIp();

  JsonObject wifi = json["wifi"].to<JsonObject>();
  wifi["connected"] = wifi_->connected();
  wifi["connecting"] = wifi_->connecting();
  wifi["ssid"] = wifi_->activeSsid();
  wifi["url"] = wifi_->localUrl();

  char output[kStatusBufferSize];
  const size_t length = serializeJson(json, output, sizeof(output));
  server_->send(200, "application/json", String(output, length));
}

void WebService::handleWifi() {
  const String ssid = server_->arg("ssid");
  const String password = server_->arg("password");
  if (ssid.length() == 0) {
    sendJson(400, "{\"ok\":false,\"error\":\"ssid required\"}");
    return;
  }
  settings_->saveWifi(ssid, password);
  wifi_->connect(ssid, password);
  sendJson(200, "{\"ok\":true}");
}

void WebService::handleSettings() {
  SaberSettings settings = saber_->settings();
  if (server_->hasArg("r")) settings.red = server_->arg("r").toInt();
  if (server_->hasArg("g")) settings.green = server_->arg("g").toInt();
  if (server_->hasArg("b")) settings.blue = server_->arg("b").toInt();
  if (server_->hasArg("brightness")) settings.brightness = server_->arg("brightness").toInt();
  if (server_->hasArg("effect")) {
    const int effect = server_->arg("effect").toInt();
    if (effect >= 0 && effect < kSaberEffectCount) {
      settings.effect = static_cast<SaberEffect>(effect);
    }
  }
  if (server_->hasArg("eye")) {
    const int eye = server_->arg("eye").toInt();
    if (eye >= 0 && eye < kEyePatternCount) {
      settings.eyePattern = static_cast<EyePattern>(eye);
    }
  }

  settings_->saveSaber(settings);
  // Use the stored copy: it has the clamped brightness and enum values.
  saber_->setSettings(settings_->saber());
  sendJson(200, "{\"ok\":true}");
}

void WebService::handlePower() {
  SaberSettings settings = saber_->settings();
  settings.power = server_->arg("state") == "1";
  saber_->setSettings(settings);
  sendJson(200, "{\"ok\":true}");
}

void WebService::handleBlender() {
  const String ip = server_->arg("ip");
  IPAddress parsed;
  if (!parsed.fromString(ip)) {
    sendJson(400, "{\"ok\":false,\"error\":\"invalid ip\"}");
    return;
  }
  settings_->saveBlenderIp(ip);
  telemetry_->setTarget(ip);
  sendJson(200, "{\"ok\":true}");
}

void WebService::handleNotFound() {
  sendJson(404, "{\"ok\":false,\"error\":\"not found\"}");
}

void WebService::sendJson(int code, const String& json) {
  server_->send(code, "application/json", json);
}
