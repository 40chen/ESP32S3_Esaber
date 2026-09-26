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

// Blows a channel towards white by `amount` (0 = unchanged, 255 = white).
uint8_t blendToWhite(uint8_t channel, uint8_t amount) {
  return static_cast<uint8_t>(channel + (kChannelMaximum - channel) * amount / kChannelMaximum);
}

// Compact clone of FastLED's HeatColor: black -> deep red -> orange -> yellow.
void heatToRgb(uint8_t heat, uint8_t& red, uint8_t& green, uint8_t& blue) {
  const uint8_t t192 = static_cast<uint8_t>(static_cast<uint16_t>(heat) * 191 / kChannelMaximum);
  if (t192 > 127) {
    red = kChannelMaximum;
    green = static_cast<uint8_t>((t192 - 128) * 2);
    blue = 0;
  } else {
    red = static_cast<uint8_t>(t192 * 2);
    green = 0;
    blue = 0;
  }
}

}  // namespace

void SaberController::begin(AudioOutput* audio, PixelStrip* strip, MotionSensor* motion,
                            const SaberSettings& initialSettings) {
  audio_ = audio;
  strip_ = strip;
  motion_ = motion;
  settings_ = initialSettings;
  strip_->setBrightness(ledBrightness());
  // The blade starts off, so no ignition sound here: it used to play on every
  // boot whether or not the saber was actually on.
  strip_->clear();
}

// settings_.brightness is a user-facing percentage; the hardware scale is
// derived here so the strip never exceeds the brown-out-safe ceiling.
uint8_t SaberController::ledBrightness() const {
  return static_cast<uint8_t>(
      min<uint32_t>(static_cast<uint32_t>(settings_.brightness) * HardwareConfig::MaxLedBrightness / 100,
                    HardwareConfig::MaxLedBrightness));
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
    // The blade keeps collapsing after the power cut; only when the retract
    // finishes does the strip actually go dark.
    if (retracting_) updateRetract(millis());
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
    strip_->setBrightness(ledBrightness());
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
    retracting_ = false;
    humPlaying_ = true;
    // Deliberately no strip fill here.  Lighting all 56 pixels at once both
    // defeated the ignition animation and spiked the supply hard enough to
    // brown out the MCU; updateLighting now ramps the blade up.
  } else {
    audio_->play(kPowerOffSound);
    humPlaying_ = false;
    swingReady_ = false;
    strikePlaying_ = false;
    strikeEffect_ = false;
    turnOnAnimation_ = false;
    // Retract instead of an instant blackout: the blade collapses from both
    // ends with a short afterglow, which reads as the plasma draining and
    // matches the power-off sound far better than a hard clear().
    retracting_ = true;
    animationPixel_ = HardwareConfig::LedCount / 2 - 1;
    effectTimer_ = now;
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
      now - strikeTimeout_ > HardwareConfig::SwingTimeout && !turnOnAnimation_ && !retracting_;
  if (!strikeDetected) return;

  strikeTimeout_ = now;
  playRandomSound(kStrikeSounds, kStrikeSoundCount);
  humTimer_ = now - HardwareConfig::HumTimeout + HardwareConfig::HumSoundDelay;
  // Normalise how hard the hit was to 0-100 so the flash length scales with
  // impact strength instead of always flashing for the same time.
  const uint8_t intensity = static_cast<uint8_t>(
      constrain(static_cast<long>(data.acceleration - HardwareConfig::StrikeThreshold) * 100 /
                    (HardwareConfig::HardStrikeThreshold - HardwareConfig::StrikeThreshold),
                0L, 100L));
  startStrike(intensity);
  strikePlaying_ = true;
}

void SaberController::handleSwing(unsigned long now) {
  const MotionData& data = motion_->data();
  if (data.rotation <= HardwareConfig::SwingLowThreshold ||
      now - swingTimeout_ <= HardwareConfig::SwingCooldown || turnOnAnimation_ || retracting_) {
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
  // A retract in progress owns the strip until the blade is fully drained.
  if (retracting_) {
    updateRetract(now);
    return;
  }

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
    if (now - hitTimer_ > hitDuration_) {
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
    case SaberEffect::Unstable:
      applyUnstable(now);
      break;
    case SaberEffect::Fire:
      updateFire(now);
      break;
    case SaberEffect::Sparkle:
      updateSparkle(now);
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
  // Swinging the blade spins the rainbow faster, which makes the effect feel
  // physically attached to the motion instead of running on a clock.
  const uint16_t swingBoost = static_cast<uint16_t>(
      min<uint16_t>(motion_->data().rotation, HardwareConfig::SwingThreshold) *
      HardwareConfig::RainbowSwingBoost);
  rainbowHue_ += HardwareConfig::RainbowHueStep + swingBoost;
}

// Unstable blade: every pixel jitters around the blade colour, and every so
// often the plasma feed surges for a single frame.
void SaberController::applyUnstable(unsigned long now) {
  if (now - effectTimer_ < HardwareConfig::FlickerIntervalMs) return;
  effectTimer_ = now;
  const int amplitude =
      esp_random() % 100 < HardwareConfig::FlickerSurgeChancePercent
          ? HardwareConfig::FlickerSurgeAmplitude
          : HardwareConfig::FlickerAmplitude;
  const int jitterRange = 2 * amplitude + 1;
  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {
    const int delta = static_cast<int>(esp_random() % jitterRange) - amplitude;
    strip_->setPixel(index, clampColor(settings_.red + delta),
                     clampColor(settings_.green + delta), clampColor(settings_.blue + delta));
  }
  strip_->show();
}

// Fire: heat is injected at the hilt, drifts towards the tip, cools on the
// way and renders through a black-red-orange-yellow ramp.  A one-byte heat
// map keeps the per-frame cost tiny on the loop task.
void SaberController::updateFire(unsigned long now) {
  if (now - effectTimer_ < HardwareConfig::FireIntervalMs) return;
  effectTimer_ = now;

  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {
    const uint8_t cooling = esp_random() % ((HardwareConfig::FireCooling * 10) / HardwareConfig::LedCount + 2);
    fireHeat_[index] = fireHeat_[index] > cooling ? static_cast<uint8_t>(fireHeat_[index] - cooling) : 0;
  }
  for (uint16_t index = HardwareConfig::LedCount - 1; index >= 2; --index) {
    const uint16_t source = index - 2;
    fireHeat_[index] = static_cast<uint8_t>(
        (static_cast<uint16_t>(fireHeat_[source]) + 2 * fireHeat_[source + 1]) / 3);
  }
  if (esp_random() % 256 < HardwareConfig::FireSparking) {
    fireHeat_[esp_random() % 3] = static_cast<uint8_t>(160 + esp_random() % 96);
  }

  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    heatToRgb(fireHeat_[index], red, green, blue);
    strip_->setPixel(index, red, green, blue);
  }
  strip_->show();
}

// Sparkle: the blade colour as a base with a couple of white glints jumping
// around each frame, like dust catching the light inside the plasma.
void SaberController::updateSparkle(unsigned long now) {
  if (now - effectTimer_ < HardwareConfig::SparkleIntervalMs) return;
  effectTimer_ = now;
  strip_->fill(settings_.red, settings_.green, settings_.blue);
  for (uint8_t glint = 0; glint < HardwareConfig::SparkleCount; ++glint) {
    const uint16_t index = esp_random() % HardwareConfig::LedCount;
    strip_->setPixel(index, blendToWhite(settings_.red, 230), blendToWhite(settings_.green, 230),
                     blendToWhite(settings_.blue, 230));
  }
  strip_->show();
}

// Reverse of the ignition: the blade collapses from both ends towards the
// middle.  The pixels about to be drained fade over the last few steps, so
// the collapse front carries a short dark gradient instead of snapping to
// black pixel by pixel.
void SaberController::updateRetract(unsigned long now) {
  if (now - effectTimer_ < HardwareConfig::FlashDelay) return;
  effectTimer_ = now;

  const int16_t edge = animationPixel_;
  for (uint8_t distance = 1; distance <= kRetractFadeSteps; ++distance) {
    const int16_t index = edge - distance;
    if (index < 0) break;
    // distance 1..3 -> 3/4, 2/4, 1/4 of full colour: every pixel decays a
    // step per frame until the edge reaches it and it goes dark.
    const uint8_t brightness = static_cast<uint8_t>(
        kChannelMaximum * (kRetractFadeSteps + 1 - distance) / (kRetractFadeSteps + 1));
    const int16_t mirrored = HardwareConfig::LedCount - 1 - index;
    strip_->setPixel(index, settings_.red * brightness / kChannelMaximum,
                     settings_.green * brightness / kChannelMaximum,
                     settings_.blue * brightness / kChannelMaximum);
    strip_->setPixel(mirrored, settings_.red * brightness / kChannelMaximum,
                     settings_.green * brightness / kChannelMaximum,
                     settings_.blue * brightness / kChannelMaximum);
  }
  strip_->setPixel(edge, 0, 0, 0);
  strip_->setPixel(HardwareConfig::LedCount - 1 - edge, 0, 0, 0);
  strip_->show();

  if (edge == 0) {
    retracting_ = false;
    strip_->clear();
    return;
  }
  --animationPixel_;
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

void SaberController::startStrike(uint8_t intensity) {
  // The flash is the blade colour blown out towards white, never a fixed
  // colour: whatever the user picked stays recognisable during a clash.
  strip_->fill(blendToWhite(settings_.red, HardwareConfig::StrikeFlashWhiteBlend),
               blendToWhite(settings_.green, HardwareConfig::StrikeFlashWhiteBlend),
               blendToWhite(settings_.blue, HardwareConfig::StrikeFlashWhiteBlend));
  hitDuration_ = HardwareConfig::HitBaseMs +
                 static_cast<uint16_t>(intensity * HardwareConfig::HitExtraMs / 100);
  hitTimer_ = millis();
  strikeEffect_ = true;
  ++strikeCount_;
}

void SaberController::playRandomSound(const char* const sounds[], uint8_t count) {
  audio_->play(sounds[esp_random() % count]);
}
