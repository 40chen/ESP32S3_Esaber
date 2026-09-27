#pragma once

#include <Arduino.h>

enum class SaberEffect : uint8_t {
  Solid = 0,
  Pulse = 1,
  Rainbow = 2,
  Scanner = 3,
  Unstable = 4,
  Fire = 5,
  Sparkle = 6,
};

enum class EyePattern : uint8_t {
  Normal = 0,
  Sleep = 1,
  Angry = 2,
};

// Used to clamp values that arrive from NVS or the web API, so a stale or
// malformed value can never produce an out of range enum.
constexpr uint8_t kSaberEffectCount = 7;
constexpr uint8_t kEyePatternCount = 3;

struct SaberSettings {
  bool power = false;
  // #FF33CC, the pink-purple blade a fresh device starts with.  Only used as
  // the default for a missing NVS key, so an existing device keeps whatever
  // colour it was last set to.
  uint8_t red = 255;
  uint8_t green = 51;
  uint8_t blue = 204;
  // User-facing brightness in percent, 0-100.  The strip scale it maps onto is
  // capped by MaxLedBrightness, and the current limiter in PixelStrip has the
  // final say on what the 5 V rail is allowed to deliver.
  uint8_t brightness = 80;
  // Audio level, in percent of the codec ceiling.  The firmware default is the
  // level at which the effect files still fit without clipping.
  uint8_t volume = 80;
  SaberEffect effect = SaberEffect::Pulse;
  EyePattern eyePattern = EyePattern::Normal;
};
