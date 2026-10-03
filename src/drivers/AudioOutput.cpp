// ============================================================================
// AudioOutput —— ES8311 编解码器 + ESP32-audioI2S 解码器 + 功放管理
// 三个刻意为之、极易被"顺手优化"掉的设计（详见 .h 文件头）：
//   1. 增益结构：软件音量恒为单位增益，电平设在 codec 上（反接会放大量化噪声）
//   2. 切流时静音 codec 而非软件音量（I2S DMA 有 ~190ms 深度，软件切换会"咔"）
//   3. 功放只在刀亮时开，收刃音播完才关（省电 + 消开机爆音 + 无底噪嘶声）
// ============================================================================
#include "AudioOutput.h"

#include <SD_MMC.h>

#include "../../include/HardwareConfig.h"
#include "SdCardDriver.h"

#include <Audio.h>
#include <AudioBoard.h>
#include <DriverPins.h>

namespace {

// codec 需要在 I2S 时钟起来后缓一缓才允许功放推喇叭，否则上电"噗"声可闻
constexpr uint16_t kCodecSettleMs = 200;
// 本板有 PSRAM：解码缓冲放那里，内部堆留给 WiFi/TLS 栈。
// 大的 PSRAM 数值是为了吸收慢速 SD 读：每次切音效都开新文件，
// 缓冲在卡忙时被抽干 = 听到的就是断音。
constexpr int kDecodeBufferPsram = 65536;
constexpr int kDecodeBufferRam = 1600 * 5;

}  // namespace

AudioOutput::~AudioOutput() {
  delete audio_;
  delete board_;
  delete pins_;
}

bool AudioOutput::begin(bool sdReady) {
  if (started_) return ready_;   // 幂等：重复 begin 直接返回
  started_ = true;

  pins_ = new audio_driver::DriverPins();
  // 这里只描述 codec：端口和时钟回落默认值，I2C 总线解析到共享的 Wire 实例
  pins_->addI2C(PinFunction::CODEC, HardwareConfig::I2cScl, HardwareConfig::I2cSda);

  CodecConfig config;
  // 没装麦克风：它馈的 ADC 只会给共享供电添噪声和电流。
  // 保持 ADC_INPUT_NONE 还能把 codec 降到纯解码模式。
  config.input_device = ADC_INPUT_NONE;
  config.output_device = DAC_OUTPUT_ALL;
  config.i2s.bits = BIT_LENGTH_16BITS;
  config.i2s.rate = RATE_48K;
  config.i2s.fmt = I2S_NORMAL;

  // 功放在刀点亮前保持关闭：PA_EN 拉高时无音可播，喇叭只会嘶嘶放着 codec 本底噪声
  pinMode(HardwareConfig::PaEnable, OUTPUT);
  digitalWrite(HardwareConfig::PaEnable, LOW);

  if (!sdReady) {                       // 没卡就没有音源
    Serial.println("[AUDIO] no SD card, audio disabled");
    return false;
  }

  board_ = new audio_driver::AudioBoard(AudioDriverES8311, *pins_);
  if (!board_->begin(config)) {
    Serial.println("[AUDIO] codec init failed, audio disabled");
    return false;
  }
  // 驱动初始化时自带 70% 默认音量（+14.5dB，嘶声的来源）。
  // 初始化后再显式设定目标电平，防止被覆盖；设置加载完成后
  // 控制台的音量设定会接管它。
  board_->setVolume(codecVolume_);

  audio_ = new Audio();
  if (audio_ == nullptr) {              // 堆耗尽
    Serial.println("[AUDIO] out of memory, audio disabled");
    return false;
  }

  audio_->setPinout(HardwareConfig::I2sBck, HardwareConfig::I2sWs, HardwareConfig::I2sDo, -1,
                    HardwareConfig::I2sMck);
  audio_->setBufsize(kDecodeBufferRam, kDecodeBufferPsram);   // 内部堆 + PSRAM 双缓冲
  audio_->setVolume(HardwareConfig::AudioVolume);             // 软件级音量（恒定）
  // 这里只存增益值；biquad 系数由库在每次设采样率时重算（每个文件一次）
  audio_->setTone(HardwareConfig::ToneLowShelf, HardwareConfig::TonePeak,
                  HardwareConfig::ToneHighShelf);

  delay(kCodecSettleMs);   // 等 codec 时钟稳定，消上电"噗"声
  ready_ = true;
  Serial.printf("[AUDIO] ready, codec volume %u%%, software volume %u/21\n",
                HardwareConfig::CodecVolume, HardwareConfig::AudioVolume);
  return ready_;
}

// 每圈主循环调用：喂解码器 → 检查延时解除静音 → 检查延时关功放
void AudioOutput::loop() {
  if (!ready_) return;
  audio_->loop();
  unmuteIfDue();
  updateAmplifier();
}

bool AudioOutput::isRunning() const {
  return ready_ && audio_ != nullptr && audio_->isRunning();
}

void AudioOutput::play(const char* file) {
  if (!ready_) return;
  // 先解析路径再动功放/静音状态机：卡上没有的文件静默跳过，不产生
  // 无意义的功放脉冲与静音-解除循环。裸文件名按 sfx_default → 根目录 →
  // sfx_user 回落（旧 NVS 值与内置碰撞/挥动音效表无需迁移即可继续播放）。
  const String path = SdCardDriver::resolveSoundPath(file);
  if (path.isEmpty()) return;

  if (currentIsHum_) {
    if (audio_->isRunning()) {
      humResumePosition_ = audio_->stopSong();
      humPaused_ = true;
    } else {
      humResumePosition_ = 0;   // 底噪自然播完：下次从头循环
      humPaused_ = false;
    }
    currentIsHum_ = false;
  }

  playPath(path);
}

void AudioOutput::playHum(const char* file) {
  if (!ready_) return;
  const String path = SdCardDriver::resolveSoundPath(file);
  if (path.isEmpty()) return;

  if (path != humPath_) {
    humPath_ = path;
    humResumePosition_ = 0;
    humPaused_ = false;
  } else if (currentIsHum_) {
    if (audio_->isRunning()) return;
    humResumePosition_ = 0;   // 自然播完，按原有逻辑从头循环
    humPaused_ = false;
  }

  const uint32_t resumePosition = humPaused_ ? humResumePosition_ : 0;
  if (playPath(path, resumePosition)) {
    currentIsHum_ = true;
    humPaused_ = false;
    humResumePosition_ = 0;
  }
}

bool AudioOutput::playPath(const String& path, uint32_t resumePosition) {
  // 播音效就隐含"要功放"，无论刀当前什么状态：收刃音也必须听得见
  setAmplifierEnabled(true);
  enableAmplifierNow();

  // 在 codec 端静音，不在软件音量端。I2S DMA 缓着约 190ms 音频：
  // 软件改变要五分之一秒后才被听见——那时流已切换、环形缓冲已在波形
  // 任意位置被冲掉，那就是那声"咔"。codec 的静音带斜坡且直接作用于 DAC。
  board_->setMute(true);
  const bool started = audio_->connecttoFS(SD_MMC, path.c_str(), resumePosition);
  unmuteDue_ = millis() + HardwareConfig::MuteSwitchMs;   // 到点后自动解除静音
  if (!started) {
    Serial.printf("[AUDIO] failed to open sound: %s\n", path.c_str());
  }
  return started;
}

// 库按"满量程的百分比"计数，不是 codec 的：100 是固件允许的最响档，
// 效果音文件刚好在该电平下不削波
void AudioOutput::setVolume(uint8_t percent) {
  const uint8_t codec =
      static_cast<uint8_t>((static_cast<uint16_t>(percent) * HardwareConfig::MaxCodecVolume) / 100);
  codecVolume_ = codec;
  if (ready_) board_->setVolume(codec);
}

void AudioOutput::setAmplifierEnabled(bool enabled) {
  amplifierWanted_ = enabled;
  if (enabled) amplifierOffDue_ = 0;   // 重新要功放：取消挂起的关闭
}

void AudioOutput::enableAmplifierNow() {
  if (amplifierEnabled_) return;
  digitalWrite(HardwareConfig::PaEnable, HIGH);
  delay(HardwareConfig::PaSettleMs);   // 【改动④协同】功放稳定窗：浪涌先走完
  amplifierEnabled_ = true;
  amplifierOffDue_ = 0;
}

void AudioOutput::unmuteIfDue() {
  if (unmuteDue_ == 0) return;
  if (static_cast<long>(millis() - unmuteDue_) < 0) return;   // 未到期
  unmuteDue_ = 0;
  // es8311_mute() 把 DAC 音量寄存器清零，而驱动的解除静音不会恢复它——
  // 所以必须先恢复音量再解除静音：否则第一次切流后 codec 会
  // 一直哑着 95dB，直到重启。
  board_->setVolume(codecVolume_);
  board_->setMute(false);
}

// 关功放必须等正在播的播完，否则收刃音的尾巴被切。
// 解码器停止后会给 DAC 喂零，之后再切 PA_EN 就是无声的。
void AudioOutput::updateAmplifier() {
  if (amplifierWanted_ || !amplifierEnabled_) return;   // 还要功放 / 已关闭

  if (amplifierOffDue_ == 0) {          // 第一阶段：等播完
    if (audio_->isRunning()) return;
    amplifierOffDue_ = millis() + HardwareConfig::PaSettleMs;   // 第二阶段：稳定延时后断电
    return;
  }

  if (static_cast<long>(millis() - amplifierOffDue_) < 0) return;   // 未到期
  digitalWrite(HardwareConfig::PaEnable, LOW);
  amplifierEnabled_ = false;
  amplifierOffDue_ = 0;
}
