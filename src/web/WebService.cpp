// ============================================================================
// WebService —— HTTP 控制台：固件内嵌页面 + REST API
// 路由：/（内嵌页）/api/status（状态）/api/settings（设置）/api/power（开关）
//       /api/sounds（SD 音频清单）/api/blender（动捕地址）
// 所有 handler 都跑在 loop 任务上，禁止阻塞——页面是 PROGMEM 原始字符串。
// ============================================================================
#include "WebService.h"

#include <ArduinoJson.h>
#include <SD_MMC.h>

namespace {

// 旧负载 512B 就够；三个可选音效名最坏再加 ~150B（48 字符名）
constexpr size_t kStatusBufferSize = 640;
// 控制台下拉不需要整张卡：64 个已经是很满的音效包，
// 同时给 loop 任务上的目录遍历一个上界。
constexpr size_t kMaxSoundEntries = 64;

// 音效槽位只会播 SD 根目录的一个文件，所以名字拼 "/" 前必须安全：
// 扁平名、字符干净、无路径花招。这里不限扩展名——
// 解码器支持哪些格式是固件属性，不支持的文件播出来就是没声。
bool validSoundName(const String& name) {
  if (name.isEmpty() || name.length() >= kSoundNameLength) return false;
  if (name[0] == '.') return false;                      // 隐藏文件、".."
  if (name.indexOf("..") >= 0) return false;             // 防目录穿越
  if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) return false;   // 防子目录
  for (unsigned int i = 0; i < name.length(); ++i) {
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// 只有真实在卡上的名字才允许持久化：否则手搓请求里的一个拼写错误
// 就会悄悄废掉一个音效槽。扩展名检查让下拉与槽位都落在
// 解码器支持的格式内（大小写不敏感，与 /api/sounds 契约一致）。
bool hasAudioExtension(const String& name) {
  const int dot = name.lastIndexOf('.');
  if (dot < 0) return false;
  String ext = name.substring(dot);
  ext.toLowerCase();
  return ext == ".wav" || ext == ".flac" || ext == ".mp3";
}

bool soundOnCard(const String& name) {
  if (!validSoundName(name) || !hasAudioExtension(name)) return false;
  return SD_MMC.exists("/" + name);   // 最终真相：卡上有没有这个文件
}
constexpr size_t kJsonCapacity = 512;

String effectName(uint8_t effect) {
  static const char* const names[kSaberEffectCount] = {"solid",  "pulse",   "rainbow",
                                                       "scanner", "unstable", "fire", "sparkle"};
  return names[effect < kSaberEffectCount ? effect : 0];   // 越界回落 solid
}

String eyeName(uint8_t pattern) {
  static const char* const names[kEyePatternCount] = {"normal", "sleep", "angry"};
  return names[pattern < kEyePatternCount ? pattern : 0];
}

const char INDEX_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#000000">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<title>ESABER 控制台</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Crect x='14' y='2' width='4' height='20' rx='2' fill='%230A84FF'/%3E%3Crect x='11' y='22' width='10' height='8' rx='2' fill='%238E8E93'/%3E%3C/svg%3E">
<style>
:root{
  color-scheme:dark;
  --bg:#000; --card:rgba(28,28,30,.72); --line:rgba(255,255,255,.10);
  --text:#f5f5f7; --muted:#9d9da5; --accent:#0a84ff; --accent-soft:rgba(10,132,255,.14);
  --green:#30d158; --amber:#ffd60a; --red:#ff453a;
  --radius:22px; --radius-s:14px;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;padding:0}
body{
  min-height:100vh;background:var(--bg);color:var(--text);
  font-family:"PingFang SC","HarmonyOS Sans SC","Noto Sans CJK SC","Microsoft YaHei UI","Microsoft YaHei",-apple-system,BlinkMacSystemFont,sans-serif;
  display:flex;justify-content:center;
  padding:calc(env(safe-area-inset-top) + 0px) 16px calc(env(safe-area-inset-bottom) + 28px);
  line-height:1.5;
}
.wrap{width:100%;max-width:430px}
button,input{font-family:inherit;color:inherit}
:focus-visible{outline:2px solid #2997ff;outline-offset:2px;border-radius:6px}
.hidden{display:none!important}

/* ---------- 顶栏 ---------- */
.topbar{
  position:sticky;top:0;z-index:20;margin:0 -16px;padding:calc(env(safe-area-inset-top) + 10px) 20px 10px;
  display:flex;align-items:center;justify-content:space-between;
  background:rgba(0,0,0,.66);backdrop-filter:blur(20px) saturate(180%);-webkit-backdrop-filter:blur(20px) saturate(180%);
  border-bottom:1px solid var(--line);
}
.brand{font-size:16px;font-weight:700;letter-spacing:.32em;text-indent:.32em}
.pill{
  display:inline-flex;align-items:center;gap:6px;font-size:13px;color:var(--muted);
  background:rgba(255,255,255,.07);border:1px solid var(--line);border-radius:999px;padding:5px 12px;
  cursor:pointer;max-width:56vw;white-space:nowrap;overflow:hidden;
}
.pill .dot{width:7px;height:7px;border-radius:50%;background:var(--green);flex:none}
.pill.offline .dot{background:var(--amber)}
.pill span{overflow:hidden;text-overflow:ellipsis}

/* ---------- 通用卡片 ---------- */
.card{
  background:var(--card);border:1px solid var(--line);border-radius:var(--radius);
  padding:18px;margin-top:14px;
  backdrop-filter:blur(20px) saturate(180%);-webkit-backdrop-filter:blur(20px) saturate(180%);
}
h2{font-size:17px;font-weight:600;margin:0 0 2px}
.hint{font-size:13px;color:var(--muted);margin:2px 0 0}

/* ---------- 光剑预览 ---------- */
.stage{
  position:relative;height:clamp(216px,52vw,264px);border-radius:var(--radius) var(--radius) 0 0;
  border:1px solid var(--line);border-bottom:none;overflow:hidden;
  background:radial-gradient(circle at 50% 86%, var(--amb,transparent), transparent 62%),#0a0a0c;
  transition:background .5s;
  display:flex;flex-direction:column;align-items:center;justify-content:flex-end;
  cursor:pointer;
}
.stage::after{content:"";position:absolute;inset:0;background:linear-gradient(180deg,rgba(255,255,255,.04),transparent 30%);pointer-events:none}
.blade-wrap{position:relative;width:26px;height:calc(100% - 78px);display:flex;align-items:flex-end;justify-content:center}
.blade{
  width:26px;height:100%;border-radius:14px;transform-origin:bottom center;
  transform:scaleY(.04);background:#26262a;transition:transform .48s cubic-bezier(.2,.8,.25,1),background .3s;
  position:relative;
}
.stage.on .blade{transform:scaleY(1)}
.blade .scan{display:none;position:absolute;left:0;right:0;height:22px;border-radius:8px;filter:blur(3px);pointer-events:none}
.fx-3 .blade .scan{display:block;animation:scanMove 1.15s ease-in-out infinite alternate;background:linear-gradient(180deg,transparent,rgba(255,255,255,.95),transparent)}
.fx-3 .blade{filter:brightness(.72)}
.fx-1 .blade{animation:breathe 2.2s ease-in-out infinite}
.fx-2 .blade{animation:rain 4s linear infinite}
.fx-4 .blade{animation:jitter .13s steps(2,end) infinite}
.fx-5 .blade{animation:jitter .16s steps(2,end) infinite}
.fx-6 .blade{filter:brightness(.88)}
.blade .spark{display:none;position:absolute;width:5px;height:5px;border-radius:50%;background:#fff;box-shadow:0 0 6px rgba(255,255,255,.95);pointer-events:none}
.fx-6 .blade .spark{display:block;animation:twinkle 1.05s ease-in-out infinite}
.fx-6 .blade .spark.s1{top:22%;left:18%}
.fx-6 .blade .spark.s2{top:57%;left:62%;animation-delay:.5s}
@keyframes breathe{0%,100%{filter:brightness(.88)}50%{filter:brightness(1.3)}}
@keyframes jitter{0%{filter:brightness(.7)}50%{filter:brightness(1.25)}}
@keyframes twinkle{0%,100%{opacity:0;transform:scale(.5)}50%{opacity:1;transform:scale(1.15)}}
@keyframes rain{from{filter:hue-rotate(0)}to{filter:hue-rotate(360deg)}}
@keyframes scanMove{from{top:2px}to{top:calc(100% - 24px)}}
.hilt{
  width:44px;height:44px;border-radius:10px 10px 16px 16px;position:relative;flex:none;
  background:linear-gradient(180deg,#4a4a4f,#333338 55%,#232327);
}
.hilt::before,.hilt::after{content:"";position:absolute;left:7px;right:7px;height:2px;background:rgba(0,0,0,.45);border-radius:1px}
.hilt::before{top:14px}.hilt::after{top:22px}
.readout{
  display:flex;align-items:center;justify-content:space-between;gap:8px;
  border:1px solid var(--line);border-top:none;border-radius:0 0 var(--radius) var(--radius);
  background:var(--card);padding:12px 18px;
  backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);
  font-size:13px;color:var(--muted);
}
.readout b{color:var(--text);font-weight:600}
.stage-row{display:flex;align-items:center;justify-content:space-between;padding:14px 2px 2px}
.stage-row .label{font-size:17px;font-weight:600}

/* ---------- iOS 开关 ---------- */
.switch{position:relative;width:51px;height:31px;flex:none}
.switch input{position:absolute;opacity:0;width:100%;height:100%;margin:0;cursor:pointer}
.knob{position:absolute;inset:0;border-radius:999px;background:rgba(120,120,128,.32);transition:background .25s}
.knob::after{content:"";position:absolute;top:2px;left:2px;width:27px;height:27px;border-radius:50%;background:#fff;transition:transform .25s;box-shadow:0 2px 6px rgba(0,0,0,.4)}
.switch input:checked + .knob{background:var(--green)}
.switch input:checked + .knob::after{transform:translateX(20px)}

/* ---------- 表单 ---------- */
.field{margin-top:12px}
.field label{display:block;font-size:13px;color:var(--muted);margin-bottom:6px}
.input{
  width:100%;height:48px;border-radius:var(--radius-s);border:1px solid var(--line);
  background:rgba(255,255,255,.06);padding:0 14px;font-size:17px;
}
.input::placeholder{color:#6c6c72}
.input:focus{outline:none;border-color:var(--accent)}
/* 音效下拉：沿用输入框底色，右侧画一枚与文字同灰度的折叠箭头 */
.select{
  appearance:none;-webkit-appearance:none;
  background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='14' height='14' viewBox='0 0 24 24' fill='none' stroke='%239d9da5' stroke-width='2.6' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E");
  background-repeat:no-repeat;background-position:right 14px center;
  padding-right:40px;color:var(--text);color-scheme:dark;
}
.select:disabled{opacity:.45}
.btn{
  width:100%;height:50px;border:none;border-radius:var(--radius-s);background:var(--accent);
  color:#fff;font-size:17px;font-weight:600;cursor:pointer;margin-top:16px;transition:opacity .15s;
}
.btn:active{opacity:.75}
.btn.secondary{background:rgba(255,255,255,.10)}
.msg{font-size:13px;margin:10px 0 0;min-height:1em}
.msg.ok{color:var(--green)}.msg.err{color:var(--red)}

/* ---------- 颜色 ---------- */
.color-row{display:flex;align-items:center;gap:14px;margin:14px 0 4px}
.swatch{width:46px;height:46px;border-radius:50%;flex:none;border:1px solid rgba(255,255,255,.18);box-shadow:inset 0 0 0 4px rgba(0,0,0,.35)}
.hex{font-size:17px;font-weight:600;letter-spacing:.04em}
.picker{margin-left:auto}
.picker input{width:120px;height:40px;border:none;background:none;padding:0;cursor:pointer}
.presets{display:grid;grid-template-columns:repeat(8,1fr);gap:10px;margin-top:12px}
.preset{aspect-ratio:1;border-radius:50%;border:2px solid transparent;cursor:pointer;padding:0;transition:transform .15s,border-color .15s}
.preset:active{transform:scale(.9)}
.preset[aria-pressed="true"]{border-color:#fff;box-shadow:0 0 0 2px rgba(255,255,255,.25)}

/* ---------- 滑条 ---------- */
.slider-row{display:flex;align-items:center;gap:14px;margin-top:14px}
.slider-row output{font-size:15px;font-weight:600;min-width:3.2em;text-align:right;font-variant-numeric:tabular-nums}
input[type=range]{-webkit-appearance:none;appearance:none;flex:1;height:28px;background:none;cursor:pointer}
input[type=range]::-webkit-slider-runnable-track{height:5px;border-radius:3px;background:var(--track,rgba(120,120,128,.32))}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:26px;height:26px;border-radius:50%;background:#fff;box-shadow:0 1px 5px rgba(0,0,0,.5);margin-top:-10.5px}
input[type=range]::-moz-range-track{height:5px;border-radius:3px;background:rgba(120,120,128,.32)}
input[type=range]::-moz-range-progress{height:5px;border-radius:3px;background:var(--accent)}
input[type=range]::-moz-range-thumb{width:26px;height:26px;border:none;border-radius:50%;background:#fff;box-shadow:0 1px 5px rgba(0,0,0,.5)}

/* ---------- 特效 / 眼睛选择块 ---------- */
.tiles{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-top:14px}
.tiles.c3{grid-template-columns:repeat(3,1fr)}
.tile{
  border:1.5px solid var(--line);border-radius:var(--radius-s);background:rgba(255,255,255,.05);
  padding:12px 6px 10px;display:flex;flex-direction:column;align-items:center;gap:9px;cursor:pointer;
  transition:border-color .15s,background .15s;
}
.tile span{font-size:13px;color:var(--muted)}
.tile[aria-pressed="true"]{border-color:var(--accent);background:var(--accent-soft)}
.tile[aria-pressed="true"] span{color:var(--text)}
.mini{width:54px;height:12px;border-radius:7px;background:var(--c,#0a84ff);position:relative;overflow:hidden}
.fx-mini-0 .mini{background:var(--c,#0a84ff)}
.fx-mini-1 .mini{animation:breathe 2.2s ease-in-out infinite;background:var(--c,#0a84ff)}
.fx-mini-2 .mini{background:linear-gradient(90deg,#ff453a,#ffd60a,#30d158,#0a84ff,#bf5af2);background-size:200% 100%;animation:slide 2.4s linear infinite}
.fx-mini-3 .mini{filter:brightness(.62)}
.fx-mini-3 .mini::after{content:"";position:absolute;top:-2px;bottom:-2px;width:16px;border-radius:6px;background:rgba(255,255,255,.95);filter:blur(2px);animation:miniScan 1.15s ease-in-out infinite alternate}
.fx-mini-4 .mini{background:var(--c,#0a84ff);animation:jitter .14s steps(2,end) infinite}
.fx-mini-5 .mini{background:linear-gradient(90deg,#4a1200,#ff5a00,#ffc400,#fff3b0);animation:jitter .18s steps(2,end) infinite}
.fx-mini-6 .mini{background:var(--c,#0a84ff);filter:brightness(.85)}
.fx-mini-6 .mini::after{content:"";position:absolute;top:26%;left:56%;width:4px;height:4px;border-radius:50%;background:#fff;box-shadow:0 0 5px #fff;animation:twinkle 1.05s ease-in-out infinite}
@keyframes slide{from{background-position:0 0}to{background-position:200% 0}}
@keyframes miniScan{from{left:1px}to{left:calc(100% - 17px)}}
.face{width:44px;height:44px}
.face circle,.face path,.face line{vector-effect:non-scaling-stroke}

/* ---------- 信息行 ---------- */
.rows{margin-top:6px}
.row{
  display:flex;align-items:center;justify-content:space-between;gap:10px;
  padding:13px 2px;border-bottom:1px solid rgba(255,255,255,.07);font-size:15px;
}
.row:last-child{border-bottom:none}
.row .k{color:var(--muted)}
.row .v{display:flex;align-items:center;gap:6px;text-align:right;min-width:0}
.row .v a{color:#2997ff;text-decoration:none;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.row.link{cursor:pointer;border-radius:10px}
.row.link:hover{background:rgba(255,255,255,.04)}
.chev{color:#5c5c63;flex:none}

/* ---------- 状态条 / 徽标 ---------- */
.btn.ghost-btn{background:rgba(255,255,255,.08);color:var(--text)}
.demo-badge{
  display:inline-flex;align-items:center;gap:6px;font-size:12px;color:var(--amber);
  background:rgba(255,214,10,.10);border:1px solid rgba(255,214,10,.3);
  border-radius:999px;padding:4px 10px;margin-top:14px;
}
.toast{
  position:fixed;left:50%;bottom:calc(env(safe-area-inset-bottom) + 30px);transform:translateX(-50%) translateY(20px);
  background:rgba(50,50,54,.92);border:1px solid var(--line);border-radius:999px;
  padding:10px 20px;font-size:14px;opacity:0;pointer-events:none;transition:opacity .25s,transform .25s;z-index:50;
  max-width:80vw;text-align:center;
}
.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
.toast.err{color:var(--red)}.toast.ok{color:var(--green)}

/* ---------- 连接页 ---------- */
.connect-hero{text-align:center;padding:26px 0 6px}
.connect-hero .mark{
  width:64px;height:64px;margin:0 auto 16px;border-radius:18px;
  background:radial-gradient(circle at 50% 30%,rgba(10,132,255,.5),rgba(10,132,255,.08) 70%),rgba(255,255,255,.05);
  border:1px solid var(--line);display:flex;align-items:center;justify-content:center;
}
.connect-hero h1{font-size:26px;font-weight:700;letter-spacing:.3em;text-indent:.3em;margin:0 0 6px}
.connect-hero p{color:var(--muted);font-size:14px;margin:0}
.steps{font-size:13px;color:var(--muted);line-height:1.9;margin:0;padding-left:18px}
@media (prefers-reduced-motion:reduce){
  .fx-1 .blade,.fx-2 .blade,.fx-3 .blade,.fx-4 .blade,.fx-5 .blade,.scan,.spark,.mini,
  .fx-mini-1 .mini,.fx-mini-2 .mini,.fx-mini-3 .mini::after,.fx-mini-4 .mini,.fx-mini-5 .mini,.fx-mini-6 .mini::after{animation:none!important}
  .blade{transition:none}
}
</style>
</head>
<body>
<div class="wrap">

  <header class="topbar" data-section="topbar">
    <div class="brand">ESABER</div>
    <button class="pill" id="netPill" type="button" aria-label="网络设置">
      <i class="dot" aria-hidden="true"></i><span id="netName">未连接</span>
    </button>
  </header>

  <!-- ============ 演示模式徽标 ============ -->
  <div class="demo-badge hidden" id="demoPill" title="未检测到设备，正在使用模拟数据演示界面">演示模式 · 未检测到设备</div>

  <!-- ============ 指引态：未检测到设备（控制台为单页直达，此页仅作引导） ============ -->
  <section id="view-connect" data-section="connect" class="hidden">
    <div class="connect-hero">
      <div class="mark" aria-hidden="true">
        <svg width="30" height="30" viewBox="0 0 32 32" fill="none"><rect x="14" y="2" width="4" height="20" rx="2" fill="#0A84FF"/><rect x="11" y="22" width="10" height="8" rx="2" fill="#8E8E93"/></svg>
      </div>
      <h1>ESABER</h1>
      <p id="connectSub">光剑控制台</p>
    </div>
    <div class="card" data-card="guide">
      <h2>连接设备</h2>
      <ol class="steps">
        <li>在手机 WLAN 中连接光剑热点 <b style="color:var(--text)">Esaber-Setup</b>（免密直连，无需输入密码）</li>
        <li>连接后打开 <b style="color:var(--text)">192.168.4.1</b>，或扫描屏幕上的二维码</li>
        <li>直接进入控制台，无需登录或配置</li>
      </ol>
      <p class="hint">控制台由光剑自身发出，全程无需外网。</p>
    </div>
    <div class="card" data-card="reach">
      <p class="msg" id="reachMsg" aria-live="polite">未检测到设备——请确认手机已连接热点 Esaber-Setup。</p>
      <button class="btn" id="retryBtn" type="button">重新检测</button>
      <button class="btn ghost-btn" id="demoBtn" type="button">进入演示模式（模拟数据预览界面）</button>
    </div>
  </section>

  <!-- ============ 控制台（单页直达） ============ -->
  <section id="view-console" data-section="console" class="hidden">

    <div class="stage-row" style="padding:14px 2px 0">
      <span class="label">光剑预览</span>
      <span class="hint" id="stateHint">已关闭</span>
    </div>
    <div class="stage fx-1" id="stage" data-card="preview" role="button" tabindex="0" aria-pressed="false" aria-label="光剑预览，点按切换电源">
      <div class="blade-wrap"><div class="blade" id="blade"><i class="scan" aria-hidden="true"></i><i class="spark s1" aria-hidden="true"></i><i class="spark s2" aria-hidden="true"></i></div></div>
      <div class="hilt" aria-hidden="true"></div>
    </div>
    <div class="readout" data-card="readout">
      <span>颜色 <b id="roHex">#0A84FF</b></span>
      <span>特效 <b id="roEffect">呼吸</b></span>
      <span>亮度 <b id="roBright">80%</b></span>
    </div>

    <div class="stage-row">
      <span class="label">电源</span>
      <span class="switch"><input type="checkbox" id="powerSw" role="switch" aria-label="光剑电源"><i class="knob" aria-hidden="true"></i></span>
    </div>

    <div class="card" data-card="color">
      <h2>刀色</h2>
      <div class="color-row">
        <div class="swatch" id="swatch" aria-hidden="true"></div>
        <div><div class="hex" id="hexText">#0A84FF</div><div class="hint">实时同步到灯条</div></div>
        <div class="picker"><input type="color" id="colorPick" aria-label="自定义颜色" value="#0a84ff"></div>
      </div>
      <div class="presets" id="presets" role="group" aria-label="预设颜色"></div>
    </div>

    <div class="card" data-card="brightness">
      <h2>亮度</h2>
      <div class="slider-row">
        <input type="range" id="bright" min="0" max="100" value="80" aria-label="亮度百分比">
        <output id="brightVal" for="bright">80%</output>
      </div>
    </div>

    <div class="card" data-card="volume">
      <h2>音量</h2>
      <div class="slider-row">
        <input type="range" id="volume" min="0" max="21" step="1" value="14" aria-label="音量">
        <output id="volVal" for="volume">67%</output>
      </div>
      <p class="hint">音效响度，0-21 级，即调即响。</p>
    </div>

    <div class="card" data-card="effects">
      <h2>特效</h2>
      <p class="hint">预览缩略图与灯条效果一致</p>
      <div class="tiles" id="effects" role="group" aria-label="特效选择"></div>
    </div>

    <div class="card" data-card="eyes">
      <h2>眼睛表情</h2>
      <p class="hint">屏幕待机表情（虹膜颜色跟随刀色）</p>
      <div class="tiles c3" id="eyes" role="group" aria-label="眼睛表情选择"></div>
    </div>

    <div class="card" data-card="sounds">
      <h2>音效</h2>
      <p class="hint">从 SD 卡根目录选择音频文件（.wav / .flac / .mp3），即选即存、断电不丢；底噪换曲立即生效</p>
      <div class="field">
        <label for="bootSound">开机音效（开刃）</label>
        <select class="input select" id="bootSound" aria-label="开机音效选择"></select>
      </div>
      <div class="field">
        <label for="shutdownSound">关机音效（收刃）</label>
        <select class="input select" id="shutdownSound" aria-label="关机音效选择"></select>
      </div>
      <div class="field">
        <label for="humSound">底噪音（亮刀期间循环）</label>
        <select class="input select" id="humSound" aria-label="底噪音选择"></select>
      </div>
      <p class="msg" id="soundsMsg" aria-live="polite"></p>
    </div>

    <div class="card" data-card="blender">
      <h2>Blender 动捕</h2>
      <p class="hint">姿态数据经 UDP 推送到电脑上的 Blender 模型（默认端口 5005）</p>
      <form id="blenderForm" novalidate>
        <div class="field">
          <label for="blenderIp">电脑 IP 地址</label>
          <input class="input" id="blenderIp" inputmode="numeric" placeholder="192.168.1.100" autocomplete="off">
        </div>
        <button class="btn secondary" type="submit">保存地址</button>
        <p class="msg" id="blenderMsg" aria-live="polite"></p>
      </form>
    </div>

    <div class="card" data-card="info">
      <h2>设备</h2>
      <div class="rows">
        <div class="row"><span class="k">当前网络</span><span class="v" id="infoSsid">—</span></div>
        <div class="row"><span class="k">访问地址</span><span class="v"><a id="infoUrl" href="#" target="_blank" rel="noopener">—</a></span></div>
        <div class="row link" id="gotoNet" role="button" tabindex="0">
          <span class="k">网络设置</span>
          <span class="v"><svg class="chev" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m9 5 7 7-7 7"/></svg></span>
        </div>
      </div>
      <!-- 扩展位：音量滑条 / OTA 升级 / 重启 —— 等固件新增对应 API 后在此追加 -->
    </div>
  </section>

  <div class="toast" id="toast" role="status" aria-live="polite"></div>
</div>

<script>
(function(){
'use strict';
var $ = function(id){ return document.getElementById(id); };

/* ---------------- 常量 ---------------- */
var EFFECTS = [
  {id:0, name:'常亮', fx:'fx-mini-0'},
  {id:1, name:'呼吸', fx:'fx-mini-1'},
  {id:2, name:'彩虹', fx:'fx-mini-2'},
  {id:3, name:'扫描', fx:'fx-mini-3'},
  {id:4, name:'不稳定', fx:'fx-mini-4'},
  {id:5, name:'火焰', fx:'fx-mini-5'},
  {id:6, name:'星尘', fx:'fx-mini-6'}
];
var EYES = ['普通','瞌睡','生气'];
var PRESETS = [
  ['绝地蓝','#0A84FF'],['西斯红','#FF453A'],['绝地绿','#30D158'],['温德紫','#BF5AF2'],
  ['阿索卡白','#F2F2F7'],['燃烧橙','#FF9F0A'],['绝地黄','#FFD60A'],['冰晶青','#64D2FF']
];
var EFFECT_NAMES = ['常亮','呼吸','彩虹','扫描','不稳定','火焰','星尘'];

/* ---------------- 状态 ---------------- */
var S = {power:false, color:'#0A84FF', brightness:80, volume:14, volumeMax:21, effect:1, eye:0,
         bootSound:'endlock1.wav', shutdownSound:'endlock2.wav', humSound:'111.wav', sounds:[],
         blenderIp:'', ssid:'', url:'', connected:false};
var localUntil = 0;        // 用户操作后的静默期：轮询不回写控件，避免打架
var demo = false;          // 演示模式（无设备时的可交互预览，显式进入）
var failStreak = 0;        // 连续轮询失败次数（≥2 才切引导态，防偶发闪切）
var lastRaw = '';

/* ---------------- 工具 ---------------- */
function clamp(v,a,b){ return Math.min(b, Math.max(a, v)); }
function hexRgb(h){
  var n = parseInt(h.replace('#',''), 16);
  return [(n>>16)&255, (n>>8)&255, n&255];
}
function rgba(h,a){
  var c = hexRgb(h);
  return 'rgba(' + c[0] + ',' + c[1] + ',' + c[2] + ',' + a.toFixed(3) + ')';
}
function mixWhite(h,t){
  var c = hexRgb(h);
  return '#' + c.map(function(v){ return Math.round(v + (255-v)*t).toString(16).padStart(2,'0'); }).join('');
}
function toast(text, kind){
  var t = $('toast');
  t.textContent = text;
  t.className = 'toast show ' + (kind || '');
  clearTimeout(toast._t);
  toast._t = setTimeout(function(){ t.className = 'toast'; }, 1900);
}
function api(path, body){
  if (demo) return demoApi(path, body);
  return fetch(path, {method:'POST', body:new URLSearchParams(body || {})}).then(function(r){
    if (!r.ok) throw new Error('http ' + r.status);
    return r.json();
  });
}

/* ---------------- 演示模式（未检测到设备时，显式进入） ---------------- */
function demoApi(path, body){
  return new Promise(function(resolve){
    setTimeout(function(){
      if (path === '/api/power'){
        S.power = body.state === '1';
      } else if (path === '/api/settings'){
        if ('r' in body) S.color = '#' + [body.r, body.g, body.b].map(function(v){ return (+v).toString(16).padStart(2,'0'); }).join('').toUpperCase();
        if ('brightness' in body) S.brightness = clamp(Math.round(+body.brightness), 0, 100);
        if ('volume' in body) S.volume = clamp(+body.volume, 0, S.volumeMax);
        if ('effect' in body) S.effect = +body.effect;
        if ('eye' in body) S.eye = +body.eye;
      } else if (path === '/api/blender'){
        S.blenderIp = body.ip;
      } else if (path === '/api/sounds'){
        // demo-only branch: the real console reads this with a plain GET.
      }
      if ('boot_sound' in body) S.bootSound = body.boot_sound;
      if ('shutdown_sound' in body) S.shutdownSound = body.shutdown_sound;
      if ('hum_sound' in body) S.humSound = body.hum_sound;
      resolve({ok:true});
    }, 90);
  });
}
function enableDemo(){
  demo = true;
  Object.assign(S, {power:false, color:'#0A84FF', brightness:80, effect:1, eye:0,
                    ssid:'Esaber-Setup', url:'http://192.168.4.1', blenderIp:'192.168.1.50', connected:true});
  $('demoPill').classList.remove('hidden');
  render();
  loadSounds();
}

/* ---------------- 渲染 ---------------- */
function setView(view){
  $('view-connect').classList.toggle('hidden', view !== 'connect');
  $('view-console').classList.toggle('hidden', view !== 'console');
  if (view === 'console' && !soundLoaded) loadSounds();
}

/* ---------------- 音效 ---------------- */
var soundLoaded = false;   // 每次进入控制台只拉一次 SD 清单，刷新页面重读
var DEMO_SOUNDS = ['111.wav','endlock1.wav','endlock2.wav','clsh1.wav','clsh2.wav','swng1.wav'];
function loadSounds(){
  if (demo){
    S.sounds = DEMO_SOUNDS.slice();
    soundLoaded = true;
    renderSoundSelects();
    return;
  }
  fetch('/api/sounds').then(function(r){ return r.json(); }).then(function(j){
    S.sounds = (j && j.sounds) || [];
    soundLoaded = true;
    renderSoundSelects();
  }).catch(function(){
    S.sounds = [];
    renderSoundSelects();
  });
}
function fillSoundSelect(id, current){
  var sel = $(id);
  sel.innerHTML = '';
  var names = S.sounds;
  // SD 卡被更换后当前值可能不在清单里：保留原值选项，避免静默换音
  if (current && names.indexOf(current) < 0){
    var keep = document.createElement('option');
    keep.value = current;
    keep.textContent = current + '（SD 缺失）';
    sel.appendChild(keep);
  }
  for (var i = 0; i < names.length; i++){
    var o = document.createElement('option');
    o.value = names[i];
    o.textContent = names[i];
    sel.appendChild(o);
  }
  if (current) sel.value = current;
  sel.disabled = names.length === 0;
}
function renderSoundSelects(){
  if (!S.sounds.length){
    $('soundsMsg').textContent = '未在 SD 卡根目录发现音频文件（.wav / .flac / .mp3）';
    $('soundsMsg').className = 'msg err';
  } else {
    $('soundsMsg').textContent = '';
    $('soundsMsg').className = 'msg';
  }
  fillSoundSelect('bootSound', S.bootSound);
  fillSoundSelect('shutdownSound', S.shutdownSound);
  fillSoundSelect('humSound', S.humSound);
}
// 音效是离散单选，不走滑条的 80ms 合并节流，一次选择一发落地
function pushSound(key, value){
  var part = {};
  part[key] = value;
  S[key] = value;
  localUntil = Date.now() + 1500;
  api('/api/settings', part).then(function(){
    toast('音效已保存' + (key === 'humSound' ? '，循环已切换' : ''), 'ok');
  }).catch(function(){ toast('保存失败，请重试', 'err'); });
}
function render(){
  var online = S.connected;
  if (online){
    setView('console');
  } else {
    setView('connect');
    $('connectSub').textContent = '光剑控制台';
  }
  $('netPill').classList.toggle('offline', !online);
  $('netName').textContent = online ? (S.ssid || '热点已连接') : '未连接';
  $('infoSsid').textContent = online ? (S.ssid || '—') : '—';
  var urlEl = $('infoUrl');
  if (online && S.url){ urlEl.textContent = S.url.replace(/^https?:\/\//,''); urlEl.href = S.url; }
  else { urlEl.textContent = '—'; urlEl.removeAttribute('href'); }

  // 控件始终反映 S；「不覆盖用户输入」的守卫在 applyStatus 的服务端合并处
  if ($('colorPick').value.toLowerCase() !== S.color.toLowerCase()) $('colorPick').value = S.color;
  if (+$('bright').value !== S.brightness) $('bright').value = S.brightness;
  var vol = $('volume');
  if (+vol.max !== S.volumeMax) vol.max = S.volumeMax;
  if (+vol.value !== S.volume) vol.value = S.volume;
  $('powerSw').checked = S.power;
  markActive($('effects'), S.effect);
  markActive($('eyes'), S.eye);
  updateBlade();
  updatePresetMarks();
  $('stateHint').textContent = S.power ? '已点亮' : '已关闭';
}
function markActive(group, index){
  var kids = group.children;
  for (var i = 0; i < kids.length; i++) kids[i].setAttribute('aria-pressed', i === index ? 'true' : 'false');
}
function updatePresetMarks(){
  var kids = $('presets').children;
  for (var i = 0; i < kids.length; i++)
    kids[i].setAttribute('aria-pressed', kids[i].dataset.color.toUpperCase() === S.color.toUpperCase() ? 'true' : 'false');
}
function updateBlade(){
  var stage = $('stage'), blade = $('blade');
  var pct = S.brightness / 100;
  stage.className = 'stage fx-' + S.effect + (S.power ? ' on' : '');
  stage.setAttribute('aria-pressed', S.power ? 'true' : 'false');
  if (S.power){
    var g1 = .14 + .5 * pct, g2 = .07 + .3 * pct, g3 = .04 + .16 * pct;
    if (S.effect === 5){
      // 火焰：热力调色板（黑→红→橙→黄），与固件渲染一致，不随刀色变化
      blade.style.background = 'linear-gradient(0deg,#1a0400 0%,#7a1f00 20%,#ff5a00 46%,#ffc400 72%,#fff3b0 96%)';
      blade.style.boxShadow = '0 0 12px rgba(255,110,0,' + g1 + '), 0 0 36px rgba(255,90,0,' + g2 + '), 0 0 80px rgba(255,70,0,' + g3 + ')';
      stage.style.setProperty('--amb', 'rgba(255,110,0,' + (.06 + .1 * pct) + ')');
    } else {
      blade.style.background = S.color;
      blade.style.boxShadow = '0 0 10px ' + rgba(mixWhite(S.color,.35), g1) +
        ', 0 0 34px ' + rgba(S.color, g2) + ', 0 0 80px ' + rgba(S.color, g3);
      stage.style.setProperty('--amb', rgba(S.color, .05 + .09 * pct));
    }
  } else {
    blade.style.background = '#26262a';
    blade.style.boxShadow = 'none';
    stage.style.setProperty('--amb', 'transparent');
  }
  var tiles = document.querySelectorAll('.tile');
  for (var i = 0; i < tiles.length; i++) tiles[i].style.setProperty('--c', S.color);
  $('roHex').textContent = S.color.toUpperCase();
  $('roEffect').textContent = EFFECT_NAMES[S.effect] || '—';
  $('roBright').textContent = S.brightness + '%';
  $('volVal').textContent = Math.round(S.volume / S.volumeMax * 100) + '%';
  $('brightVal').textContent = S.brightness + '%';
  $('swatch').style.background = S.color;
  $('hexText').textContent = S.color.toUpperCase();
  var b = $('bright');
  var pctNow = (b.value / b.max) * 100;
  b.style.setProperty('--track', 'linear-gradient(90deg, var(--accent) ' + pctNow + '%, rgba(120,120,128,.32) ' + pctNow + '%)');
  var vol = $('volume');
  var volNow = (vol.value / vol.max) * 100;
  vol.style.setProperty('--track', 'linear-gradient(90deg, var(--accent) ' + volNow + '%, rgba(120,120,128,.32) ' + volNow + '%)');
}

/* ---------------- 数据同步 ---------------- */
// AP-only：能加载本页并拿到 /api/status 即视为「设备可达」，控制台单页直达
function applyStatus(s){
  var wifi = s.wifi || {};
  S.connected = true;
  S.ssid = wifi.ssid || S.ssid;             // AP 模式下 activeSsid = 热点名
  S.url = wifi.url || '';
  S.blenderIp = s.blenderIp || S.blenderIp;
  if (!$('blenderIp').value && S.blenderIp) $('blenderIp').value = S.blenderIp;
  if (Date.now() >= localUntil){
    S.power = !!s.power;
    S.color = s.color || S.color;
    S.brightness = clamp(Math.round(+s.brightness || 0), 0, 100);
    S.volume = clamp(+s.volume || 0, 0, +s.volumeMax || S.volumeMax);
    if (s.volumeMax) S.volumeMax = +s.volumeMax;
    S.effect = clamp(+s.effect || 0, 0, 6);
    S.eye = clamp(+s.eye || 0, 0, 2);
    var soundDirty = (s.bootSound && s.bootSound !== S.bootSound) ||
                     (s.shutdownSound && s.shutdownSound !== S.shutdownSound) ||
                     (s.humSound && s.humSound !== S.humSound);
    if (s.bootSound) S.bootSound = s.bootSound;
    if (s.shutdownSound) S.shutdownSound = s.shutdownSound;
    if (s.humSound) S.humSound = s.humSound;
    // 下拉只在新值与当前显示不一致时重建，避免轮询期间丢失选中焦点
    if (soundDirty && soundLoaded) renderSoundSelects();
  }
  render();
}
function poll(){
  fetch('/api/status').then(function(r){
    if (!r.ok) throw 0;
    return r.json();
  }).then(function(s){
    failStreak = 0;
    var raw = JSON.stringify(s);
    if (raw === lastRaw && !demo) return;
    lastRaw = raw;
    if (demo){ demo = false; $('demoPill').classList.add('hidden'); }
    applyStatus(s);
  }).catch(function(){
    if (demo) return;                        // 演示模式不受轮询失败影响
    failStreak++;
    if (failStreak < 2) return;              // 容忍 1 次偶发失败，避免视图闪切
    S.connected = false;
    $('reachMsg').textContent = '未检测到设备——请确认手机已连接热点 Esaber-Setup。';
    $('reachMsg').className = 'msg';
    render();
  });
}

/* ---------------- 修改指令 ---------------- */
function settingsPayload(){
  return {
    r: hexRgb(S.color)[0], g: hexRgb(S.color)[1], b: hexRgb(S.color)[2],
    brightness: S.brightness, volume: S.volume,
    effect: S.effect, eye: S.eye
  };
}
// 拖动时 input 事件以 ~60Hz 连发，这里做客户端节流：最小间隔 80ms、
// change/pointerup 收尾必发一次、载荷未变化直接跳过，请求率压到 ~12Hz。
var lastSentJson = null;
var lastSendAt = 0;
var sendTimer = null;
function sendSettingsNow(){
  if (sendTimer){ clearTimeout(sendTimer); sendTimer = null; }
  var payload = settingsPayload();
  var json = JSON.stringify(payload);
  if (json === lastSentJson) return;   // 值未变，跳过
  lastSentJson = json;
  lastSendAt = Date.now();
  api('/api/settings', payload).catch(function(){ toast('操作失败，正在重试同步', 'err'); });
}
function pushSettings(part){
  Object.assign(S, part);
  localUntil = Date.now() + 1500;
  render();
  var payload = settingsPayload();
  var json = JSON.stringify(payload);
  if (json === lastSentJson) return;   // 值未变，跳过
  var wait = 80 - (Date.now() - lastSendAt);
  if (wait <= 0){ sendSettingsNow(); return; }
  if (!sendTimer) sendTimer = setTimeout(sendSettingsNow, wait);  // 窗口内合并，只发尾包
}
function pushSettingsFinal(){
  if (!sendTimer) return;              // 无待发载荷则无需补发
  sendSettingsNow();                   // 收尾必发一次
}
function setPower(on){
  S.power = !!on;
  localUntil = Date.now() + 1500;
  render();
  api('/api/power', {state: on ? '1' : '0'}).catch(function(){ toast('操作失败，正在重试同步', 'err'); });
}

/* ---------------- 构建选择块 ---------------- */
(function buildControls(){
  var p = $('presets');
  PRESETS.forEach(function(pr){
    var b = document.createElement('button');
    b.type = 'button'; b.className = 'preset'; b.dataset.color = pr[1];
    b.style.background = pr[1]; b.title = pr[0]; b.setAttribute('aria-label', pr[0] + ' ' + pr[1]);
    b.onclick = function(){ pushSettings({color: pr[1]}); };
    p.appendChild(b);
  });
  var fx = $('effects');
  EFFECTS.forEach(function(e){
    var b = document.createElement('button');
    b.type = 'button'; b.className = 'tile ' + e.fx; b.style.setProperty('--c', S.color);
    b.innerHTML = '<i class="mini" aria-hidden="true"></i><span>' + e.name + '</span>';
    b.setAttribute('aria-pressed', 'false');
    b.onclick = function(){ pushSettings({effect: e.id}); };
    fx.appendChild(b);
  });
  var ey = $('eyes');
  var faces = [
    '<svg class="face" viewBox="0 0 44 44" aria-hidden="true"><circle cx="22" cy="22" r="16" fill="rgba(255,255,255,.12)" stroke="rgba(255,255,255,.25)"/><circle cx="22" cy="22" r="7.5" fill="var(--accent)"/><circle cx="19.5" cy="19.5" r="2.2" fill="#fff"/></svg>',
    '<svg class="face" viewBox="0 0 44 44" aria-hidden="true"><circle cx="22" cy="22" r="16" fill="rgba(255,255,255,.06)" stroke="rgba(255,255,255,.2)"/><path d="M12 24q10 7 20 0" stroke="rgba(255,255,255,.75)" stroke-width="2.2" fill="none" stroke-linecap="round"/></svg>',
    '<svg class="face" viewBox="0 0 44 44" aria-hidden="true"><circle cx="22" cy="22" r="16" fill="rgba(255,69,58,.10)" stroke="rgba(255,69,58,.3)"/><path d="M11 15l9 5M33 15l-9 5" stroke="rgba(255,255,255,.75)" stroke-width="2.2" stroke-linecap="round"/><circle cx="22" cy="26" r="6.5" fill="var(--accent)"/></svg>'
  ];
  faces.forEach(function(f, i){
    var b = document.createElement('button');
    b.type = 'button'; b.className = 'tile'; b.innerHTML = f + '<span>' + EYES[i] + '</span>';
    b.setAttribute('aria-pressed', 'false');
    b.onclick = function(){ pushSettings({eye: i}); };
    ey.appendChild(b);
  });
})();

/* ---------------- 事件绑定 ---------------- */
$('stage').addEventListener('click', function(){ setPower(!S.power); });
$('stage').addEventListener('keydown', function(e){
  if (e.key === 'Enter' || e.key === ' '){ e.preventDefault(); setPower(!S.power); }
});
$('powerSw').addEventListener('change', function(){ setPower(this.checked); });

$('colorPick').addEventListener('input', function(){
  pushSettings({color: this.value.toUpperCase()});
});
$('colorPick').addEventListener('change', pushSettingsFinal);
$('bright').addEventListener('input', function(){
  pushSettings({brightness: +this.value});
});
$('bright').addEventListener('change', function(){
  localUntil = Date.now() + 1200;   // 松手后留一个同步窗口
  pushSettingsFinal();
});
$('bright').addEventListener('pointerup', pushSettingsFinal);

$('volume').addEventListener('input', function(){
  pushSettings({volume: +this.value});
});
$('volume').addEventListener('change', function(){
  localUntil = Date.now() + 1200;
  pushSettingsFinal();
});
$('volume').addEventListener('pointerup', pushSettingsFinal);

$('bootSound').addEventListener('change', function(){ pushSound('bootSound', this.value); });
$('shutdownSound').addEventListener('change', function(){ pushSound('shutdownSound', this.value); });
$('humSound').addEventListener('change', function(){ pushSound('humSound', this.value); });

$('blenderForm').addEventListener('submit', function(e){
  e.preventDefault();
  var ip = $('blenderIp').value.trim();
  var parts = ip.split('.');
  var ok = parts.length === 4 && parts.every(function(x){ return /^\d{1,3}$/.test(x) && +x <= 255; });
  if (!ok){ $('blenderMsg').textContent = 'IP 格式不对，形如 192.168.1.100'; $('blenderMsg').className = 'msg err'; return; }
  $('blenderMsg').textContent = ''; $('blenderMsg').className = 'msg';
  api('/api/blender', {ip: ip}).then(function(){
    S.blenderIp = ip;
    toast('动捕地址已保存', 'ok');
  }).catch(function(){ toast('保存失败，请重试', 'err'); });
});

function openGuide(){ setView('connect'); poll(); }
$('netPill').addEventListener('click', openGuide);
$('gotoNet').addEventListener('click', openGuide);
$('gotoNet').addEventListener('keydown', function(e){
  if (e.key === 'Enter' || e.key === ' '){ e.preventDefault(); openGuide(); }
});
$('retryBtn').addEventListener('click', function(){
  $('reachMsg').textContent = '正在检测设备…'; $('reachMsg').className = 'msg';
  poll();
});
$('demoBtn').addEventListener('click', enableDemo);

/* ---------------- 启动 ---------------- */
render();
poll();
setInterval(poll, 2500);
})();
</script>
</body>
</html>)html";

}  // namespace

// 路由注册 + 各 API 入口。契约：
//   GET  /api/status   → {power, color, brightness, volume, effect, eye,
//                          bootSound, shutdownSound, humSound, blenderIp, wifi{...}}
//   POST /api/settings → 可选参数 r/g/b, brightness, volume, effect, eye,
//                          boot_sound, shutdown_sound, hum_sound（只改所带的槽）
//   GET  /api/sounds   → {ok, sounds:[...]}：SD 根目录的合法音频名
void WebService::begin(WebServer* server, SaberController* saber, WifiService* wifi,
                       SettingsStore* settings, MotionTelemetry* telemetry) {
  server_ = server;
  saber_ = saber;
  wifi_ = wifi;
  settings_ = settings;
  telemetry_ = telemetry;

  server_->on("/", HTTP_GET, [this]() { handleRoot(); });
  server_->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  server_->on("/api/sounds", HTTP_GET, [this]() { handleSounds(); });
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
  json["volume"] = saber.volume;
  // The console scales its slider by this, so 100 is simply "as loud as the
  // firmware will go" -- see MaxCodecVolume for what that costs.
  json["volumeMax"] = 100;
  json["blenderIp"] = settings_->blenderIp();
  // Selectable sound slots, echoed so the console dropdowns can restore the
  // current pick even when the SD listing changes underneath them.
  json["bootSound"] = saber.bootSound;
  json["shutdownSound"] = saber.shutdownSound;
  json["humSound"] = saber.humSound;
  // Build stamp, so the console can be told apart from a cached copy of an
  // older one and a flashed build from one that never made it onto the board.
  json["firmware"] = __DATE__ " " __TIME__;

  // Always the access point now, so these are the device's own address and
  // network name rather than a state that can change.
  JsonObject wifi = json["wifi"].to<JsonObject>();
  wifi["ssid"] = wifi_->ssid();
  wifi["url"] = wifi_->localUrl();

  char output[kStatusBufferSize];
  const size_t length = serializeJson(json, output, sizeof(output));
  server_->send(200, "application/json", String(output, length));
}

// Lists the playable audio files on the SD card root for the console
// dropdowns.  The walk is bounded by kMaxSoundEntries so a pathological card
// can never stall the loop task that also runs the audio decoder.
void WebService::handleSounds() {
  JsonDocument json;
  JsonArray sounds = json["sounds"].to<JsonArray>();
  if (SD_MMC.cardType() != CARD_NONE) {
    File root = SD_MMC.open("/");
    if (root && root.isDirectory()) {
      File entry = root.openNextFile();
      while (entry && sounds.size() < kMaxSoundEntries) {
        String name = entry.name();
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (validSoundName(name) && hasAudioExtension(name)) sounds.add(name);
        entry.close();
        entry = root.openNextFile();
      }
      root.close();
    }
  }
  json["ok"] = sounds.size() > 0;
  String output;
  serializeJson(json, output);
  sendJson(200, output);
}

void WebService::handleSettings() {
  SaberSettings settings = saber_->settings();
  // Sound slots are optional parameters: a request that only carries one
  // leaves the other slots untouched.  Unknown or absent files are rejected
  // outright so a slot can never be pointed at something unplayable.
  const auto applySound = [this, &settings](const char* arg, char* slot) -> bool {
    if (!server_->hasArg(arg)) return true;
    const String name = server_->arg(arg);
    if (!soundOnCard(name)) return false;
    strlcpy(slot, name.c_str(), kSoundNameLength);
    return true;
  };
  if (!applySound("boot_sound", settings.bootSound) ||
      !applySound("shutdown_sound", settings.shutdownSound) ||
      !applySound("hum_sound", settings.humSound)) {
    sendJson(400, "{\"ok\":false,\"error\":\"unknown sound\"}");
    return;
  }
  if (server_->hasArg("r")) settings.red = server_->arg("r").toInt();
  if (server_->hasArg("g")) settings.green = server_->arg("g").toInt();
  if (server_->hasArg("b")) settings.blue = server_->arg("b").toInt();
  if (server_->hasArg("brightness")) {
    settings.brightness =
        static_cast<uint8_t>(constrain(server_->arg("brightness").toInt(), 0, 100));
  }
  if (server_->hasArg("volume")) settings.volume = server_->arg("volume").toInt();
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
