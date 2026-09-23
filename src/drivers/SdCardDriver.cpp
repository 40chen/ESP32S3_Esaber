#include "SdCardDriver.h"

#include <SD_MMC.h>

#include "../../include/HardwareConfig.h"

namespace {

constexpr const char* kMountPoint = "/sdcard";
constexpr bool kOneBitMode = true;
constexpr bool kFormatIfMountFailed = false;
constexpr int kFrequencyKhz = 20000;
// The card reported 0x107 (timeout) during init, which is typical of a marginal
// signal.  A second attempt at half the clock often trains where the first
// did not.  Everything the saber plays lives on the card, so it is worth it.
constexpr int kRetryFrequencyKhz = 10000;
constexpr uint16_t kRetryDelayMs = 50;

bool mount(int frequencyKhz) {
  SD_MMC.setPins(HardwareConfig::SdClk, HardwareConfig::SdCmd, HardwareConfig::SdD0, -1, -1, -1);
  return SD_MMC.begin(kMountPoint, kOneBitMode, kFormatIfMountFailed, frequencyKhz);
}

}  // namespace

bool SdCardDriver::begin() {
  if (mount(kFrequencyKhz)) {
    Serial.println("[SD] initialized");
    return true;
  }

  SD_MMC.end();
  delay(kRetryDelayMs);
  if (mount(kRetryFrequencyKhz)) {
    Serial.printf("[SD] initialized at the reduced clock (%d kHz)\n", kRetryFrequencyKhz);
    return true;
  }

  Serial.println("[SD] initialization failed - check the card is seated");
  return false;
}
