#include "SaberController.h"

#include "../../include/HardwareConfig.h"

namespace {

// ---- 音效资源表：文件名必须与 SD 卡根目录一致 ----
constexpr uint8_t kStrikeSoundCount = 10;                   // 碰撞音效 10 个
constexpr uint8_t kSwingSoundCount = 14;                    // 挥动音效 14 个
constexpr uint8_t kSwingSoundFastCount = kSwingSoundCount / 2;  // 慢速挥动只用前一半（低沉）
constexpr float kPulseSmoothing = 0.8f;                     // 呼吸偏移的低通系数（越大越平滑）
constexpr float kRadiansToDegrees = 57.2958f;               // 弧度 → 角度
constexpr float kAccelLsbPerGravityUnit = 2048.0f;          // ±8g 量程下 2048 LSB = 1g
constexpr uint16_t kPulseRandomRange = 1024;                // 呼吸随机数取值范围
constexpr uint16_t kPulseRandomMax = kPulseRandomRange - 1;
constexpr uint8_t kChannelMaximum = 255;                    // 单通道满量程
constexpr uint16_t kHueFullCircle = 65535;                  // HSV 色相满圈

const char* const kStrikeSounds[kStrikeSoundCount] = {
    "clsh1.wav", "clsh2.wav", "clsh3.wav", "clsh4.wav", "clsh5.wav",
    "clsh6.wav", "clsh7.wav", "clsh8.wav", "clsh9.wav", "clsh10.wav"};
const char* const kSwingSounds[kSwingSoundCount] = {
    "swng1.wav",  "swng2.wav",  "swng3.wav",  "swng4.wav",  "swng5.wav",
    "swng6.wav",  "swng7.wav",  "swng8.wav",  "swng9.wav",  "swng10.wav",
    "swng11.wav", "swng12.wav", "swng13.wav", "swng14.wav"};

uint8_t clampColor(int value) {          // 通道值钳到 0-255（特效加减偏移后防溢出）
  return static_cast<uint8_t>(constrain(value, 0, kChannelMaximum));
}

float square(float value) { return value * value; }

// 把一个通道往白色方向拉：amount 0 = 原色，255 = 纯白。碰撞闪光用。
uint8_t blendToWhite(uint8_t channel, uint8_t amount) {
  return static_cast<uint8_t>(channel + (kChannelMaximum - channel) * amount / kChannelMaximum);
}

// FastLED HeatColor 的精简移植：热度 → 黑→深红→橙→黄 渐变（火焰特效渲染）。
void heatToRgb(uint8_t heat, uint8_t& red, uint8_t& green, uint8_t& blue) {
  const uint8_t t192 = static_cast<uint8_t>(static_cast<uint16_t>(heat) * 191 / kChannelMaximum);
  if (t192 > 127) {                      // 高温段：满红 + 绿色爬升（红→橙→黄）
    red = kChannelMaximum;
    green = static_cast<uint8_t>((t192 - 128) * 2);
    blue = 0;
  } else {                               // 低温段：红色爬升，无绿无蓝（黑→深红）
    red = static_cast<uint8_t>(t192 * 2);
    green = 0;
    blue = 0;
  }
}

}  // namespace

// ------------------------------------------------------------------ 初始化
void SaberController::begin(AudioOutput* audio, PixelStrip* strip, MotionSensor* motion,
                            const SaberSettings& initialSettings) {
  audio_ = audio;
  strip_ = strip;
  motion_ = motion;
  settings_ = initialSettings;
  strip_->setBrightness(ledBrightness());   // 百分比换算成硬件刻度
  audio_->setVolume(audioVolume());
  // 刀是灭的，所以开机不放开刃音效——旧版每次上电都播，不管刀有没有亮。
  strip_->clear();
  // pitch 历史窗口初始全零：如果开机时刀恰好被握在翻转阈值之外，
  // 全零窗口的"极差"看起来就像一次甩腕，会自己开刃。
  // 因此先收集满第一个窗口（gestureWarmup_），再信任阈值判定。
  gestureWarmup_ = kImuSampleCount;
}

// settings_.brightness 是用户-facing 百分比；在这里换算到硬件刻度，
// 保证灯条永不越过防爆流上限。最终到达 LED 的亮度还会被 PixelStrip 的
// 限流器二次封顶——那里才知道当前颜色值多少电流。
uint8_t SaberController::ledBrightness() const {
  return static_cast<uint8_t>(min<uint32_t>(
      static_cast<uint32_t>(settings_.brightness) * HardwareConfig::MaxLedBrightness / 100,
      HardwareConfig::MaxLedBrightness));
}

uint8_t SaberController::audioVolume() const {   // 音量百分比钳 0-100
  return min(settings_.volume, static_cast<uint8_t>(100));
}

// ------------------------------------------------------------------ 主循环
void SaberController::update() {
  audio_->loop();            // 解码器泵：每圈必须喂，否则断音
  motion_->update();         // IMU 采样 + 姿态解算（内部自带节流）

  readGesture();             // 灭刀状态下的甩腕开关刃
  if (gestureRequested_) setPower(!settings_.power);

  if (!settings_.power) {
    // 灭刀态：清掉一切进行中的效果；收刃动画要继续跑完——
    // 刀是在断电后继续塌缩的，塌完灯条才真正全黑。
    strikePlaying_ = false;
    strikeEffect_ = false;
    turnOnAnimation_ = false;
    humPlaying_ = false;
    if (retracting_) updateRetract(millis());
    return;
  }

  const unsigned long now = millis();
  handleStrike(now);         // 碰撞检测
  handleSwing(now);          // 挥动检测
  updateHum(now);            // 嗡鸣续播
  updateLighting(now);       // 灯条状态机
}

// ------------------------------------------------------------------ 设置变更
// web 端改设置后统一走到这里；逐字段 diff，只对真正变化的部分做动作。
void SaberController::setSettings(const SaberSettings& settings) {
  const bool powerChanged = settings.power != settings_.power;
  const bool brightnessChanged = settings.brightness != settings_.brightness;
  const bool volumeChanged = settings.volume != settings_.volume;
  const bool colorChanged = settings.red != settings_.red || settings.green != settings_.green ||
                            settings.blue != settings_.blue;
  const bool humChanged = strcmp(settings.humSound, settings_.humSound) != 0;
  settings_ = settings;

  if (brightnessChanged) {
    strip_->setBrightness(ledBrightness());
  }
  if (volumeChanged) {
    settings_.volume = audioVolume();
    // 编解码器立即应用新音量，无需重绘灯条。
    audio_->setVolume(settings_.volume);
  }
  if (powerChanged) {
    setPower(settings.power);
    return;                  // 开关刃已经把灯条安排明白了，不再走下面
  }
  // 亮刀中途换嗡鸣文件：立刻切换播放，旧流同时被丢弃，耳朵马上听到变化。
  // 开机音/收刀音不追改，下次触发时自然用新文件。
  if (humChanged && humPlaying_ && settings_.power) {
    audio_->playHum(settings_.humSound);
  }
  if (!settings_.power) {
    strip_->clear();
  } else if (colorChanged) {
    strip_->fill(settings_.red, settings_.green, settings_.blue);
  }
}

// ------------------------------------------------------------------ 开关刃
void SaberController::setPower(bool enabled) {
  gestureRequested_ = false;   // 消费掉手势请求
  if (enabled == settings_.power) return;

  settings_.power = enabled;
  if (enabled) {
    audio_->play(settings_.bootSound);          // 先响开刃音
    audio_->setAmplifierEnabled(true);          // 再抬功放
    turnOnAnimation_ = true;
    animationPixel_ = 0;
    retracting_ = false;
    humPlaying_ = true;
    // 【改动④ · 开刃串行化】把电流台阶排成队：功放稳定窗（PaSettleMs=20ms，
    // 用户不可感知）先走，灯条展开动画在窗走完之前一颗像素都不点——
    // PA 浪涌与 LED 首帧电流不再同帧叠加，3.3V 轨峰值直接被砍掉。
    ignitionLightDue_ = millis() + HardwareConfig::PaSettleMs;
    effectTimer_ = millis();
    // 这里故意不填灯条。一次性点亮全部像素既毁掉开刃动画，
    // 又会把电源拉到 MCU 掉电复位；展开交由 updateLighting 逐帧推进。
  } else {
    audio_->play(settings_.shutdownSound);
    // 注意顺序：必须先 play() 再关功放标志——play() 会抢占功放保证收刀音播出，
    // 之后的"请求关闭"让驱动等这声音播完才真正拉低 PA_EN。
    audio_->setAmplifierEnabled(false);
    humPlaying_ = false;
    swingReady_ = false;
    strikePlaying_ = false;
    strikeEffect_ = false;
    turnOnAnimation_ = false;
    // 收刃而不是瞬间黑屏：刀从两端向中间塌缩、带一点余晖，
    // 视觉上像等离子体排空，比硬 clear() 贴合关刀音效。
    retracting_ = true;
    animationPixel_ = HardwareConfig::LedCount / 2 - 1;   // 从最外端往里收
    effectTimer_ = millis();
  }
}

// ------------------------------------------------------------------ 手势：甩腕开关刃
// 检测刀在灭掉状态下的快速翻转（pitch 突变）。窗口 8 样本算极差，
// 超过 OpenThreshold 且冷却结束才算一次有效手势。
void SaberController::readGesture() {
  const unsigned long now = millis();
  if (now - gestureTimer_ < HardwareConfig::GestureInterval) return;   // 采样节流
  gestureTimer_ = now;

  const MotionData& data = motion_->data();
  const float accelX = static_cast<float>(data.ax) / kAccelLsbPerGravityUnit;   // 原始 LSB → g
  const float accelY = static_cast<float>(data.ay) / kAccelLsbPerGravityUnit;
  const float accelZ = static_cast<float>(data.az) / kAccelLsbPerGravityUnit;
  const uint8_t sampleIndex = gestureCounter_ & (kImuSampleCount - 1);          // 环形下标
  pitchSamples_[sampleIndex] =
      atan2(-accelX, sqrt(square(accelY) + square(accelZ))) * kRadiansToDegrees;  // 俯仰角

  // 先把窗口填满再信任它：每个槽位都是真采样后，极差才有意义。
  if (gestureWarmup_ > 0) {
    --gestureWarmup_;
    ++gestureCounter_;
    return;
  }

  float pitchMinimum = pitchSamples_[0];
  float pitchMaximum = pitchMinimum;
  for (uint8_t index = 0; index < kImuSampleCount; ++index) {
    pitchMinimum = min(pitchMinimum, pitchSamples_[index]);
    pitchMaximum = max(pitchMaximum, pitchSamples_[index]);
  }

  // 灭刀 + 窗口极差超阈 + 不在冷却 → 请求开刃（冷却防手抖连触）
  if (!settings_.power && pitchMaximum - pitchMinimum > HardwareConfig::OpenThreshold &&
      gestureCooldown_ == 0) {
    gestureRequested_ = true;
    gestureCooldown_ = HardwareConfig::GestureToggleCount;
  }

  ++gestureCounter_;
  if (gestureCooldown_ > 0) --gestureCooldown_;
}

// ------------------------------------------------------------------ 碰撞
// 加速度落在 [StrikeThreshold, HardStrikeThreshold) 区间 = 一次碰撞。
// 超过硬碰撞上限的留给将来的"重击"逻辑；开刃/收刃动画期间不判定。
void SaberController::handleStrike(unsigned long now) {
  const MotionData& data = motion_->data();
  const bool strikeDetected =
      data.acceleration > HardwareConfig::StrikeThreshold &&
      data.acceleration < HardwareConfig::HardStrikeThreshold &&
      now - strikeTimeout_ > HardwareConfig::SwingTimeout && !turnOnAnimation_ && !retracting_;
  if (!strikeDetected) return;

  strikeTimeout_ = now;
  playRandomSound(kStrikeSounds, kStrikeSoundCount);   // 随机碰撞音
  // 碰撞音播完后嗡鸣自动续上（updateHum 的续播逻辑兜底）。
  // 把撞击强度归一到 0-100，闪光时长随力度缩放，而不是每次都一样长。
  const uint8_t intensity = static_cast<uint8_t>(
      constrain(static_cast<long>(data.acceleration - HardwareConfig::StrikeThreshold) * 100 /
                    (HardwareConfig::HardStrikeThreshold - HardwareConfig::StrikeThreshold),
                0L, 100L));
  startStrike(intensity);
  strikePlaying_ = true;
}

// ------------------------------------------------------------------ 挥动
// 双阈值分档：超 SwingThreshold 用全套音效（快挥），只过低速阈值用前一半
// （慢挥，音色更沉）。冷却 + 最小间隔双防抖，碰撞音期间不叠挥动音。
void SaberController::handleSwing(unsigned long now) {
  const MotionData& data = motion_->data();
  // 动画期间静默；turnOnAnimation_/retracting_ 为开刃/收刃标志
  if (data.rotation <= HardwareConfig::SwingLowThreshold ||
      now - swingTimeout_ <= HardwareConfig::SwingCooldown || turnOnAnimation_ || retracting_) {
    return;
  }
  swingTimeout_ = now;
  if (now - swingTimer_ <= HardwareConfig::SwingTimeout || !swingReady_ || strikePlaying_) {
    return;
  }

  if (data.rotation >= HardwareConfig::SwingThreshold) {          // 快挥：全 14 个
    playRandomSound(kSwingSounds, kSwingSoundCount);
  } else {                                                        // 慢挥：前 7 个
    playRandomSound(kSwingSounds, kSwingSoundFastCount);
  }

  swingReady_ = false;   // 嗡鸣续播时重新武装（updateHum）
  swingTimer_ = now;
}

// ------------------------------------------------------------------ 嗡鸣循环
// 亮刀期间持续循环的底噪。策略是"播放落空沿续播"：不管在响什么
//（开刃音、挥动、碰撞），播完空出来嗡鸣就补上——不内置定时器，
// 对任意时长的音频文件都成立。
void SaberController::updateHum(unsigned long now) {
  (void)now;                        // 参数保留为签名一致，逻辑不需要
  if (!humPlaying_) return;
  if (audio_->isRunning()) return;  // 有声音在播：等它播完
  audio_->playHum(settings_.humSound);
  swingReady_ = true;               // 嗡鸣就位 → 挥动检测重新武装
  strikePlaying_ = false;           // 碰撞音已结束
}

// ------------------------------------------------------------------ 灯条总调度
// 优先级：收刃 > 开刃动画 > 碰撞白闪 > 七种特效。
void SaberController::updateLighting(unsigned long now) {
  // 收刃进行中：灯条归它独占，直到刀完全排空。
  if (retracting_) {
    updateRetract(now);
    return;
  }

  if (turnOnAnimation_) {
    // 【改动④ · 开刃串行化（执行端）】动画在这里等 ignitionLightDue_：
    // 功放稳定窗没走完就持续 return，PA 浪涌和首帧 LED 电流永不同帧。
    if (now < ignitionLightDue_) return;
    if (now - effectTimer_ < HardwareConfig::FlashDelay) return;   // 帧节流
    effectTimer_ = now;
    strip_->setPixel(animationPixel_, settings_.red, settings_.green, settings_.blue);
    strip_->setPixel(HardwareConfig::LedCount - 1 - animationPixel_, settings_.red,   // 镜像端同步点亮
                     settings_.green, settings_.blue);
    strip_->show();
    ++animationPixel_;
    if (animationPixel_ >= HardwareConfig::LedCount / 2) {   // 两端汇合 → 开刃完成
      // 奇数颗时中心像素不属任何镜像对（动画只画 (0,N-1)…(N/2-1,N/2+1)），
      // 而常亮特效不重绘（setSettings 只在改设置时填一次色）——缺这一笔，
      // 开刃完成后中心会永久留一颗暗点。偶数颗时它是无害的冗余填色。
      strip_->fill(settings_.red, settings_.green, settings_.blue);
      animationPixel_ = 0;
      turnOnAnimation_ = false;
      effectTimer_ = now;
    }
    return;
  }

  if (strikeEffect_) {
    // 碰撞白闪：倒计时结束回到刀身色。
    if (now - hitTimer_ > hitDuration_) {
      strikeEffect_ = false;
      strip_->fill(settings_.red, settings_.green, settings_.blue);
    }
    return;
  }

  switch (settings_.effect) {    // 常规特效分发
    case SaberEffect::Pulse:
      applyPulse(now);
      break;
    case SaberEffect::Rainbow:
      updateRainbow(now);
      break;
    case SaberEffect::Scanner:
      updateScanner(now);
      break;
    case SaberEffect::Unstable:
      applyUnstable(now);
      break;
    case SaberEffect::Fire:
      updateFire(now);
      break;
    case SaberEffect::Sparkle:
      updateSparkle(now);
      break;
    case SaberEffect::Solid:
    default:
      break;                     // 常亮：setSettings 里已经填过色，无需动
  }
}

// ---- 呼吸：随机目标偏移 + 低通平滑，避免机械感 ----
void SaberController::applyPulse(unsigned long now) {
  if (now - pulseTimer_ <= HardwareConfig::PulseDelay) return;
  pulseTimer_ = now;
  const int randomOffset = map(static_cast<long>(esp_random() % kPulseRandomRange), 0L,
                               static_cast<long>(kPulseRandomMax),
                               -HardwareConfig::PulseAmplitude,
                               HardwareConfig::PulseAmplitude);
  pulseOffset_ = static_cast<int>(static_cast<float>(pulseOffset_) * kPulseSmoothing +
                                  static_cast<float>(randomOffset) * (1.0f - kPulseSmoothing));
  strip_->fill(clampColor(settings_.red + pulseOffset_),     // 全条同色微调
               clampColor(settings_.green + pulseOffset_),
               clampColor(settings_.blue + pulseOffset_));
}

// ---- 彩虹：整条按索引铺满色相环；挥动越快转得越快，像物理带动 ----
void SaberController::updateRainbow(unsigned long now) {
  if (now - effectTimer_ < kEffectIntervalMs) return;
  effectTimer_ = now;
  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {
    const uint16_t hue = rainbowHue_ + (index * kHueFullCircle) / HardwareConfig::LedCount;
    strip_->setPixel(index, strip_->colorHsv(hue));
  }
  strip_->show();
  // 挥动加速色相推进：特效像是"挂在动作上"，而不是按钟表跑。
  const uint16_t swingBoost = static_cast<uint16_t>(
      min<uint16_t>(motion_->data().rotation, HardwareConfig::SwingThreshold) *
      HardwareConfig::RainbowSwingBoost);
  rainbowHue_ += HardwareConfig::RainbowHueStep + swingBoost;
}

// ---- 不稳定：每像素在刀色附近抖动，偶发单帧"等离子涌动" ----
void SaberController::applyUnstable(unsigned long now) {
  if (now - effectTimer_ < HardwareConfig::FlickerIntervalMs) return;
  effectTimer_ = now;
  const int amplitude =
      esp_random() % 100 < HardwareConfig::FlickerSurgeChancePercent
          ? HardwareConfig::FlickerSurgeAmplitude      // 涌动帧：振幅放大
          : HardwareConfig::FlickerAmplitude;          // 普通帧
  const int jitterRange = 2 * amplitude + 1;
  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {
    const int delta = static_cast<int>(esp_random() % jitterRange) - amplitude;
    strip_->setPixel(index, clampColor(settings_.red + delta),
                     clampColor(settings_.green + delta), clampColor(settings_.blue + delta));
  }
  strip_->show();
}

// ---- 火焰：热注入于刀柄 → 向刀尖漂移 → 途中冷却 → 黑红橙黄渐变渲染。
//      每像素 1 字节热度图，主循环节拍内的每帧开销极小。----
void SaberController::updateFire(unsigned long now) {
  if (now - effectTimer_ < HardwareConfig::FireIntervalMs) return;
  effectTimer_ = now;

  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {   // ① 随机冷却
    const uint8_t cooling =
        esp_random() % ((HardwareConfig::FireCooling * 10) / HardwareConfig::LedCount + 2);
    fireHeat_[index] =
        fireHeat_[index] > cooling ? static_cast<uint8_t>(fireHeat_[index] - cooling) : 0;
  }
  for (uint16_t index = HardwareConfig::LedCount - 1; index >= 2; --index) {  // ② 向上扩散
    const uint16_t source = index - 2;
    fireHeat_[index] = static_cast<uint8_t>(
        (static_cast<uint16_t>(fireHeat_[source]) + 2 * fireHeat_[source + 1]) / 3);  // 加权平均
  }
  if (esp_random() % 256 < HardwareConfig::FireSparking) {   // ③ 底部注入火星
    fireHeat_[esp_random() % 3] = static_cast<uint8_t>(160 + esp_random() % 96);
  }

  for (uint16_t index = 0; index < HardwareConfig::LedCount; ++index) {   // ④ 渲染
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    heatToRgb(fireHeat_[index], red, green, blue);
    strip_->setPixel(index, red, green, blue);
  }
  strip_->show();
}

// ---- 星尘：刀色打底，每帧几粒白光点跳来跳去，像等离子体里的浮尘 ----
void SaberController::updateSparkle(unsigned long now) {
  if (now - effectTimer_ < HardwareConfig::SparkleIntervalMs) return;
  effectTimer_ = now;
  strip_->fill(settings_.red, settings_.green, settings_.blue);
  for (uint8_t glint = 0; glint < HardwareConfig::SparkleCount; ++glint) {
    const uint16_t index = esp_random() % HardwareConfig::LedCount;
    strip_->setPixel(index, blendToWhite(settings_.red, 230), blendToWhite(settings_.green, 230),
                     blendToWhite(settings_.blue, 230));
  }
  strip_->show();
}

// ---- 收刃：开刃的逆过程，刀从两端向中间塌缩。
//      塌缩前沿带 3 级渐变余晖，看起来像等离子排空而不是逐像素硬切。----
void SaberController::updateRetract(unsigned long now) {
  if (now - effectTimer_ < HardwareConfig::FlashDelay) return;
  effectTimer_ = now;

  const int16_t edge = animationPixel_;
  for (uint8_t distance = 1; distance <= kRetractFadeSteps; ++distance) {   // 前沿后方渐变
    const int16_t index = edge - distance;
    if (index < 0) break;
    // distance 1..3 → 满色的 3/4、2/4、1/4：每像素每帧衰减一级，
    // 直到前沿到达变黑。
    const uint8_t brightness = static_cast<uint8_t>(
        kChannelMaximum * (kRetractFadeSteps + 1 - distance) / (kRetractFadeSteps + 1));
    const int16_t mirrored = HardwareConfig::LedCount - 1 - index;   // 镜像端同步
    strip_->setPixel(index, settings_.red * brightness / kChannelMaximum,
                     settings_.green * brightness / kChannelMaximum,
                     settings_.blue * brightness / kChannelMaximum);
    strip_->setPixel(mirrored, settings_.red * brightness / kChannelMaximum,
                     settings_.green * brightness / kChannelMaximum,
                     settings_.blue * brightness / kChannelMaximum);
  }
  strip_->setPixel(edge, 0, 0, 0);                            // 前沿本体熄灭
  strip_->setPixel(HardwareConfig::LedCount - 1 - edge, 0, 0, 0);
  strip_->show();

  if (edge == 0) {              // 两端汇合 → 收刃完成
    retracting_ = false;
    strip_->clear();
    return;
  }
  --animationPixel_;
}

// ---- 扫描：一个光头带拖尾来回跑，拖尾按 1/(n+1) 衰减 ----
void SaberController::updateScanner(unsigned long now) {
  if (now - effectTimer_ < HardwareConfig::FlashDelay) return;
  effectTimer_ = now;
  strip_->fill(0, 0, 0);
  for (uint8_t trail = 0; trail < HardwareConfig::ScannerTrailLength; ++trail) {
    const int16_t index = static_cast<int16_t>(scannerPixel_) - trail * scannerDirection_;
    if (index < 0 || index >= static_cast<int16_t>(HardwareConfig::LedCount)) continue;
    const uint8_t brightness = kChannelMaximum / (trail + 1);   // 越远越暗
    strip_->setPixel(index, settings_.red * brightness / kChannelMaximum,
                     settings_.green * brightness / kChannelMaximum,
                     settings_.blue * brightness / kChannelMaximum);
  }
  strip_->show();
  scannerPixel_ = static_cast<uint16_t>(static_cast<int16_t>(scannerPixel_) + scannerDirection_);
  if (scannerPixel_ == 0 || scannerPixel_ == HardwareConfig::LedCount - 1) {   // 到头折返
    scannerDirection_ = static_cast<int8_t>(-scannerDirection_);
  }
}

// ---- 碰撞闪光：刀色整体往白拉（保用户色相，不换固定白色）----
void SaberController::startStrike(uint8_t intensity) {
  // 闪光是刀色被冲向白，而非固定色：碰撞瞬间用户选的颜色仍然认得出。
  strip_->fill(blendToWhite(settings_.red, HardwareConfig::StrikeFlashWhiteBlend),
               blendToWhite(settings_.green, HardwareConfig::StrikeFlashWhiteBlend),
               blendToWhite(settings_.blue, HardwareConfig::StrikeFlashWhiteBlend));
  hitDuration_ = HardwareConfig::HitBaseMs +
                 static_cast<uint16_t>(intensity * HardwareConfig::HitExtraMs / 100);  // 力度→时长
  hitTimer_ = millis();
  strikeEffect_ = true;
}

void SaberController::playRandomSound(const char* const sounds[], uint8_t count) {
  audio_->play(sounds[esp_random() % count]);   // 硬件随机数选曲
}
