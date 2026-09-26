#pragma once

#include <TFT_eSPI.h>

#include "../../include/AppTypes.h"

// Draws the animated eye and the WiFi QR code on the round GC9A01 panel.
//
// The eye is composed into a PSRAM sprite and pushed to the panel in a single
// burst.  Redrawing the panel directly flickered badly, because a full 240x240
// frame takes longer to clock out over SPI than the render interval and the
// panel was therefore always caught mid update.  The sprite also lets the
// driver skip the SPI burst entirely while the eye is not moving, which hands
// the CPU back to the audio decoder.
class DisplayDriver {
 public:
  void begin();

  void showBoot(bool ready);
  // hint is an optional ASCII-only credentials line drawn under the QR block.
  void showQr(const char* url, const char* hint = nullptr);

  // lookX / lookY are normalised gaze targets in [-1, 1]; the driver smooths
  // them, adds idle movement and blinking, and pushes a frame only when the
  // picture actually changed.  bladeRed/Green/Blue tint the iris so the eye
  // follows the blade colour.
  void drawEye(float lookX, float lookY, EyePattern pattern, uint8_t bladeRed,
               uint8_t bladeGreen, uint8_t bladeBlue);

  // One-shot flinch: openness dips and recovers over durationMs.
  void squint(unsigned long durationMs);

 private:
  // Which screen currently owns the panel.  The eye only has to clear the
  // screen when something else painted over it, and the QR is expensive enough
  // to draw that it is worth skipping while the address is unchanged.
  enum class Panel : uint8_t { Eye, Boot, Qr };

  static constexpr int16_t kCanvasX = 24;
  static constexpr int16_t kCanvasY = 28;
  static constexpr int16_t kCanvasWidth = 192;
  static constexpr int16_t kCanvasHeight = 176;

  static constexpr int16_t kEyeCenterX = kCanvasWidth / 2;
  static constexpr int16_t kEyeCenterY = 90;
  // Roughly 1.8:1, the proportion that reads as an eye rather than a ball.
  static constexpr int16_t kEyeHalfWidth = 88;
  static constexpr int16_t kUpperLidOpen = 54;
  static constexpr int16_t kLowerLidOpen = 42;

  static constexpr int16_t kIrisRadius = 32;
  static constexpr int16_t kPupilRadius = 11;
  static constexpr int16_t kGazeRangeX = 40;
  static constexpr int16_t kGazeRangeY = 10;

  static constexpr int16_t kLidStrokeUpper = 4;
  static constexpr int16_t kLidStrokeLower = 2;

  // Angry pinches the upper lid down towards the middle and lays a heavy brow
  // just above it.  A brow that merely follows the lid's arc peaks in the
  // middle and reads as a hat rather than a scowl.
  static constexpr int16_t kAngryLidNotch = 15;
  static constexpr int16_t kAngryBrowGap = 5;
  static constexpr int16_t kAngryBrowLift = 18;
  static constexpr int16_t kAngryBrowThickness = 11;
  static constexpr float kAngryBrowSpan = 0.62f;

  static constexpr float kAngryOpenness = 0.85f;
  static constexpr float kSleepOpenness = 0.05f;

  // Recomputes the lid curves for the current opening and expression.
  void buildLidProfile(float openness, bool scowl);
  void paintSclera();
  void paintIris(int16_t centerX, int16_t centerY, uint16_t hue);
  void maskOutsideEye();
  void paintLidStrokes();
  void paintBrow();
  void paintClosedLid();

  void updateGaze(unsigned long now, float targetX, float targetY, bool attentive);
  void updateBlink(unsigned long now);

  // Envelope for the clash flinch; 1.0 when no squint is active.
  float squintEnvelope(unsigned long now) const;

  // Returns true when the frame differs enough from the last pushed frame to
  // be worth drawing and clocking out.
  bool frameChanged(float openness, EyePattern pattern) const;
  void markFramePushed(float openness, EyePattern pattern);

  void drawCenteredText(const char* text, int16_t y, uint8_t size, uint16_t color);

  TFT_eSPI tft_;
  TFT_eSprite canvas_{&tft_};
  bool canvasReady_ = false;

  Panel panel_ = Panel::Boot;
  String qrUrl_;   // last address rendered, so an unchanged QR is not redrawn
  String qrHint_;  // last hint line rendered, same purpose

  int16_t upperLid_[kCanvasWidth] = {0};
  int16_t lowerLid_[kCanvasWidth] = {0};

  float gazeX_ = 0.0f;
  float gazeY_ = 0.0f;
  float idleTargetX_ = 0.0f;
  float idleTargetY_ = 0.0f;
  float idleX_ = 0.0f;
  float idleY_ = 0.0f;
  float openness_ = 1.0f;

  bool blinking_ = false;
  unsigned long blinkPhaseStart_ = 0;
  unsigned long blinkDue_ = 0;
  unsigned long blinkTimer_ = 0;
  unsigned long idleTimer_ = 0;
  unsigned long idleDue_ = 0;
  unsigned long squintStart_ = 0;
  unsigned long squintUntil_ = 0;

  float pushedGazeX_ = 2.0f;
  float pushedGazeY_ = 2.0f;
  float pushedOpenness_ = -1.0f;
  int8_t pushedPattern_ = -1;
};
