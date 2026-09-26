#include "DisplayDriver.h"

#include <math.h>
#include <string.h>

#include <qrcode.h>

#include "../../include/HardwareConfig.h"
#include "ESABER_QRBanner.h"

namespace {

constexpr int16_t kScreenSize = 240;
constexpr uint8_t kQrVersion = 3;
constexpr uint8_t kQrScale = 5;
// QR screen layout follows the visual spec (ESABER_visual_design_spec.md §三):
// Chinese banner on top, QR block in the middle, two grey ASCII lines below.
constexpr int16_t kQrBannerWidth = 200;  // artwork 196px + 4 blank padding columns
constexpr int16_t kQrBannerHeight = 16;
constexpr int16_t kQrBannerY = 14;   // banner top edge
constexpr int16_t kQrTopY = 46;      // QR block top edge
constexpr int16_t kQrUrlY = 210;     // device URL line, centre-anchored, size 2
constexpr int16_t kQrHintY = 228;    // hotspot credentials, centre-anchored, size 1
constexpr uint16_t kQrUrlGrey = 0x632C;   // #666666
constexpr uint16_t kQrHintGrey = 0x9CD3;  // #999999

constexpr uint8_t kScleraBands = 14;
// Soft shadow under the upper lid, then plain white, then a gentle shade
// towards the lower lid.  An eyeball is white; over-shading it reads as a ball.
constexpr float kScleraShadowEnd = 0.18f;
constexpr float kScleraShadeStart = 0.62f;

constexpr uint8_t kIrisRings = 6;
constexpr uint8_t kIrisFibreCount = 26;
constexpr float kTwoPi = 6.2831853f;

constexpr int16_t kHighlightMainRadius = 10;
constexpr int16_t kHighlightMainX = -11;
constexpr int16_t kHighlightMainY = -12;
constexpr int16_t kHighlightSparkRadius = 4;
constexpr int16_t kHighlightSparkX = 12;
constexpr int16_t kHighlightSparkY = 13;

// Gaze tracking and idle behaviour.
constexpr float kGazeSmoothing = 0.18f;
constexpr float kIdleSmoothing = 0.05f;
constexpr float kIdleWander = 0.11f;
constexpr float kIdleWanderVertical = 0.6f;
constexpr float kFrameEpsilon = 0.004f;

constexpr unsigned long kBlinkMinInterval = 2200;
constexpr unsigned long kBlinkMaxInterval = 6400;
constexpr unsigned long kBlinkCloseMs = 70;
constexpr unsigned long kBlinkOpenMs = 115;

constexpr unsigned long kIdleMinInterval = 1800;
constexpr unsigned long kIdleMaxInterval = 4600;

struct Rgb {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

constexpr Rgb kScleraTop{198, 206, 222};
constexpr Rgb kScleraLight{252, 252, 254};
constexpr Rgb kScleraBottom{216, 222, 236};
constexpr Rgb kLidLine{58, 64, 78};
constexpr Rgb kBrow{44, 48, 60};
constexpr Rgb kClosedLid{76, 84, 100};

// A closed lid relaxes into a gentle downward bow with tapered ends.
constexpr int16_t kClosedLidBow = 13;
constexpr int16_t kClosedLidThickness = 6;

// A real iris is darkest at the limbal ring and brightens towards the pupil,
// with a pale halo around it.  The reverse reads as a black hole.
// All iris colours derive from one hue so the eye can follow the blade colour.
constexpr uint16_t kHueFullCircle = 65535;
constexpr uint16_t kDefaultIrisHue = 25490;  // the original blue, hue of (26,88,180)
constexpr Rgb kPupil{6, 8, 16};

// A clash makes the eye flinch: openness dips towards kSquintDepth and back.
constexpr float kSquintDepth = 0.22f;

uint16_t color565(const Rgb& color) {
  return static_cast<uint16_t>((static_cast<uint16_t>(color.red & 0xF8) << 8) |
                               (static_cast<uint16_t>(color.green & 0xFC) << 3) |
                               (color.blue >> 3));
}

uint8_t lerpChannel(uint8_t from, uint8_t to, float amount) {
  return static_cast<uint8_t>(static_cast<float>(from) +
                              (static_cast<float>(to) - static_cast<float>(from)) * amount);
}

Rgb lerpColor(const Rgb& from, const Rgb& to, float amount) {
  return Rgb{lerpChannel(from.red, to.red, amount),
             lerpChannel(from.green, to.green, amount),
             lerpChannel(from.blue, to.blue, amount)};
}

// smoothstep, for blinks that start and stop without a visible snap.
float smoothStep(float value) {
  return value * value * (3.0f - 2.0f * value);
}

float clampUnit(float value) {
  if (!isfinite(value)) return 0.0f;
  return constrain(value, -1.0f, 1.0f);
}

// Half height of the eye opening at a normalised horizontal position.
// Guards the domain of sqrt so a rounding overshoot can never produce NaN.
float lidCurve(float position) {
  const float squared = 1.0f - position * position;
  return squared > 0.0f ? sqrtf(squared) : 0.0f;
}

Rgb sampleSclera(float position) {
  if (position < kScleraShadowEnd) {
    return lerpColor(kScleraTop, kScleraLight, position / kScleraShadowEnd);
  }
  if (position < kScleraShadeStart) return kScleraLight;
  return lerpColor(kScleraLight, kScleraBottom,
                   (position - kScleraShadeStart) / (1.0f - kScleraShadeStart));
}

// Hue (0-65535) of an RGB colour; greys and black fall back to the default
// blue so a colourless blade can never wash the iris out.
uint16_t rgbToHue(const Rgb& color, uint16_t fallback) {
  const uint8_t maxChannel = max(color.red, max(color.green, color.blue));
  const uint8_t minChannel = min(color.red, min(color.green, color.blue));
  if (maxChannel == 0 || maxChannel == minChannel) return fallback;

  const float delta = static_cast<float>(maxChannel - minChannel);
  float sector;
  if (maxChannel == color.red) {
    sector = (static_cast<float>(color.green) - static_cast<float>(color.blue)) / delta;
  } else if (maxChannel == color.green) {
    sector = 2.0f + (static_cast<float>(color.blue) - static_cast<float>(color.red)) / delta;
  } else {
    sector = 4.0f + (static_cast<float>(color.red) - static_cast<float>(color.green)) / delta;
  }
  float hue = sector * (static_cast<float>(kHueFullCircle) / 6.0f);
  if (hue < 0.0f) hue += static_cast<float>(kHueFullCircle);
  return static_cast<uint16_t>(hue);
}

// HSV to RGB for iris shading; sat/val are plain 0-255 channel scales.
Rgb hsvToRgb(uint16_t hue, uint8_t sat, uint8_t val) {
  const float h = static_cast<float>(hue) / (static_cast<float>(kHueFullCircle) / 6.0f);
  const float s = static_cast<float>(sat) / 255.0f;
  const float v = static_cast<float>(val) / 255.0f;
  const uint8_t sector = static_cast<uint8_t>(static_cast<int>(h) % 6);
  const float fraction = h - static_cast<float>(static_cast<int>(h));
  const auto scale = [v, s](float factor) {
    return static_cast<uint8_t>(v * (1.0f - s * factor) * 255.0f + 0.5f);
  };
  const uint8_t p = scale(1.0f);
  const uint8_t q = scale(fraction);
  const uint8_t t = scale(1.0f - fraction);
  const uint8_t vv = static_cast<uint8_t>(v * 255.0f + 0.5f);
  switch (sector) {
    case 0: return Rgb{vv, t, p};
    case 1: return Rgb{q, vv, p};
    case 2: return Rgb{p, vv, t};
    case 3: return Rgb{p, q, vv};
    case 4: return Rgb{t, p, vv};
    default: return Rgb{vv, p, q};
  }
}

}  // namespace

void DisplayDriver::begin() {
  pinMode(HardwareConfig::DisplayBacklight, OUTPUT);
  digitalWrite(HardwareConfig::DisplayBacklight, LOW);

  tft_.init();
  tft_.setRotation(0);
  tft_.invertDisplay(true);
  tft_.fillScreen(TFT_BLACK);

  canvas_.setColorDepth(16);
  // 192x176x2 bytes: with PSRAM present TFT_eSPI allocates the sprite there,
  // so the internal heap is untouched.
  canvasReady_ = canvas_.createSprite(kCanvasWidth, kCanvasHeight) != nullptr;
  if (!canvasReady_) {
    Serial.println("[DISPLAY] eye sprite allocation failed");
  }

  digitalWrite(HardwareConfig::DisplayBacklight, HIGH);
}

void DisplayDriver::showBoot(bool ready) {
  tft_.fillScreen(TFT_BLACK);
  drawCenteredText("ESABER", 104, 4, ready ? TFT_GREEN : TFT_RED);
  drawCenteredText(ready ? "READY" : "HARDWARE CHECK", 148, 2, TFT_WHITE);
  panel_ = Panel::Boot;
}

void DisplayDriver::showQr(const char* url, const char* hint) {
  // Re-rendering the QR costs tens of milliseconds of blocking SPI, and the
  // address only changes when the network does.
  if (panel_ == Panel::Qr && qrUrl_ == url && qrHint_ == (hint ? hint : "")) return;
  panel_ = Panel::Qr;
  qrUrl_ = url;
  qrHint_ = hint ? hint : "";

  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(kQrVersion)];
  qrcode_initText(&qrcode, qrcodeData, kQrVersion, ECC_LOW, url);

  tft_.fillScreen(TFT_WHITE);
  // User-approved Chinese instruction as a 1-bit bitmap: the GLCD font has no
  // CJK glyphs.  Drawn 200px wide (25 bytes per row) although the artwork is
  // 196px: 200 is byte aligned, so the row layout matches both TFT_eSPI's and
  // Adafruit_GFX's drawBitmap bit-packing conventions, and the extra columns
  // are blank padding.
  tft_.drawBitmap((kScreenSize - kQrBannerWidth) / 2, kQrBannerY, kQrBannerJoin,
                  kQrBannerWidth, kQrBannerHeight, TFT_BLACK);

  const uint16_t qrPixels = static_cast<uint16_t>(qrcode.size) * kQrScale;
  const int16_t left = (kScreenSize - qrPixels) / 2;
  const int16_t top = kQrTopY;

  for (uint8_t row = 0; row < qrcode.size; ++row) {
    for (uint8_t column = 0; column < qrcode.size; ++column) {
      if (qrcode_getModule(&qrcode, column, row)) {
        tft_.fillRect(left + column * kQrScale, top + row * kQrScale, kQrScale, kQrScale,
                      TFT_BLACK);
      }
    }
  }

  // ASCII-only lines below the QR: device address in dark grey, hotspot
  // credentials in a lighter grey.
  if (url != nullptr && *url != '\0') {
    const char* shown = url;
    if (strncmp(shown, "http://", 7) == 0) shown += 7;
    drawCenteredText(shown, kQrUrlY, 2, kQrUrlGrey);
  }
  if (qrHint_.length() > 0) {
    drawCenteredText(qrHint_.c_str(), kQrHintY, 1, kQrHintGrey);
  }
}

void DisplayDriver::squint(unsigned long durationMs) {
  squintStart_ = millis();
  squintUntil_ = squintStart_ + durationMs;
}

// Envelope for the clash flinch: dips towards kSquintDepth at mid-squint and
// recovers, multipled into whatever the blink machinery is doing.
float DisplayDriver::squintEnvelope(unsigned long now) const {
  if (now >= squintUntil_) return 1.0f;
  const float phase = static_cast<float>(now - squintStart_) /
                      static_cast<float>(squintUntil_ - squintStart_);
  const float triangle = 1.0f - fabsf(2.0f * phase - 1.0f);
  return kSquintDepth + (1.0f - kSquintDepth) * smoothStep(triangle);
}

void DisplayDriver::drawEye(float lookX, float lookY, EyePattern pattern, uint8_t bladeRed,
                            uint8_t bladeGreen, uint8_t bladeBlue) {
  if (!canvasReady_) return;

  const unsigned long now = millis();
  const bool attentive = pattern != EyePattern::Sleep;
  updateGaze(now, clampUnit(lookX), clampUnit(lookY), attentive);
  updateBlink(now);

  const float baseOpenness = attentive ? openness_ : kSleepOpenness;
  const float openness = baseOpenness * squintEnvelope(now);
  if (!frameChanged(openness, pattern)) return;

  const int16_t irisX =
      kEyeCenterX + static_cast<int16_t>(gazeX_ * static_cast<float>(kGazeRangeX));
  const int16_t irisY =
      kEyeCenterY + static_cast<int16_t>(gazeY_ * static_cast<float>(kGazeRangeY));

  // A boot or QR screen painted over the whole panel; clear the pixels outside
  // the sprite before the eye comes back.
  if (panel_ != Panel::Eye) {
    panel_ = Panel::Eye;
    tft_.fillScreen(TFT_BLACK);
  }

  const bool angry = pattern == EyePattern::Angry;
  canvas_.fillSprite(TFT_BLACK);
  if (attentive) {
    buildLidProfile(angry ? openness * kAngryOpenness : openness, angry);
    paintSclera();
    const uint16_t irisHue =
        rgbToHue(Rgb{bladeRed, bladeGreen, bladeBlue}, kDefaultIrisHue);
    paintIris(irisX, irisY, irisHue);
    maskOutsideEye();
    paintLidStrokes();
    if (angry) paintBrow();
  } else {
    buildLidProfile(kSleepOpenness, false);
    paintClosedLid();
  }

  canvas_.pushSprite(kCanvasX, kCanvasY);
  markFramePushed(openness, pattern);
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

// The open eye is a lens: an upper and a lower arc that meet at the corners.
// When scowling, the upper opening is pinched in the middle, which lowers the
// lid there and drags the brow into a frown with it.
void DisplayDriver::buildLidProfile(float openness, bool scowl) {
  for (int16_t x = 0; x < kCanvasWidth; ++x) {
    const float position = static_cast<float>(x - kEyeCenterX) / static_cast<float>(kEyeHalfWidth);
    const float clamped = constrain(position, -1.0f, 1.0f);
    const float curve = lidCurve(clamped);

    float upperOpening = static_cast<float>(kUpperLidOpen) * openness;
    if (scowl) {
      upperOpening -= static_cast<float>(kAngryLidNotch) * (1.0f - fabsf(clamped));
    }
    upperLid_[x] = kEyeCenterY - static_cast<int16_t>(upperOpening * curve);
    lowerLid_[x] =
        kEyeCenterY + static_cast<int16_t>(static_cast<float>(kLowerLidOpen) * curve * openness);
  }
}

// Vertical shading across the eyeball: shadow under the upper lid, highlight
// in the upper middle, a softer shade towards the lower lid.
void DisplayDriver::paintSclera() {
  for (int16_t x = 0; x < kCanvasWidth; ++x) {
    const int16_t top = upperLid_[x];
    const int16_t height = lowerLid_[x] - top;
    if (height <= 0) continue;

    int16_t y = top;
    for (uint8_t band = 0; band < kScleraBands; ++band) {
      const int16_t nextY = top + (height * (band + 1)) / kScleraBands;
      const int16_t bandHeight = nextY - y;
      if (bandHeight > 0) {
        const float position = static_cast<float>(band) / static_cast<float>(kScleraBands - 1);
        canvas_.drawFastVLine(x, y, bandHeight, color565(sampleSclera(position)));
      }
      y = nextY;
    }
  }
}

void DisplayDriver::paintIris(int16_t centerX, int16_t centerY, uint16_t hue) {
  // The iris palette tracks the blade colour: dark limbal ring, saturated
  // body, pale halo around the pupil.
  const Rgb rim = hsvToRgb(hue, 255, 38);
  const Rgb outer = hsvToRgb(hue, 235, 150);
  const Rgb inner = hsvToRgb(hue, 150, 235);
  const Rgb fibre = hsvToRgb(hue, 205, 190);

  // Leaves a thin dark limbal ring visible around the body.
  canvas_.fillCircle(centerX, centerY, kIrisRadius, color565(rim));

  // Rings tile contiguously from the rim inwards so no dark band is left
  // between the body and the pupil.
  const int16_t bodyRadius = kIrisRadius - 2;
  for (uint8_t ring = 0; ring < kIrisRings; ++ring) {
    const float position = static_cast<float>(ring) / static_cast<float>(kIrisRings - 1);
    const int16_t radius =
        bodyRadius - static_cast<int16_t>(static_cast<float>(bodyRadius - kPupilRadius) * position);
    canvas_.fillCircle(centerX, centerY, radius,
                       color565(lerpColor(outer, inner, position)));
  }

  // Radial fibres are what make an iris read as an iris rather than a disc.
  const int16_t fibreInner = kPupilRadius + 3;
  const int16_t fibreOuter = kIrisRadius - 3;
  for (uint8_t index = 0; index < kIrisFibreCount; ++index) {
    const float angle = (static_cast<float>(index) / static_cast<float>(kIrisFibreCount)) * kTwoPi;
    const float cosine = cosf(angle);
    const float sine = sinf(angle);
    canvas_.drawLine(centerX + static_cast<int16_t>(cosine * fibreInner),
                     centerY + static_cast<int16_t>(sine * fibreInner),
                     centerX + static_cast<int16_t>(cosine * fibreOuter),
                     centerY + static_cast<int16_t>(sine * fibreOuter),
                     color565(fibre));
  }

  canvas_.fillCircle(centerX, centerY, kPupilRadius, color565(kPupil));
  canvas_.fillCircle(centerX + kHighlightMainX, centerY + kHighlightMainY, kHighlightMainRadius,
                     TFT_WHITE);
  canvas_.fillCircle(centerX + kHighlightSparkX, centerY + kHighlightSparkY, kHighlightSparkRadius,
                     TFT_WHITE);
}

// The iris and pupil travel past the lids; clipping them back to the opening
// keeps the silhouette a clean almond instead of a sliding disc.
void DisplayDriver::maskOutsideEye() {
  for (int16_t x = 0; x < kCanvasWidth; ++x) {
    if (upperLid_[x] > 0) {
      canvas_.drawFastVLine(x, 0, upperLid_[x], TFT_BLACK);
    }
    if (lowerLid_[x] < kCanvasHeight - 1) {
      const int16_t start = lowerLid_[x] + 1;
      canvas_.drawFastVLine(x, start, kCanvasHeight - start, TFT_BLACK);
    }
  }
}

void DisplayDriver::paintLidStrokes() {
  for (int16_t x = 0; x < kCanvasWidth; ++x) {
    const int16_t top = upperLid_[x];
    if (top >= 0 && top < kCanvasHeight) {
      const int16_t height = min<int16_t>(kLidStrokeUpper, kCanvasHeight - top);
      canvas_.drawFastVLine(x, top, height, color565(kLidLine));
    }
    const int16_t bottom = lowerLid_[x];
    if (bottom >= 0 && bottom < kCanvasHeight) {
      const int16_t height = min<int16_t>(kLidStrokeLower, bottom + 1);
      canvas_.drawFastVLine(x, bottom - height + 1, height, color565(kLidLine));
    }
  }
}

// A short, straight brow anchored just above the scowl notch.  Riding the lid
// all the way to the corners made it hook downwards and read as a hat, so it
// keeps its own span and rise instead.
void DisplayDriver::paintBrow() {
  for (int16_t x = 0; x < kCanvasWidth; ++x) {
    const float position = static_cast<float>(x - kEyeCenterX) / static_cast<float>(kEyeHalfWidth);
    if (fabsf(position) >= kAngryBrowSpan) continue;

    const int16_t lift =
        static_cast<int16_t>(kAngryBrowLift * (fabsf(position) / kAngryBrowSpan));
    const int16_t top = upperLid_[x] - kAngryBrowGap - lift;
    if (top < 0) continue;
    const int16_t height = min<int16_t>(kAngryBrowThickness, kCanvasHeight - top);
    canvas_.drawFastVLine(x, top, height, color565(kBrow));
  }
}

// A closed eye reads as one heavy lash line rather than a white sliver: bowed
// downwards, thickest in the middle and tapering to the corners.
void DisplayDriver::paintClosedLid() {
  for (int16_t x = 0; x < kCanvasWidth; ++x) {
    const float position = static_cast<float>(x - kEyeCenterX) / static_cast<float>(kEyeHalfWidth);
    const float curve = lidCurve(constrain(position, -1.0f, 1.0f));
    if (curve <= 0.0f) continue;

    const int16_t centerY = kEyeCenterY + static_cast<int16_t>(static_cast<float>(kClosedLidBow) * curve);
    const int16_t thickness =
        static_cast<int16_t>(static_cast<float>(kClosedLidThickness) * curve * curve);
    if (thickness <= 0 || centerY < 0) continue;
    const int16_t height = min<int16_t>(thickness, kCanvasHeight - centerY);
    canvas_.drawFastVLine(x, centerY, height, color565(kClosedLid));
  }
}

void DisplayDriver::drawCenteredText(const char* text, int16_t y, uint8_t size, uint16_t color) {
  tft_.setTextSize(size);
  // Transparent background: a background fill of black made the title on the
  // white QR screen invisible.
  tft_.setTextColor(color);
  tft_.setTextDatum(MC_DATUM);
  tft_.drawString(text, kScreenSize / 2, y);
}
