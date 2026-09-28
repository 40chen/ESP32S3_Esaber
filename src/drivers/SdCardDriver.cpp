// ============================================================================
// SdCardDriver —— SD_MMC 挂载（音效文件所在，1-bit 模式省引脚）
// ============================================================================
#include "SdCardDriver.h"

#include <SD_MMC.h>

#include "../../include/HardwareConfig.h"

namespace {

constexpr const char* kMountPoint = "/sdcard";
constexpr bool kOneBitMode = true;            // 1-bit：省 3 根数据线
constexpr bool kFormatIfMountFailed = false;  // 绝不静默格式化用户的卡
constexpr int kFrequencyKhz = 20000;          // 首选 20MHz
// 卡在初始化时报过 0x107（超时），典型的信号边际问题——
// 半频重试往往能训练成功。音源全在卡上，值得多试这一下。
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

  SD_MMC.end();                    // 半频重试前先彻底释放
  delay(kRetryDelayMs);
  if (mount(kRetryFrequencyKhz)) {
    Serial.printf("[SD] initialized at the reduced clock (%d kHz)\n", kRetryFrequencyKhz);
    return true;
  }

  Serial.println("[SD] initialization failed - check the card is seated");
  return false;
}
