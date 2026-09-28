#pragma once

#include <TFT_eSPI.h>

#include "../../include/AppTypes.h"

// ============================================================================
// DisplayDriver —— GC9A01 圆屏（240×240）：动画眼睛 + WiFi 二维码
// ============================================================================
// 渲染方式：整只眼睛是一块 RGB565 素材（tools/make_eye_texture.py 生成），
// 逐行 blit 进全屏精灵图。两个前提让这件事很便宜：
//   1. 素材合成在纯黑底上 → 透明角无需遮罩；
//   2. 屏幕底色也是黑 → 行可以平移/压缩而不必逐像素运算。
//
// 为什么走精灵图整帧推送：直接重绘屏幕会闪——一帧 240×240 走 SPI 的耗时
// 超过渲染间隔，屏幕永远被抓在"推到一半"的状态。精灵图还带来一个好处：
// 眼睛不动时可以整帧跳过 SPI 推送，把 CPU 让给音频解码器。
// ============================================================================
class DisplayDriver {
 public:
  void begin();

  // 故意与 begin() 分开：背光涌浪电流要避开屏幕和 SD 卡的初始化窗口，
  // 而且没人想看一块还没初始化好的屏幕。【改动① 的入口】
  void enableBacklight();

  void showBoot(bool ready);                       // 开机画面（绿 READY / 红硬件自检）
  void showQr(const char* title, const char* url); // 二维码页（配网入口）

  // 重跑屏幕的复位序列 + 厂商寄存器表。本板 GC9A01 没接复位线（TFT_RST=-1），
  // 串行时序一旦被打断，软件层面无法恢复，花屏会一直留到断电。
  // 等启动阶段最耗时的部件都跑完后再调一次，修复这类花屏。
  // TFT_eSPI 的第二次 init() 会跳过总线配置、只重复复位和寄存器表，可安全重入。
  void repairPanel();

  // lookX/lookY 是 [-1,1] 的归一化视线目标；驱动内部做平滑、
  // 加待机游移和眨眼，且只在画面真的变化时才推帧。
  void drawEye(float lookX, float lookY, EyePattern pattern);

 private:
  // 当前屏幕归属。眼睛只需在"被别的画面盖过"时清屏；
  // 二维码很贵，地址没变就整帧跳过。
  enum class Panel : uint8_t { Eye, Boot, Qr };

  static constexpr int16_t kPanelSize = 240;       // 面板边长（正方形缓冲）
  static constexpr int16_t kEyeCenterX = kPanelSize / 2;   // 眼球中心
  static constexpr int16_t kEyeCenterY = kPanelSize / 2;
  // 眼球能滑离中心多远：水平方向满偏时眼球边缘正好压在屏边（视线不出屏）；
  // 垂直方向留小些——屏幕是圆的，上下空间少。
  static constexpr int16_t kEyeTravelX = 20;
  static constexpr int16_t kEyeTravelY = 14;

  // 精灵图分配失败（固件最大的单次内存申请）按此周期重试，
  // 而不是让眼睛黑屏一整个会话。
  static constexpr unsigned long kCanvasRetryMs = 1000;

  bool ensureCanvas(unsigned long now);            // 精灵图懒分配 + 失败重试
  void paintEye(int16_t offsetX, int16_t offsetY, float openness);    // 画眼
  void paintBrows(int16_t offsetX, int16_t offsetY, float openness);  // 生气眉毛

  void updateGaze(unsigned long now, float targetX, float targetY, bool attentive);
  void updateBlink(unsigned long now);             // 眨眼相位状态机

  // 与上一推送帧差异足够大才值得重绘 + 占用 SPI，省 CPU。
  bool frameChanged(float openness, EyePattern pattern) const;
  void markFramePushed(float openness, EyePattern pattern);   // 记录已推送帧基线

  void drawCenteredText(const char* text, int16_t y, uint8_t size, uint16_t color);

  TFT_eSPI tft_;                                   // 总线与面板
  TFT_eSprite canvas_{&tft_};                      // 全屏精灵图（离屏合成）
  bool canvasReady_ = false;                       // 精灵图是否可用
  unsigned long canvasRetry_ = 0;                  // 上次分配失败的时刻

  Panel panel_ = Panel::Boot;
  String qrUrl_;   // 上次渲染的地址：没变就不重画（二维码绘制阻塞数十 ms）

  float gazeX_ = 0.0f;         // 平滑后的视线（-1..1）
  float gazeY_ = 0.0f;
  float idleTargetX_ = 0.0f;   // 待机游移的目标点
  float idleTargetY_ = 0.0f;
  float idleX_ = 0.0f;         // 待机游移的当前值（低通后）
  float idleY_ = 0.0f;
  float openness_ = 1.0f;      // 眼睛开合度（1 全开，眨眼时下降）

  bool blinking_ = false;                    // 眨眼进行中
  unsigned long blinkPhaseStart_ = 0;        // 眨眼相位起点
  unsigned long blinkDue_ = 0;               // 下次眨眼的随机间隔
  unsigned long blinkTimer_ = 0;             // 上次眨眼结束时刻
  unsigned long idleTimer_ = 0;              // 待机游移节拍
  unsigned long idleDue_ = 0;

  float pushedGazeX_ = 2.0f;    // 上一推送帧的基线（初值取界外，保证首帧必画）
  float pushedGazeY_ = 2.0f;
  float pushedOpenness_ = -1.0f;
  int8_t pushedPattern_ = -1;
};
