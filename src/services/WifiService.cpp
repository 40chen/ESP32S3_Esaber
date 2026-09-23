#include "WifiService.h"

#include "../../include/HardwareConfig.h"

void WifiService::begin(const String& ssid, const String& password) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  startAccessPoint();
  if (ssid.length() > 0) connect(ssid, password);
}

void WifiService::connect(const String& ssid, const String& password) {
  if (ssid.length() == 0) return;
  WiFi.begin(ssid.c_str(), password.c_str());
  connectStart_ = millis();
  connecting_ = true;
  Serial.print("[WiFi] connecting to ");
  Serial.println(ssid);
}

void WifiService::update() {
  if (!connecting_) return;

  if (connected()) {
    connecting_ = false;
    Serial.print("[WiFi] connected: ");
    Serial.println(WiFi.localIP());
    return;
  }

  // Give up and fall back to the access point rather than retrying forever.
  if (millis() - connectStart_ < HardwareConfig::WifiConnectTimeout) return;

  connecting_ = false;
  WiFi.disconnect(false, false);
  Serial.println("[WiFi] join timed out, staying on the setup access point");
}

String WifiService::localUrl() const {
  return "http://" + (connected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
}

String WifiService::activeSsid() const {
  if (connected()) return WiFi.SSID();
  return HardwareConfig::DefaultApSsid;
}

void WifiService::startAccessPoint() {
  WiFi.softAP(HardwareConfig::DefaultApSsid, HardwareConfig::DefaultApPassword);
  Serial.print("[WiFi] AP: ");
  Serial.println(WiFi.softAPIP());
}
