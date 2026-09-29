// ============================================================================
// SettingsStore —— 用户设置的持久化（NVS：flash 里的键值库，断电不丢）
// 读取全程防御性钳制：陈旧值/手改值不可能变成越界枚举或超量程数值。
// 写入走节流：控制台拖滑条是"每次 POST 都调 saveSaber"的高频流，
// 直接落盘既磨损 flash 又拖累主循环——这里先记脏，每 500ms 至多批量写
// 一次，且只写真正变化过的键。RAM 值始终即时生效（灯/音量立刻响应）。
// ============================================================================
#include "SettingsStore.h"

#include <string.h>

#include "../../include/HardwareConfig.h"

namespace {

// 两次 NVS 批量写入的最小间隔。断电会丢窗口内最后一次修改——
// 只丢设置项，不丢硬件状态，可接受。
constexpr unsigned long kNvsMinWriteMs = 500;

// 陈旧或手改的 NVS 值绝不能变成越界枚举
uint8_t clampIndex(uint8_t value, uint8_t count) {
  return value < count ? value : 0;
}

// 亮度/音量都是用户视角的百分比；旧固件的亮度曾是 0-255 原始刻度，
// 陈旧值直接钳到 100，避免把灯条推过上限
uint8_t clampPercent(uint8_t value) {
  return min(value, static_cast<uint8_t>(100));
}

// 落盘关注点的逐一比对。power 是运行态不入 NVS，不参与脏判定
bool saberKeysDiffer(const SaberSettings& a, const SaberSettings& b) {
  return a.red != b.red || a.green != b.green || a.blue != b.blue ||
         a.brightness != b.brightness || a.volume != b.volume ||
         a.effect != b.effect || a.eyePattern != b.eyePattern ||
         strcmp(a.bootSound, b.bootSound) != 0 ||
         strcmp(a.shutdownSound, b.shutdownSound) != 0 ||
         strcmp(a.humSound, b.humSound) != 0;
}

}  // namespace

void SettingsStore::begin() {
  preferences_.begin(HardwareConfig::PreferencesNamespace, false);   // false=读写模式
  // 默认值与 SaberSettings 的出厂值保持一致（#FF33CC）
  saber_.red = preferences_.getUChar("red", 255);
  saber_.green = preferences_.getUChar("green", 51);
  saber_.blue = preferences_.getUChar("blue", 204);
  saber_.brightness = clampPercent(preferences_.getUChar("brightness", HardwareConfig::DefaultBrightness));
  saber_.volume = clampPercent(preferences_.getUChar("volume", HardwareConfig::DefaultVolume));
  saber_.effect = static_cast<SaberEffect>(
      clampIndex(preferences_.getUChar("effect", static_cast<uint8_t>(SaberEffect::Pulse)),
                 kSaberEffectCount));
  saber_.eyePattern = static_cast<EyePattern>(
      clampIndex(preferences_.getUChar("eye", static_cast<uint8_t>(EyePattern::Normal)),
                 kEyePatternCount));
  // AP-only 模式下电脑连设备热点、拿首个 DHCP 位（192.168.4.2），动捕默认发往那里。
  // 旧默认 192.168.10.5 是 STA 时代遗留——AP 模式下那个地址不存在，包必丢。
  blenderIp_ = readString("blender_ip", "192.168.4.2");        // 动捕目标默认值
  readSound("boot_snd", saber_.bootSound, "endlock1.wav");     // 开机音效
  readSound("off_snd", saber_.shutdownSound, "endlock2.wav");  // 关机音效
  readSound("hum_snd", saber_.humSound, "111.wav");            // 底噪循环
  persisted_ = saber_;   // 脏判定基准 = 刚从 NVS 读回的内容
}

// Preferences 对每次未命中的键都按 error 级别打日志，第一次开机看起来像坏了。
// 先问键存不存在，而不是依赖带默认值的读取——日志就干净了。
String SettingsStore::readString(const char* key, const char* fallback) {
  if (!preferences_.isKey(key)) return String(fallback);
  return preferences_.getString(key, fallback);
}

void SettingsStore::readSound(const char* key, char* out, const char* fallback) {
  const String value = readString(key, fallback);
  strlcpy(out, value.c_str(), kSoundNameLength);   // 定长拷贝，防溢出
}

String SettingsStore::blenderIp() const {
  return blenderIp_;
}

void SettingsStore::saveSaber(const SaberSettings& settings) {
  saber_ = settings;
  saber_.brightness = clampPercent(saber_.brightness);   // 落盘前再钳一次（web 端可能漏检）
  saber_.volume = clampPercent(saber_.volume);
  if (saberKeysDiffer(saber_, persisted_)) saberDirty_ = true;
  flushIfDue();
}

void SettingsStore::tick() { flushIfDue(); }   // 主循环每圈调用，冲刷到期窗口

// 延迟落盘：有脏键且距上次写入超过最小窗口，才真正碰 flash
void SettingsStore::flushIfDue() {
  if (!saberDirty_) return;
  const unsigned long now = millis();
  if (now - lastNvsWriteMs_ < kNvsMinWriteMs) return;
  writeSaberKeys();
  lastNvsWriteMs_ = now;
}

// 逐键比对快照，只写变化过的键——滑条连拖时绝大多数键纹丝不动，
// 一次批量写从"10 键全写"缩到常见的 1-2 键
void SettingsStore::writeSaberKeys() {
  if (saber_.red != persisted_.red) preferences_.putUChar("red", saber_.red);
  if (saber_.green != persisted_.green) preferences_.putUChar("green", saber_.green);
  if (saber_.blue != persisted_.blue) preferences_.putUChar("blue", saber_.blue);
  if (saber_.brightness != persisted_.brightness) {
    preferences_.putUChar("brightness", saber_.brightness);
  }
  if (saber_.volume != persisted_.volume) preferences_.putUChar("volume", saber_.volume);
  if (saber_.effect != persisted_.effect) {
    preferences_.putUChar("effect", static_cast<uint8_t>(saber_.effect));
  }
  if (saber_.eyePattern != persisted_.eyePattern) {
    preferences_.putUChar("eye", static_cast<uint8_t>(saber_.eyePattern));
  }
  // 音效名只在用户选文件时变化；逐键比对让滑条连拖绝不重写这三个字符串键
  if (strcmp(saber_.bootSound, persisted_.bootSound) != 0) {
    preferences_.putString("boot_snd", saber_.bootSound);
  }
  if (strcmp(saber_.shutdownSound, persisted_.shutdownSound) != 0) {
    preferences_.putString("off_snd", saber_.shutdownSound);
  }
  if (strcmp(saber_.humSound, persisted_.humSound) != 0) {
    preferences_.putString("hum_snd", saber_.humSound);
  }
  persisted_ = saber_;
  saberDirty_ = false;
}

void SettingsStore::saveBlenderIp(const String& ip) {
  if (ip == blenderIp_) return;   // 同值跳过：控制台重复提交不碰 flash
  blenderIp_ = ip;
  // IP 改动是用户手动低频操作，立即落盘（不经节流窗口）
  preferences_.putString("blender_ip", ip);
}
