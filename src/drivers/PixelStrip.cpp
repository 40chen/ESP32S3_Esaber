// ============================================================================
// PixelStrip —— WS2812 灯条：帧缓冲 + 亮度 + 供电限流三合一
// ============================================================================
#include "PixelStrip.h"

namespace {

// 整帧通道和的供电上限，单位与亮度缩放一致（每通道 0..255）：
// 预算 mA × 255 / 单通道 mA = 允许的通道和
constexpr uint32_t kChannelBudget =
    (static_cast<uint32_t>(HardwareConfig::LedCurrentBudgetMa) * 255) /
    HardwareConfig::LedChannelMilliamps;

constexpr unsigned long kLimitLogIntervalMs = 1000;   // "被限流"日志节流

// ----------------------------------------------------------------------------
// 物理序 → 数据链 remap（规格图《ESaber 灯序 remap 规格图》05 节 方案 A，定稿表）。
// kRemap[物理序 i] = 数据链编号：物理序 i 是自 P0 起沿进线数的第 i 颗，i=0 在
// 刀柄侧（IO 引出端，管理员 2026-10-03 拍板）；数据链编号即效果代码的像素下标，
// 0 = 刀柄、67 = 刀尖。68 颗来回折成 4 等份蛇形（段号 = 规格图物理段号）：
//   段1 i0–16  → 数据链 0–16（柄端 1/4，自柄向折返）
//   段2 i17–33 → 数据链 67–51（尖端 1/4，自尖回扫）
//   段3 i34–50 → 数据链 34–50
//   段4 i51–67 → 数据链 33–17
// 四段值域并集恰铺满 0–67（双射），且表为对合：kRemap[kRemap[i]] = i。
// 效果帧 frame_ 按数据链序存放；show() 沿物理序逐颗查表取色。
constexpr uint8_t kRemap[HardwareConfig::LedCount] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,   // 段1
    67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51,   // 段2
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,   // 段3
    33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17,   // 段4
};

}  // namespace

PixelStrip::PixelStrip()
    : strip_(HardwareConfig::LedCount, HardwareConfig::LedPin, NEO_GRB + NEO_KHZ800) {}

void PixelStrip::begin() {
  strip_.begin();
  // 255 是库的"不缩放"值：库内部存的是参数+1，那里传 0 表示最暗而不是灭。
  // 电平在 show() 里施加——那里才看得见供电预算。
  strip_.setBrightness(255);
  brightness_ = HardwareConfig::DefaultBrightness;
  clear();   // 上电即灭（顺带清掉复位期间锁存的噪声色）
}

void PixelStrip::clear() {
  fill(0, 0, 0);
}

void PixelStrip::fill(uint8_t red, uint8_t green, uint8_t blue) {
  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {
    uint8_t* pixel = &frame_[index * 3];
    pixel[0] = red;
    pixel[1] = green;
    pixel[2] = blue;
  }
  show();
}

void PixelStrip::setPixel(uint16_t index, uint8_t red, uint8_t green, uint8_t blue) {
  if (index >= HardwareConfig::LedCount) return;
  uint8_t* pixel = &frame_[index * 3];
  pixel[0] = red;
  pixel[1] = green;
  pixel[2] = blue;
}

void PixelStrip::setPixel(uint16_t index, uint32_t color) {
  setPixel(index, static_cast<uint8_t>(color >> 16), static_cast<uint8_t>(color >> 8),
           static_cast<uint8_t>(color));
}

uint32_t PixelStrip::colorHsv(uint16_t hue) const {
  return strip_.ColorHSV(hue);
}

void PixelStrip::setBrightness(uint8_t brightness) {
  brightness_ = brightness;
}

// LED 画的所有东西归结为每帧一个缩放因子：请求的电平，
// 若请求的颜色会拉超过供电能力的电流则被拉回。所有通道乘同一因子
// 保持色相、只压暗刀身——而不是让 MCU 欠压复位，
// 这是单纯的亮度上限做不到的：同样设置下纯白电流是单通道的三倍。
void PixelStrip::show() {
  uint32_t sum = 0;
  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {
    const uint8_t* pixel = &frame_[index * 3];
    sum += static_cast<uint32_t>(pixel[0]) + pixel[1] + pixel[2];   // 整帧通道和
  }

  uint32_t factor = brightness_;                          // 基础因子=用户电平
  if (((sum * factor) >> 8) > kChannelBudget) {           // 超预算：等比压暗
    factor = (kChannelBudget << 8) / sum;
    const unsigned long now = millis();
    if (now - limitLogTimer_ >= kLimitLogIntervalMs) {    // 日志每秒最多一条
      limitLogTimer_ = now;
      Serial.printf("[LED] frame held to %u%% of the requested level (%u mA budget)\n",
                    static_cast<unsigned>((factor * 100) / 255),
                    HardwareConfig::LedCurrentBudgetMa);
    }
  }

  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {
    // index = 物理序（进线方向）：查表得该珠应显示的数据链色，效果逻辑零改动。
    const uint8_t* pixel = &frame_[kRemap[index] * 3];
    strip_.setPixelColor(index, static_cast<uint8_t>((pixel[0] * factor) >> 8),
                         static_cast<uint8_t>((pixel[1] * factor) >> 8),
                         static_cast<uint8_t>((pixel[2] * factor) >> 8));
  }
  strip_.show();
}
