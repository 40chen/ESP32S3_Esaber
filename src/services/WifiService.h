#pragma once

#include <Arduino.h>
#include <WiFi.h>

// Brings up a configuration access point and optionally joins a home network.
//
// The access point stays up alongside the station connection.  Tearing it down
// with softAPdisconnect() + WiFi.mode(WIFI_STA) while the HTTP server had a
// client attached was both a crash risk and a lockout risk: a wrong password
// left no way back in.
class WifiService {
 public:
  void begin(const String& ssid, const String& password);
  void connect(const String& ssid, const String& password);
  void update();

  bool connected() const { return WiFi.status() == WL_CONNECTED; }
  bool connecting() const { return connecting_; }

  String localUrl() const;
  String activeSsid() const;

 private:
  void startAccessPoint();

  unsigned long connectStart_ = 0;
  bool connecting_ = false;
};
