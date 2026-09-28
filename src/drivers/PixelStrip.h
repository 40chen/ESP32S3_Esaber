#pragma once

#include <Adafruit_NeoPixel.h>

#include "../../include/HardwareConfig.h"

// WS2812 灯条：亮度与供电限流都收在这层。
//
// 帧以未缩放形式存于影子缓冲，输出时才转换，换来两件事：
// 电平改动下一次 show() 即生效（不用等特效重绘），
// 且限流器看到的是"请求的颜色"而不是已经压暗的副本。
// Adafruit_NeoPixel 自带的缩放因此被关掉——
// 它在写入时就把电平折进像素数据，事后无法再量。
class PixelStrip {
 public:
  PixelStrip();
  void begin();
  void clear();
  void fill(uint8_t red, uint8_t green, uint8_t blue);                    // 整条填色
  void setPixel(uint16_t index, uint8_t red, uint8_t green, uint8_t blue);
  void setPixel(uint16_t index, uint32_t color);
  uint32_t colorHsv(uint16_t hue) const;
  void setBrightness(uint8_t brightness);
  void show();   // 输出：限流 → 缩放 → 锁存

 private:
  Adafruit_NeoPixel strip_;
  uint8_t frame_[HardwareConfig::LedCount * 3] = {0};   // 影子帧缓冲（未缩放 RGB）
  uint8_t brightness_ = HardwareConfig::DefaultBrightness;
  // "帧被限流"提示的节流器，否则每帧都打
  unsigned long limitLogTimer_ = 0;
};
