#pragma once

#include <Arduino.h>

class SdCardDriver {
 public:
  bool begin();

  // 把音效引用解析成卡上真实存在的相对路径；找不到返回空串。
  // "sfx_default/x.wav" / "sfx_user/x.wav" 原样校验；裸文件名（旧 NVS 值、
  // 内置碰撞/挥动音效表）按 sfx_default → 根目录 → sfx_user 顺序回落，
  // 旧值无需迁移即可继续播放。返回值不带前导 '/'（connecttoFS 约定）。
  static String resolveSoundPath(const String& name);
};
