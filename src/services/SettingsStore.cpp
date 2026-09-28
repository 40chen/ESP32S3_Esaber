// ============================================================================
// SettingsStore —— 用户设置的持久化（NVS：flash 里的键值库，断电不丢）
// 读取全程防御性钳制：陈旧值/手改值不可能变成越界枚举或超量程数值。
// ============================================================================
#include "SettingsStore.h"

#include "../../include/HardwareConfig.h"

namespace {

// 陈旧或手改的 NVS 值绝不能变成越界枚举
uint8_t clampIndex(uint8_t value, uint8_t count) {
  return value < count ? value : 0;
}

// 亮度/音量都是用户视角的百分比；旧固件的亮度曾是 0-255 原始刻度，
// 陈旧值直接钳到 100，避免把灯条推过上限
uint8_t clampPercent(uint8_t value) {
  return min(value, static_cast<uint8_t>(100));
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
  blenderIp_ = readString("blender_ip", "192.168.10.5");       // 动捕目标默认值
  readSound("boot_snd", saber_.bootSound, "endlock1.wav");     // 开机音效
  readSound("off_snd", saber_.shutdownSound, "endlock2.wav");  // 关机音效
  readSound("hum_snd", saber_.humSound, "111.wav");            // 底噪循环
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
  preferences_.putUChar("red", saber_.red);
  preferences_.putUChar("green", saber_.green);
  preferences_.putUChar("blue", saber_.blue);
  preferences_.putUChar("brightness", saber_.brightness);
  preferences_.putUChar("volume", saber_.volume);
  preferences_.putUChar("effect", static_cast<uint8_t>(saber_.effect));
  preferences_.putUChar("eye", static_cast<uint8_t>(saber_.eyePattern));
  // 音效名只在用户选文件时变化：写入频率是"web 保存"级，远低于 NVS 磨损担忧
  preferences_.putString("boot_snd", saber_.bootSound);
  preferences_.putString("off_snd", saber_.shutdownSound);
  preferences_.putString("hum_snd", saber_.humSound);
}

void SettingsStore::saveBlenderIp(const String& ip) {
  blenderIp_ = ip;
  preferences_.putString("blender_ip", ip);
}
