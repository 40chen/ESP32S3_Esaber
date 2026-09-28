#include "DisplayDriver.h"

#include <math.h>

#include <qrcode.h>

#include "../../include/HardwareConfig.h"
#include "EyeTexture.h"

namespace {

constexpr uint8_t kQrVersion = 3;      // QR 版本（决定容量，29×29 模块）
constexpr uint8_t kQrScale = 5;        // 每模块 5 像素
constexpr int16_t kQrTitleY = 16;      // 标题基线

// ---- 视线追踪与待机行为 ----
constexpr float kGazeSmoothing = 0.18f;        // 视线低通系数（越小越"跟手慢"）
constexpr float kIdleSmoothing = 0.05f;        // 待机游移的低通系数
constexpr float kIdleWander = 0.11f;           // 待机游移的幅度（不喧宾夺主）
constexpr float kIdleWanderVertical = 0.6f;    // 垂直方向再收一点
constexpr float kFrameEpsilon = 0.004f;        // 判"帧没变"的阈值

// ---- 眼睛开合度：睡觉是一条缝，生气是眯眼 ----
constexpr float kSleepOpenness = 0.06f;
constexpr float kAngryOpenness = 0.82f;

constexpr unsigned long kBlinkMinInterval = 2200;   // 眨眼间隔随机区间 ms
constexpr unsigned long kBlinkMaxInterval = 6400;
constexpr unsigned long kBlinkCloseMs = 70;         // 闭眼时长
constexpr unsigned long kBlinkOpenMs = 115;         // 睁眼时长

// 【改动① · 背光 LEDC PWM】背光是 3.3V 轨上最大的稳态负载，
// 因此不再数字常亮，而是走 LEDC PWM 降占空比（66%）。
// 可调参数（频率/位深/占空比）都在 HardwareConfig 的引脚旁；
// 只有通道号是实现细节——本面板配置下 TFT_eSPI 不占用任何 LEDC 通道，
// 通道 0 全仓库由背光独占，与灯条/WiFi 无冲突。
constexpr uint8_t kBacklightPwmChannel = 0;

constexpr unsigned long kIdleMinInterval = 1800;    // 待机游移节拍随机区间 ms
constexpr unsigned long kIdleMaxInterval = 4600;

// 生气表情在眼睛两侧画短眉。这两条原本挂在"上眼睑"上；
// 素材没有眼睑，于是放在压扁的眼球上方，向外上挑——皱眉的样子。
constexpr int16_t kBrowGap = 8;        // 眉与眼球之间的空隙（面板行数）
constexpr int16_t kBrowInnerX = 22;    // 眉的低端起点（距中心）
constexpr int16_t kBrowLength = 58;    // 眉长
constexpr int16_t kBrowRise = 16;      // 外端上挑高度
constexpr int16_t kBrowThickness = 9;  // 眉粗细
constexpr uint16_t kBrow565 = 0x2987;  // RGB(44,48,60)，过程式眼睛时代的眉色

// smoothStep 缓动：眨眼开合用，端点导数为零，动作更自然
float smoothStep(float value) {
  return value * value * (3.0f - 2.0f * value);
}

// 钳到 [-1,1] 并过滤 NaN/Inf（IMU 数据异常时不至于瞬移）
float clampUnit(float value) {
  if (!isfinite(value)) return 0.0f;
  return constrain(value, -1.0f, 1.0f);
}

}  // namespace

void DisplayDriver::begin() {
  // 上电先明确拉低背光，避免初始化期间的引脚悬空闪烁
  pinMode(HardwareConfig::DisplayBacklight, OUTPUT);
  digitalWrite(HardwareConfig::DisplayBacklight, LOW);

  // 【改动① · LEDC 初始化三部曲】把引脚交给 LEDC：
  // ① ledcSetup 配频率（10kHz，避开可听频段）+ 位深（10bit → duty 0..1023）；
  // ② ledcAttachPin 挂引脚；③ ledcWrite 写 0 占空比。
  // 全程电平保持低，与上面 digitalWrite(LOW) 无缝衔接——无毛刺；
  // 占空比要到 enableBacklight() 才抬起来。
  ledcSetup(kBacklightPwmChannel, HardwareConfig::DisplayBacklightPwmFrequency,
            HardwareConfig::DisplayBacklightPwmRes);
  ledcAttachPin(HardwareConfig::DisplayBacklight, kBacklightPwmChannel);
  ledcWrite(kBacklightPwmChannel, 0);

  tft_.init();                 // 面板复位 + 厂商寄存器表
  tft_.setRotation(0);         // 竖屏
  tft_.invertDisplay(true);    // GC9A01 需要反显才黑得正确
  tft_.fillScreen(TFT_BLACK);

  ensureCanvas(millis());      // 精灵图先试一次，失败也有开机画面可看
}

// 【改动① · 占空比入口】全项目唯一写背光 duty 的地方。
// 66% × (2^10 − 1) = 675，整除无截断；从这里把稳态电流降下来。
void DisplayDriver::enableBacklight() {
  const uint32_t maxDuty = (1u << HardwareConfig::DisplayBacklightPwmRes) - 1;  // 1023
  ledcWrite(kBacklightPwmChannel,
            static_cast<uint32_t>(HardwareConfig::DefaultBacklightPct) * maxDuty / 100);
}

// 二次 init() 只重复复位和寄存器表（总线配置被 TFT_eSPI 跳过）；
// setRotation/invertDisplay 每次都会重新应用。
void DisplayDriver::repairPanel() {
  tft_.init();
  tft_.setRotation(0);
  tft_.invertDisplay(true);
  tft_.fillScreen(TFT_BLACK);
  panel_ = Panel::Boot;  // 后续画面必须整个重铺
}

// 精灵图是显示链路里唯一会自己失败的环节：它是固件最大的一笔内存申请。
// 注意 TFT_eSPI 自己的 PSRAM 路径在本 SDK 上被编译掉了
//（它测的是 CONFIG_SPIRAM_SUPPORT，ESP-IDF 4.4 已不定义该宏），
// 所以这里就是普通 calloc——能落进 PSRAM 只是因为 IDF 分配器会把大请求
// 送去 PSRAM。位置是策略，不是保证。
//
// 曾经分配失败是静默且永久的：之后每一帧都直接 return。改成重试自愈。
bool DisplayDriver::ensureCanvas(unsigned long now) {
  if (canvasReady_) return true;                                    // 已就绪
  if (canvasRetry_ != 0 && now - canvasRetry_ < kCanvasRetryMs) return false;  // 限频
  canvasRetry_ = now;

  canvas_.setColorDepth(16);                                        // RGB565
  canvasReady_ = canvas_.createSprite(kPanelSize, kPanelSize) != nullptr;
  if (canvasReady_) {
    // TFT_eSPI 在 RAM 里按字节交换存放精灵像素（见 Sprite.cpp 的 drawPixel：
    // 恒做 (color >> 8) | (color << 8)），这让 pushSprite 可以直接钟出。
    // 精灵自带绘制调用会自动转换，但 pushImage 是逐字节 memcpy——
    // flash 里的纹理是自然序，必须在写入时交换。打开这个开关，
    // pushImage 就会做这件事；不开的话每像素字节颠倒，绿眼睛变品红。
    canvas_.setSwapBytes(true);
  }
  Serial.printf("[DISPLAY] eye sprite %s (%d bytes)\n",
                canvasReady_ ? "ready" : "allocation failed",
                kPanelSize * kPanelSize * 2);
  return canvasReady_;
}

void DisplayDriver::showBoot(bool ready) {
  tft_.fillScreen(TFT_BLACK);
  drawCenteredText("ESABER", 104, 4, ready ? TFT_GREEN : TFT_RED);
  drawCenteredText(ready ? "READY" : "HARDWARE CHECK", 148, 2, TFT_WHITE);
  panel_ = Panel::Boot;
}

void DisplayDriver::showQr(const char* title, const char* url) {
  // 重画一次 QR 要阻塞数十毫秒的 SPI，而地址只随网络变化——地址没变就跳过。
  if (panel_ == Panel::Qr && qrUrl_ == url) return;
  panel_ = Panel::Qr;
  qrUrl_ = url;

  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(kQrVersion)];   // 栈上缓冲，版本 3 足够
  qrcode_initText(&qrcode, qrcodeData, kQrVersion, ECC_LOW, url);

  tft_.fillScreen(TFT_WHITE);
  drawCenteredText(title, kQrTitleY, 2, TFT_BLACK);

  const uint16_t qrPixels = static_cast<uint16_t>(qrcode.size) * kQrScale;  // 码区边长
  const int16_t left = (kPanelSize - qrPixels) / 2;       // 居中
  const int16_t top = (kPanelSize - qrPixels) / 2 + kQrTitleY / 2;  // 给标题让位

  for (uint8_t row = 0; row < qrcode.size; ++row) {
    for (uint8_t column = 0; column < qrcode.size; ++column) {
      if (qrcode_getModule(&qrcode, column, row)) {       // 黑模块才画，省 SPI
        tft_.fillRect(left + column * kQrScale, top + row * kQrScale, kQrScale, kQrScale,
                      TFT_BLACK);
      }
    }
  }
}

void DisplayDriver::drawEye(float lookX, float lookY, EyePattern pattern) {
  const unsigned long now = millis();
  if (!ensureCanvas(now)) return;          // 精灵图未就绪：本帧放弃

  const bool attentive = pattern != EyePattern::Sleep;
  updateGaze(now, clampUnit(lookX), clampUnit(lookY), attentive);   // 视线 + 待机游移
  updateBlink(now);                        // 眨眼相位

  const float openness = attentive ? openness_ : kSleepOpenness;    // 睡觉恒为缝
  if (!frameChanged(openness, pattern)) return;    // 没变就不占 SPI

  const int16_t offsetX = static_cast<int16_t>(gazeX_ * static_cast<float>(kEyeTravelX));
  const int16_t offsetY = static_cast<int16_t>(gazeY_ * static_cast<float>(kEyeTravelY));

  // 开机/二维码页曾整屏覆盖过；眼睛回来前把精灵范围外的像素清干净
  if (panel_ != Panel::Eye) {
    panel_ = Panel::Eye;
    tft_.fillScreen(TFT_BLACK);
  }

  canvas_.fillSprite(TFT_BLACK);
  paintEye(offsetX, offsetY, openness);
  if (pattern == EyePattern::Angry) paintBrows(offsetX, offsetY, openness);

  canvas_.pushSprite(0, 0);                // 整帧一次性推屏（不闪的关键）
  markFramePushed(openness, pattern);
}

// 目标行与源行一一对应：平移 = 挪目标行，闭眼 = 绕中心收缩行数。
// 直接采样（不做插值）在这里就够了：素材本来就是最终尺寸，
// 收缩只是减少行数，不会放大失真。
void DisplayDriver::paintEye(int16_t offsetX, int16_t offsetY, float openness) {
  const int16_t height =
      max<int16_t>(2, static_cast<int16_t>(static_cast<float>(kEyeTextureSize) * openness));
  const int16_t left = kEyeCenterX + offsetX - kEyeTextureSize / 2;  // 绕中心定位
  const int16_t top = kEyeCenterY + offsetY - height / 2;

  for (int16_t row = 0; row < height; ++row) {
    const int16_t sourceRow = static_cast<int16_t>(static_cast<int32_t>(row) * kEyeTextureSize /
                                                   static_cast<int32_t>(height));  // 行映射
    canvas_.pushImage(left, top + row, kEyeTextureSize, 1,
                      kEyeTexture + sourceRow * kEyeTextureSize);
  }
}

void DisplayDriver::paintBrows(int16_t offsetX, int16_t offsetY, float openness) {
  const int16_t height =
      max<int16_t>(2, static_cast<int16_t>(static_cast<float>(kEyeTextureSize) * openness));
  const int16_t base = kEyeCenterY + offsetY - height / 2 - kBrowGap;   // 眉基线
  if (base - kBrowThickness - kBrowRise < 0) return;    // 眼睛太开时眉出屏，放弃

  const int16_t centre = kEyeCenterX + offsetX;
  for (int16_t index = 0; index < kBrowLength; ++index) {
    const int16_t offset = kBrowInnerX + index;
    const int16_t y = base - kBrowRise * index / (kBrowLength - 1);   // 向外上挑
    canvas_.drawFastVLine(centre - offset, y, kBrowThickness, kBrow565);  // 左眉
    canvas_.drawFastVLine(centre + offset, y, kBrowThickness, kBrow565);  // 右眉
  }
}

void DisplayDriver::updateGaze(unsigned long now, float targetX, float targetY,
                               bool attentive) {
  if (!attentive) {          // 睡觉：目标归零，视线慢慢回到中间
    targetX = 0.0f;
    targetY = 0.0f;
  }

  // 小幅不自主跳动：握住不动时眼睛也不显得"冻住"
  if (now - idleTimer_ > idleDue_) {
    idleTimer_ = now;
    idleDue_ = random(kIdleMinInterval, kIdleMaxInterval);
    idleTargetX_ = (static_cast<float>(random(-100, 101)) / 100.0f) * kIdleWander;
    idleTargetY_ =
        (static_cast<float>(random(-100, 101)) / 100.0f) * kIdleWander * kIdleWanderVertical;
  }
  idleX_ += (idleTargetX_ - idleX_) * kIdleSmoothing;   // 游移低通
  idleY_ += (idleTargetY_ - idleY_) * kIdleSmoothing;

  gazeX_ += (clampUnit(targetX + idleX_) - gazeX_) * kGazeSmoothing;   // 视线低通
  gazeY_ += (clampUnit(targetY + idleY_) - gazeY_) * kGazeSmoothing;
}

// 眨眼三相位：等间隔 → 70ms 闭（smoothStep 下降）→ 115ms 开（上升）→ 归位
void DisplayDriver::updateBlink(unsigned long now) {
  if (!blinking_) {
    if (now - blinkTimer_ <= blinkDue_) return;   // 还没到点
    blinking_ = true;
    blinkPhaseStart_ = now;
  }

  const unsigned long elapsed = now - blinkPhaseStart_;
  if (elapsed < kBlinkCloseMs) {                  // 闭眼段
    openness_ = 1.0f - smoothStep(static_cast<float>(elapsed) / static_cast<float>(kBlinkCloseMs));
    return;
  }

  const unsigned long opening = elapsed - kBlinkCloseMs;
  if (opening < kBlinkOpenMs) {                   // 睁眼段
    openness_ = smoothStep(static_cast<float>(opening) / static_cast<float>(kBlinkOpenMs));
    return;
  }

  openness_ = 1.0f;                               // 完成，安排下一次
  blinking_ = false;
  blinkTimer_ = now;
  blinkDue_ = random(kBlinkMinInterval, kBlinkMaxInterval);
}

bool DisplayDriver::frameChanged(float openness, EyePattern pattern) const {
  if (panel_ != Panel::Eye) return true;          // 别的画面盖过：必须重画
  if (static_cast<int8_t>(pattern) != pushedPattern_) return true;
  if (fabsf(openness - pushedOpenness_) > kFrameEpsilon) return true;
  if (fabsf(gazeX_ - pushedGazeX_) > kFrameEpsilon) return true;
  if (fabsf(gazeY_ - pushedGazeY_) > kFrameEpsilon) return true;
  return false;
}

void DisplayDriver::markFramePushed(float openness, EyePattern pattern) {
  pushedGazeX_ = gazeX_;
  pushedGazeY_ = gazeY_;
  pushedOpenness_ = openness;
  pushedPattern_ = static_cast<int8_t>(pattern);
}

void DisplayDriver::drawCenteredText(const char* text, int16_t y, uint8_t size, uint16_t color) {
  tft_.setTextSize(size);
  // 透明文字底：黑底填充曾把白底二维码页上的标题吞掉
  tft_.setTextColor(color);
  tft_.setTextDatum(MC_DATUM);
  tft_.drawString(text, kPanelSize / 2, y);
}
