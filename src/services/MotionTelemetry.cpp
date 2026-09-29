// ============================================================================
// MotionTelemetry —— 动捕遥测：UDP 单播姿态数据给电脑上的 Blender
// 契约见 shared/esaber-blender-link/PROTOCOL.md（动捕协议 v1：31B 定长帧，50Hz）。
// ============================================================================
#include "MotionTelemetry.h"

#include <string.h>

#include "../../include/HardwareConfig.h"

namespace {

constexpr uint8_t kProtocolVersion = 0x01;  // 帧头版本；接收端遇未知版本必须整帧丢弃
constexpr size_t kFrameBytes = 31;          // 1B 版本 + 7×4B 浮点 + 1B 序号 + 1B 校验

}  // namespace

void MotionTelemetry::begin(const String& targetIp) {
  udp_.begin(0);       // 随机本地端口：只发不收
  setTarget(targetIp);
}

void MotionTelemetry::update(const MotionSensor& sensor) {
  const unsigned long now = millis();
  // 双重门：未到节拍 / 目标未设置 → 直接返回。
  // 刻意不查 WiFi 连接状态：AP-only 模式下电脑连的是设备自己的热点，
  // 而 WiFi.isConnected() 查的是 STA，在纯 AP 模式下恒为 false——留着这个门
  // 会掐死整条链路。目标不可达的代价只是一条被丢弃的数据报，没有重试意义。
  if (now - lastSend_ < HardwareConfig::TelemetryInterval || target_ == IPAddress(0, 0, 0, 0)) {
    return;
  }
  lastSend_ = now;

  const MotionData& data = sensor.data();
  const float fields[7] = {data.quaternion[0], data.quaternion[1], data.quaternion[2],
                           data.quaternion[3], data.position[0], data.position[1],
                           data.position[2]};

  uint8_t frame[kFrameBytes] = {};
  frame[0] = kProtocolVersion;
  // ESP32-S3 的 float 内存布局就是小端 IEEE-754，与线格式逐字节一致，免逐字段转换
  memcpy(&frame[1], fields, sizeof(fields));
  frame[29] = sequence_++;
  uint8_t checksum = 0;
  for (size_t i = 0; i + 1 < kFrameBytes; ++i) checksum ^= frame[i];   // 前 30 字节异或
  frame[30] = checksum;

  udp_.beginPacket(target_, HardwareConfig::TelemetryPort);   // → Blender 端 5005
  udp_.write(frame, sizeof(frame));
  udp_.endPacket();
}

void MotionTelemetry::setTarget(const String& targetIp) {
  target_.fromString(targetIp);   // 非法串 → 0.0.0.0，update 里的门会拦住
}
