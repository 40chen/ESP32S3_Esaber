#pragma once

#include <Preferences.h>

#include "../../include/AppTypes.h"

class SettingsStore {
 public:
  void begin();
  const SaberSettings& saber() const { return saber_; }
  String wifiSsid() const;
  String wifiPassword() const;
  String blenderIp() const;

  void saveWifi(const String& ssid, const String& password);
  void saveSaber(const SaberSettings& settings);
  void saveBlenderIp(const String& ip);

 private:
  String readString(const char* key, const char* fallback);

  SaberSettings saber_;
  String wifiSsid_;
  String wifiPassword_;
  String blenderIp_;
  Preferences preferences_;
};
