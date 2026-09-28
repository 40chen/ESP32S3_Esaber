#pragma once

#include <Arduino.h>

#include "../../include/HardwareConfig.h"

class Audio;

namespace audio_driver {
class AudioBoard;
class DriverPins;
}  // namespace audio_driver

// ES8311 codec plus the ESP32-audioI2S decoder.
//
// Three things here are deliberate and easy to undo by accident:
//
//   * The gain structure.  The software volume stays at unity and the level is
//     set on the codec, because the reverse arrangement (attenuate in software,
//     boost in the codec) amplifies the truncation noise of the attenuation by
//     exactly as much as it boosts the signal.
//   * Stream switches mute the codec rather than the software volume.  The I2S
//     DMA holds around 190 ms of audio, so a software change is heard a fifth
//     of a second later -- long after the stream has been swapped and the DMA
//     flushed mid waveform, which is what a click is.
//   * The amplifier is only powered while the blade is on.  PA_EN left high
//     makes it amplify the codec's own noise floor, audible as hiss in a quiet
//     room.
class AudioOutput {
 public:
  bool begin(bool sdReady);
  void loop();
  void play(const char* file);

  // True while any stream (effect or hum) is on the air.  The saber uses the
  // falling edge to re-arm the hum loop, which is what makes the hum work
  // with any file duration instead of a baked-in one.
  bool isRunning() const;

  // Audio level in percent, 0..100, where 100 is the loudest the firmware
  // allows.  Applied to the codec, so it takes effect immediately.
  void setVolume(uint8_t percent);

  // The amplifier follows the blade: on while the saber is lit, off once the
  // retraction sound has finished.
  void setAmplifierEnabled(bool enabled);
  ~AudioOutput();

 private:
  void enableAmplifierNow();
  void unmuteIfDue();
  void updateAmplifier();

  Audio* audio_ = nullptr;
  audio_driver::DriverPins* pins_ = nullptr;
  audio_driver::AudioBoard* board_ = nullptr;
  bool ready_ = false;
  bool started_ = false;

  // Codec DAC volume in the codec's own units, not the console's percentage.
  uint8_t codecVolume_ = HardwareConfig::CodecVolume;

  bool amplifierEnabled_ = false;
  bool amplifierWanted_ = false;
  // millis() deadline for the pending unmute, 0 when nothing is pending.
  unsigned long unmuteDue_ = 0;
  // millis() deadline for switching the amplifier off once the codec is muted.
  unsigned long amplifierOffDue_ = 0;
};
