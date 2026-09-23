#pragma once

#include <Arduino.h>
#include <MPU6050.h>
#include <MadgwickAHRS.h>

struct MotionData {
  int16_t ax = 0;
  int16_t ay = 0;
  int16_t az = 0;
  int16_t gx = 0;
  int16_t gy = 0;
  int16_t gz = 0;
  uint32_t acceleration = 0;
  uint32_t rotation = 0;
  float roll = 0.0f;
  float pitch = 0.0f;
  float quaternion[4] = {1.0f, 0.0f, 0.0f, 0.0f};
  float position[3] = {0.0f, 0.0f, 0.0f};
};

// MPU6050 plus a Madgwick AHRS filter.  Publishes raw magnitudes for the
// swing / strike detectors, Euler angles for the eye, and a quaternion plus a
// dead-reckoned position for the Blender feed.
class MotionSensor {
 public:
  bool begin();
  void update();

  const MotionData& data() const { return data_; }

 private:
  void updatePosition(const float accel[3], float deltaTime);

  MPU6050 imu_;
  Madgwick filter_;
  MotionData data_;
  bool connected_ = false;
  unsigned long lastSample_ = 0;
  float velocity_[3] = {0.0f, 0.0f, 0.0f};
};
