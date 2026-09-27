#include "SystemController.h"

#include <Wire.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../../include/HardwareConfig.h"

namespace {

constexpr unsigned long kSerialSettleMs = 1000;
constexpr uint16_t kQrRefreshMs = 1000;
// Gap between the backlight and the radio so their current steps cannot land
// together on a supply that is already marginal.
constexpr unsigned long kBootStepSettleMs = 80;
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
  // First, before anything slow or current hungry.  The strip latches whatever
  // its floating data line picks up while the MCU is in reset, and it keeps
  // those colours until the first show().  That show() used to happen after the
  // serial settle, the NVS read, the panel init, the card mount and the codec
  // settle -- well over a second during which the blade glowed a random colour
  // and pulled a matching random current, straight through the panel's
  // initialisation.  Holding the line low stops anything further being latched;
  // the all-black frame clears what already was.
  pinMode(HardwareConfig::LedPin, OUTPUT);
  digitalWrite(HardwareConfig::LedPin, LOW);
  strip_.begin();

  // The amplifier enable floats during reset as well; left floating it can sit
  // high and amplify the codec's noise floor before anything is ever played.
  pinMode(HardwareConfig::PaEnable, OUTPUT);
  digitalWrite(HardwareConfig::PaEnable, LOW);

  // Before the first read of it, so a floating pin cannot look like a press.
  pinMode(HardwareConfig::BootButton, INPUT_PULLUP);

  Serial.begin(115200);
  delay(kSerialSettleMs);
  Serial.println("\n[ESABER] starting");
  logBuildStamp();
  logResetReason();

  Wire.begin(HardwareConfig::I2cSda, HardwareConfig::I2cScl);
  settings_.begin();
  display_.begin();

  const bool motionReady = motion_.begin();
  const bool sdReady = sdCard_.begin();
  const bool audioReady = audio_.begin(sdReady);
  hardwareReady_ = motionReady && sdReady && audioReady;
  // One line that says which stage a bad boot stopped at.  The panel init can
  // only be judged from the serial log when it fails, because a dead panel
  // looks the same whatever went wrong.
  Serial.printf("[ESABER] motion %s, sd %s, audio %s\n", motionReady ? "ok" : "FAIL",
                sdReady ? "ok" : "FAIL", audioReady ? "ok" : "FAIL");

  // Second chance for the panel.  The card mount and the codec settle are the
  // heaviest current steps in the boot, and the panel's register sequence is
  // open loop: disturbed in that window it stays broken until the next power
  // cycle.  Redone while the backlight is still off, so it costs nothing
  // visible -- but it does cost the panel's reset delay, hence once only.
  display_.repairPanel();
  display_.showBoot(hardwareReady_);
  Serial.println("[BOOT] panel ready");

  // The radio draws its heaviest pulse while it calibrates, and the backlight's
  // steady current is a large fraction of this supply's budget.  They are kept
  // apart: the panel is initialised and painted with the backlight still off,
  // the radio starts on its own, and only then does the screen light up.
  delay(kBootStepSettleMs);

  wifi_.begin();
  Serial.println("[BOOT] wifi up");

  display_.enableBacklight();
  Serial.println("[BOOT] backlight on");

  telemetry_.begin(settings_.blenderIp());
  saber_.begin(&audio_, &strip_, &motion_, settings_.saber());
  web_.begin(&server_, &saber_, &wifi_, &settings_, &telemetry_);
  Serial.println("[ESABER] ready");
}

void SystemController::update() {
  // Keep the audio decoder fed first: everything below can block.
  saber_.update();
  server_.handleClient();
  telemetry_.update(motion_);
  handleBootButton();
  logDiagnosticsOnce();

  if (screenMode_ == ScreenMode::Eye) {
    const MotionData& motion = motion_.data();
    display_.drawEye(motion.roll / HardwareConfig::EyeGazeRadiansHorizontal,
                     motion.pitch / HardwareConfig::EyeGazeRadiansVertical,
                     settings_.saber().eyePattern);
    return;
  }

  // Throttle only the address lookup: building localUrl() every iteration
  // churns the heap.  The driver itself skips the repaint while the address is
  // unchanged, so this does not redraw the QR once a second.
  const unsigned long now = millis();
  if (now - lastQrRefresh_ > kQrRefreshMs) {
    lastQrRefresh_ = now;
    display_.showQr("ESABER WEB", wifi_.localUrl().c_str());
  }
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

// Which build is actually running, and its app image hash.  The hash is the
// same one the panic handler prints, so a crash log and a boot log can be
// matched up without guessing whether the flash took.
void SystemController::logBuildStamp() {
  // The descriptor's own date/time fields come from the prebuilt framework, so
  // the compile-time macros are what identify this build.  The hash, on the
  // other hand, is patched into the image after linking and therefore changes
  // with every build -- it is exactly what the panic handler prints.
  const esp_app_desc_t* app = esp_ota_get_app_description();
  char sha[17];
  for (uint8_t index = 0; index < 8; ++index) {
    snprintf(&sha[index * 2], 3, "%02x", app->app_elf_sha256[index]);
  }
  Serial.printf("[ESABER] build %s %s, app sha %s\n", __DATE__, __TIME__, sha);
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
    display_.showQr("ESABER WEB", wifi_.localUrl().c_str());
    lastQrRefresh_ = millis();
  }
  // Going back to the eye needs no paint here: the driver notices that the
  // panel was taken over and repaints itself on the next frame.
}
