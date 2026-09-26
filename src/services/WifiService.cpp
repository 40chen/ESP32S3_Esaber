#include "WifiService.h"

#include "../../include/HardwareConfig.h"

void WifiService::begin() {
  WiFi.mode(WIFI_AP);
  startAccessPoint();
}

String WifiService::localUrl() const {
  return "http://" + WiFi.softAPIP().toString();
}

String WifiService::activeSsid() const {
  return HardwareConfig::DefaultApSsid;
}

void WifiService::startAccessPoint() {
  // Open hotspot (no passphrase) per user request: guests join without typing.
  WiFi.softAP(HardwareConfig::DefaultApSsid);
  Serial.print("[WiFi] AP: ");
  Serial.print(HardwareConfig::DefaultApSsid);
  Serial.print(" @ ");
  Serial.println(WiFi.softAPIP());
}
