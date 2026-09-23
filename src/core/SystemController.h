#pragma once

#include <WebServer.h>

#include "../app/SaberController.h"
#include "../drivers/AudioOutput.h"
#include "../drivers/DisplayDriver.h"
#include "../drivers/MotionSensor.h"
#include "../drivers/PixelStrip.h"
#include "../drivers/SdCardDriver.h"
#include "../services/MotionTelemetry.h"
#include "../services/SettingsStore.h"
#include "../services/WifiService.h"
#include "../web/WebService.h"

// Owns every subsystem, wires them together and drives the main loop.
// The panel alternates between the eye and the WiFi QR code; the boot button
// switches between the two.
class SystemController {
 public:
  void begin();
  void update();

 private:
  enum class ScreenMode : uint8_t { Eye, Qr };

  void logResetReason();
  void logDiagnosticsOnce();
  void handleBootButton();
  void setScreenMode(ScreenMode mode);

  DisplayDriver display_;
  SdCardDriver sdCard_;
  AudioOutput audio_;
  PixelStrip strip_;
  MotionSensor motion_;
  SettingsStore settings_;
  WifiService wifi_;
  MotionTelemetry telemetry_;
  SaberController saber_;
  WebServer server_{80};
  WebService web_;

  ScreenMode screenMode_ = ScreenMode::Eye;
  bool hardwareReady_ = false;
  bool lastButtonState_ = true;
  bool buttonHandled_ = false;
  bool diagnosticsLogged_ = false;
  unsigned long lastButtonChange_ = 0;
  unsigned long lastQrRefresh_ = 0;
};
