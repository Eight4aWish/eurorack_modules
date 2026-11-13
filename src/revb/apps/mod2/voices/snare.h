// HAGIWO MOD2 LFSR Snare Ver1.0 (modulove/MOD2, Firmware/snare/snare.ino), CC0 1.0 Universal:
// a retro-game-style one-shot snare from a 15-bit LFSR. Its engine is kept as published, at its
// own 31.25 kHz with 8-bit PWM: setup_luts(), start_snare_from_request(), render_snare_sample().
// The port (the class around it) is MIT, Copyright (c) 2026 David Baghurst.

#ifndef MOD2_VOICES_SNARE_H
#define MOD2_VOICES_SNARE_H

#include "../voice.h"

namespace mod2 {

class Snare final : public Voice {
  static constexpr uint32_t SAMPLE_RATE = 31250;
  static constexpr uint16_t PWM_WRAP = 255;
  static constexpr float OUTPUT_GAIN = 0.92f;
  static constexpr int ADC_LUT_SIZE = 4096;
  static constexpr int EXP2_LUT_SIZE = 257;
  static constexpr float DECAY_MIN_SECONDS = 0.015f;
  static constexpr float DECAY_MAX_SECONDS = 0.400f;
  static constexpr uint32_t IN2_HOLD_MS = 15;
  static constexpr uint32_t IN2_GRACE_SAMPLES = (SAMPLE_RATE * IN2_HOLD_MS) / 1000;

 public:
  float Rate() const override { return SAMPLE_RATE; }
  uint16_t Wrap() const override { return PWM_WRAP; }
  void Init() override { setup_luts(); }

  // IN1, or BUTTON's press: start_snare_from_request() with the pots as they are
  void Trigger() override {
    uint16_t adc2 = 4095 - Adc12(2);  // A2 is inverted by the circuit
    start_snare(Adc12(0), Adc12(1), adc2, panel.in2Recent);
  }
  void Button(bool pressed, uint32_t heldMs) override {
    (void)heldMs;
    if (pressed) Trigger();
  }

  uint16_t Render() override { return audio_to_pwm(voice.active ? render_snare_sample() : 0.0f); }

  bool Active() const override { return voice.active; }
  float Led() const override { return voice.active ? 1.0f : 0.0f; }  // "playback indicator"
  void Label(char *out) const override { strcpy(out, "SNARE"); }

 private:
  struct SnareVoice {
    bool active;
    uint32_t sample_index;
    uint32_t duration_samples;
    uint16_t lfsr;
    float clock_phase;
    float hp_low_state;
    float base_clock_hz;
    float pitch_octaves;
    float noise_env;
    float click_env;
    float pitch_env;
    float noise_coef;
    float click_coef;
    float pitch_coef;
    float highpass_alpha;
    float click_level;
    float noise_level;
    float drive;
    float volume_scale;
    bool in2_applied;
  };

  static float exp_map_norm(float norm, float low, float high) { return low * powf(high / low, norm); }

  // audio_to_pwm_cc(): -1..1 to the 8-bit duty, truncated
  static uint16_t audio_to_pwm(float x) {
    x = Clamp(x, -1.0f, 1.0f);
    float y = x * 0.5f + 0.5f;
    return (uint16_t)(y * PWM_WRAP) & PWM_WRAP;
  }

  float lookup_exp2_0_4(float x) const {
    x = Clamp(x, 0.0f, 4.0f);
    float pos = x * ((float)(EXP2_LUT_SIZE - 1) * 0.25f);
    int idx = (int)pos;
    if (idx >= EXP2_LUT_SIZE - 1) return exp2_lut[EXP2_LUT_SIZE - 1];
    float frac = pos - (float)idx;
    return exp2_lut[idx] + (exp2_lut[idx + 1] - exp2_lut[idx]) * frac;
  }

  void advance_lfsr_once() {
    uint16_t bit = (voice.lfsr ^ (voice.lfsr >> 1)) & 1u;
    voice.lfsr = (voice.lfsr >> 1) | (bit << 14);
  }

  void apply_in2_effect_to_voice() {
    if (voice.in2_applied) return;
    voice.volume_scale = 0.5f;
    voice.in2_applied = true;
  }

  void setup_luts() {
    for (int i = 0; i < EXP2_LUT_SIZE; i++) {
      float x = 4.0f * (float)i / (float)(EXP2_LUT_SIZE - 1);
      exp2_lut[i] = powf(2.0f, x);
    }
    for (int adc = 0; adc < ADC_LUT_SIZE; adc++) {
      float a = (float)adc / 4095.0f;
      float decay_seconds = exp_map_norm(a, DECAY_MIN_SECONDS, DECAY_MAX_SECONDS);
      float duration_seconds = decay_seconds * 2.4f + 0.080f;
      if (duration_seconds > 2.2f) duration_seconds = 2.2f;
      if (duration_seconds < 0.001f) duration_seconds = 0.001f;
      duration_samples_lut[adc] = (uint32_t)(duration_seconds * SAMPLE_RATE);
      float decay_for_coef = decay_seconds;
      if (decay_for_coef < 0.004f) decay_for_coef = 0.004f;
      noise_coef_lut[adc] = expf(-1.0f / (decay_for_coef * SAMPLE_RATE));
      base_clock_lut[adc] = exp_map_norm(a, 1000.0f, 32000.0f);
      float pitch_env_time = 0.010f + a * 0.070f;
      pitch_coef_lut[adc] = expf(-1.0f / (pitch_env_time * SAMPLE_RATE));
      float hp_cutoff = 900.0f + a * 2600.0f;
      highpass_alpha_lut[adc] = 1.0f - expf(-2.0f * PI_F * hp_cutoff / (float)SAMPLE_RATE);
    }
  }

  void start_snare(uint16_t adc0, uint16_t adc1, uint16_t adc2, bool in2_active) {
    if (adc0 > 4095) adc0 = 4095;
    if (adc1 > 4095) adc1 = 4095;
    if (adc2 > 4095) adc2 = 4095;
    float env_amount = (float)adc2 / 4095.0f;

    voice.active = true;
    voice.sample_index = 0;
    voice.duration_samples = duration_samples_lut[adc0];
    voice.lfsr = 0x7FFF;
    voice.clock_phase = 0.0f;
    voice.hp_low_state = 0.0f;
    voice.base_clock_hz = base_clock_lut[adc1];
    voice.pitch_octaves = env_amount * 4.0f;
    voice.noise_env = 1.0f;
    voice.click_env = 1.0f;
    voice.pitch_env = 1.0f;
    voice.noise_coef = noise_coef_lut[adc0];
    voice.click_coef = 0.99161f;
    voice.click_level = 0.22f;
    voice.pitch_coef = pitch_coef_lut[adc2];
    voice.highpass_alpha = highpass_alpha_lut[adc2];
    voice.noise_level = 0.78f + env_amount * 0.10f;
    voice.drive = 1.25f + env_amount * 0.55f;
    voice.volume_scale = 1.0f;
    voice.in2_applied = false;
    if (in2_active) apply_in2_effect_to_voice();
  }

  float render_snare_sample() {
    if (!voice.active) return 0.0f;
    if (!voice.in2_applied && voice.sample_index < IN2_GRACE_SAMPLES) {
      if (panel.in2) apply_in2_effect_to_voice();
    }
    if (voice.sample_index >= voice.duration_samples || voice.noise_env < 0.00020f) {
      voice.active = false;
      return 0.0f;
    }
    float pitch_x = voice.pitch_octaves * voice.pitch_env;
    float pitch_mul = lookup_exp2_0_4(pitch_x);
    float current_clock = voice.base_clock_hz * pitch_mul;
    if (current_clock < 120.0f) current_clock = 120.0f;
    if (current_clock > (float)SAMPLE_RATE * 4.0f) current_clock = (float)SAMPLE_RATE * 4.0f;
    voice.clock_phase += current_clock / (float)SAMPLE_RATE;
    while (voice.clock_phase >= 1.0f) {
      advance_lfsr_once();
      voice.clock_phase -= 1.0f;
    }
    float lfsr_out = (voice.lfsr & 1u) ? 1.0f : -1.0f;
    voice.hp_low_state += voice.highpass_alpha * (lfsr_out - voice.hp_low_state);
    float tone_noise = lfsr_out - voice.hp_low_state;
    float raw = tone_noise * voice.noise_env * voice.noise_level;
    raw += lfsr_out * voice.click_env * voice.click_level;
    // Soft clip removed. Final clipping is handled in audio_to_pwm_cc().
    float out = raw * voice.drive * OUTPUT_GAIN;
    out *= voice.volume_scale;
    voice.noise_env *= voice.noise_coef;
    voice.click_env *= voice.click_coef;
    voice.pitch_env *= voice.pitch_coef;
    voice.sample_index++;
    return out;
  }

  uint32_t duration_samples_lut[ADC_LUT_SIZE];
  float noise_coef_lut[ADC_LUT_SIZE];
  float base_clock_lut[ADC_LUT_SIZE];
  float pitch_coef_lut[ADC_LUT_SIZE];
  float highpass_alpha_lut[ADC_LUT_SIZE];
  float exp2_lut[EXP2_LUT_SIZE];
  SnareVoice voice = {false, 0, 0, 0x7FFF, 0.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.99f, 0.99f, 0.99f, 0.2f, 0.22f, 0.78f, 1.25f, 1.0f, false};
};

}  // namespace mod2

#endif
