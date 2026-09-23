#include "MotionTelemetry.h"

#include <WiFi.h>

#include "../../include/HardwareConfig.h"

void MotionTelemetry::begin(const String& targetIp) {
  udp_.begin(0);
  setTarget(targetIp);
}

void MotionTelemetry::update(const MotionSensor& sensor) {
  const unsigned long now = millis();
  if (now - lastSend_ < HardwareConfig::TelemetryInterval || !WiFi.isConnected() ||
      target_ == IPAddress(0, 0, 0, 0)) {
    return;
  }
  lastSend_ = now;

  const MotionData& data = sensor.data();
  char packet[100];
  snprintf(packet, sizeof(packet), "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",
           data.quaternion[0], data.quaternion[1], data.quaternion[2], data.quaternion[3],
           data.position[0], data.position[1], data.position[2]);
  udp_.beginPacket(target_, HardwareConfig::TelemetryPort);
  udp_.print(packet);
  udp_.endPacket();
}

void MotionTelemetry::setTarget(const String& targetIp) {
  target_.fromString(targetIp);
}
