// ============================================================================
// MotionTelemetry —— 动捕遥测：UDP 单播姿态数据给电脑上的 Blender
// 契约见 shared/blender/blender_mocap.py（7 字段 CSV，10Hz）。
// ============================================================================
#include "MotionTelemetry.h"

#include <WiFi.h>

#include "../../include/HardwareConfig.h"

void MotionTelemetry::begin(const String& targetIp) {
  udp_.begin(0);       // 随机本地端口：只发不收
  setTarget(targetIp);
}

void MotionTelemetry::update(const MotionSensor& sensor) {
  const unsigned long now = millis();
  // 三重门：未到 100ms 节拍 / WiFi 未连接 / 目标未设置 → 直接返回
  if (now - lastSend_ < HardwareConfig::TelemetryInterval || !WiFi.isConnected() ||
      target_ == IPAddress(0, 0, 0, 0)) {
    return;
  }
  lastSend_ = now;

  const MotionData& data = sensor.data();
  char packet[100];   // 7×%.4f 富余，绝不截断
  snprintf(packet, sizeof(packet), "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",
           data.quaternion[0], data.quaternion[1], data.quaternion[2], data.quaternion[3],
           data.position[0], data.position[1], data.position[2]);
  udp_.beginPacket(target_, HardwareConfig::TelemetryPort);   // → Blender 端 5005
  udp_.print(packet);
  udp_.endPacket();
}

void MotionTelemetry::setTarget(const String& targetIp) {
  target_.fromString(targetIp);   // 非法串 → 0.0.0.0，update 里的门会拦住
}
