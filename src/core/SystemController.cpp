// ============================================================================
// SystemController —— 总装配体：持有全部子系统、接线、驱动主循环
// 初始化顺序刻意设计（见 begin() 内逐段注释）；
// 主循环顺序有讲究：音频解码最优先（断流可闻），其余依次。
// ============================================================================
#include "SystemController.h"

#include <Wire.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../../include/HardwareConfig.h"

namespace {

constexpr unsigned long kSerialSettleMs = 1000;   // 串口稳定等待
constexpr uint16_t kQrRefreshMs = 1000;           // 二维码地址查询节流
// 【改动④关联 · 启动错峰】背光与 radio 的启动间隔——两者的电流台阶
// 不落在同一时刻，减轻本就紧张的 3.3V 轨压力。
constexpr unsigned long kBootStepSettleMs = 80;
// One-shot resource report, late enough that the web server and the audio
// decoder have both run at least once.
constexpr unsigned long kDiagnosticsDelayMs = 5000;

// A reset reason is the only way to tell a brown-out from a crash from a
// watchdog reset after the fact, so name it explicitly on every boot.
// 复位原因是事后区分"欠压 / 崩溃 / 看门狗"的唯一线索，每次开机都明确报出
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
  // ---- 第 1 步：灯条数据线先拉低（一切慢操作/大电流操作之前）----
  // MCU 复位期间，灯条会把悬空数据线上的噪声锁存成随机颜色，并保持到第一次
  // show()。那次 show() 曾发生在串口稳定、NVS 读取、屏幕初始化、挂卡、解码器
  // 稳定之后——超过一秒里刀身发着随机颜色、抽着随机电流，还穿过屏幕初始化窗口。
  // 数据线保持低电平可阻止继续锁存；全黑帧清掉已锁存的。
  pinMode(HardwareConfig::LedPin, OUTPUT);
  digitalWrite(HardwareConfig::LedPin, LOW);          // 数据线拉低 = 禁止锁存
  strip_.begin();

  // ---- 第 2 步：功放使能脚拉低 ----
  // 复位期间它也是悬空的：可能停在高电平，让编解码器的本底噪声直接上喇叭。
  pinMode(HardwareConfig::PaEnable, OUTPUT);
  digitalWrite(HardwareConfig::PaEnable, LOW);        // 静默功放

  // ---- 第 3 步：BOOT 按键上拉（在读它之前，悬空会被误判成按下）----
  pinMode(HardwareConfig::BootButton, INPUT_PULLUP);

  Serial.begin(115200);
  delay(kSerialSettleMs);                             // 等串口稳定再打日志
  Serial.println("\n[ESABER] starting");
  logBuildStamp();
  logResetReason();

  // ---- 第 4 步：基础服务 ----
  Wire.begin(HardwareConfig::I2cSda, HardwareConfig::I2cScl);   // IMU 总线
  settings_.begin();                                  // NVS 读回用户设置
  // display_.begin();          // [no-display] 屏幕功能整体停用（GC9A01 不初始化，背光仍灭）

  const bool motionReady = motion_.begin();
  const bool sdReady = sdCard_.begin();
  const bool audioReady = audio_.begin(sdReady);      // 挂卡成功才扫音效
  hardwareReady_ = motionReady && sdReady && audioReady;
  // 一行日志说明坏启动卡在哪一环。屏幕挂了只能看串口判断（死屏上看不出差别）。
  Serial.printf("[ESABER] motion %s, sd %s, audio %s\n", motionReady ? "ok" : "FAIL",
                sdReady ? "ok" : "FAIL", audioReady ? "ok" : "FAIL");

  // ---- 第 5 步：屏幕二次修复 ----
  // 挂卡和解码器稳定是启动里最重的电流台阶，而屏幕寄存器序列是开环的：
  // 在那个窗口被打断就一直坏到下次断电。趁背光还灭着重跑一遍，
  // 代价只是屏幕的复位延时——所以只跑这一次。
  // display_.repairPanel();        // [no-display] 屏幕复位序列停用
  // display_.showBoot(hardwareReady_);  // [no-display] 开机画面停用
  // Serial.println("[BOOT] panel ready");
  Serial.println("[BOOT] display disabled (no-display build)");

  // ---- 第 6 步：启动错峰【改动④关联】----
  // radio 校准时抽最重的脉冲，背光稳定电流又是供电预算的大头——两者错开：
  // 屏幕背光仍灭时完成初始化和首绘 → radio 单独启动 → 最后才点亮背光。
  delay(kBootStepSettleMs);

  wifi_.begin();                                      // AP 热点 + beacon 配置
  Serial.println("[BOOT] wifi up");

  // [no-display] 背光功能停用：显式拉低 GPIO8，避免浮空导致背光半亮
  pinMode(HardwareConfig::DisplayBacklight, OUTPUT);
  digitalWrite(HardwareConfig::DisplayBacklight, LOW);
  Serial.println("[BOOT] backlight off (no-display build)");

  // ---- 第 7 步：业务子系统 ----
  // telemetry_.begin(settings_.blenderIp());  // 动捕停用；保留 IMU 供光剑手势使用
  saber_.begin(&audio_, &strip_, &motion_, settings_.saber());
  web_.begin(&server_, &saber_, &wifi_, &settings_, &telemetry_);
  Serial.println("[ESABER] ready");
}

void SystemController::update() {
  // 先喂音频解码器：下面任何一步都可能阻塞，断流是听得见的
  saber_.update();                    // 光剑：手势/音效/灯效
  server_.handleClient();             // web 控制台请求
  settings_.tick();                   // 冲刷到期的 NVS 延迟写入（500ms 节流窗口）
  // telemetry_.update(motion_);       // 动捕停用，不再发送 UDP 遥测
  handleBootButton();
  logDiagnosticsOnce();

  if (screenMode_ == ScreenMode::Eye) {
    // [no-display] 眼睛动画停用：Eye 模式下不渲染任何画面
    // const MotionData& motion = motion_.data();
    // display_.drawEye(motion.roll / HardwareConfig::EyeGazeRadiansHorizontal,   // roll→水平视线
    //                  motion.pitch / HardwareConfig::EyeGazeRadiansVertical,    // pitch→垂直视线
    //                  settings_.saber().eyePattern);
    return;
  }

  // 只节流地址查询：每圈构造 localUrl() 会搅动堆。驱动对未变化的地址
  // 本来就跳过重绘，所以这里不是每秒重画二维码。
  const unsigned long now = millis();
  if (now - lastQrRefresh_ > kQrRefreshMs) {
    lastQrRefresh_ = now;
    // display_.showQr("ESABER WEB", wifi_.localUrl().c_str());  // [no-display] 二维码页停用
  }
}

// 启动几秒后报告一次。栈余量是判断"重启背后是否栈溢出"最快的依据：
// 如果趋势逼近 0，就要再加大栈了。
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
    // display_.showQr("ESABER WEB", wifi_.localUrl().c_str());  // [no-display] 二维码页停用
    lastQrRefresh_ = millis();
  }
  // Going back to the eye needs no paint here: the driver notices that the
  // panel was taken over and repaints itself on the next frame.
}
