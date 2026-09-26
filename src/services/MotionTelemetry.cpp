#include "MotionTelemetry.h"

#include <string.h>

#include "../../include/HardwareConfig.h"

// Wire format "MotionTelemetryProtocol v1": one fixed 31-byte datagram.
//   [0]     u8  version, always 0x01
//   [1..28] 7 x float32 little-endian: w, x, y, z, px, py, pz
//   [29]    u8  sequence number, wraps mod 256 (first frame is 0)
//   [30]    u8  checksum, XOR of bytes [0..29]
// Field order mirrors the legacy CSV feed, so tooling built against that
// contract only has to swap the parser, not the semantic mapping.
namespace {
constexpr uint8_t kProtocolVersion = 0x01;
constexpr size_t kFrameBytes = 31;
}  // namespace

void MotionTelemetry::begin(const String& targetIp) {
  udp_.begin(0);
  setTarget(targetIp);
}

void MotionTelemetry::update(const MotionSensor& sensor) {
  const unsigned long now = millis();
  // No link check on purpose: in AP-only mode the PC joins the device
  // hotspot, and an unreachable target just costs a discarded datagram.
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
  // ESP32-S3 stores floats little-endian IEEE-754, matching the wire format
  // byte for byte; no per-field conversion needed.
  memcpy(&frame[1], fields, sizeof(fields));
  frame[29] = sequence_++;
  uint8_t checksum = 0;
  for (size_t i = 0; i + 1 < kFrameBytes; ++i) checksum ^= frame[i];
  frame[30] = checksum;

  udp_.beginPacket(target_, HardwareConfig::TelemetryPort);
  udp_.write(frame, sizeof(frame));
  udp_.endPacket();
}

void MotionTelemetry::setTarget(const String& targetIp) {
  target_.fromString(targetIp);
}
