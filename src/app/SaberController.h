#pragma once

#include <Arduino.h>

#include "../../include/AppTypes.h"
#include "../drivers/AudioOutput.h"
#include "../drivers/MotionSensor.h"
#include "../drivers/PixelStrip.h"

// ============================================================================
// SaberController —— 光剑业务核心：把 IMU 动作变成光与声
// ============================================================================
// 职责：挥动呼啸（swing）、碰撞闪光（clash）、待机嗡鸣循环（hum）、
//       开刃/收刃动画，以及七种灯条特效的状态机。
// 主循环每圈被 SystemController::update() 调一次，全部逻辑单线程顺序执行。
// ============================================================================
class SaberController {
 public:
  void begin(AudioOutput* audio, PixelStrip* strip, MotionSensor* motion,
             const SaberSettings& initialSettings);   // 注入依赖 + 应用初始设置
  void update();                                      // 主循环节拍：音频/姿态/手势/灯光

  const SaberSettings& settings() const { return settings_; }
  void setSettings(const SaberSettings& settings);    // web 改设置后的统一入口
  void setPower(bool enabled);                        // 开刃 / 收刃

 private:
  static constexpr uint8_t kImuSampleCount = 8;       // 手势判定窗口 = 8 个 pitch 采样
  static constexpr uint8_t kEffectIntervalMs = 20;    // 彩虹等连续特效的帧周期 20ms ≈ 50fps
  static constexpr uint8_t kRetractFadeSteps = 3;     // 收刃前沿的渐隐级数

  // ---- 内部子状态机 ----
  void readGesture();                                 // 快速甩腕 180° 开关刃手势
  void handleStrike(unsigned long now);               // 碰撞检测 + 音效 + 闪光
  void handleSwing(unsigned long now);                // 挥动检测 + 分档音效
  void updateHum(unsigned long now);                  // 嗡鸣循环（空闲即续播）
  void updateLighting(unsigned long now);             // 灯条总调度：开刃/收刃/闪光/特效
  void applyPulse(unsigned long now);
  void applyUnstable(unsigned long now);
  void updateRainbow(unsigned long now);
  void updateScanner(unsigned long now);
  void updateFire(unsigned long now);
  void updateSparkle(unsigned long now);
  void updateRetract(unsigned long now);
  void startStrike(uint8_t intensity);                // 按撞击强度定闪光时长
  void playRandomSound(const char* const sounds[], uint8_t count);
  uint8_t ledBrightness() const;                      // 百分比 → 硬件刻度（封顶防爆）
  uint8_t audioVolume() const;

  // ---- 依赖（SystemController 注入，生命周期归它管）----
  AudioOutput* audio_ = nullptr;
  PixelStrip* strip_ = nullptr;
  MotionSensor* motion_ = nullptr;
  SaberSettings settings_;                            // 当前生效的用户设置快照

  // ---- 状态标志 ----
  bool gestureRequested_ = false;   // 手势请求开关刃（下帧 update 消费，避免在采样回调里切状态）
  bool humPlaying_ = false;         // 嗡鸣是否该在循环（亮刀期间为 true）
  bool swingReady_ = false;         // 嗡鸣续播后武装挥动检测，防一次动作双触发
  bool strikePlaying_ = false;      // 碰撞音效播放中（期间抑制挥动音）
  bool strikeEffect_ = false;       // 碰撞白闪进行中
  bool turnOnAnimation_ = false;    // 开刃展开动画进行中
  bool retracting_ = false;         // 收刃塌缩动画进行中

  // ---- 毫秒计时器（millis() 基准）----
  unsigned long gestureTimer_ = 0;    // 手势采样节流
  unsigned long pulseTimer_ = 0;      // 呼吸特效节流
  unsigned long swingTimer_ = 0;      // 挥动音效的最小间隔
  unsigned long swingTimeout_ = 0;    // 挥动冷却
  unsigned long strikeTimeout_ = 0;   // 碰撞冷却
  unsigned long effectTimer_ = 0;     // 特效/动画通用帧节拍
  unsigned long hitTimer_ = 0;        // 碰撞闪光起始时刻
  // 【改动④ · 开刃串行化】开刃灯效的最早启动时刻：先让功放稳定窗走完，
  // 第一颗像素才点亮——PA 浪涌电流与 LED 电流永不同帧，消除 3.3V 轨峰值叠加。
  // 声明为绝对时刻（millis()+PaSettleMs），updateLighting 里 now < 它则持续等待。
  unsigned long ignitionLightDue_ = 0;
  unsigned long gestureCounter_ = 0;  // 采样环形下标（& (N-1) 取模，N 为 2 的幂）
  uint8_t gestureCooldown_ = 0;       // 开关刃手势的冷却帧数
  // 预热计数：窗口 8 个槽位全部装进真实采样前，不信任极差判定——
  // 否则开机时若恰好握持在阈值外的姿态，全零窗口会被误判成"快速翻转"自动开刃。
  uint8_t gestureWarmup_ = 0;
  uint16_t hitDuration_ = HardwareConfig::HitBaseMs;  // 本次碰撞闪光的时长 ms
  uint8_t animationPixel_ = 0;        // 开刃/收刃动画的推进下标
  int pulseOffset_ = 0;               // 呼吸特效的当前亮度偏移（低通滤波后）
  uint16_t rainbowHue_ = 0;           // 彩虹特效的全局色相相位（0-65535）
  int8_t scannerDirection_ = 1;       // 扫描特效的方向 +1/-1
  uint16_t scannerPixel_ = 0;         // 扫描特效的光头位置
  float pitchSamples_[kImuSampleCount] = {0.0f};  // 手势判定的 pitch 滑动窗口
  // 每像素 1 字节热度：火焰特效的全部状态（黑→红→橙→黄 渲染）
  uint8_t fireHeat_[HardwareConfig::LedCount] = {0};
};
