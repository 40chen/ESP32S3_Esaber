#pragma once

#include <Arduino.h>

// ============================================================================
// HardwareConfig —— 全项目唯一的引脚分配 / 时序 / 调参常量表
// ============================================================================
// 所有可调常量集中在这一个命名空间里：改这里 = 改整机行为，不用翻源码。
// 分工边界：屏幕几何参数在 DisplayDriver，音效曲线参数在 SaberController。
//
// 【3.3V 供电优化 · 四项改动索引】（本次优化涉及的常量都标了【改动 N】）
//   改动① 背光 PWM：DisplayBacklightPwmFrequency / DisplayBacklightPwmRes /
//                    DefaultBacklightPct（本文件 display 段，86-88 行）
//   改动② CPU 降频：platformio.ini 的 board_build.f_cpu = 160000000L
//   改动③ beacon 拉长：WifiService.cpp 的 esp_wifi_config_beacon_interval
//   改动④ 开刃串行化：PaSettleMs（本文件 77 行）+
//                     SaberController 的 ignitionLightDue_ 门控
// ============================================================================
namespace HardwareConfig {

// -------------------------------------------------------------------- I2C
// IMU（MPU6050）总线。除非动硬件，不要改。
constexpr uint8_t I2cSda = 1;                    // 数据线 GPIO1
constexpr uint8_t I2cScl = 2;                    // 时钟线 GPIO2

// ---------------------------------------------------------------- SD card
// SD_MMC 1-bit 模式三根线（CLK/CMD/D0）。音效文件全在卡上。
constexpr int8_t SdClk = 47;                     // 时钟
constexpr int8_t SdCmd = 48;                     // 命令
constexpr int8_t SdD0 = 21;                      // 数据 0（1-bit 模式只用这一根）

// -------------------------------------------------------------- LED strip
constexpr uint8_t LedPin = 5;                    // WS2812 数据脚
constexpr uint16_t LedCount = 68;                // 灯珠数量（现实修改）
// 出厂亮度百分比（与 SaberSettings::brightness 同量纲 0-100），
// 由 SaberController::ledBrightness() 映射到 MaxLedBrightness 的刻度上。
constexpr uint8_t DefaultBrightness = 80;
// 100% 用户亮度对应的硬件刻度（满刻度 255）。这是供电预算之外的第二道保险——
// 真正把电流卡死的是下面的 LedCurrentBudgetMa，这里只是先压一层。
constexpr uint8_t MaxLedBrightness = 150;
// 功耗模型：一颗 WS2812 单通道满刻度约 20mA，所以整帧电流正比于所有通道值之和。
// 单纯的亮度上限卡不住它——纯白色会画出三倍于单通道的电流——
// 所以 PixelStrip::show() 逐帧累加通道和，超预算就整体等比压暗。
//
// LedCurrentBudgetMa = 150 是本机实测能站住的值：纯红 @亮度100 从未掉电，
// 而 #FF33CC（红+大部分蓝）同样亮度就触发过 BROWNOUT。
// 等 5V 轨能扛更多再上调——通常缺的是灯条电源端的滤波大电容。
constexpr uint16_t LedChannelMilliamps = 20;     // 单通道满刻度电流估算
constexpr uint16_t LedCurrentBudgetMa = 150;     // 整帧通道和电流预算（实测口径）

// ------------------------------------------------------------------ audio
// ES8311 codec 的 I2S 四线 + 功放使能脚。
constexpr uint8_t I2sMck = 38;                   // 主时钟
constexpr uint8_t I2sBck = 14;                   // 位时钟
constexpr uint8_t I2sWs = 13;                    // 字选择（左右声道）
constexpr uint8_t I2sDo = 45;                    // 数据输出（ESP→codec）
constexpr uint8_t PaEnable = 9;                  // 功放 PA_EN（高电平开）
// 软件音量：ESP32-audioI2S 只有 0..21 共 22 级，21 = 原样输出（表顶 64/64）。
// 在软件里降采样会直接丢精度，所以听感音量放在 codec 上调，这里恒定满档。
constexpr uint8_t AudioVolume = 21;
// ES8311 DAC 音量（百分比）。驱动按 255*log10(9v/100+1) 映射到 -95.5dB 起步、
// 0.5dB 步进：50 → -1.5dB，52 → +0.5dB，56 → +4dB。
// 驱动默认 70 = +14.5dB——之前听到的底噪嘶声就是这个增益放出来的。
//
// CodecVolume = 50 对应控制台音量滑条拉到 DefaultVolume 时的实际响度 = 0dB，
// 这是音效文件不削波的上限（它们按满刻度归一化）。滑条可以推到 MaxCodecVolume
// 换更大声，代价是最响的峰会被削平。
constexpr uint8_t CodecVolume = 50;              // 常规音量上限（0dB）
constexpr uint8_t MaxCodecVolume = 62;           // 滑条可推的极限（有削波风险）
constexpr uint8_t DefaultVolume = 80;            // 出厂音量百分比
// 20x30mm 4Ω 小喇叭的音色补偿：500Hz 以下的低搁架它根本发不出来，
// 只会白白拖累功放，砍掉低频把余量让给人声频段。单位 dB，范围 -40..+6。
constexpr int8_t ToneLowShelf = -12;             // 低搁架衰减
constexpr int8_t TonePeak = 1;                   // 中频峰化
constexpr int8_t ToneHighShelf = 2;              // 高搁架补偿
// 切换音源时 codec 静音的时长。静音必须在旧流被丢弃【之前】生效
//（否则 DMA flush 会切在波形中间出爆音），也要在新流开始后【尽快】解除，
// 否则碰撞音的第一个样本会被吃掉。
constexpr uint16_t MuteSwitchMs = 25;
// 功放电源切换的稳定窗口：PA_EN 拉高后先等这么久再喂声音（避免开机砰声），
// 输出静音后也等这么久再拉低 PA_EN（避免关机噪声）。
// 【改动④关联】开刃串行化用同一个常量：PA 使能 → 等 PaSettleMs → 灯条才亮首帧，
// 让功放涌流和 LED 电流错开 tick，不在 3.3V 轨上叠加。
constexpr uint16_t PaSettleMs = 20;

// SD 卡音效双目录：固件启动时自动创建（已存在则跳过）。
// 目录用 ASCII 命名——SD 卡文件系统默认代码页对中文长名支持不稳，
// 「默认音效 / 我的音效」的中文标签由控制台前端负责显示。
constexpr const char* const SfxDefaultDir = "sfx_default";  // 出厂音效（audio packet 全集）
constexpr const char* const SfxUserDir = "sfx_user";        // 用户自定义音效

// ---------------------------------------------------------------- display
constexpr int8_t DisplayBacklight = 8;           // 背光控制脚（GC9A01 圆屏）
// 背光是 3.3V 轨（AMS1117 供电）上最大的静态负载，所以用 LEDC PWM 驱动
// 而不是普通数字高电平。
// 【改动①】三件套如下：
//   - 10kHz：在可听频段之上（功放里不会啸叫），也避开了相机条纹干扰区间；
//   - 10bit：占空比刻度 0..1023，66% 对应 675，无截断；
//   - 66%：室内观感与全亮几乎无差别，静态电流直降约 1/3。
//   真机验收硬指标：QR 码界面必须仍能被手机扫码。
constexpr uint32_t DisplayBacklightPwmFrequency = 10000;  // PWM 频率 Hz
constexpr uint8_t DisplayBacklightPwmRes = 10;   // PWM 分辨率 bit（duty 0..1023）
constexpr uint8_t DefaultBacklightPct = 66;      // 默认占空比 %（改动①本体）

// ----------------------------------------------------------------- button
constexpr int8_t BootButton = 0;                 // 板载 BOOT 键（切眼睛/QR 界面）
constexpr uint32_t ButtonDebounce = 40;          // 消抖窗口 ms

// ------------------------------------------------------------- task budget
// 主循环任务栈大小。通过 main.cpp 里对 Arduino core 弱符号
// getArduinoLoopTaskStackSize() 的强覆盖生效（默认 8KB 不够音频+web 共用）。
constexpr uint32_t LoopStackSize = 16384;

// ----------------------------------------------------------------- motion
// MPU6050 采样与动捕遥测节拍。
constexpr uint16_t MotionInterval = 10;          // IMU 采样周期 ms（100Hz）
constexpr uint16_t TelemetryInterval = 20;       // UDP 遥测发送周期 ms（50Hz，动捕协议 v1）
constexpr uint16_t TelemetryPort = 5005;         // 遥测端口（Blender 端脚本同端口）
constexpr float AccelLsbPerGravity = 2048.0f;    // ±16g 量程：2048 LSB/g
constexpr float GyroLsbPerDegree = 131.0f;       // ±250dps 量程：131 LSB/(°/s)
constexpr float PositionDeadZone = 0.15f;        // m/s²，低于此的加速度视为漂移清零
constexpr float PositionLimit = 2.0f;            // m，位置积分钳制（防 Blender 飞走）

// --------------------------------------------------------- saber gameplay
// 挥剑/碰撞/手势判定阈值（量纲见 MotionSensor 的 magnitude/rotation 计算）。
constexpr uint32_t SwingTimeout = 500;           // 挥动判定后的事件窗口 ms
constexpr uint16_t SwingLowThreshold = 80;       // 慢速挥动阈值
constexpr uint16_t SwingThreshold = 180;         // 快速挥动阈值（选用快挥音效组）
constexpr uint16_t StrikeThreshold = 40;         // 碰撞触发阈值
constexpr uint16_t HardStrikeThreshold = 160;    // 重击阈值（闪光更久）
constexpr uint16_t OpenThreshold = 60;           // 旋腕开合检测阈值
constexpr uint8_t GestureToggleCount = 20;       // 旋腕切换所需的累计计数
constexpr uint32_t GestureInterval = 50;         // 手势累计采样周期 ms
// 底噪循环靠播放结束的下降沿重新拉起（SaberController 通过
// AudioOutput::isRunning 感知），所以不需要时长/延时常量：
// 用户换任何长度的 hum 文件都能直接用。
constexpr uint32_t SwingCooldown = 100;          // 两次挥动音效最小间隔 ms
constexpr uint8_t PulseAmplitude = 10;           // 呼吸特效振幅
constexpr uint16_t PulseDelay = 30;              // 呼吸特效节拍 ms
constexpr uint16_t FlashDelay = 15;              // 碰撞闪光节拍 ms
// 碰撞闪光：刀色向白色冲淡（方向量由 StrikeFlashWhiteBlend 控制），
// 持续时长随撞击力度缩放，HitExtraMs 在 100% 力度时叠加。
constexpr uint16_t HitBaseMs = 180;              // 基础闪光时长 ms
constexpr uint16_t HitExtraMs = 170;             // 满力度附加时长 ms
constexpr uint8_t StrikeFlashWhiteBlend = 165;   // 0=保持刀色，255=纯白
constexpr uint8_t ScannerTrailLength = 8;        // 扫描特效拖尾长度
constexpr uint16_t RainbowHueStep = 512;         // 彩虹特效每帧色相步进
constexpr uint16_t RainbowSwingBoost = 25;       // 挥动越快色相滚动越快的加成

// ------------------------------------------------- unstable / sparkle / fire
// 不稳定 / 星尘 / 火焰三个特效的手感参数。
constexpr uint16_t FlickerIntervalMs = 45;       // 不稳定：抖动周期 ms
constexpr int8_t FlickerAmplitude = 9;           // 不稳定：每像素常驻抖动
constexpr int8_t FlickerSurgeAmplitude = 55;     // 不稳定：偶发等离子涌动幅度
constexpr uint8_t FlickerSurgeChancePercent = 6; // 不稳定：涌动触发概率 %
constexpr uint16_t SparkleIntervalMs = 70;       // 星尘：周期 ms
constexpr uint8_t SparkleCount = 2;              // 星尘：每帧白色闪点数
constexpr uint16_t FireIntervalMs = 35;          // 火焰：帧周期 ms
constexpr uint8_t FireCooling = 55;              // 火焰：冷却 0-255，越大火苗越短
constexpr uint8_t FireSparking = 90;             // 火焰：每帧火星概率（满 256）

// ----------------------------------------------------------- eye tracking
// roll/pitch（弧度）推到多大会把视线推到活动范围边缘。
constexpr float EyeGazeRadiansHorizontal = 1.0f; // 水平视线满偏角
constexpr float EyeGazeRadiansVertical = 1.4f;   // 垂直视线满偏角

// ------------------------------------------------------------- networking
constexpr const char* DefaultApSsid = "Esaber-Setup";   // 出厂热点名
// 故意不设密码：配置热点保持开放，设备配置错了也永远进得去，
// 不依赖记住密码。WiFi.softAP 传 NULL 即 WIFI_AUTH_OPEN——
// 如果传了短于 8 位的密码，调用会直接失败，设备连热点都没有了。
constexpr const char* DefaultApPassword = nullptr;      // NULL = 开放热点
constexpr const char* PreferencesNamespace = "esaber";  // NVS 命名空间
constexpr uint32_t WifiConnectTimeout = 15000;          // STA 连接超时 ms（保留）

}  // namespace HardwareConfig
