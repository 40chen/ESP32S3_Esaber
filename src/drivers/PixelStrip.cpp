#include "PixelStrip.h"

namespace {

// The frame's channel sum that the supply is allowed to feed, in the same
// units as the brightness scaling (0..255 per channel).
constexpr uint32_t kChannelBudget =
    (static_cast<uint32_t>(HardwareConfig::LedCurrentBudgetMa) * 255) /
    HardwareConfig::LedChannelMilliamps;

constexpr unsigned long kLimitLogIntervalMs = 1000;

}  // namespace

PixelStrip::PixelStrip()
    : strip_(HardwareConfig::LedCount, HardwareConfig::LedPin, NEO_GRB + NEO_KHZ800) {}

void PixelStrip::begin() {
  strip_.begin();
  // 255 is the library's "no scaling" value: it stores the argument plus one,
  // so 0 there means minimum brightness, not off.  The level is applied in
  // show() instead, where the supply budget can be folded into it.
  strip_.setBrightness(255);
  brightness_ = HardwareConfig::DefaultBrightness;
  clear();
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

// Everything the LEDs draw is one scale factor per frame: the requested level,
// pulled back if the colour that has been asked for would pull too much current
// for the supply.  Scaling every channel by the same factor keeps the colour
// and dims the blade instead of browning out the MCU, which is what a plain
// brightness ceiling cannot do -- full white draws three times what a single
// channel does at the same setting.
void PixelStrip::show() {
  uint32_t sum = 0;
  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {
    const uint8_t* pixel = &frame_[index * 3];
    sum += static_cast<uint32_t>(pixel[0]) + pixel[1] + pixel[2];
  }

  uint32_t factor = brightness_;
  if (((sum * factor) >> 8) > kChannelBudget) {
    factor = (kChannelBudget << 8) / sum;
    const unsigned long now = millis();
    if (now - limitLogTimer_ >= kLimitLogIntervalMs) {
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
