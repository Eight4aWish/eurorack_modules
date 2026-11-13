// HAGIWO MOD2 Clap Ver1.1 (modulove/MOD2, Firmware/clap/clap.ino), CC0 1.0 Universal: a
// TR-808-style hand clap, two gated noise bursts and an exponential tail through a band-pass filter.
// Its engine is kept as published, at its own 36.6 kHz with 10-bit PWM. The original renders a whole
// hit into a 22 000-sample buffer when triggered (in loop(), so about 20 ms late) and plays it back;
// here buildClap()'s loop body runs one sample at a time as the ISR plays it, so the samples are the
// same and the hit starts at once. The noise table is still made once, from rand(), as setup() made it.
// The port (the class around it) is MIT, Copyright (c) 2026 David Baghurst.

#ifndef MOD2_VOICES_CLAP_H
#define MOD2_VOICES_CLAP_H

#include "../voice.h"

namespace mod2 {

class Clap final : public Voice {
  static constexpr float AUDIO_FS = FS_36K6;  // SYS_CLOCK / 4096
  static constexpr uint32_t TABLE_SZ = 22000;
  static constexpr float PWM_FS = 1023.0f;
  static constexpr float PWM_MID = PWM_FS / 2.0f;
  static constexpr float AMP_SCALE = 3.5f;
  static constexpr float MASTER_ATTEN = 0.8f;
  static constexpr uint16_t FADE_IN_SMP = 70;
  static constexpr uint16_t FADE_OUT_SMP = 40;
  static constexpr float DECAY_CURVE = 2.0f;
  static constexpr uint8_t BURSTS = 3;

 public:
  float Rate() const override { return AUDIO_FS; }
  uint16_t Wrap() const override { return 1023; }

  void Init() override {
    NewlibRand rand(0x4D4F4432u);  // the original seeds from a floating ADC pin: any fixed seed is as good
    for (uint32_t i = 0; i < TABLE_SZ; ++i) noiseTbl[i] = 2.0f * (rand() / (float)RAND_MAX_NEWLIB) - 1.0f;
  }

  // triggerISR(), then loop()'s pot reading and buildClap()'s set-up
  void Trigger() override {
    volFactor = panel.in2 ? 0.5f : 1.0f;
    float rawA0 = panel.a[0];         // Q control
    float rawA1 = panel.a[1];         // Decay time control
    float norm2 = 1.0f - panel.a[2];  // Fc inverted mapping
    float bpfQ = 0.5f + 3.5f * rawA0;
    float decayMs = 20.0f + 180.0f * rawA1;
    float fc = 50.0f + 7950.0f * norm2;

    // 2-pole band-pass filter coefficients (Cookbook)
    const float Q = bpfQ;
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

    const float tau_s = decayMs / 1000.0f;
    expK = expf(-1.0f / (tau_s * AUDIO_FS));

    nextOn = 0;
    nextOff = BURST_LEN;
    current = 0;
    gateOn = false;
    tailActive = false;
    env = 0.0f;
    x1 = x2 = y1 = y2 = 0.0f;
    idxClap = 0;
    playingClap = true;
  }
  // manualButtonISR(): a trigger on the press
  void Button(bool pressed, uint32_t heldMs) override {
    (void)heldMs;
    if (pressed) Trigger();
  }

  // buildClap()'s loop body for sample idxClap, then on_pwm_wrap()'s output
  uint16_t Render() override {
    if (!playingClap) {
      led = 0.0f;
      return (uint16_t)PWM_MID;
    }
    uint32_t i = idxClap;
    if (i == nextOn && current < BURSTS) {
      if (current < BURSTS - 1) {  // First two bursts: 4 ms gate
        gateOn = true;
        nextOff = i + BURST_LEN;
      } else {  // Final burst: exponential tail
        tailActive = true;
        env = 1.0f;
      }
      current++;
      nextOn += PULSE_INTERVAL;
    }
    if (gateOn && i >= nextOff) gateOn = false;
    float curEnv = 0.0f;
    if (gateOn) {
      curEnv = 1.0f;
    } else if (tailActive) {
      curEnv = powf(env, DECAY_CURVE);
      env *= expK;
    }
    float x0 = noiseTbl[i] * curEnv * volFactor;
    float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x0;
    y2 = y1;
    y1 = y0;
    float fade = 1.0f;
    if (i < FADE_IN_SMP) fade = i / float(FADE_IN_SMP);
    else if (i > TABLE_SZ - FADE_OUT_SMP - 1) fade = (TABLE_SZ - i - 1) / float(FADE_OUT_SMP);
    y0 *= fade;
    led = uint16_t(curEnv * fade * PWM_FS) / PWM_FS;  // envTbl[i], the LED's brightness
    int16_t mix = int16_t(Clamp(y0, -1.0f, 1.0f) * PWM_MID * MASTER_ATTEN);  // outClap[i]

    int32_t val = PWM_MID + int32_t(mix * AMP_SCALE);
    if (val < 0) val = 0;
    else if (val > 1023) val = 1023;
    if (++idxClap >= TABLE_SZ) {
      playingClap = false;
      idxClap = 0;
    }
    return (uint16_t)val;
  }

  bool Active() const override { return playingClap; }
  float Led() const override { return led; }
  void Label(char *out) const override { strcpy(out, "CLAP"); }

 private:
  const uint32_t PULSE_INTERVAL = uint32_t(0.015f * AUDIO_FS);  // 15 ms gap
  const uint32_t BURST_LEN = uint32_t(0.004f * AUDIO_FS);       // 4 ms gate

  float noiseTbl[TABLE_SZ];
  bool playingClap = false;
  uint32_t idxClap = 0;
  float volFactor = 1.0f;
  float led = 0.0f;
  // buildClap()'s state, kept between samples
  float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, expK = 0;
  uint32_t nextOn = 0, nextOff = 0;
  uint8_t current = 0;
  bool gateOn = false, tailActive = false;
  float env = 0.0f;
  float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

}  // namespace mod2

#endif
