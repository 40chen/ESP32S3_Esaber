#pragma once

#include <Arduino.h>
#include <MPU6050.h>
#include <MadgwickAHRS.h>

// IMU 原始数据 + 加工结果：幅值给动作判定，欧拉角给眼睛，
// 四元数 + 位置给 Blender 动捕
struct MotionData {
  int16_t ax = 0;          // 原始加速度（LSB）
  int16_t ay = 0;
  int16_t az = 0;
  int16_t gx = 0;          // 原始角速度（LSB）
  int16_t gy = 0;
  int16_t gz = 0;
  uint32_t acceleration = 0;   // 加速度合成幅值（阈值判定用）
  uint32_t rotation = 0;       // 角速度合成幅值（挥动判定用）
  float roll = 0.0f;           // 横滚（弧度，眼睛视线）
  float pitch = 0.0f;          // 俯仰（弧度，眼睛视线）
  float quaternion[4] = {1.0f, 0.0f, 0.0f, 0.0f};   // w,x,y,z（遥测）
  float position[3] = {0.0f, 0.0f, 0.0f};           // 航位推算位置（米）
};

// MPU6050 + Madgwick AHRS。发布：挥/砍判定用的原始幅值、
// 眼睛用的欧拉角、Blender 数据流用的四元数 + 航位推算位置。
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
