#pragma once

#include <Adafruit_NeoPixel.h>

#include "../../include/HardwareConfig.h"

// WS2812 strip with the brightness and the supply budget handled here.
//
// The frame is kept unscaled in a shadow buffer and only converted when it is
// clocked out, which buys two things: a level change takes effect on the next
// show() instead of waiting for the effect to repaint, and the supply limit
// gets to see the colours that were asked for rather than an already dimmed
// copy of them.  Adafruit_NeoPixel's own scaling is switched off for the same
// reason -- it folds the level into the pixel data at set time, where nothing
// can re-measure it afterwards.
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
  uint8_t frame_[HardwareConfig::LedCount * 3] = {0};
  uint8_t brightness_ = HardwareConfig::DefaultBrightness;
  // Throttles the "frame limited" note, which otherwise repeats every frame.
  unsigned long limitLogTimer_ = 0;
};
