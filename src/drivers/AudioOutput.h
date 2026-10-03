#pragma once

#include <Arduino.h>

#include "../../include/HardwareConfig.h"

class Audio;

namespace audio_driver {
class AudioBoard;
class DriverPins;
}  // namespace audio_driver

// ============================================================================
// AudioOutput —— ES8311 codec + ESP32-audioI2S 解码器
// ============================================================================
// 三条刻意为之、极易被"顺手优化"掉的设计：
//   * 增益结构。软件音量恒为单位增益，电平设在 codec 上——
//     反过来（软件衰减 + codec 增益）会把衰减的截断噪声放大得和信号一样多。
//   * 切流静音在 codec 端而非软件端。I2S DMA 缓着约 190ms 音频，
//     软件改变五分之一秒后才被听见——那时流早换了、DMA 在波形中途被冲掉，
//     那就是那声"咔"。
//   * 功放只在刀亮时上电。PA_EN 挂高会让它放大 codec 自己的本底噪声，
//     安静房间里就是嘶嘶声。
// ============================================================================
class AudioOutput {
 public:
  bool begin(bool sdReady);     // sdReady=挂卡结果：没卡不扫音源
  void loop();                  // 每圈主循环：喂解码器 + 静音/功放状态机
  void play(const char* file);     // 播放会打断底噪的 SD 音效
  void playHum(const char* file);  // 播放/续播底噪；效果音插播后从断点恢复

  // 任何流（音效或 hum）在播时为 true。光剑用它 falling edge 重新武装
  // hum 循环——这正是不依赖固定时长、任何文件都能当 hum 的原因。
  bool isRunning() const;

  // 音量百分比 0..100，100 = 固件允许的最响档。作用于 codec，立即生效
  void setVolume(uint8_t percent);

  // 功放跟随刀：亮刀期间开，收刃音播完后关
  void setAmplifierEnabled(bool enabled);
  ~AudioOutput();

 private:
  void enableAmplifierNow();
  void unmuteIfDue();
  void updateAmplifier();
  bool playPath(const String& path, uint32_t resumePosition = 0);

  Audio* audio_ = nullptr;
  audio_driver::DriverPins* pins_ = nullptr;
  audio_driver::AudioBoard* board_ = nullptr;
  String humPath_;
  uint32_t humResumePosition_ = 0;
  bool ready_ = false;
  bool started_ = false;
  bool currentIsHum_ = false;
  bool humPaused_ = false;

  // codec 的 DAC 音量，用 codec 自己的单位，不是控制台的百分比
  uint8_t codecVolume_ = HardwareConfig::CodecVolume;

  bool amplifierEnabled_ = false;    // 功放实际状态
  bool amplifierWanted_ = false;     // 业务层想要的功放状态
  // 挂起的解除静音时刻（millis()），0 = 无挂起
  unsigned long unmuteDue_ = 0;
  // codec 静音后关功放的到期时刻，0 = 无挂起
  unsigned long amplifierOffDue_ = 0;
};
