#pragma once

#include <Arduino.h>
#include <WiFi.h>

// Brings up the access point the console is reached through.
//
// There is no station mode on purpose.  Joining a home network used to be the
// first thing the console asked for, which meant a mistyped password could lock
// the device away until it was reflashed, and it left the console at an address
// that changed with the DHCP lease.  On its own access point the address is
// always the same, so the QR code on the screen always works.
class WifiService {
 public:
  void begin();

  String localUrl() const;
  String ssid() const;

 private:
  void startAccessPoint();
};
