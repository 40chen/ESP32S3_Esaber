#include <Arduino.h>

#include "../include/HardwareConfig.h"
#include "core/SystemController.h"

// Strong override of the Arduino core's weak hook.  The audio decoder, the web
// server and the JSON responses all run on the loop task, and the 8 KB default
// left too little headroom.
size_t getArduinoLoopTaskStackSize(void) {
  return HardwareConfig::LoopStackSize;
}

SystemController saberSystem;

void setup() {
  saberSystem.begin();
}

void loop() {
  saberSystem.update();
}
