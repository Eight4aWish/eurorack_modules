// The noise schedule as a Teensy Audio library object: two inputs (the codec's), two outputs.
// Built at 48 kHz with 48-sample blocks (platformio.ini, the noise_* envs).
#pragma once
#include <Audio.h>
#include "noise_schedule.h"

#if AUDIO_BLOCK_SAMPLES != 48
#error "the noise test runs 48-sample blocks"
#endif

class NoiseStream : public AudioStream {
 public:
  explicit NoiseStream(noise::Player *p) : AudioStream(2, inputs_), player_(p) {}

  void update() override {
    audio_block_t *a = receiveReadOnly(0), *b = receiveReadOnly(1);
    audio_block_t *l = allocate(), *r = allocate();
    if (l && r) {
      for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        in_[0][i] = a ? a->data[i] * (1.f / 32768.f) : 0.f;
        in_[1][i] = b ? b->data[i] * (1.f / 32768.f) : 0.f;
      }
      player_->Process(in_[0], in_[1], out_[0], out_[1], AUDIO_BLOCK_SAMPLES);
      for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        l->data[i] = Clip(out_[0][i]);
        r->data[i] = Clip(out_[1][i]);
      }
      transmit(l, 0);
      transmit(r, 1);
    }
    if (l) release(l);
    if (r) release(r);
    if (a) release(a);
    if (b) release(b);
  }

 private:
  static int16_t Clip(float v) {
    v *= 32767.f;
    return (int16_t)(v > 32767.f ? 32767.f : v < -32768.f ? -32768.f : v);
  }
  audio_block_t *inputs_[2];
  noise::Player *player_;
  float in_[2][AUDIO_BLOCK_SAMPLES], out_[2][AUDIO_BLOCK_SAMPLES];
};
