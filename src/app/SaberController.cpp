#include "SaberController.h"

#include "../../include/HardwareConfig.h"

namespace {

constexpr uint8_t kStrikeSoundCount = 10;
constexpr uint8_t kSwingSoundCount = 14;
constexpr uint8_t kSwingSoundFastCount = kSwingSoundCount / 2;
constexpr float kPulseSmoothing = 0.8f;
constexpr float kRadiansToDegrees = 57.2958f;
constexpr float kAccelLsbPerGravityUnit = 2048.0f;
constexpr uint16_t kPulseRandomRange = 1024;
constexpr uint16_t kPulseRandomMax = kPulseRandomRange - 1;
constexpr uint8_t kChannelMaximum = 255;
constexpr uint16_t kHueFullCircle = 65535;

constexpr const char* kPowerOnSound = "saber.flac";
constexpr const char* kPowerOffSound = "out1.wav";

const char* const kStrikeSounds[kStrikeSoundCount] = {
    "clsh1.wav", "clsh2.wav", "clsh3.wav", "clsh4.wav", "clsh5.wav",
    "clsh6.wav", "clsh7.wav", "clsh8.wav", "clsh9.wav", "clsh10.wav"};
const char* const kSwingSounds[kSwingSoundCount] = {
    "swng1.wav",  "swng2.wav",  "swng3.wav",  "swng4.wav",  "swng5.wav",
    "swng6.wav",  "swng7.wav",  "swng8.wav",  "swng9.wav",  "swng10.wav",
    "swng11.wav", "swng12.wav", "swng13.wav", "swng14.wav"};

uint8_t clampColor(int value) {
  return static_cast<uint8_t>(constrain(value, 0, kChannelMaximum));
}

float square(float value) { return value * value; }

}  // namespace

void SaberController::begin(AudioOutput* audio, PixelStrip* strip, MotionSensor* motion,
                            const SaberSettings& initialSettings) {
  audio_ = audio;
  strip_ = strip;
  motion_ = motion;
  settings_ = initialSettings;
  settings_.brightness = ledBrightness();
  strip_->setBrightness(settings_.brightness);
  // The blade starts off, so no ignition sound here: it used to play on every
  // boot whether or not the saber was actually on.
  strip_->clear();
}

uint8_t SaberController::ledBrightness() const {
  return min(settings_.brightness, HardwareConfig::MaxLedBrightness);
}

void SaberController::update() {
  audio_->loop();
  motion_->update();

  readGesture();
  if (gestureRequested_) setPower(!settings_.power);

  if (!settings_.power) {
    strikePlaying_ = false;
    strikeEffect_ = false;
    turnOnAnimation_ = false;
    humPlaying_ = false;
    return;
  }

  const unsigned long now = millis();
  handleStrike(now);
  handleSwing(now);
  updateHum(now);
  updateLighting(now);
}

void SaberController::setSettings(const SaberSettings& settings) {
  const bool powerChanged = settings.power != settings_.power;
  const bool brightnessChanged = settings.brightness != settings_.brightness;
  const bool colorChanged = settings.red != settings_.red || settings.green != settings_.green ||
                            settings.blue != settings_.blue;
  settings_ = settings;

  if (brightnessChanged) {
    settings_.brightness = ledBrightness();
    strip_->setBrightness(settings_.brightness);
  }
  if (powerChanged) {
    setPower(settings.power);
    return;
  }
  if (!settings_.power) {
    strip_->clear();
  } else if (colorChanged) {
    strip_->fill(settings_.red, settings_.green, settings_.blue);
  }
}

void SaberController::setPower(bool enabled) {
  gestureRequested_ = false;
  if (enabled == settings_.power) return;

  settings_.power = enabled;
  const unsigned long now = millis();
  if (enabled) {
    audio_->play(kPowerOnSound);
    humTimer_ = now - HardwareConfig::HumTimeout + HardwareConfig::HumActivationDelay;
    turnOnAnimation_ = true;
    animationPixel_ = 0;
    humPlaying_ = true;
    // Deliberately no strip fill here.  Lighting all 56 pixels at once both
    // defeated the ignition animation and spiked the supply hard enough to
    // brown out the MCU; updateLighting now ramps the blade up.
  } else {
    audio_->play(kPowerOffSound);
    strip_->clear();
    humPlaying_ = false;
    swingReady_ = false;
    strikePlaying_ = false;
    strikeEffect_ = false;
    turnOnAnimation_ = false;
  }
}

// Detects the quick twist that toggles the blade while it is off.
void SaberController::readGesture() {
  const unsigned long now = millis();
  if (now - gestureTimer_ < HardwareConfig::GestureInterval) return;
  gestureTimer_ = now;

  const MotionData& data = motion_->data();
  const float accelX = static_cast<float>(data.ax) / kAccelLsbPerGravityUnit;
  const float accelY = static_cast<float>(data.ay) / kAccelLsbPerGravityUnit;
  const float accelZ = static_cast<float>(data.az) / kAccelLsbPerGravityUnit;
  const uint8_t sampleIndex = gestureCounter_ & (kImuSampleCount - 1);
  rollSamples_[sampleIndex] = atan2(accelY, accelZ) * kRadiansToDegrees;
  pitchSamples_[sampleIndex] =
      atan2(-accelX, sqrt(square(accelY) + square(accelZ))) * kRadiansToDegrees;

  float pitchMinimum = pitchSamples_[0];
  float pitchMaximum = pitchMinimum;
  for (uint8_t index = 0; index < kImuSampleCount; ++index) {
    pitchMinimum = min(pitchMinimum, pitchSamples_[index]);
    pitchMaximum = max(pitchMaximum, pitchSamples_[index]);
  }

  if (!settings_.power && pitchMaximum - pitchMinimum > HardwareConfig::OpenThreshold &&
      gestureCooldown_ == 0) {
    gestureRequested_ = true;
    gestureCooldown_ = HardwareConfig::GestureToggleCount;
  }

  ++gestureCounter_;
  if (gestureCooldown_ > 0) --gestureCooldown_;
}

void SaberController::handleStrike(unsigned long now) {
  const MotionData& data = motion_->data();
  const bool strikeDetected =
      data.acceleration > HardwareConfig::StrikeThreshold &&
      data.acceleration < HardwareConfig::HardStrikeThreshold &&
      now - strikeTimeout_ > HardwareConfig::SwingTimeout && !turnOnAnimation_;
  if (!strikeDetected) return;

  strikeTimeout_ = now;
  playRandomSound(kStrikeSounds, kStrikeSoundCount);
  humTimer_ = now - HardwareConfig::HumTimeout + HardwareConfig::HumSoundDelay;
  startStrike();
  strikePlaying_ = true;
}

void SaberController::handleSwing(unsigned long now) {
  const MotionData& data = motion_->data();
  if (data.rotation <= HardwareConfig::SwingLowThreshold ||
      now - swingTimeout_ <= HardwareConfig::SwingCooldown || turnOnAnimation_) {
    return;
  }
  swingTimeout_ = now;
  if (now - swingTimer_ <= HardwareConfig::SwingTimeout || !swingReady_ || strikePlaying_) {
    return;
  }

  if (data.rotation >= HardwareConfig::SwingThreshold) {
    playRandomSound(kSwingSounds, kSwingSoundCount);
  } else {
    playRandomSound(kSwingSounds, kSwingSoundFastCount);
  }

  humTimer_ = now - HardwareConfig::HumTimeout + HardwareConfig::HumSoundDelay;
  swingReady_ = false;
  swingTimer_ = now;
}

void SaberController::updateHum(unsigned long now) {
  if (!humPlaying_ || now - humTimer_ <= HardwareConfig::HumTimeout) return;
  audio_->play("hum1.wav");
  humTimer_ = now;
  swingReady_ = true;
  strikePlaying_ = false;
}

void SaberController::updateLighting(unsigned long now) {
  if (turnOnAnimation_) {
    if (now - effectTimer_ < HardwareConfig::FlashDelay) return;
    effectTimer_ = now;
    strip_->setPixel(animationPixel_, settings_.red, settings_.green, settings_.blue);
    strip_->setPixel(HardwareConfig::LedCount - 1 - animationPixel_, settings_.red,
                     settings_.green, settings_.blue);
    strip_->show();
    ++animationPixel_;
    if (animationPixel_ >= HardwareConfig::LedCount / 2) {
      animationPixel_ = 0;
      turnOnAnimation_ = false;
      effectTimer_ = now;
    }
    return;
  }

  if (strikeEffect_) {
    if (now - hitTimer_ > HardwareConfig::HitDuration) {
      strikeEffect_ = false;
      strip_->fill(settings_.red, settings_.green, settings_.blue);
    }
    return;
  }

  switch (settings_.effect) {
    case SaberEffect::Pulse:
      applyPulse(now);
      break;
    case SaberEffect::Rainbow:
      updateRainbow(now);
      break;
    case SaberEffect::Scanner:
      updateScanner(now);
      break;
    case SaberEffect::Solid:
    default:
      break;
  }
}

void SaberController::applyPulse(unsigned long now) {
  if (now - pulseTimer_ <= HardwareConfig::PulseDelay) return;
  pulseTimer_ = now;
  const int randomOffset = map(static_cast<long>(esp_random() % kPulseRandomRange), 0L,
                               static_cast<long>(kPulseRandomMax),
                               -HardwareConfig::PulseAmplitude,
                               HardwareConfig::PulseAmplitude);
  pulseOffset_ = static_cast<int>(static_cast<float>(pulseOffset_) * kPulseSmoothing +
                                  static_cast<float>(randomOffset) * (1.0f - kPulseSmoothing));
  strip_->fill(clampColor(settings_.red + pulseOffset_),
               clampColor(settings_.green + pulseOffset_),
               clampColor(settings_.blue + pulseOffset_));
}

void SaberController::updateRainbow(unsigned long now) {
  if (now - effectTimer_ < kEffectIntervalMs) return;
  effectTimer_ = now;
  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {
    const uint16_t hue = rainbowHue_ + (index * kHueFullCircle) / HardwareConfig::LedCount;
    strip_->setPixel(index, strip_->colorHsv(hue));
  }
  strip_->show();
  rainbowHue_ += HardwareConfig::RainbowHueStep;
}

void SaberController::updateScanner(unsigned long now) {
  if (now - effectTimer_ < HardwareConfig::FlashDelay) return;
  effectTimer_ = now;
  strip_->fill(0, 0, 0);
  for (uint8_t trail = 0; trail < HardwareConfig::ScannerTrailLength; ++trail) {
    const int16_t index = static_cast<int16_t>(scannerPixel_) - trail * scannerDirection_;
    if (index < 0 || index >= static_cast<int16_t>(HardwareConfig::LedCount)) continue;
    const uint8_t brightness = kChannelMaximum / (trail + 1);
    strip_->setPixel(index, settings_.red * brightness / kChannelMaximum,
                     settings_.green * brightness / kChannelMaximum,
                     settings_.blue * brightness / kChannelMaximum);
  }
  strip_->show();
  scannerPixel_ = static_cast<uint16_t>(static_cast<int16_t>(scannerPixel_) + scannerDirection_);
  if (scannerPixel_ == 0 || scannerPixel_ == HardwareConfig::LedCount - 1) {
    scannerDirection_ = static_cast<int8_t>(-scannerDirection_);
  }
}

void SaberController::startStrike() {
  strip_->fill(HardwareConfig::StrikeFlashRed, HardwareConfig::StrikeFlashGreen,
               HardwareConfig::StrikeFlashBlue);
  hitTimer_ = millis();
  strikeEffect_ = true;
}

void SaberController::playRandomSound(const char* const sounds[], uint8_t count) {
  audio_->play(sounds[esp_random() % count]);
}
