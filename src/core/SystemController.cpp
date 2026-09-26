#include "SystemController.h"

#include <Wire.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <ArduinoOTA.h>

#include "../../include/HardwareConfig.h"

namespace {

constexpr unsigned long kSerialSettleMs = 1000;
constexpr uint16_t kQrRefreshMs = 1000;
// One-shot resource report, late enough that the web server and the audio
// decoder have both run at least once.
constexpr unsigned long kDiagnosticsDelayMs = 5000;

// A reset reason is the only way to tell a brown-out from a crash from a
// watchdog reset after the fact, so name it explicitly on every boot.
const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic (exception / stack overflow)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "BROWNOUT - supply sagged";
    case ESP_RST_SDIO: return "SDIO";
    default: return "unknown";
  }
}

}  // namespace

void SystemController::begin() {
  Serial.begin(115200);
  delay(kSerialSettleMs);
  Serial.println("\n[ESABER] starting");
  logResetReason();

  Wire.begin(HardwareConfig::I2cSda, HardwareConfig::I2cScl);
  settings_.begin();
  display_.begin();
  pinMode(HardwareConfig::BootButton, INPUT_PULLUP);

  const bool motionReady = motion_.begin();
  const bool sdReady = sdCard_.begin();
  const bool audioReady = audio_.begin(sdReady);
  strip_.begin();
  hardwareReady_ = motionReady && sdReady && audioReady;
  display_.showBoot(hardwareReady_);

  wifi_.begin();
  telemetry_.begin(settings_.blenderIp());
  saber_.begin(&audio_, &strip_, &motion_, settings_.saber());
  audio_.setVolume(settings_.volume());
  web_.begin(&server_, &saber_, &wifi_, &settings_, &telemetry_, &audio_);

  // Wireless firmware updates, so iterating no longer means opening the hilt.
  ArduinoOTA.setHostname(HardwareConfig::OtaHostname);
  ArduinoOTA.setPassword(HardwareConfig::DefaultOtaPassword);
  ArduinoOTA.begin();
  Serial.println("[ESABER] ready");
}

void SystemController::update() {
  // Keep the audio decoder fed first: everything below can block.
  saber_.update();
  server_.handleClient();
  ArduinoOTA.handle();
  telemetry_.update(motion_);
  settings_.tick();  // deferred NVS persistence for streamed settings
  handleBootButton();
  logDiagnosticsOnce();
  reactToClash();

  if (screenMode_ == ScreenMode::Eye) {
    const MotionData& motion = motion_.data();
    const SaberSettings& saber = settings_.saber();
    display_.drawEye(motion.roll / HardwareConfig::EyeGazeRadiansHorizontal,
                     motion.pitch / HardwareConfig::EyeGazeRadiansVertical, saber.eyePattern,
                     saber.red, saber.green, saber.blue);
    return;
  }

  // Throttle only the address lookup: building localUrl() every iteration
  // churns the heap.  The driver itself skips the repaint while the address is
  // unchanged, so this does not redraw the QR once a second.
  const unsigned long now = millis();
  if (now - lastQrRefresh_ > kQrRefreshMs) {
    lastQrRefresh_ = now;
    display_.showQr(wifi_.localUrl().c_str(), qrHint().c_str());
  }
}

// The hotspot is open (no password, per user request), so the line under the
// QR names the network and says so.  ASCII-only: the GLCD font cannot render
// Chinese here.
String SystemController::qrHint() const {
  return "Esaber-Setup (open)";
}

// The eye flinches when the blade hits something: one squint per clash, keyed
// on the strike counter so a single hit never queues more than one flinch.
void SystemController::reactToClash() {
  const uint8_t strikes = saber_.strikeCount();
  if (strikes == lastStrikeCount_) return;
  lastStrikeCount_ = strikes;
  display_.squint(HardwareConfig::ClashSquintMs);
}

// Reported once, a few seconds in.  The headroom figure is the quickest way
// to confirm or rule out a loop-task stack overflow behind the reboots; if it
// trends towards zero the stack needs raising again.
void SystemController::logDiagnosticsOnce() {
  if (diagnosticsLogged_ || millis() < kDiagnosticsDelayMs) return;
  diagnosticsLogged_ = true;
  Serial.printf("[ESABER] loop stack headroom: %u bytes of %u, heap: %u free\n",
                uxTaskGetStackHighWaterMark(nullptr), HardwareConfig::LoopStackSize,
                ESP.getFreeHeap());
}

void SystemController::logResetReason() {
  const esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("[ESABER] reset reason: %s (%d)\n", resetReasonName(reason),
                static_cast<int>(reason));
  Serial.printf("[ESABER] heap: %u free / %u total, psram: %u free\n", ESP.getFreeHeap(),
                ESP.getHeapSize(), ESP.getFreePsram());
}

void SystemController::handleBootButton() {
  const bool pressed = digitalRead(HardwareConfig::BootButton) == LOW;
  const unsigned long now = millis();

  if (pressed != lastButtonState_) {
    lastButtonState_ = pressed;
    lastButtonChange_ = now;
    // Arm on the press edge so holding the button does not retrigger.
    if (pressed) buttonHandled_ = false;
    return;
  }

  if (!pressed || buttonHandled_ || now - lastButtonChange_ < HardwareConfig::ButtonDebounce) return;

  buttonHandled_ = true;
  setScreenMode(screenMode_ == ScreenMode::Eye ? ScreenMode::Qr : ScreenMode::Eye);
}

void SystemController::setScreenMode(ScreenMode mode) {
  screenMode_ = mode;
  if (mode == ScreenMode::Qr) {
    display_.showQr(wifi_.localUrl().c_str(), qrHint().c_str());
    lastQrRefresh_ = millis();
  }
  // Going back to the eye needs no paint here: the driver notices that the
  // panel was taken over and repaints itself on the next frame.
}
