#pragma once

#include <WebServer.h>

#include "../app/SaberController.h"
#include "../services/MotionTelemetry.h"
#include "../services/SettingsStore.h"
#include "../services/WifiService.h"

// web 控制台的路由层：持各子系统指针，handler 全部跑在 loop 任务上
class WebService {
 public:
  void begin(WebServer* server, SaberController* saber, WifiService* wifi,
             SettingsStore* settings, MotionTelemetry* telemetry);

 private:
  void handleRoot();       // /        → PROGMEM 内嵌页面
  void handleStatus();     // /api/status
  void handleSounds();     // /api/sounds：扫两个音效目录，分组返回
  void handleSettings();   // /api/settings：可选参数，只改所带的槽
  void handlePower();      // /api/power：开刃/收刃
  void handleBlender();    // /api/blender：动捕目标 IP
  void handleNotFound();   // 404 兜底
  void sendJson(int code, const String& json);

  WebServer* server_ = nullptr;
  SaberController* saber_ = nullptr;
  WifiService* wifi_ = nullptr;
  SettingsStore* settings_ = nullptr;
  MotionTelemetry* telemetry_ = nullptr;
};
