#pragma once

#include <Arduino.h>

class Audio;

namespace audio_driver {
class AudioBoard;
class DriverPins;
}  // namespace audio_driver

class AudioOutput {
 public:
  bool begin(bool sdReady);
  void loop();
  void play(const char* file);
  void setVolume(uint8_t volume);
  ~AudioOutput();

 private:
  Audio* audio_ = nullptr;
  audio_driver::DriverPins* pins_ = nullptr;
  audio_driver::AudioBoard* board_ = nullptr;
  bool ready_ = false;
  bool started_ = false;
};
