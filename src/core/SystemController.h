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

// ============================================================================
// SystemController —— 总装配体：持有全部子系统、接线、驱动主循环
// ============================================================================
// 屏幕在眼睛与 WiFi 二维码之间切换，BOOT 按键负责切换。
// 主循环顺序有讲究：音频解码最优先（断流可闻），其余依次。
// ============================================================================
class SystemController {
 public:
  void begin();     // 上电初始化序列（顺序敏感，见 .cpp 内注释）
  void update();    // 主循环节拍

 private:
  enum class ScreenMode : uint8_t { Eye, Qr };   // 面板当前归属

  void logBuildStamp();          // 打印构建信息 + app 哈希
  void logResetReason();         // 打印复位原因（判断 BROWNOUT 的唯一线索）
  void logDiagnosticsOnce();     // 开机 5s 后一次性资源报告
  void handleBootButton();       // BOOT 按键消抖 + 翻转屏幕
  void setScreenMode(ScreenMode mode);

  // ---- 子系统（成员顺序即依赖顺序）----
  DisplayDriver display_;      // 圆屏 + 眼睛 + 二维码
  SdCardDriver sdCard_;        // SD_MMC 挂载（音效文件所在）
  AudioOutput audio_;          // ES8311 编解码 + 功放管理
  PixelStrip strip_;           // WS2812 灯条（含限流）
  MotionSensor motion_;        // MPU6050 + Madgwick 姿态
  SettingsStore settings_;     // NVS 持久化设置
  WifiService wifi_;           // AP 热点 + beacon 配置
  MotionTelemetry telemetry_;  // UDP 动捕遥测（发 Blender）
  SaberController saber_;      // 光剑业务核心
  WebServer server_{80};       // HTTP 服务器
  WebService web_;             // web 控制台路由

  ScreenMode screenMode_ = ScreenMode::Eye;
  bool hardwareReady_ = false;       // 三大件自检结果
  bool lastButtonState_ = true;      // 按键上次电平（高=未按，上拉）
  bool buttonHandled_ = false;       // 本次按下是否已处理
  bool diagnosticsLogged_ = false;   // 资源报告只发一次
  unsigned long lastButtonChange_ = 0;   // 消抖计时
  unsigned long lastQrRefresh_ = 0;      // 二维码地址查询节流
};
