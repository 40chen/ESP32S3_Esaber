#pragma once

#include <Preferences.h>

#include "../../include/AppTypes.h"

// ============================================================================
// SettingsStore —— 用户设置的持久化（NVS / flash 里的键值库）
// ============================================================================
// 断电不丢。读取时做防御性钳制：NVS 里的陈旧值或手改值
// 永远不可能变成越界枚举或超量程数值。
// 写入走节流：控制台拖滑条是"每次 POST 都调 saveSaber"的高频流，
// 直接落盘既磨损 flash 又拖累主循环——这里先记脏，每 500ms 至多批量写
// 一次，且只写真正变化过的键。RAM 值始终即时生效（灯/音量立刻响应）。
// ============================================================================
class SettingsStore {
 public:
  void begin();                                       // 打开 NVS + 读回全部键
  void tick();                                        // 冲刷到期的延迟写入；主循环每圈调一次
  const SaberSettings& saber() const { return saber_; }
  String blenderIp() const;

  void saveSaber(const SaberSettings& settings);      // web 保存时整体写回（落盘走节流）
  void saveBlenderIp(const String& ip);

 private:
  String readString(const char* key, const char* fallback);
  // 音效文件名从 Preferences 以 String 读出，落进定长缓冲
  void readSound(const char* key, char* out, const char* fallback);
  void flushIfDue();          // 有脏键且写窗口已到才真正碰 flash
  void writeSaberKeys();      // 逐键比对快照，只写变化过的键

  SaberSettings saber_;
  SaberSettings persisted_;   // NVS 里真实内容的快照（脏判定的对照基准）
  bool saberDirty_ = false;   // saber 与快照存在未落盘差异
  String blenderIp_;
  unsigned long lastNvsWriteMs_ = 0;
  Preferences preferences_;
};
