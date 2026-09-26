#pragma once

#include <Preferences.h>

#include "../../include/AppTypes.h"
#include "../../include/HardwareConfig.h"

class SettingsStore {
 public:
  void begin();
  // Flushes deferred NVS writes; call once per main-loop iteration.
  void tick();
  const SaberSettings& saber() const { return saber_; }
  String blenderIp() const;
  uint8_t volume() const { return volume_; }

  void saveSaber(const SaberSettings& settings);
  void saveBlenderIp(const String& ip);
  void saveVolume(uint8_t volume);

 private:
  String readString(const char* key, const char* fallback);
  void flushIfDue();
  void writeSaberKeys();

  SaberSettings saber_;
  SaberSettings persisted_;  // snapshot of what is actually stored in NVS
  bool saberDirty_ = false;
  String blenderIp_;
  uint8_t volume_ = HardwareConfig::AudioVolume;
  uint8_t volumePersisted_ = HardwareConfig::AudioVolume;
  unsigned long lastNvsWriteMs_ = 0;
  Preferences preferences_;
};
