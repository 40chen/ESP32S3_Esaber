# ESP32S3_Esaber · 固件改动点地图（速查表）

> 产出：通用助手 ｜ 2026-09-28 ｜ 供真机验收与回滚速查
> 对应提交：**0a8dce3**（main，本地领先 origin 1 提交，待推送）· 基线 e23cb1d
> 范围：14 文件，+337 / −34 · 六块主题 = 验收口径【改动①~④】+【音效集】+【修复】
> 行内逐行注释：设计匠人单一写入中（本文与其互补：一个管速查/回滚，一个管逐行理解）
> ⚠️ 行号以 0a8dce3 原始终态为准；行内注释合入后行号会漂移，请以「符号/函数名」为锚。
> 精确 diff 随时可查：`git show 0a8dce3 -- <文件>`

## 总览

| # | 主题 | 一句话 | 主要文件 |
|---|---|---|---|
| ① | 背光 PWM 调光 | 数字高 → LEDC PWM 66%，3.3V 最大稳态负载减半 | HardwareConfig.h / DisplayDriver.cpp |
| ② | CPU 降频 + PSRAM 声明 | 240→160MHz 降动态电流；BOARD_HAS_PSRAM 显式化 | platformio.ini |
| ③ | AP beacon 降频 | 100→300ms，radio 周期发包 1/3 | WifiService.cpp |
| ④ | 开刃串行化 | PA 使能→20ms 稳定窗→灯条首帧，峰值错峰防棕死 | SaberController.cpp/.h |
| ⑤ | 音效三下拉 | 开机/关机/底噪 wav 用户可选，SD 扫描 + NVS 持久化 | WebService / SettingsStore / AppTypes / SaberController |
| ⑥ | 编译修复 | 删重复 now 声明（编译错源）+ 死常量/humTimer_ 清理 | SaberController.cpp / HardwareConfig.h |

---

## 【改动①】背光 PWM 调光

| 位置（0a8dce3） | 内容 |
|---|---|
| `include/HardwareConfig.h:82-88` | 新增 `DisplayBacklightPwmFrequency=10000`（10kHz）、`DisplayBacklightPwmRes=10`（10bit，0..1023）、`DefaultBacklightPct=66` 三常量 |
| `src/drivers/DisplayDriver.cpp:32-38` | `kBacklightPwmChannel=0`（TFT_eSPI 不占 LEDC 通道，通道 0 安全） |
| `src/drivers/DisplayDriver.cpp:68-74` | `begin()` 中引脚移交 LEDC：`ledcSetup → ledcAttachPin → ledcWrite(0)`，0 占空起步电平不变 |
| `src/drivers/DisplayDriver.cpp:83-88` | `enableBacklight()`：`digitalWrite(HIGH)` → `ledcWrite(66% × maxDuty)`，**全工程唯一占空入口** |

- **为什么**：背光是 3.3V（AMS1117 供电）最大稳态负载；10kHz 高于人耳频段（不啸叫进功放）且避开相机条纹频段；66% 室内视觉近无感。
- **真机验收**：QR 码屏幕在 66% 亮度下仍可扫；无高频电流音。
- **回滚**：删 3 个常量 + `kBacklightPwmChannel`，`begin()` 删 3 行 LEDC 接线，`enableBacklight()` 恢复 `digitalWrite(HardwareConfig::DisplayBacklight, HIGH)`。

## 【改动②】CPU 160MHz + PSRAM 显式声明

| 位置（0a8dce3） | 内容 |
|---|---|
| `platformio.ini:14-19` | `board_build.f_cpu = 160000000L`（原默认 240MHz） |
| `platformio.ini:21-31` | `build_flags` 新增 `-D BOARD_HAS_PSRAM`（8MB octal PSRAM 显式启用）；同块缩进 tab→空格统一（无语义变化） |

- **为什么**：眼睛渲染/音频解码/网页控制台 160MHz 余量充足，动态电流与 AMS1117 压差发热双降；PSRAM 本来就在用，加 flag 是显式化——音频 64KB 缓冲与眼睛贴图依赖它，缺了会落回内部堆。
- **真机验收**：眼睛动画帧率可接受、控制台无卡顿、长时运行温升降低。
- **回滚**：`f_cpu` 改回 `240000000L` 即可（注释里已写明）；⚠️ `-D BOARD_HAS_PSRAM` **不要删**（音频缓冲依赖）。

## 【改动③】AP beacon 间隔 300ms

| 位置（0a8dce3） | 内容 |
|---|---|
| `src/services/WifiService.cpp:4` | `#include <esp_wifi.h>` |
| `src/services/WifiService.cpp:30` | `startAccessPoint()` 内 `softAP` 之后调用 `esp_wifi_config_beacon_interval(WIFI_IF_AP, 300)` |

- **为什么**：beacon 默认 100ms 一发，300ms 让 radio 周期发包降为 1/3；手机停在控制台页无感（IDF 合法区间 100-60000ms）。必须在 `softAP` 之后调用才生效。
- **真机验收**：待机功耗下降；控制台页面连接、加载正常。
- **回滚**：删该行 + include。

## 【改动④】开刃串行化（峰值电流错峰）

| 位置（0a8dce3） | 内容 |
|---|---|
| `src/app/SaberController.h:66-68` | +成员 `ignitionLightDue_`（灯条首帧到点时刻）；同文件删除 `humTimer_`（归⑤/⑥） |
| `src/app/SaberController.cpp:161` | `setPower(true)`：`ignitionLightDue_ = millis() + PaSettleMs(20ms)` —— PA 先使能并独占 20ms 稳定窗 |
| `src/app/SaberController.cpp:295-297` | `updateLighting()` 执行端：`now < ignitionLightDue_` 直接 return，灯条首帧等 PA 稳定窗过去才铺开 |
| `src/app/SaberController.cpp:162` | `effectTimer_ = millis()` 同步重置（动画时基） |

- **为什么**：PA 浪涌与 LED 全亮电流不同 tick 共用 3.3V，消除开刃瞬间压降棕死；20ms 视觉不可见。
- **真机验收**：开刃不重启/不闪屏，开刃动画正常自下而上铺开。
- **回滚**：删 `ignitionLightDue_` 两处 + `updateLighting` 恢复立即填充（注意原注释即说明"整条 fill 会击穿供电"，回滚=接受该风险）。

## 【音效集】三下拉音效全链路（功能项）

数据流：网页卡片 → `/api/settings` → `SettingsStore`(NVS) → `SaberSettings` → 播放。

| 位置（0a8dce3） | 内容 |
|---|---|
| `include/AppTypes.h:26-31` | `kSoundNameLength=48`；定长 `char` 数组而非 String（避免 loop 任务堆碎片） |
| `include/AppTypes.h:49-53` | `SaberSettings` +`bootSound="endlock1.wav"` / `shutdownSound="endlock2.wav"` / `humSound="111.wav"` |
| `src/services/SettingsStore.h:18-19` | 私有助手 `readSound()` 声明（String→定长缓冲 strlcpy） |
| `src/services/SettingsStore.cpp:36-38` | `begin()` 读 NVS 三键：`boot_snd` / `off_snd` / `hum_snd` |
| `src/services/SettingsStore.cpp:48-51` | `readSound()` 实现 |
| `src/services/SettingsStore.cpp:70-72` | `saveSaber()` 写 NVS 三键（写频=网页保存频率，无 NVS 磨损顾虑） |
| `src/web/WebService.h:18` | +`handleSounds()` 声明 |
| `src/web/WebService.cpp:892` | `begin()` 注册路由 `GET /api/sounds` |
| `src/web/WebService.cpp:946` | `handleSounds()` 实现：SD 卡**根目录**扫描 .wav → JSON 列表 |
| `src/web/WebService.cpp:920-924` | `handleStatus()` 附带当前三个音效名（页面回显） |
| `src/web/WebService.cpp:1-52,186-193` | INDEX_HTML：音效卡片样式（⚠️ 原始字符串区，只改样式与 DOM，无 C++ 注释） |
| `src/web/WebService.cpp:396-413` | 音效卡片 HTML：开机/关机/底噪三个下拉，位于特效卡片下方 |
| `src/web/WebService.cpp:468,518-523,534-603` | 前端 JS：`/api/sounds` 拉列表、demo 兼容、保存 + toast「音效已保存」、失败回滚、SD 空时禁用下拉并提示 |
| `src/web/WebService.cpp:699-706,839-842` | `applyStatus()` 回填三个下拉值 + change 监听提交 |
| `src/app/SaberController.cpp:15-17` | −硬编码 `kPowerOnSound/kPowerOffSound`（改由设置驱动） |
| `src/app/SaberController.cpp:118` | `setSettings()` +`humChanged` 检测（strcmp） |
| `src/app/SaberController.cpp:135-136` | 底噪即选即换：播放中热切换 `audio_->play(settings_.humSound)`，即刻可闻 |
| `src/app/SaberController.cpp:152` | 开机音：`play(settings_.bootSound)`（原 `endlock1.wav` 硬编码） |
| `src/app/SaberController.cpp:166` | 关机音：`play(settings_.shutdownSound)` |
| `src/app/SaberController.cpp:276-281` | `updateHum()` 重写：删 humTimer_ 定时器机制 → `audio_->isRunning()` **下降沿重挂**（任何时长的文件都通用，不内建定时） |
| `src/drivers/AudioOutput.h:36-40` / `.cpp:100-102` | +`isRunning() const`（供上述下降沿判断） |

- **语义变化**：底噪不再"到点强制抢占"，而是等当前音（开刃音/挥砍/碰撞）自然播完后续上——听感更顺，任意时长 wav 通用。
- **回滚**：整块 revert；或最小回滚 = 恢复 3 个 Hum 常量 + humTimer_ 旧机制 + 两处硬编码音名。

## 【修复】编译修复与死代码清理

| 位置（0a8dce3） | 内容 |
|---|---|
| `src/app/SaberController.cpp:148` | −`setPower()` 内重复的 `const unsigned long now` 声明（重复声明是编译错误源） |
| `src/app/SaberController.cpp:175` | `effectTimer_ = millis()` 直接调用（原 `now` 引用已随上条删除） |
| `include/HardwareConfig.h:114-120` | −`HumTimeout / HumActivationDelay / HumSoundDelay` 三常量（旧 hum 定时机制的死配置，被⑤的下降沿机制取代） |

---

## 回滚三档

1. **单块回滚**：按上表定位，`git show 0a8dce3 -- <文件>` 对照手工还原；行内注释合入后行号漂移，**以函数名/常量名为锚**。
2. **整提交回滚**：`git revert 0a8dce3`——若届时行内注释已 commit，revert 会与纯注释行冲突，按"保留注释、还原代码"手工解冲突。
3. **全退**：`git reset --hard e23cb1d`（连音效三下拉一起丢弃，慎用）。

## 真机验收清单（对照四项方向）

- [ ] ① QR 码 66% 亮度可扫、无啸叫
- [ ] ② 眼睛动画流畅（160MHz）、AMS1117 温升明显降低
- [ ] ③ AP 待机发包降为 1/3、控制台响应正常
- [ ] ④ 开刃瞬间不棕死不重启、动画正常铺开
- [ ] ⑤ 音效三下拉：切换出 toast「音效已保存」、断电重启读回、SD 空时禁用并提示
