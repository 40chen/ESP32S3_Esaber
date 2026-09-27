#include "WifiService.h"

#include "../../include/HardwareConfig.h"

void WifiService::begin() {
  WiFi.mode(WIFI_AP);
  // Transmit power is peak current, and this supply browns out when the
  // radio's peaks land on top of the display backlight.  8.5 dBm still covers
  // a room comfortably; the default 19.5 dBm is what trips the detector.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  // The audio decoder and the web server share the loop task; radio power save
  // adds latency the console notices and saves nothing worth having here.
  // (Sleep would also make the transmit bursts peakier, not gentler.)
  WiFi.setSleep(false);
  startAccessPoint();
}

String WifiService::localUrl() const {
  return "http://" + WiFi.softAPIP().toString();
}

String WifiService::ssid() const {
  return HardwareConfig::DefaultApSsid;
}

void WifiService::startAccessPoint() {
  WiFi.softAP(HardwareConfig::DefaultApSsid, HardwareConfig::DefaultApPassword);
  Serial.printf("[WiFi] AP: %s (open, no password), http://%s\n", HardwareConfig::DefaultApSsid,
                WiFi.softAPIP().toString().c_str());
}
