// HAGIWO MOD2 Claves Ver1.1 (modulove/MOD2, Firmware/claves/claves.ino), CC0 1.0 Universal: a
// claves from a sine/triangle morph under an exponential decay, pitch on V/oct. Its engine is kept as
// published, at its own 36.6 kHz with 10-bit PWM. As in the original, a trigger waits 5 ms for the CV
// to settle before reading the controls; the original then renders all 8192 samples before playing,
// where here onTrigger()'s loop body runs one sample at a time, so the samples are the same without
// the render's wait.
// The port (the class around it) is MIT, Copyright (c) 2026 David Baghurst.

#ifndef MOD2_VOICES_CLAVES_H
#define MOD2_VOICES_CLAVES_H

#include "../voice.h"

namespace mod2 {

class Claves final : public Voice {
  static constexpr uint16_t TABLE_SZ = 8192;
  static constexpr uint8_t LUT_SZ = 64;
  static constexpr uint8_t LUT_MASK = LUT_SZ - 1;
  static constexpr float PWM_FS = 1023.0f;
  static constexpr float PWM_MID = PWM_FS / 2.0f;
  static constexpr float AUDIO_FS = FS_36K6;
  static constexpr float PH_INC_K = float(LUT_SZ) / AUDIO_FS;
  static constexpr float CV_FULL_V = 5.0f;
  static constexpr float PITCH_F0 = 50.0f;
  static constexpr float PITCH_MAX_HZ = 1500.0f;
  static constexpr uint32_t SETTLE = uint32_t(0.005f * AUDIO_FS);  // delay(5)

 public:
  float Rate() const override { return AUDIO_FS; }
  uint16_t Wrap() const override { return 1023; }

  void Init() override {
    for (uint8_t i = 0; i < LUT_SZ; ++i) {
      float th = (float)(2.0 * PI_D * i / LUT_SZ);
      sineLUT[i] = sinf(th);
      triLUT[i] = (float)((2.0 / PI_D) * (double)asinf(sineLUT[i]));  // Triangle via inverse sine
    }
  }

  // onTrigger(): stop, wait 5 ms, then read the controls and start
  void Trigger() override {
    playing = false;
    settle = SETTLE;
  }
  void Button(bool pressed, uint32_t heldMs) override {
    (void)heldMs;
    if (pressed) Trigger();
  }

  uint16_t Render() override {
    if (settle && --settle == 0) Start();
    if (!playing) {
      led = 0.0f;
      return (uint16_t)PWM_MID;
    }
    // onTrigger()'s table loop for sample tblIdx
    uint16_t i = tblIdx;
    uint8_t idx = uint8_t(ph) & LUT_MASK;
    uint8_t idx2 = (idx + 1) & LUT_MASK;
    float frac = ph - uint8_t(ph);
    float s = sineLUT[idx] * (1 - frac) + sineLUT[idx2] * frac;
    float t = triLUT[idx] * (1 - frac) + triLUT[idx2] * frac;
    float w = s * (1 - waveMorph) + t * waveMorph;
    float y = w * env;
    float envValue = env;
    env *= expK;
    if (i >= fadeStart) {
      float mu = (i - fadeStart) * invFadeLen;
      float mu2 = 0.5f * (1 - cosf((float)((double)mu * PI_D)));
      y *= (1 - mu2);
      envValue *= (1 - mu2);
    }
    uint16_t out = uint16_t((y + 1.0f) * (PWM_FS / 2.0f));  // finalTbl[i]
    led = uint16_t(envValue * PWM_FS) / PWM_FS;              // envTbl[i]
    ph += phInc;
    if (ph >= LUT_SZ) ph -= LUT_SZ;
    if (++tblIdx >= TABLE_SZ) {
      playing = false;
      tblIdx = 0;
    }
    return out;
  }

  bool Active() const override { return playing || settle; }
  float Led() const override { return led; }
  void Label(char *out) const override { strcpy(out, "CLAVES"); }

 private:
  void Start() {
    float decayRate = 1.0f + 9.0f * panel.a[0];  // 1-10 range
    waveMorph = panel.a[1];                      // 0-1
    float adc = 1.0f - panel.a[2];               // Invert: the CV and POT3 up, pitch up
    float cvV = adc * CV_FULL_V;
    float baseFreq = PITCH_F0 * powf(2.0f, cvV);  // 1 V/Oct mapping
    if (baseFreq > PITCH_MAX_HZ) baseFreq = PITCH_MAX_HZ;

    const float invN = 1.0f / (TABLE_SZ - 1);
    expK = expf(-decayRate * invN);  // Per-sample decay
    env = 1.0f;
    phInc = baseFreq * PH_INC_K;
    ph = 0.0f;
    tblIdx = 0;
    playing = true;
  }

  const uint16_t fadeStart = uint16_t(TABLE_SZ * 0.95f);  // Last 5 % tail fade
  const float invFadeLen = 1.0f / (TABLE_SZ - 1 - fadeStart);

  float sineLUT[LUT_SZ];
  float triLUT[LUT_SZ];
  bool playing = false;
  uint32_t settle = 0;
  uint16_t tblIdx = 0;
  float waveMorph = 0.0f;
  float expK = 1.0f, env = 1.0f, phInc = 0.0f, ph = 0.0f;
  float led = 0.0f;
};

}  // namespace mod2

#endif
