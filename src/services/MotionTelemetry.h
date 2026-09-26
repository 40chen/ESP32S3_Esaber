#pragma once

#include <IPAddress.h>
#include <WiFiUdp.h>

#include "../drivers/MotionSensor.h"

class MotionTelemetry {
 public:
  void begin(const String& targetIp);
  void update(const MotionSensor& sensor);
  void setTarget(const String& targetIp);

 private:
  WiFiUDP udp_;
  IPAddress target_;
  unsigned long lastSend_ = 0;
  uint8_t sequence_ = 0;  // wire seq byte, wraps mod 256
};
