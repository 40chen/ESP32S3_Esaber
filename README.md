# ESABER — ESP32-S3 智能光剑

基于 ESP32-S3 的光剑固件：MPU6050 姿态解算驱动灯效与音效，圆形 IPS 屏显示会跟随动作的眼睛，板载 Web 控制台负责配置，同时把姿态数据通过 UDP 推送给 Blender 做动作捕捉。

---

## 项目目标

1. **光剑功能**：根据陀螺仪的数据变化形成不同的灯效和音效
2. **屏幕功能**：显示 Web 二维码辅助 Wi-Fi 登录；按下 BOOT 键调出二维码，再次按下退出；不显示二维码时，显示一只随陀螺仪数据左看右看的眼睛
3. **Web 功能**：一级菜单输入 Wi-Fi 账号密码，确认后进入二级菜单；二级菜单可自定义光剑颜色、亮度、特效，并切换预设眼睛图案，UI 参照苹果设计风格
4. **Blender 动捕**：将陀螺仪数据传送到 Blender 模型，让虚拟模型与现实光剑联动

---

## 功能特性

### 光剑

- **开关剑**：关剑状态下快速翻转手腕即可点亮，剑身逐像素由内向外展开，同时播放开机音；关剑播放收剑音
- **敲击**：检测到瞬时冲击时闪黄光并播放随机碰撞音（`clsh1..10.wav`）
- **挥动**：按角速度大小播放随机挥剑音（`swng1..14.wav`），弱挥动只从音效库里取前 7 个
- **底噪**：循环播放 `hum1.wav`，挥剑与敲击时重新起播，保证剑鸣始终在音效之下可闻
- **灯效**：常亮 / 呼吸 / 彩虹 / 扫描四种，见下表

| 特效 | 枚举值 | 说明 |
| --- | --- | --- |
| 常亮 Solid | 0 | 固定颜色 |
| 呼吸 Pulse | 1 | 在基础色上做平滑随机游走，幅度 ±10 |
| 彩虹 Rainbow | 2 | 整条灯带色相循环 |
| 扫描 Scanner | 3 | 8 像素拖尾往返扫描 |

### 屏幕（GC9A01 圆形屏，240×240）

- **眼睛**：跟随 roll / pitch 转动的眼球，带瞳孔、眼睑、眉毛；待机时随机小幅游移，并以 2.2–6.4 秒的随机间隔眨眼
- **三种表情**：正常 Normal / 睡觉 Sleep（几乎闭眼）/ 生气 Angry（上眼睑下压并加粗眉）
- **二维码**：显示控制台的访问地址，供手机扫码进入 Web 界面
- **切换**：BOOT 键在「眼睛 ↔ 二维码」之间切换

### Web 控制台

- 未联网时只显示 Wi-Fi 表单；连接成功后自动切换到控制页
- 可调：电源开关、颜色（取色器）、亮度、特效、眼睛图案、Blender 目标 IP
- 单页内嵌在固件里（`WebService.cpp` 的 `INDEX_HTML`），无需外部文件系统

### Blender 动捕

- 以 10 Hz 向配置的 IP 的 **5005** 端口发送 UDP 包
- 包体为逗号分隔的 7 个浮点数：`qw,qx,qy,qz,px,py,pz`（姿态四元数 + 位置，单位米）

---

## 硬件

| 功能 | 引脚 | 备注 |
| --- | --- | --- |
| I2C SDA / SCL | 1 / 2 | MPU6050 与 ES8311 共用 |
| SD_MMC CLK / CMD / D0 | 47 / 48 / 21 | 1-bit 模式 |
| LED 数据 | 5 | 56 颗 WS2812（GRB，800 kHz） |
| I2S MCK / BCK / WS / DO | 38 / 14 / 13 / 45 | ES8311 音频输出 |
| PA_EN | 9 | 功放使能，待机时拉低 |
| 屏幕 MOSI / SCLK / CS / DC | 18 / 17 / 16 / 15 | GC9A01，SPI，40 MHz |
| 屏幕背光 | 8 | |
| BOOT 按键 | 0 | 内部上拉，40 ms 消抖 |

开发板：`4d_systems_esp32s3_gen4_r8n16`（ESP32-S3 + 8 MB PSRAM + 16 MB Flash）。

### SD 卡内容

音频全部从 SD 卡读取，需放在卡根目录：

```
saber.flac     开剑音
out1.wav       收剑音
hum1.wav       剑鸣底噪（循环）
clsh1..10.wav  碰撞音
swng1..14.wav  挥剑音
```

> 仓库里不再附带示例音频，需要自备。SD 卡挂载点 `/sdcard`，若音频缺失则只会没有声音，不影响其他功能。

---

## 软件架构

分层结构，`SystemController` 是唯一的组装点，其余模块之间不互相持有。

```
                    SystemController
        （持有全部子系统，驱动主循环，处理 BOOT 键与屏幕切换）
                            │
    ┌───────────────┬───────┴───────┬──────────────┐
    │               │               │              │
SaberController  WebService   MotionTelemetry   DisplayDriver
 （灯效+音效+手势） （HTTP API）  （UDP → Blender）      （眼睛/二维码）
    │               │               │
    └───────┬───────┴───────┬───────┘
            │               │
      SettingsStore    WifiService
        （NVS）        （AP + STA）
            │
    ┌───────┴───────────────────────────────┐
    │ AudioOutput  PixelStrip  MotionSensor │  ← Driver 层
    │ SdCardDriver            DisplayDriver │
    └───────────────────────────────────────┘
```

### 目录

```
include/
  HardwareConfig.h    所有引脚、时序与调参常量集中于此
  AppTypes.h          SaberSettings / SaberEffect / EyePattern
  TFT_eSPI_Setup.h    TFT_eSPI 的 USER_SETUP，由 platformio.ini 强制包含

src/
  main.cpp            建立 16 KB 主循环栈，启动 SystemController
  core/
    SystemController  组装子系统、主循环、复位原因与资源诊断、BOOT 键与屏幕模式
  app/
    SaberController   手势识别、敲击/挥动/剑鸣、四种灯效与开关剑动画
  web/
    WebService        HTTP 路由、状态 JSON、内嵌控制台页面
  services/
    SettingsStore     NVS 持久化（颜色、亮度、特效、眼睛、Wi-Fi、Blender IP）
    WifiService       AP 与 STA 并存，负责连接状态机
    MotionTelemetry   把四元数与位置打包成 UDP 发往 Blender
  drivers/
    MotionSensor      MPU6050 + Madgwick 姿态解算
    AudioOutput       ES8311 编解码 + ESP32-audioI2S 播放
    PixelStrip        WS2812 灯带封装
    DisplayDriver     眼睛动画与二维码渲染
    SdCardDriver      SD_MMC 挂载（含降频重试）
lib/
  arduino-audio-driver-main   ES8311 板级驱动的本地副本
```

### 主循环顺序

`SystemController::update()` 的顺序不是随意的：

1. `saber_.update()` —— 音频解码优先喂饱，后面几步都可能阻塞
2. `server_.handleClient()` / `wifi_.update()`
3. `telemetry_.update()`
4. `handleBootButton()`
5. 按屏幕模式渲染眼睛或二维码

---

## Web API

| 方法 | 路径 | 参数 | 说明 |
| --- | --- | --- | --- |
| GET | `/` | — | 控制台页面 |
| GET | `/api/status` | — | 设备状态 JSON |
| POST | `/api/wifi` | `ssid`, `password` | 保存并尝试连接 Wi-Fi |
| POST | `/api/settings` | `r`, `g`, `b`, `brightness`, `effect`, `eye` | 更新外观设置 |
| POST | `/api/power` | `state`（`1`/`0`） | 开剑 / 关剑 |
| POST | `/api/blender` | `ip` | 保存 Blender 目标地址 |

`/api/status` 返回示例：

```json
{
  "power": true, "color": "#FF0000", "brightness": 100,
  "effect": 1, "effectName": "pulse",
  "eye": 0, "eyeName": "normal",
  "blenderIp": "192.168.10.5",
  "wifi": { "connected": true, "connecting": false,
            "ssid": "MyWiFi", "url": "http://192.168.1.42" }
}
```

参数缺省时保持原值；`effect` / `eye` 越界会被忽略，`brightness` 会被钳到上限。

### 网络行为

- 上电即开启配置热点 **`Esaber-Setup` / `esaber123`**，二维码指向当前可用的访问地址
- 连上家里的 Wi-Fi 后**热点不会关闭**：早期版本用 `softAPdisconnect()` 切到纯 STA 模式，既容易在 HTTP 客户端挂着时崩溃，一旦密码输错也会彻底失去入口
- 默认 Blender 目标地址 `192.168.10.5`，可在 Web 界面修改

---

## 构建与烧录

```bash
pio run                 # 编译
pio run -t upload       # 烧录
pio device monitor      # 串口日志（115200）
```

`platformio.ini` 要点：`default_16MB.csv` 分区表、`ARDUINO_USB_MODE=1`，并通过 `-include include/TFT_eSPI_Setup.h` 强制注入屏幕配置（TFT_eSPI 没有随环境变量走的标准配置方式）。

### 串口诊断

每次启动都会打印复位原因（区分掉电、崩溃、看门狗、**BROWNOUT**）以及堆 / PSRAM 余量；启动 5 秒后会补一条主循环栈余量：

```
[ESABER] reset reason: power-on (1)
[ESABER] heap: 341224 free / 405508 total, psram: 8321024 free
[ESABER] loop stack headroom: 9820 bytes of 16384, heap: 336488 free
```

栈余量持续走低就说明 16 KB 不够用了，需要继续调大 `HardwareConfig::LoopStackSize`。

---

## 值得注意的实现细节

这些是踩过坑之后留下的处理，改动相关代码前值得先读一眼：

- **亮度上限 150**：56 颗 WS2812 全白超过 1 A，会把电源拉垮导致 MCU 复位，所以无论面板要求多少都要钳位
- **开剑逐像素展开**：一次性点亮 56 颗的电流冲击同样会掉电重启，改为逐像素填充的动画
- **眼睛走 PSRAM sprite**：直接刷屏时，一帧 240×240 的 SPI 耗时超过渲染间隔，画面总被拍在中途而闪烁；改用 sprite 整帧推送，并在画面无变化时完全跳过 SPI
- **加速度量程 16g**：`MPU6050_ACCEL_FS_16` 是 2048 LSB/g，早先用 16384 做除数导致加速度小了 8 倍，去重力后残留约 8.6 m/s² 的偏置，送给 Blender 的位置一路顶到钳位值
- **位置死区**：静止时把加速度与速度清零，否则积分漂移会把位置推走；位置同样做了 ±2 m 钳位
- **SD 降频重试**：初始化报 0x107 超时（信号裕量不足）时，用 10 MHz 重挂一次
- **`Preferences` 读取前先 `isKey()`**：否则首次启动时每个缺失的键都会打一条 error 日志，看起来像坏了
