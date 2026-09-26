#include "SettingsStore.h"

#include "../../include/HardwareConfig.h"

namespace {

// Slider drags stream one POST per input event; persisting at most once per
// window keeps flash wear and loop stalls bounded.  Values still apply to RAM
// (and therefore to the LED) immediately.
constexpr unsigned long kNvsMinWriteMs = 500;

// A stale or hand-edited NVS value must never become an out of range enum.
uint8_t clampIndex(uint8_t value, uint8_t count) {
  return value < count ? value : 0;
}

uint8_t clampBrightness(uint8_t value) {
  return min(value, static_cast<uint8_t>(100));
}

}  // namespace

void SettingsStore::begin() {
  preferences_.begin(HardwareConfig::PreferencesNamespace, false);
  saber_.red = preferences_.getUChar("red", 255);
  saber_.green = preferences_.getUChar("green", 0);
  saber_.blue = preferences_.getUChar("blue", 0);
  // Brightness is a percentage now.  Legacy builds stored the raw LED scale
  // (0-255 clamped to 150), so anything above 100 is migrated: 150 -> 100%,
  // 100 -> 67%.
  const uint8_t storedBrightness = preferences_.getUChar("brightness", HardwareConfig::DefaultBrightness);
  saber_.brightness =
      storedBrightness <= 100
          ? clampBrightness(storedBrightness)
          : static_cast<uint8_t>((storedBrightness * 100 + 50) / HardwareConfig::MaxLedBrightness);
  saber_.effect = static_cast<SaberEffect>(
      clampIndex(preferences_.getUChar("effect", static_cast<uint8_t>(SaberEffect::Pulse)),
                 kSaberEffectCount));
  saber_.eyePattern = static_cast<EyePattern>(
      clampIndex(preferences_.getUChar("eye", static_cast<uint8_t>(EyePattern::Normal)),
                 kEyePatternCount));
  volume_ =
      min(preferences_.getUChar("volume", HardwareConfig::AudioVolume), HardwareConfig::MaxAudioVolume);
  // AP-only mode: the PC joins the Esaber-Setup hotspot and gets the first
  // DHCP lease, so that is where the Blender feed goes by default.  The
  // pre-AP-only default (192.168.10.5) pointed at a home LAN that no longer
  // exists in this mode.
  blenderIp_ = readString("blender_ip", "192.168.4.2");
  persisted_ = saber_;
  volumePersisted_ = volume_;
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
  saber_.brightness = clampBrightness(saber_.brightness);
  if (saber_.red != persisted_.red || saber_.green != persisted_.green ||
      saber_.blue != persisted_.blue || saber_.brightness != persisted_.brightness ||
      saber_.effect != persisted_.effect || saber_.eyePattern != persisted_.eyePattern) {
    saberDirty_ = true;
  }
  flushIfDue();
}

void SettingsStore::tick() { flushIfDue(); }

// Deferred persistence: mark dirty on change, then write at most once per
// window and only the keys whose value really moved.
void SettingsStore::flushIfDue() {
  if (!saberDirty_ && volume_ == volumePersisted_) return;
  const unsigned long now = millis();
  if (now - lastNvsWriteMs_ < kNvsMinWriteMs) return;
  if (saberDirty_) writeSaberKeys();
  if (volume_ != volumePersisted_) {
    preferences_.putUChar("volume", volume_);
    volumePersisted_ = volume_;
  }
  lastNvsWriteMs_ = now;
}

void SettingsStore::writeSaberKeys() {
  if (saber_.red != persisted_.red) preferences_.putUChar("red", saber_.red);
  if (saber_.green != persisted_.green) preferences_.putUChar("green", saber_.green);
  if (saber_.blue != persisted_.blue) preferences_.putUChar("blue", saber_.blue);
  if (saber_.brightness != persisted_.brightness) {
    preferences_.putUChar("brightness", saber_.brightness);
  }
  if (saber_.effect != persisted_.effect) {
    preferences_.putUChar("effect", static_cast<uint8_t>(saber_.effect));
  }
  if (saber_.eyePattern != persisted_.eyePattern) {
    preferences_.putUChar("eye", static_cast<uint8_t>(saber_.eyePattern));
  }
  persisted_ = saber_;
  saberDirty_ = false;
}

void SettingsStore::saveBlenderIp(const String& ip) {
  if (ip == blenderIp_) return;
  blenderIp_ = ip;
  preferences_.putString("blender_ip", ip);
}

void SettingsStore::saveVolume(uint8_t volume) {
  volume_ = min(volume, HardwareConfig::MaxAudioVolume);
  flushIfDue();
}
