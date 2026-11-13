// HAGIWO MOD2 HiHat Ver1.1 (modulove/MOD2, Firmware/hihat/hihat.ino), CC0 1.0 Universal: a hi-hat
// from a table of blue or white noise through a band-pass filter. Its engine is kept as published, at
// its own 36.6 kHz with 10-bit PWM. The original renders a whole hit into a 30 000-sample buffer in
// loop() and plays it back; here buildVoice()'s loop body runs one sample at a time as the ISR plays
// it, so the samples are the same and the hit starts at once.
// The port (the class around it) is MIT, Copyright (c) 2026 David Baghurst.

#ifndef MOD2_VOICES_HIHAT_H
#define MOD2_VOICES_HIHAT_H

#include "../voice.h"

namespace mod2 {

class HiHat final : public Voice {
  static constexpr float AUDIO_FS = FS_36K6;
  static constexpr uint32_t TABLE_SZ = 30000;
  static constexpr float PWM_FS = 1023.0f;
  static constexpr float PWM_MID = PWM_FS / 2.0f;
  static constexpr float AMP_SCALE = 3.5f;
  static constexpr float MASTER_ATTEN = 0.8f;
  static constexpr uint16_t FADE_IN_SMP = 73;
  static constexpr uint16_t FADE_OUT_SMP = 40;
  static constexpr uint32_t LONG_PRESS_MS = 500;

 public:
  float Rate() const override { return AUDIO_FS; }
  uint16_t Wrap() const override { return 1023; }

  void Init() override { updateNoiseTable(); }

  // triggerISR(), then loop()'s pot reading and buildVoice()'s set-up
  void Trigger() override {
    volFactor = panel.in2 ? 0.5f : 1.0f;
    float norm0 = 1.0f - panel.a[0];
    float norm1 = 1.0f - panel.a[1];
    float norm2 = 1.0f - panel.a[2];
    float decayBase = 0.1f + 9.0f * norm0;
    float decayCurve = 0.2f + 5.0f * norm1;
    float fc = 100.0f + 15900.0f * norm2;

    const float Q = 0.8f;
    float w0 = (float)(2.0 * PI_D * (double)fc / (double)AUDIO_FS);
    float sw = sinf(w0), cw = cosf(w0);
    float alpha = sw / (2.0f * Q);
    b0 = Q * alpha;
    b1 = 0.0f;
    b2 = -Q * alpha;
    float a0 = 1.0f + alpha;
    a1 = -2.0f * cw;
    a2 = 1.0f - alpha;
    float ia0 = 1.0f / a0;
    b0 *= ia0;
    b1 *= ia0;
    b2 *= ia0;
    a1 *= ia0;
    a2 *= ia0;
    x1 = x2 = y1 = y2 = 0.0f;
    env = 1.0f;
    expK = expf(-decayBase * decayCurve / TABLE_SZ);
    idxHH = 0;
    playingHH = true;
  }
  // buttonISR(): a short press triggers on release, a long one (500 ms) swaps blue and white noise
  void Button(bool pressed, uint32_t heldMs) override {
    if (pressed) return;
    if (heldMs < LONG_PRESS_MS) {
      Trigger();
    } else {
      noiseMode ^= 1;
      reqNoiseUpdate = true;
    }
  }
  // loop(): the new table is built here, while the ISR may be playing the old one, as in the original
  void Background() override {
    if (reqNoiseUpdate) {
      reqNoiseUpdate = false;
      updateNoiseTable();
    }
  }

  // buildVoice()'s loop body for sample idxHH, then on_pwm_wrap()'s output
  uint16_t Render() override {
    if (!playingHH) {
      led = 0.0f;
      return (uint16_t)PWM_MID;
    }
    uint32_t i = idxHH;
    float x0 = noiseTbl[i] * env;
    float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x0;
    y2 = y1;
    y1 = y0;
    float fade = 1.2f;
    if (i < FADE_IN_SMP) fade = i / float(FADE_IN_SMP);
    else if (i > TABLE_SZ - FADE_OUT_SMP - 1) fade = (TABLE_SZ - i - 1) / float(FADE_OUT_SMP);
    y0 *= fade;
    led = Clamp(uint16_t(env * fade * PWM_FS) / PWM_FS, 0.0f, 1.0f);  // envTbl[i]
    int16_t mix = int16_t(Clamp(y0, -1.0f, 1.0f) * PWM_MID * MASTER_ATTEN);  // outHH[i]
    env *= expK;

    int32_t val = PWM_MID + int32_t(mix * AMP_SCALE * volFactor);
    if (val < 0) val = 0;
    else if (val > 1023) val = 1023;
    if (++idxHH >= TABLE_SZ) {
      playingHH = false;
      idxHH = 0;
    }
    return (uint16_t)val;
  }

  bool Active() const override { return playingHH; }
  float Led() const override { return led; }
  void Label(char *out) const override { strcpy(out, noiseMode ? "HAT WHITE" : "HAT BLUE"); }

 private:
  void generateBlueNoise() {
    float prev = 2.0f * (rand() / (float)RAND_MAX_NEWLIB) - 1.0f;
    noiseTbl[0] = prev * 0.5f;
    for (uint32_t i = 1; i < TABLE_SZ; ++i) {
      float w = 2.0f * (rand() / (float)RAND_MAX_NEWLIB) - 1.0f;
      float b = (w - prev) * 0.5f;
      prev = w;
      noiseTbl[i] = Clamp(b, -1.0f, 1.0f);
    }
  }
  void generateWhiteNoise() {
    for (uint32_t i = 0; i < TABLE_SZ; ++i) noiseTbl[i] = 2.0f * (rand() / (float)RAND_MAX_NEWLIB) - 1.0f;
  }
  void updateNoiseTable() {
    if (noiseMode == 0) generateBlueNoise();
    else generateWhiteNoise();
  }

  // the original seeds from a floating ADC pin: any fixed seed is as good. A swap of noise draws
  // the next numbers from the same sequence, as it does there.
  NewlibRand rand{0x48484154u};
  float noiseTbl[TABLE_SZ];
  bool playingHH = false;
  uint32_t idxHH = 0;
  float volFactor = 1.0f;
  volatile uint8_t noiseMode = 0;  // 0: Blue, 1: White
  volatile bool reqNoiseUpdate = false;
  float led = 0.0f;
  // buildVoice()'s state, kept between samples
  float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, expK = 1.0f, env = 1.0f;
  float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

}  // namespace mod2

#endif
