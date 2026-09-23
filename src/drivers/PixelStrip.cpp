#include "PixelStrip.h"

#include "../../include/HardwareConfig.h"

PixelStrip::PixelStrip()
    : strip_(HardwareConfig::LedCount, HardwareConfig::LedPin, NEO_GRB + NEO_KHZ800) {}

void PixelStrip::begin() {
  strip_.begin();
  strip_.setBrightness(HardwareConfig::DefaultBrightness);
  clear();
}

void PixelStrip::clear() {
  fill(0, 0, 0);
}

void PixelStrip::fill(uint8_t red, uint8_t green, uint8_t blue) {
  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {
    strip_.setPixelColor(index, red, green, blue);
  }
  strip_.show();
}

void PixelStrip::setPixel(uint16_t index, uint8_t red, uint8_t green, uint8_t blue) {
  strip_.setPixelColor(index, red, green, blue);
}

void PixelStrip::setPixel(uint16_t index, uint32_t color) {
  strip_.setPixelColor(index, color);
}

uint32_t PixelStrip::colorHsv(uint16_t hue) const {
  return strip_.ColorHSV(hue);
}

void PixelStrip::setBrightness(uint8_t brightness) {
  strip_.setBrightness(brightness);
}

void PixelStrip::show() {
  strip_.show();
}
