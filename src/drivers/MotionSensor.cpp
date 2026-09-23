#include "MotionSensor.h"

#include <Wire.h>

#include "../../include/HardwareConfig.h"

namespace {

constexpr float kGravity = 9.80665f;
// MPU6050_ACCEL_FS_16 reports 2048 LSB per g.  The old divisor of 16384 made
// every acceleration eight times too small, so subtracting gravity left a
// permanent ~8.6 m/s^2 bias and the position sent to Blender saturated at its
// clamp immediately.
constexpr float kAccelScale = kGravity / HardwareConfig::AccelLsbPerGravity;
// Madgwick expects degrees per second, which it converts to radians itself.
constexpr float kGyroScale = 1.0f / HardwareConfig::GyroLsbPerDegree;
constexpr float kFilterRateHz = 1000.0f / HardwareConfig::MotionInterval;
constexpr int16_t kMagnitudeDivisor = 100;
constexpr int16_t kRotationDivisor = 2;

float square(float value) { return value * value; }

void rotateToWorld(const float quaternion[4], float ax, float ay, float az, float result[3]) {
  const float qw = quaternion[0];
  const float qx = quaternion[1];
  const float qy = quaternion[2];
  const float qz = quaternion[3];

  result[0] = ax * (1.0f - 2.0f * (qy * qy + qz * qz)) +
              ay * 2.0f * (qx * qy - qz * qw) +
              az * 2.0f * (qx * qz + qy * qw);
  result[1] = ax * 2.0f * (qx * qy + qz * qw) +
              ay * (1.0f - 2.0f * (qx * qx + qz * qz)) +
              az * 2.0f * (qy * qz - qx * qw);
  result[2] = ax * 2.0f * (qx * qz - qy * qw) +
              ay * 2.0f * (qy * qz + qx * qw) +
              az * (1.0f - 2.0f * (qx * qx + qy * qy));
}

}  // namespace

bool MotionSensor::begin() {
  imu_.initialize();
  imu_.setFullScaleAccelRange(MPU6050_ACCEL_FS_16);
  imu_.setFullScaleGyroRange(MPU6050_GYRO_FS_250);
  connected_ = imu_.testConnection();
  filter_.begin(kFilterRateHz);
  if (!connected_) {
    Serial.println("[IMU] MPU6050 not responding");
  }
  return connected_;
}

void MotionSensor::update() {
  const unsigned long now = millis();
  if (now - lastSample_ < HardwareConfig::MotionInterval) return;

  const float deltaTime = static_cast<float>(now - lastSample_) / 1000.0f;
  lastSample_ = now;
  if (!connected_) return;

  imu_.getMotion6(&data_.ax, &data_.ay, &data_.az, &data_.gx, &data_.gy, &data_.gz);

  const uint32_t ax = abs(data_.ax / kMagnitudeDivisor);
  const uint32_t ay = abs(data_.ay / kMagnitudeDivisor);
  const uint32_t az = abs(data_.az / kMagnitudeDivisor);
  const uint32_t gx = abs(data_.gx / kMagnitudeDivisor);
  const uint32_t gy = abs(data_.gy / kMagnitudeDivisor);
  const uint32_t gz = abs(data_.gz / kMagnitudeDivisor);
  data_.acceleration = sqrt(square(static_cast<float>(ax)) + square(static_cast<float>(ay)) +
                            square(static_cast<float>(az)));
  data_.rotation = sqrt(square(static_cast<float>(gx)) + square(static_cast<float>(gy)) +
                        square(static_cast<float>(gz))) /
                   kRotationDivisor;

  const float accelX = static_cast<float>(data_.ax) * kAccelScale;
  const float accelY = static_cast<float>(data_.ay) * kAccelScale;
  const float accelZ = static_cast<float>(data_.az) * kAccelScale;
  filter_.updateIMU(static_cast<float>(data_.gx) * kGyroScale,
                    static_cast<float>(data_.gy) * kGyroScale,
                    static_cast<float>(data_.gz) * kGyroScale, accelX, accelY, accelZ);

  data_.roll = filter_.getRollRadians();
  data_.pitch = filter_.getPitchRadians();
  const float roll = data_.roll;
  const float pitch = data_.pitch;
  const float yaw = filter_.getYawRadians();
  const float cy = cos(yaw * 0.5f);
  const float sy = sin(yaw * 0.5f);
  const float cp = cos(pitch * 0.5f);
  const float sp = sin(pitch * 0.5f);
  const float cr = cos(roll * 0.5f);
  const float sr = sin(roll * 0.5f);
  data_.quaternion[0] = cr * cp * cy + sr * sp * sy;
  data_.quaternion[1] = sr * cp * cy - cr * sp * sy;
  data_.quaternion[2] = cr * sp * cy + sr * cp * sy;
  data_.quaternion[3] = cr * cp * sy - sr * sp * cy;

  const float accel[3] = {accelX, accelY, accelZ};
  updatePosition(accel, deltaTime);
}

// Dead-reckons a position for the Blender feed.  Acceleration is rotated into
// the world frame, gravity removed, and a dead zone zeroes the drift that
// would otherwise integrate away while the saber is held still.
void MotionSensor::updatePosition(const float accel[3], float deltaTime) {
  float worldAccel[3];
  rotateToWorld(data_.quaternion, accel[0], accel[1], accel[2], worldAccel);
  worldAccel[2] -= kGravity;

  for (uint8_t axis = 0; axis < 3; ++axis) {
    if (fabsf(worldAccel[axis]) < HardwareConfig::PositionDeadZone) {
      worldAccel[axis] = 0.0f;
      velocity_[axis] = 0.0f;
    }
    velocity_[axis] += worldAccel[axis] * deltaTime;
    data_.position[axis] += velocity_[axis] * deltaTime;
    data_.position[axis] =
        constrain(data_.position[axis], -HardwareConfig::PositionLimit,
                  HardwareConfig::PositionLimit);
  }
}
