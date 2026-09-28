// ============================================================================
// AppTypes —— 跨层共享的类型定义（特效/表情枚举、设置结构体）
// ============================================================================
#pragma once

#include <Arduino.h>

// 灯条特效（web 的 effect 参数即枚举值，顺序不可变）
enum class SaberEffect : uint8_t {
  Solid = 0,      // 常亮
  Pulse = 1,      // 呼吸
  Rainbow = 2,    // 彩虹
  Scanner = 3,    // 扫描
  Unstable = 4,   // 不稳定
  Fire = 5,       // 火焰
  Sparkle = 6,    // 星尘
};

// 屏幕眼睛表情
enum class EyePattern : uint8_t {
  Normal = 0,     // 普通
  Sleep = 1,      // 瞌睡
  Angry = 2,      // 生气
};

// 用于钳制来自 NVS 或 web API 的值：陈旧/畸形值永远造不成越界枚举
constexpr uint8_t kSaberEffectCount = 7;
constexpr uint8_t kEyePatternCount = 3;

// 用户可选音效槽位的文件在 SD 卡根目录。用定长缓冲而非 String：
// 它们随每次 SaberSettings 拷贝走，loop 任务的堆最不该被长 web 会话
// 碎片化。48 字节足够放任何正常的音效包文件名。
constexpr uint8_t kSoundNameLength = 48;

// 控制台可改的全部设置：web 端读写、NVS 持久化、业务层消费
struct SaberSettings {
  bool power = false;   // 开刃状态（web 开关 / IMU 翻腕手势都能改）
  // #FF33CC，新机开箱的粉紫色。只作为 NVS 键缺失时的默认值——
  // 已配置的设备保持它上次设置的刀色。
  uint8_t red = 255;
  uint8_t green = 51;
  uint8_t blue = 204;
  // 用户视角亮度 0-100。映射到的灯条刻度受 MaxLedBrightness 钳制，
  // 5V 轨允许供多少电流最终由 PixelStrip 的限流器说了算。
  uint8_t brightness = 80;
  // 音量，codec 上限的百分比。固件默认是效果音文件不削波的最高电平。
  uint8_t volume = 80;
  SaberEffect effect = SaberEffect::Pulse;
  EyePattern eyePattern = EyePattern::Normal;
  // 开机、关机、待机嗡鸣循环。默认值与卡上预置的经典音效包对应。
  char bootSound[kSoundNameLength] = "endlock1.wav";
  char shutdownSound[kSoundNameLength] = "endlock2.wav";
  char humSound[kSoundNameLength] = "111.wav";
};
