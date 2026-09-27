#include "DisplayDriver.h"

#include <math.h>

#include <qrcode.h>

#include "../../include/HardwareConfig.h"
#include "EyeTexture.h"

namespace {

constexpr uint8_t kQrVersion = 3;
constexpr uint8_t kQrScale = 5;
constexpr int16_t kQrTitleY = 16;

// Gaze tracking and idle behaviour.
constexpr float kGazeSmoothing = 0.18f;
constexpr float kIdleSmoothing = 0.05f;
constexpr float kIdleWander = 0.11f;
constexpr float kIdleWanderVertical = 0.6f;
constexpr float kFrameEpsilon = 0.004f;

// How shut the eye is: asleep is a sliver, angry is a squint.
constexpr float kSleepOpenness = 0.06f;
constexpr float kAngryOpenness = 0.82f;

constexpr unsigned long kBlinkMinInterval = 2200;
constexpr unsigned long kBlinkMaxInterval = 6400;
constexpr unsigned long kBlinkCloseMs = 70;
constexpr unsigned long kBlinkOpenMs = 115;

constexpr unsigned long kIdleMinInterval = 1800;
constexpr unsigned long kIdleMaxInterval = 4600;

// Angry lays a short brow over each side of the eye.  These two used to hang
// off the upper lid; the artwork has no lid, so they sit above the squashed
// disc instead, sloping up towards the outside the way a scowl does.
constexpr int16_t kBrowGap = 8;       // panel rows between the disc and the brow
constexpr int16_t kBrowInnerX = 22;   // where the low end starts, from the centre
constexpr int16_t kBrowLength = 58;
constexpr int16_t kBrowRise = 16;     // how much the outer end lifts
constexpr int16_t kBrowThickness = 9;
// RGB(44, 48, 60), the colour the procedural eye drew its brow in.
constexpr uint16_t kBrow565 = 0x2987;

float smoothStep(float value) {
  return value * value * (3.0f - 2.0f * value);
}

float clampUnit(float value) {
  if (!isfinite(value)) return 0.0f;
  return constrain(value, -1.0f, 1.0f);
}

}  // namespace

void DisplayDriver::begin() {
  pinMode(HardwareConfig::DisplayBacklight, OUTPUT);
  digitalWrite(HardwareConfig::DisplayBacklight, LOW);

  tft_.init();
  tft_.setRotation(0);
  tft_.invertDisplay(true);
  tft_.fillScreen(TFT_BLACK);

  ensureCanvas(millis());
}

void DisplayDriver::enableBacklight() {
  digitalWrite(HardwareConfig::DisplayBacklight, HIGH);
}

// init() only repeats the reset and the register table on a second call;
// setRotation and invertDisplay are applied fresh because it does not.
void DisplayDriver::repairPanel() {
  tft_.init();
  tft_.setRotation(0);
  tft_.invertDisplay(true);
  tft_.fillScreen(TFT_BLACK);
  panel_ = Panel::Boot;  // whatever happens next must repaint over this
}

// The sprite is the one piece of the display path that can fail on its own:
// it is the largest allocation the firmware makes.  Note that TFT_eSPI's own
// PSRAM path is compiled out on this SDK (it tests for CONFIG_SPIRAM_SUPPORT,
// which ESP-IDF 4.4 no longer defines), so this is a plain calloc that only
// ends up in PSRAM because the IDF allocator sends large requests there while
// PSRAM is up.  Placement is therefore policy, not a guarantee.
//
// A failed allocation used to be silent and permanent: every later frame
// returned without drawing.  Retry instead, so it heals itself.
bool DisplayDriver::ensureCanvas(unsigned long now) {
  if (canvasReady_) return true;
  if (canvasRetry_ != 0 && now - canvasRetry_ < kCanvasRetryMs) return false;
  canvasRetry_ = now;

  canvas_.setColorDepth(16);
  canvasReady_ = canvas_.createSprite(kPanelSize, kPanelSize) != nullptr;
  if (canvasReady_) {
    // TFT_eSPI keeps sprite pixels byte swapped in RAM (see drawPixel in
    // Sprite.cpp: it always applies (color >> 8) | (color << 8)), which is what
    // lets pushSprite clock them straight out to the panel.  Anything written
    // with the sprite's own draw calls is converted for free, but pushImage
    // memcpy()s verbatim -- so the texture from flash, which is in the natural
    // order, has to be swapped on the way in.  Turning this on makes pushImage
    // do exactly that; without it every pixel reaches the panel with its bytes
    // reversed and the green eye comes out magenta.
    canvas_.setSwapBytes(true);
  }
  Serial.printf("[DISPLAY] eye sprite %s (%d bytes)\n",
                canvasReady_ ? "ready" : "allocation failed",
                kPanelSize * kPanelSize * 2);
  return canvasReady_;
}

void DisplayDriver::showBoot(bool ready) {
  tft_.fillScreen(TFT_BLACK);
  drawCenteredText("ESABER", 104, 4, ready ? TFT_GREEN : TFT_RED);
  drawCenteredText(ready ? "READY" : "HARDWARE CHECK", 148, 2, TFT_WHITE);
  panel_ = Panel::Boot;
}

void DisplayDriver::showQr(const char* title, const char* url) {
  // Re-rendering the QR costs tens of milliseconds of blocking SPI, and the
  // address only changes when the network does.
  if (panel_ == Panel::Qr && qrUrl_ == url) return;
  panel_ = Panel::Qr;
  qrUrl_ = url;

  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(kQrVersion)];
  qrcode_initText(&qrcode, qrcodeData, kQrVersion, ECC_LOW, url);

  tft_.fillScreen(TFT_WHITE);
  drawCenteredText(title, kQrTitleY, 2, TFT_BLACK);

  const uint16_t qrPixels = static_cast<uint16_t>(qrcode.size) * kQrScale;
  const int16_t left = (kPanelSize - qrPixels) / 2;
  const int16_t top = (kPanelSize - qrPixels) / 2 + kQrTitleY / 2;

  for (uint8_t row = 0; row < qrcode.size; ++row) {
    for (uint8_t column = 0; column < qrcode.size; ++column) {
      if (qrcode_getModule(&qrcode, column, row)) {
        tft_.fillRect(left + column * kQrScale, top + row * kQrScale, kQrScale, kQrScale,
                      TFT_BLACK);
      }
    }
  }
}

void DisplayDriver::drawEye(float lookX, float lookY, EyePattern pattern) {
  const unsigned long now = millis();
  if (!ensureCanvas(now)) return;

  const bool attentive = pattern != EyePattern::Sleep;
  updateGaze(now, clampUnit(lookX), clampUnit(lookY), attentive);
  updateBlink(now);

  const float openness = attentive ? openness_ : kSleepOpenness;
  if (!frameChanged(openness, pattern)) return;

  const int16_t offsetX = static_cast<int16_t>(gazeX_ * static_cast<float>(kEyeTravelX));
  const int16_t offsetY = static_cast<int16_t>(gazeY_ * static_cast<float>(kEyeTravelY));

  // A boot or QR screen painted over the whole panel; clear the pixels outside
  // the sprite before the eye comes back.
  if (panel_ != Panel::Eye) {
    panel_ = Panel::Eye;
    tft_.fillScreen(TFT_BLACK);
  }

  canvas_.fillSprite(TFT_BLACK);
  paintEye(offsetX, offsetY, openness);
  if (pattern == EyePattern::Angry) paintBrows(offsetX, offsetY, openness);

  canvas_.pushSprite(0, 0);
  markFramePushed(openness, pattern);
}

// One source row per destination row, so the eye is moved by shifting the
// destination and squeezed shut by shrinking it about its centre.  Sampling
// rows rather than interpolating between them is enough here: the artwork is
// already at its final size, and a squeeze only ever removes rows.
void DisplayDriver::paintEye(int16_t offsetX, int16_t offsetY, float openness) {
  const int16_t height =
      max<int16_t>(2, static_cast<int16_t>(static_cast<float>(kEyeTextureSize) * openness));
  const int16_t left = kEyeCenterX + offsetX - kEyeTextureSize / 2;
  const int16_t top = kEyeCenterY + offsetY - height / 2;

  for (int16_t row = 0; row < height; ++row) {
    const int16_t sourceRow = static_cast<int16_t>(static_cast<int32_t>(row) * kEyeTextureSize /
                                                   static_cast<int32_t>(height));
    canvas_.pushImage(left, top + row, kEyeTextureSize, 1,
                      kEyeTexture + sourceRow * kEyeTextureSize);
  }
}

void DisplayDriver::paintBrows(int16_t offsetX, int16_t offsetY, float openness) {
  const int16_t height =
      max<int16_t>(2, static_cast<int16_t>(static_cast<float>(kEyeTextureSize) * openness));
  const int16_t base = kEyeCenterY + offsetY - height / 2 - kBrowGap;
  if (base - kBrowThickness - kBrowRise < 0) return;

  const int16_t centre = kEyeCenterX + offsetX;
  for (int16_t index = 0; index < kBrowLength; ++index) {
    const int16_t offset = kBrowInnerX + index;
    const int16_t y = base - kBrowRise * index / (kBrowLength - 1);
    canvas_.drawFastVLine(centre - offset, y, kBrowThickness, kBrow565);
    canvas_.drawFastVLine(centre + offset, y, kBrowThickness, kBrow565);
  }
}

void DisplayDriver::updateGaze(unsigned long now, float targetX, float targetY,
                               bool attentive) {
  if (!attentive) {
    targetX = 0.0f;
    targetY = 0.0f;
  }

  // Small involuntary jumps keep the eye from looking frozen when the saber
  // is held still.
  if (now - idleTimer_ > idleDue_) {
    idleTimer_ = now;
    idleDue_ = random(kIdleMinInterval, kIdleMaxInterval);
    idleTargetX_ = (static_cast<float>(random(-100, 101)) / 100.0f) * kIdleWander;
    idleTargetY_ =
        (static_cast<float>(random(-100, 101)) / 100.0f) * kIdleWander * kIdleWanderVertical;
  }
  idleX_ += (idleTargetX_ - idleX_) * kIdleSmoothing;
  idleY_ += (idleTargetY_ - idleY_) * kIdleSmoothing;

  gazeX_ += (clampUnit(targetX + idleX_) - gazeX_) * kGazeSmoothing;
  gazeY_ += (clampUnit(targetY + idleY_) - gazeY_) * kGazeSmoothing;
}

void DisplayDriver::updateBlink(unsigned long now) {
  if (!blinking_) {
    if (now - blinkTimer_ <= blinkDue_) return;
    blinking_ = true;
    blinkPhaseStart_ = now;
  }

  const unsigned long elapsed = now - blinkPhaseStart_;
  if (elapsed < kBlinkCloseMs) {
    openness_ = 1.0f - smoothStep(static_cast<float>(elapsed) / static_cast<float>(kBlinkCloseMs));
    return;
  }

  const unsigned long opening = elapsed - kBlinkCloseMs;
  if (opening < kBlinkOpenMs) {
    openness_ = smoothStep(static_cast<float>(opening) / static_cast<float>(kBlinkOpenMs));
    return;
  }

  openness_ = 1.0f;
  blinking_ = false;
  blinkTimer_ = now;
  blinkDue_ = random(kBlinkMinInterval, kBlinkMaxInterval);
}

bool DisplayDriver::frameChanged(float openness, EyePattern pattern) const {
  // Anything other than the eye on the panel means the eye has to repaint.
  if (panel_ != Panel::Eye) return true;
  if (static_cast<int8_t>(pattern) != pushedPattern_) return true;
  if (fabsf(openness - pushedOpenness_) > kFrameEpsilon) return true;
  if (fabsf(gazeX_ - pushedGazeX_) > kFrameEpsilon) return true;
  if (fabsf(gazeY_ - pushedGazeY_) > kFrameEpsilon) return true;
  return false;
}

void DisplayDriver::markFramePushed(float openness, EyePattern pattern) {
  pushedGazeX_ = gazeX_;
  pushedGazeY_ = gazeY_;
  pushedOpenness_ = openness;
  pushedPattern_ = static_cast<int8_t>(pattern);
}

void DisplayDriver::drawCenteredText(const char* text, int16_t y, uint8_t size, uint16_t color) {
  tft_.setTextSize(size);
  // Transparent background: a background fill of black made the title on the
  // white QR screen invisible.
  tft_.setTextColor(color);
  tft_.setTextDatum(MC_DATUM);
  tft_.drawString(text, kPanelSize / 2, y);
}
