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

// 双音效目录开机即建（目录已存在时 mkdir 返回 false，忽略即可）。
// 目录缺席时控制台的分组下拉会是两个空组，用户容易摸不着头脑。
void ensureSoundFolders() {
  SD_MMC.mkdir(String("/") + HardwareConfig::SfxDefaultDir);
  SD_MMC.mkdir(String("/") + HardwareConfig::SfxUserDir);
}

}  // namespace

bool SdCardDriver::begin() {
  if (mount(kFrequencyKhz)) {
    ensureSoundFolders();
    Serial.println("[SD] initialized");
    return true;
  }

  SD_MMC.end();                    // 半频重试前先彻底释放
  delay(kRetryDelayMs);
  if (mount(kRetryFrequencyKhz)) {
    ensureSoundFolders();
    Serial.printf("[SD] initialized at the reduced clock (%d kHz)\n", kRetryFrequencyKhz);
    return true;
  }

  Serial.println("[SD] initialization failed - check the card is seated");
  return false;
}

String SdCardDriver::resolveSoundPath(const String& name) {
  if (name.isEmpty()) return String();
  if (name.indexOf('/') >= 0) {   // 带目录前缀的完整引用，原样校验
    return SD_MMC.exists("/" + name) ? name : String();
  }
  // 裸文件名回落顺序：新约定 sfx_default → 旧约定根目录 → sfx_user 兜底
  const String candidates[3] = {
      String(HardwareConfig::SfxDefaultDir) + "/" + name,
      name,
      String(HardwareConfig::SfxUserDir) + "/" + name};
  for (const String& candidate : candidates) {
    if (SD_MMC.exists("/" + candidate)) return candidate;
  }
  return String();
}
