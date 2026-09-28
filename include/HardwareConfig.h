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
constexpr uint16_t LedCount = 32;
// Percent, like SaberSettings::brightness: it is scaled onto MaxLedBrightness
// by SaberController::ledBrightness().
constexpr uint8_t DefaultBrightness = 80;
// The hardware scale (of 255) that 100% maps onto.  Kept as a second line of
// defence behind the supply budget below, which is the one that actually
// bounds the current.
constexpr uint8_t MaxLedBrightness = 150;
// Power: an WS2812 channel at full scale draws roughly this much, so the frame
// current is proportional to the sum of every channel value.  A plain
// brightness ceiling cannot bound it -- full white draws three times what a
// single channel does at the same setting -- so PixelStrip measures the frame
// and scales it down to fit.
//
// The budget is the one configuration known to work on this hardware: pure red
// at brightness 100, which never browned out, while #FF33CC (red plus most of
// the blue) at the same setting did.  Raise it once the 5 V rail can take more
// -- a bulk capacitor across the strip is the usual missing piece.
constexpr uint16_t LedChannelMilliamps = 20;
constexpr uint16_t LedCurrentBudgetMa = 150;

// ------------------------------------------------------------------ audio
constexpr uint8_t I2sMck = 38;
constexpr uint8_t I2sBck = 14;
constexpr uint8_t I2sWs = 13;
constexpr uint8_t I2sDo = 45;
constexpr uint8_t PaEnable = 9;
// Software volume: ESP32-audioI2S steps 0..21, and 21 is unity (the table tops
// out at 64/64).  Shifting samples down here throws resolution away, so the
// listening level is set on the codec instead and this stays at unity.
constexpr uint8_t AudioVolume = 21;
// ES8311 DAC volume, percent.  The driver maps it as 255*log10(9v/100+1) onto
// 0.5 dB steps from -95.5 dB, so 50 is -1.5 dB, 52 is +0.5 dB and 56 is
// +4 dB.  The driver's own default of 70 is +14.5 dB, where the hiss came from.
//
// This is the level the console's volume slider reaches at DefaultVolume --
// 0 dB, which is as loud as the effect files go without clipping, since they
// are normalised to full scale.  The slider may be pushed to MaxCodecVolume
// for more, at the cost of flattening their loudest peaks.
constexpr uint8_t CodecVolume = 50;
constexpr uint8_t MaxCodecVolume = 62;
constexpr uint8_t DefaultVolume = 80;
// Tone control for the 20x30 mm 4 ohm speaker.  It reproduces nothing useful
// below the 500 Hz low shelf but still loads the amplifier, so cutting that
// shelf turns headroom into midrange.  Gains are dB, -40..+6.
constexpr int8_t ToneLowShelf = -12;
constexpr int8_t TonePeak = 1;
constexpr int8_t ToneHighShelf = 2;
// How long the codec stays muted across a stream switch.  The mute has to be
// in force before the old stream is dropped (the DMA flush cuts mid waveform
// otherwise) and lifted soon after the new one starts, so the first samples of
// a clash are not clipped away.
constexpr uint16_t MuteSwitchMs = 25;
// Settling time around an amplifier power change: waited after PA_EN rises
// before anything is played into it, and after the output is muted before
// PA_EN drops.
constexpr uint16_t PaSettleMs = 20;

// ---------------------------------------------------------------- display
constexpr int8_t DisplayBacklight = 8;
// The backlight is the largest steady load on the 3.3 V rail (AMS1117 fed),
// so it is driven by LEDC PWM instead of a plain digital high.  10 kHz sits
// above the audible band (no whine into the amplifier) and outside the
// camera-banding range; 66 % duty is visually near-indistinguishable
// indoors.  Real-machine acceptance point: the QR screen must still scan.
constexpr uint32_t DisplayBacklightPwmFrequency = 10000;
constexpr uint8_t DisplayBacklightPwmRes = 10;  // bits: duty range 0..1023
constexpr uint8_t DefaultBacklightPct = 66;

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
// The hum loop re-arms on the falling edge of playback (SaberController sees
// it via AudioOutput::isRunning), so it needs no duration or delay constants:
// any file the user picks just works.
constexpr uint32_t SwingCooldown = 100;
constexpr uint8_t PulseAmplitude = 10;
constexpr uint16_t PulseDelay = 30;
constexpr uint16_t FlashDelay = 15;
// Clash flash: the blade colour blown towards white, with a duration that
// scales with how hard the strike came in.  HitExtraMs is added at 100%
// intensity.
constexpr uint16_t HitBaseMs = 180;
constexpr uint16_t HitExtraMs = 170;
constexpr uint8_t StrikeFlashWhiteBlend = 165;  // 0 = blade colour, 255 = white
constexpr uint8_t ScannerTrailLength = 8;
constexpr uint16_t RainbowHueStep = 512;
constexpr uint16_t RainbowSwingBoost = 25;  // extra hue step per rotation unit

// ------------------------------------------------- unstable / sparkle / fire
constexpr uint16_t FlickerIntervalMs = 45;
constexpr int8_t FlickerAmplitude = 9;            // steady per-pixel jitter
constexpr int8_t FlickerSurgeAmplitude = 55;      // occasional plasma surge
constexpr uint8_t FlickerSurgeChancePercent = 6;  // % of frames that surge
constexpr uint16_t SparkleIntervalMs = 70;
constexpr uint8_t SparkleCount = 2;  // white glints per frame
constexpr uint16_t FireIntervalMs = 35;
constexpr uint8_t FireCooling = 55;   // 0-255, higher = shorter flames
constexpr uint8_t FireSparking = 90;  // out of 256, chance of a spark per frame

// ----------------------------------------------------------- eye tracking
// Roll / pitch (radians) that push the gaze to the edge of its travel.
constexpr float EyeGazeRadiansHorizontal = 1.0f;
constexpr float EyeGazeRadiansVertical = 1.4f;

// ------------------------------------------------------------- networking
constexpr const char* DefaultApSsid = "Esaber-Setup";
// Deliberately none: the setup hotspot is open, so getting back into a
// misconfigured device never depends on remembering a passphrase.  WiFi.softAP
// takes NULL (or an empty string) as WIFI_AUTH_OPEN -- a passphrase shorter
// than 8 characters would instead make the call fail and leave the device with
// no access point at all.
constexpr const char* DefaultApPassword = nullptr;
constexpr const char* PreferencesNamespace = "esaber";
constexpr uint32_t WifiConnectTimeout = 15000;

}  // namespace HardwareConfig
