#include "AudioOutput.h"

#include <SD_MMC.h>

#include "../../include/HardwareConfig.h"

#include <Audio.h>
#include <AudioBoard.h>
#include <DriverPins.h>

namespace {

// The codec needs a moment after the I2S clocks start before the amplifier is
// allowed to drive the speaker, otherwise the power-on thump is audible.
constexpr uint16_t kCodecSettleMs = 200;
// PSRAM is present on this board, so the decode buffer lives there and the
// internal heap stays free for WiFi and the TLS stacks.
constexpr int kDecodeBufferPsram = 32768;
constexpr int kDecodeBufferRam = 1600 * 5;

}  // namespace

AudioOutput::~AudioOutput() {
  delete audio_;
  delete board_;
  delete pins_;
}

bool AudioOutput::begin(bool sdReady) {
  if (started_) return ready_;
  started_ = true;

  pins_ = new audio_driver::DriverPins();
  // Only the codec is described here: port and clock fall back to the defaults
  // and the bus resolves to the shared Wire instance.
  pins_->addI2C(PinFunction::CODEC, HardwareConfig::I2cScl, HardwareConfig::I2cSda);

  CodecConfig config;
  config.input_device = ADC_INPUT_ALL;
  config.output_device = DAC_OUTPUT_ALL;
  config.i2s.bits = BIT_LENGTH_16BITS;
  config.i2s.rate = RATE_48K;
  config.i2s.fmt = I2S_NORMAL;

  // Leave the amplifier off entirely when there is nothing to play: driving
  // PA_ENABLE into an unconfigured codec only makes a thump.
  pinMode(HardwareConfig::PaEnable, OUTPUT);
  digitalWrite(HardwareConfig::PaEnable, LOW);

  if (!sdReady) {
    Serial.println("[AUDIO] no SD card, audio disabled");
    return false;
  }

  board_ = new audio_driver::AudioBoard(AudioDriverES8311, *pins_);
  if (!board_->begin(config)) {
    Serial.println("[AUDIO] codec init failed, audio disabled");
    return false;
  }

  audio_ = new Audio();
  if (audio_ == nullptr) {
    Serial.println("[AUDIO] out of memory, audio disabled");
    return false;
  }

  audio_->setPinout(HardwareConfig::I2sBck, HardwareConfig::I2sWs, HardwareConfig::I2sDo, -1,
                    HardwareConfig::I2sMck);
  audio_->setBufsize(kDecodeBufferRam, kDecodeBufferPsram);
  audio_->setVolume(HardwareConfig::AudioVolume);

  delay(kCodecSettleMs);
  digitalWrite(HardwareConfig::PaEnable, HIGH);
  ready_ = true;
  return ready_;
}

void AudioOutput::loop() {
  if (ready_) audio_->loop();
}

void AudioOutput::setVolume(uint8_t volume) {
  if (ready_) audio_->setVolume(volume);
}

void AudioOutput::play(const char* file) {
  if (ready_) audio_->connecttoFS(SD_MMC, file);
}
