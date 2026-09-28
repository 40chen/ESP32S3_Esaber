#pragma once

#include <Preferences.h>

#include "../../include/AppTypes.h"

// ============================================================================
// SettingsStore —— 用户设置的持久化（NVS / flash 里的键值库）
// ============================================================================
// 断电不丢。读取时做防御性钳制：NVS 里的陈旧值或手改值
// 永远不可能变成越界枚举或超量程数值。
// ============================================================================
class SettingsStore {
 public:
  void begin();                                       // 打开 NVS + 读回全部键
  const SaberSettings& saber() const { return saber_; }
  String blenderIp() const;

  void saveSaber(const SaberSettings& settings);      // web 保存时整体写回
  void saveBlenderIp(const String& ip);

 private:
  String readString(const char* key, const char* fallback);
  // 音效文件名从 Preferences 以 String 读出，落进定长缓冲
  void readSound(const char* key, char* out, const char* fallback);

  SaberSettings saber_;
  String blenderIp_;
  Preferences preferences_;
};
