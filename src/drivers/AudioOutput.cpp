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
// internal heap stays free for WiFi and the TLS stacks.  The larger PSRAM
// figure is what absorbs a slow SD read: every effect switch opens a new file,
// and a buffer that drains while the card is busy is heard as a dropout.
constexpr int kDecodeBufferPsram = 65536;
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
  // No microphone is fitted, and the ADC it would feed only adds noise and
  // current to the shared supply.  Leaving it at ADC_INPUT_NONE also drops the
  // codec into decode-only mode.
  config.input_device = ADC_INPUT_NONE;
  config.output_device = DAC_OUTPUT_ALL;
  config.i2s.bits = BIT_LENGTH_16BITS;
  config.i2s.rate = RATE_48K;
  config.i2s.fmt = I2S_NORMAL;

  // The amplifier stays off until the blade is lit: with PA_EN high there is
  // nothing to play and the speaker hisses at the codec's noise floor.
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
  // The driver applies its own default of 70% on init, which is +14.5 dB and
  // where the hiss came from.  Set the intended level explicitly, after init so
  // it cannot be overwritten again; the console's volume setting replaces it
  // once the settings have been loaded.
  board_->setVolume(codecVolume_);

  audio_ = new Audio();
  if (audio_ == nullptr) {
    Serial.println("[AUDIO] out of memory, audio disabled");
    return false;
  }

  audio_->setPinout(HardwareConfig::I2sBck, HardwareConfig::I2sWs, HardwareConfig::I2sDo, -1,
                    HardwareConfig::I2sMck);
  audio_->setBufsize(kDecodeBufferRam, kDecodeBufferPsram);
  audio_->setVolume(HardwareConfig::AudioVolume);
  // Only the gains are stored here; the biquad coefficients are recalculated
  // by the library every time the sample rate is set, which happens per file.
  audio_->setTone(HardwareConfig::ToneLowShelf, HardwareConfig::TonePeak,
                  HardwareConfig::ToneHighShelf);

  delay(kCodecSettleMs);
  ready_ = true;
  Serial.printf("[AUDIO] ready, codec volume %u%%, software volume %u/21\n",
                HardwareConfig::CodecVolume, HardwareConfig::AudioVolume);
  return ready_;
}

void AudioOutput::loop() {
  if (!ready_) return;
  audio_->loop();
  unmuteIfDue();
  updateAmplifier();
}

void AudioOutput::play(const char* file) {
  if (!ready_) return;

  // Playing a sound implies the amplifier is wanted, whichever blade state the
  // caller is in: the retraction sound has to be heard too.
  setAmplifierEnabled(true);
  enableAmplifierNow();

  // Mute at the codec, not at the software volume.  The I2S DMA holds roughly
  // 190 ms of audio, so a software change would only be heard a fifth of a
  // second later -- by which time the stream has been swapped and the ring
  // buffer flushed at whatever point the waveform happened to be at, which is
  // the click.  The codec's mute ramps and acts on the DAC immediately.
  board_->setMute(true);
  audio_->connecttoFS(SD_MMC, file);
  unmuteDue_ = millis() + HardwareConfig::MuteSwitchMs;
}

// The library counts percent of the ceiling, not of the codec: 100 is the
// loudest the firmware allows, which is where the effect files still fit
// without clipping.
void AudioOutput::setVolume(uint8_t percent) {
  const uint8_t codec =
      static_cast<uint8_t>((static_cast<uint16_t>(percent) * HardwareConfig::MaxCodecVolume) / 100);
  codecVolume_ = codec;
  if (ready_) board_->setVolume(codec);
}

void AudioOutput::setAmplifierEnabled(bool enabled) {
  amplifierWanted_ = enabled;
  if (enabled) amplifierOffDue_ = 0;
}

void AudioOutput::enableAmplifierNow() {
  if (amplifierEnabled_) return;
  digitalWrite(HardwareConfig::PaEnable, HIGH);
  delay(HardwareConfig::PaSettleMs);
  amplifierEnabled_ = true;
  amplifierOffDue_ = 0;
}

void AudioOutput::unmuteIfDue() {
  if (unmuteDue_ == 0) return;
  if (static_cast<long>(millis() - unmuteDue_) < 0) return;
  unmuteDue_ = 0;
  // es8311_mute() writes the DAC volume register to zero and the driver's
  // unmute never puts it back, so the level has to be restored *before* the
  // mute is lifted -- otherwise the first stream switch leaves the codec 95 dB
  // down for the rest of the session.
  board_->setVolume(codecVolume_);
  board_->setMute(false);
}

// Turning the amplifier off has to wait for whatever is playing to finish, or
// the tail of the retraction sound is cut off.  The decoder leaves the DAC
// fed with zeros once it stops, so cutting PA_EN afterwards is quiet.
void AudioOutput::updateAmplifier() {
  if (amplifierWanted_ || !amplifierEnabled_) return;

  if (amplifierOffDue_ == 0) {
    if (audio_->isRunning()) return;
    amplifierOffDue_ = millis() + HardwareConfig::PaSettleMs;
    return;
  }

  if (static_cast<long>(millis() - amplifierOffDue_) < 0) return;
  digitalWrite(HardwareConfig::PaEnable, LOW);
  amplifierEnabled_ = false;
  amplifierOffDue_ = 0;
}
