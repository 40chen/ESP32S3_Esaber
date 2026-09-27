#pragma once

#include <TFT_eSPI.h>

#include "../../include/AppTypes.h"

// Draws the animated eye and the WiFi QR code on the round GC9A01 panel.
//
// The eye is a single RGB565 square of artwork (see tools/make_eye_texture.py)
// blitted into a full panel sprite one row at a time.  Two properties of the
// artwork make that cheap: it is composited onto black, so the transparent
// corners need no masking, and the panel behind it is black as well, so a row
// can be moved or squeezed without any per-pixel work.
//
// The sprite is pushed to the panel in one burst.  Redrawing the panel
// directly flickered badly, because a full 240x240 frame takes longer to clock
// out over SPI than the render interval and the panel was therefore always
// caught mid update.  The sprite also lets the driver skip the SPI burst
// entirely while the eye is not moving, which hands the CPU back to the audio
// decoder.
class DisplayDriver {
 public:
  void begin();

  // Deliberately separate from begin(): the backlight inrush is kept away from
  // the panel and card initialisation, and nobody wants to watch an
  // uninitialised panel.
  void enableBacklight();

  void showBoot(bool ready);
  void showQr(const char* title, const char* url);

  // Re-runs the panel's reset and vendor sequence.  The GC9A01 has no reset
  // line here (TFT_RST is -1), so once its serial interface is disturbed mid
  // sequence nothing in software brings it back -- the corruption survives
  // until the next power cycle.  Calling this once more after the current
  // hungry parts of the boot have run repairs that case.  TFT_eSPI skips the
  // bus setup on a second init() and only repeats the reset and the register
  // table, so this is safe to call again.
  void repairPanel();

  // lookX / lookY are normalised gaze targets in [-1, 1]; the driver smooths
  // them, adds idle movement and blinking, and pushes a frame only when the
  // result actually changed.
  void drawEye(float lookX, float lookY, EyePattern pattern);

 private:
  // Which screen currently owns the panel.  The eye only has to clear the
  // screen when something else painted over it, and the QR is expensive enough
  // to draw that it is worth skipping while the address is unchanged.
  enum class Panel : uint8_t { Eye, Boot, Qr };

  static constexpr int16_t kPanelSize = 240;
  static constexpr int16_t kEyeCenterX = kPanelSize / 2;
  static constexpr int16_t kEyeCenterY = kPanelSize / 2;
  // How far the eyeball may slide from centre.  Horizontally this puts the rim
  // exactly on the panel edge at full deflection, so a glance never leaves the
  // screen; vertically it is kept smaller because the panel is round.
  static constexpr int16_t kEyeTravelX = 20;
  static constexpr int16_t kEyeTravelY = 14;

  // A failed PSRAM allocation is retried this often rather than leaving the
  // eye blank for the rest of the session.
  static constexpr unsigned long kCanvasRetryMs = 1000;

  bool ensureCanvas(unsigned long now);
  void paintEye(int16_t offsetX, int16_t offsetY, float openness);
  void paintBrows(int16_t offsetX, int16_t offsetY, float openness);

  void updateGaze(unsigned long now, float targetX, float targetY, bool attentive);
  void updateBlink(unsigned long now);

  // Returns true when the frame differs enough from the last pushed frame to
  // be worth drawing and clocking out.
  bool frameChanged(float openness, EyePattern pattern) const;
  void markFramePushed(float openness, EyePattern pattern);

  void drawCenteredText(const char* text, int16_t y, uint8_t size, uint16_t color);

  TFT_eSPI tft_;
  TFT_eSprite canvas_{&tft_};
  bool canvasReady_ = false;
  unsigned long canvasRetry_ = 0;

  Panel panel_ = Panel::Boot;
  String qrUrl_;  // last address rendered, so an unchanged QR is not redrawn

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

  float pushedGazeX_ = 2.0f;
  float pushedGazeY_ = 2.0f;
  float pushedOpenness_ = -1.0f;
  int8_t pushedPattern_ = -1;
};
