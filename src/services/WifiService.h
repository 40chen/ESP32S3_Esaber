#pragma once

#include <Arduino.h>
#include <WiFi.h>

// Serves the web console from the device's own access point.
//
// The saber is AP-only by design: joining a home router created a lockout
// risk (wrong password = no way back in without serial) for little gain, and
// the fixed 192.168.4.1 address needs no discovery.  The hotspot itself is
// open (no password) by user request; OTA keeps its own flash password.
class WifiService {
 public:
  void begin();

  // A phone or laptop is currently joined to the device hotspot.
  bool stationJoined() const { return WiFi.softAPgetStationNum() > 0; }

  String localUrl() const;
  String activeSsid() const;

 private:
  void startAccessPoint();
};
