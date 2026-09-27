#include "SettingsStore.h"

#include "../../include/HardwareConfig.h"

namespace {

// A stale or hand-edited NVS value must never become an out of range enum.
uint8_t clampIndex(uint8_t value, uint8_t count) {
  return value < count ? value : 0;
}

// Brightness and volume are both user-facing percentages, so a stale value
// from an older firmware (where brightness was a raw 0-255 scale) lands on the
// maximum rather than pushing the strip past its ceiling.
uint8_t clampPercent(uint8_t value) {
  return min(value, static_cast<uint8_t>(100));
}

}  // namespace

void SettingsStore::begin() {
  preferences_.begin(HardwareConfig::PreferencesNamespace, false);
  // Defaults match SaberSettings (#FF33CC).
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
  blenderIp_ = readString("blender_ip", "192.168.10.5");
}

// Preferences logs every miss at error level, which made a first boot look
// broken.  Ask whether the key exists instead of relying on the default.
String SettingsStore::readString(const char* key, const char* fallback) {
  if (!preferences_.isKey(key)) return String(fallback);
  return preferences_.getString(key, fallback);
}

String SettingsStore::blenderIp() const {
  return blenderIp_;
}

void SettingsStore::saveSaber(const SaberSettings& settings) {
  saber_ = settings;
  saber_.brightness = clampPercent(saber_.brightness);
  saber_.volume = clampPercent(saber_.volume);
  preferences_.putUChar("red", saber_.red);
  preferences_.putUChar("green", saber_.green);
  preferences_.putUChar("blue", saber_.blue);
  preferences_.putUChar("brightness", saber_.brightness);
  preferences_.putUChar("volume", saber_.volume);
  preferences_.putUChar("effect", static_cast<uint8_t>(saber_.effect));
  preferences_.putUChar("eye", static_cast<uint8_t>(saber_.eyePattern));
}

void SettingsStore::saveBlenderIp(const String& ip) {
  blenderIp_ = ip;
  preferences_.putString("blender_ip", ip);
}
