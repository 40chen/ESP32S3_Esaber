#pragma once

#include <IPAddress.h>
#include <WiFiUdp.h>

#include "../drivers/MotionSensor.h"

// 动捕遥测发送器：把 MotionSensor 的四元数 + 位置
// 以 10Hz UDP 单播推给 Blender（IP 由控制台「动捕地址」设置）
class MotionTelemetry {
 public:
  void begin(const String& targetIp);
  void update(const MotionSensor& sensor);   // 主循环调用：自带节拍与连通性门
  void setTarget(const String& targetIp);    // web 端改目标时调用

 private:
  WiFiUDP udp_;
  IPAddress target_;            // 0.0.0.0 = 未设置，不发送
  unsigned long lastSend_ = 0;
};
