// ============================================================================
// MotionSensor —— MPU6050 + Madgwick AHRS 姿态融合
// 输出三类数据：挥/砍判定用的原始幅值、眼睛用的欧拉角、
// Blender 动捕用的四元数 + 航位推算位置。
// ============================================================================
#include "MotionSensor.h"

#include <Wire.h>

#include "../../include/HardwareConfig.h"

namespace {

constexpr float kGravity = 9.80665f;   // 标准重力 m/s²
// MPU6050_ACCEL_FS_16 报 2048 LSB/g。旧的除数 16384 让所有加速度小了八倍，
// 减重力后残留 ~8.6 m/s² 的固定偏置，发给 Blender 的位置直接饱和在钳位上。
constexpr float kAccelScale = kGravity / HardwareConfig::AccelLsbPerGravity;
// Madgwick 期望 °/s，它自己转弧度
constexpr float kGyroScale = 1.0f / HardwareConfig::GyroLsbPerDegree;
constexpr float kFilterRateHz = 1000.0f / HardwareConfig::MotionInterval;   // 滤波器采样率
constexpr int16_t kMagnitudeDivisor = 100;   // 幅值预除：防平方溢出
constexpr int16_t kRotationDivisor = 2;      // 角速度幅值缩放（手感调校）

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
  imu_.setFullScaleAccelRange(MPU6050_ACCEL_FS_16);   // ±16g：砍击不饱和
  imu_.setFullScaleGyroRange(MPU6050_GYRO_FS_250);    // ±250°/s：挥动精度
  connected_ = imu_.testConnection();
  filter_.begin(kFilterRateHz);
  if (!connected_) {
    Serial.println("[IMU] MPU6050 not responding");
  }
  return connected_;
}

void MotionSensor::update() {
  const unsigned long now = millis();
  if (now - lastSample_ < HardwareConfig::MotionInterval) return;   // 按节拍采样

  const float deltaTime = static_cast<float>(now - lastSample_) / 1000.0f;   // 实际间隔，积分用
  lastSample_ = now;
  if (!connected_) return;

  imu_.getMotion6(&data_.ax, &data_.ay, &data_.az, &data_.gx, &data_.gy, &data_.gz);

  // 幅值（除 100 防溢出，平方和开方）：给挥/砍阈值判定用，不要物理单位
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

  // 物理单位换算：m/s² 和 °/s，进 AHRS 滤波
  const float accelX = static_cast<float>(data_.ax) * kAccelScale;
  const float accelY = static_cast<float>(data_.ay) * kAccelScale;
  const float accelZ = static_cast<float>(data_.az) * kAccelScale;
  filter_.updateIMU(static_cast<float>(data_.gx) * kGyroScale,
                    static_cast<float>(data_.gy) * kGyroScale,
                    static_cast<float>(data_.gz) * kGyroScale, accelX, accelY, accelZ);

  // 欧拉角（弧度）→ 四元数（w,x,y,z）：给眼睛视线和 Blender 遥测
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
  data_.quaternion[0] = cr * cp * cy + sr * sp * sy;   // w
  data_.quaternion[1] = sr * cp * cy - cr * sp * sy;   // x
  data_.quaternion[2] = cr * sp * cy + sr * cp * sy;   // y
  data_.quaternion[3] = cr * cp * sy - sr * sp * cy;   // z

  const float accel[3] = {accelX, accelY, accelZ};
  updatePosition(accel, deltaTime);
}

// 为 Blender 数据流做航位推算：加速度旋到世界系 → 去重力 →
// 死区归零（否则静止持握时漂移会一直积分跑掉）。
void MotionSensor::updatePosition(const float accel[3], float deltaTime) {
  float worldAccel[3];
  rotateToWorld(data_.quaternion, accel[0], accel[1], accel[2], worldAccel);
  worldAccel[2] -= kGravity;   // 静止时抵消重力，只留真实运动

  for (uint8_t axis = 0; axis < 3; ++axis) {
    if (fabsf(worldAccel[axis]) < HardwareConfig::PositionDeadZone) {
      worldAccel[axis] = 0.0f;   // 死区内视为静止
      velocity_[axis] = 0.0f;    // 速度同步归零，防积分漂移
    }
    velocity_[axis] += worldAccel[axis] * deltaTime;       // v = v + a·dt
    data_.position[axis] += velocity_[axis] * deltaTime;   // x = x + v·dt
    data_.position[axis] =
        constrain(data_.position[axis], -HardwareConfig::PositionLimit,
                  HardwareConfig::PositionLimit);          // 限幅防跑飞
  }
}
