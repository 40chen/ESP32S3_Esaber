#pragma once

#include <WebServer.h>

#include "../app/SaberController.h"
#include "../services/MotionTelemetry.h"
#include "../services/SettingsStore.h"
#include "../services/WifiService.h"

class WebService {
 public:
  void begin(WebServer* server, SaberController* saber, WifiService* wifi,
             SettingsStore* settings, MotionTelemetry* telemetry);

 private:
  void handleRoot();
  void handleStatus();
  void handleWifi();
  void handleSettings();
  void handlePower();
  void handleBlender();
  void handleNotFound();
  void sendJson(int code, const String& json);

  WebServer* server_ = nullptr;
  SaberController* saber_ = nullptr;
  WifiService* wifi_ = nullptr;
  SettingsStore* settings_ = nullptr;
  MotionTelemetry* telemetry_ = nullptr;
};
