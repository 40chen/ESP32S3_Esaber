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
  uint8_t red = 255;
  uint8_t green = 0;
  uint8_t blue = 0;
  // User-facing brightness in percent (0-100); the driver scales it down to
  // the brown-out-safe hardware ceiling internally.
  uint8_t brightness = 100;
  SaberEffect effect = SaberEffect::Pulse;
  EyePattern eyePattern = EyePattern::Normal;
};
