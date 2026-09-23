#pragma once

#include <Arduino.h>

// Single place for every pin assignment, timing and tuning constant.
// Panel geometry lives in DisplayDriver, audio effect constants in
// SaberController.
namespace HardwareConfig {

// -------------------------------------------------------------------- I2C
constexpr uint8_t I2cSda = 1;
constexpr uint8_t I2cScl = 2;

// ---------------------------------------------------------------- SD card
constexpr int8_t SdClk = 47;
constexpr int8_t SdCmd = 48;
constexpr int8_t SdD0 = 21;

// -------------------------------------------------------------- LED strip
constexpr uint8_t LedPin = 5;
constexpr uint16_t LedCount = 56;
constexpr uint8_t DefaultBrightness = 100;
// A 56 pixel WS2812 strip pulls well over 1 A at full white, which browns out
// the rail and resets the MCU.  Clamp what the panel can ask for.
constexpr uint8_t MaxLedBrightness = 150;

// ------------------------------------------------------------------ audio
constexpr uint8_t I2sMck = 38;
constexpr uint8_t I2sBck = 14;
constexpr uint8_t I2sWs = 13;
constexpr uint8_t I2sDo = 45;
constexpr uint8_t PaEnable = 9;
constexpr uint8_t AudioVolume = 8;

// ---------------------------------------------------------------- display
constexpr int8_t DisplayBacklight = 8;

// ----------------------------------------------------------------- button
constexpr int8_t BootButton = 0;
constexpr uint32_t ButtonDebounce = 40;

// ------------------------------------------------------------- task budget
// Applied through the Arduino core's weak getArduinoLoopTaskStackSize() hook
// in main.cpp.
constexpr uint32_t LoopStackSize = 16384;

// ----------------------------------------------------------------- motion
constexpr uint16_t MotionInterval = 10;
constexpr uint16_t TelemetryInterval = 100;
constexpr uint16_t TelemetryPort = 5005;
constexpr float AccelLsbPerGravity = 2048.0f;   // MPU6050_ACCEL_FS_16
constexpr float GyroLsbPerDegree = 131.0f;      // MPU6050_GYRO_FS_250
constexpr float PositionDeadZone = 0.15f;       // m/s^2 below which drift is zeroed
constexpr float PositionLimit = 2.0f;           // m, keeps the Blender feed bounded

// --------------------------------------------------------- saber gameplay
constexpr uint32_t SwingTimeout = 500;
constexpr uint16_t SwingLowThreshold = 80;
constexpr uint16_t SwingThreshold = 180;
constexpr uint16_t StrikeThreshold = 40;
constexpr uint16_t HardStrikeThreshold = 160;
constexpr uint16_t OpenThreshold = 60;
constexpr uint8_t GestureToggleCount = 20;
constexpr uint32_t GestureInterval = 50;
constexpr uint32_t HumTimeout = 30228;
constexpr uint32_t HumActivationDelay = 2000;
// Restarting the hum loop this early keeps it audible under a swing or hit.
constexpr uint32_t HumSoundDelay = 1000;
constexpr uint32_t SwingCooldown = 100;
constexpr uint8_t PulseAmplitude = 10;
constexpr uint16_t PulseDelay = 30;
constexpr uint16_t FlashDelay = 15;
constexpr uint16_t HitDuration = 300;
constexpr uint8_t StrikeFlashRed = 255;
constexpr uint8_t StrikeFlashGreen = 201;
constexpr uint8_t StrikeFlashBlue = 0;
constexpr uint8_t ScannerTrailLength = 8;
constexpr uint16_t RainbowHueStep = 512;

// ----------------------------------------------------------- eye tracking
// Roll / pitch (radians) that push the gaze to the edge of its travel.
constexpr float EyeGazeRadiansHorizontal = 1.0f;
constexpr float EyeGazeRadiansVertical = 1.4f;

// ------------------------------------------------------------- networking
constexpr const char* DefaultApSsid = "Esaber-Setup";
constexpr const char* DefaultApPassword = "esaber123";
constexpr const char* PreferencesNamespace = "esaber";
constexpr uint32_t WifiConnectTimeout = 15000;

}  // namespace HardwareConfig
