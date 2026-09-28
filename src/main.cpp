#include <Arduino.h>

#include "../include/HardwareConfig.h"
#include "core/SystemController.h"

// ============================================================================
// main —— Arduino 入口（极薄，一切逻辑在 SystemController）
// ============================================================================

// 强覆写 Arduino 框架的弱符号钩子：循环任务栈大小。
// 音频解码器、web 服务器、JSON 响应都跑在 loop 任务上，
// 框架默认 8KB 余量不够（曾导致栈溢出重启），按配置表放大。
size_t getArduinoLoopTaskStackSize(void) {
  return HardwareConfig::LoopStackSize;
}

SystemController saberSystem;   // 全局唯一总装配体

void setup() {
  saberSystem.begin();          // 上电初始化序列（顺序敏感）
}

void loop() {
  saberSystem.update();         // 主循环：全部业务由它驱动
}
