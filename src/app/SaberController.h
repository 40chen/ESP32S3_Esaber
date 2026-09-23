#pragma once

#include <Arduino.h>

#include "../../include/AppTypes.h"
#include "../drivers/AudioOutput.h"
#include "../drivers/MotionSensor.h"
#include "../drivers/PixelStrip.h"

// Turns gyroscope movement into light and sound: swing whooshes, clash
// flashes, the idle hum loop and the ignition animation.
class SaberController {
 public:
  void begin(AudioOutput* audio, PixelStrip* strip, MotionSensor* motion,
             const SaberSettings& initialSettings);
  void update();

  const SaberSettings& settings() const { return settings_; }
  void setSettings(const SaberSettings& settings);
  void setPower(bool enabled);

 private:
  static constexpr uint8_t kImuSampleCount = 8;
  static constexpr uint8_t kEffectIntervalMs = 20;

  void readGesture();
  void handleStrike(unsigned long now);
  void handleSwing(unsigned long now);
  void updateHum(unsigned long now);
  void updateLighting(unsigned long now);
  void applyPulse(unsigned long now);
  void updateRainbow(unsigned long now);
  void updateScanner(unsigned long now);
  void startStrike();
  void playRandomSound(const char* const sounds[], uint8_t count);
  uint8_t ledBrightness() const;

  AudioOutput* audio_ = nullptr;
  PixelStrip* strip_ = nullptr;
  MotionSensor* motion_ = nullptr;
  SaberSettings settings_;

  bool gestureRequested_ = false;
  bool humPlaying_ = false;
  bool swingReady_ = false;
  bool strikePlaying_ = false;
  bool strikeEffect_ = false;
  bool turnOnAnimation_ = false;

  unsigned long gestureTimer_ = 0;
  unsigned long pulseTimer_ = 0;
  unsigned long humTimer_ = 0;
  unsigned long swingTimer_ = 0;
  unsigned long swingTimeout_ = 0;
  unsigned long strikeTimeout_ = 0;
  unsigned long effectTimer_ = 0;
  unsigned long hitTimer_ = 0;
  unsigned long gestureCounter_ = 0;
  uint8_t gestureCooldown_ = 0;
  uint8_t animationPixel_ = 0;
  int pulseOffset_ = 0;
  uint16_t rainbowHue_ = 0;
  int8_t scannerDirection_ = 1;
  uint16_t scannerPixel_ = 0;
  float rollSamples_[kImuSampleCount] = {0.0f};
  float pitchSamples_[kImuSampleCount] = {0.0f};
};
