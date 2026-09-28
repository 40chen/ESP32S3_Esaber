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
    const uint8_t* pixel = &frame_[index * 3];
    strip_.setPixelColor(index, static_cast<uint8_t>((pixel[0] * factor) >> 8),
                         static_cast<uint8_t>((pixel[1] * factor) >> 8),
                         static_cast<uint8_t>((pixel[2] * factor) >> 8));
  }
  strip_.show();
}
