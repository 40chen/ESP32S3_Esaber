#pragma once

#include <IPAddress.h>
#include <WiFiUdp.h>

#include "../drivers/MotionSensor.h"

// ============================================================================
// MotionTelemetry —— 动捕遥测：把姿态四元数 + 航位推算位置推给电脑上的 Blender
// ============================================================================
// 线格式 = 动捕协议 v1（契约见 shared/esaber-blender-link/PROTOCOL.md，冻结）：
//   31 字节定长 UDP 数据报，端口 TelemetryPort，节拍 TelemetryInterval（50Hz）。
// 字段顺序沿用旧 CSV 流（w,x,y,z,px,py,pz），旧工具只需换解析器，语义映射不变。
// ============================================================================
class MotionTelemetry {
 public:
  void begin(const String& targetIp);
  void update(const MotionSensor& sensor);   // 主循环调用：自带节拍与目标有效性门
  void setTarget(const String& targetIp);    // web 端改目标时调用

 private:
  WiFiUDP udp_;
  IPAddress target_;            // 0.0.0.0 = 未设置，不发送
  unsigned long lastSend_ = 0;
  uint8_t sequence_ = 0;        // 帧序号，模 256 回绕（首帧从 0 起，接收端据此统计丢帧）
};
