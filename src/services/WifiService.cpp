#include "WifiService.h"

#include <esp_wifi.h>

#include "../../include/HardwareConfig.h"

void WifiService::begin() {
  WiFi.mode(WIFI_AP);
  // 发射功率即峰值电流，而本供电在 radio 峰值叠上背光时会欠压。
  // 8.5dBm 覆盖一个房间绰绰有余；默认 19.5dBm 就是触发欠压检测的元凶。
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  // 音频解码器和 web 服务器共享 loop 任务：radio 省电模式只会加延迟，
  // 省不出值得省的电（睡着时的发射突发反而更尖，不是更平缓）。
  WiFi.setSleep(false);
  startAccessPoint();
}

String WifiService::localUrl() const {
  return "http://" + WiFi.softAPIP().toString();   // → http://192.168.4.1
}

String WifiService::ssid() const {
  return HardwareConfig::DefaultApSsid;
}

void WifiService::startAccessPoint() {
  WiFi.softAP(HardwareConfig::DefaultApSsid, HardwareConfig::DefaultApPassword);   // NULL 密码=开放热点
  // 【改动③ · beacon 间隔 100→300ms】（3.3V 供电优化）
  // beacon 默认每 100ms 发一次；300ms 把 radio 的周期性发射降到 1/3，
  // 对停在控制台页面上的手机无任何感知差异。
  // 必须在 softAP 之后调用才生效。IDF 接受 100-60000ms。
  esp_wifi_config_beacon_interval(WIFI_IF_AP, 300);
  Serial.printf("[WiFi] AP: %s (open, no password), http://%s\n", HardwareConfig::DefaultApSsid,
                WiFi.softAPIP().toString().c_str());
}
