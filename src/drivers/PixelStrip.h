#pragma once

#include <Adafruit_NeoPixel.h>

class PixelStrip {
 public:
  PixelStrip();
  void begin();
  void clear();
  void fill(uint8_t red, uint8_t green, uint8_t blue);
  void setPixel(uint16_t index, uint8_t red, uint8_t green, uint8_t blue);
  void setPixel(uint16_t index, uint32_t color);
  uint32_t colorHsv(uint16_t hue) const;
  void setBrightness(uint8_t brightness);
  void show();

 private:
  Adafruit_NeoPixel strip_;
};
