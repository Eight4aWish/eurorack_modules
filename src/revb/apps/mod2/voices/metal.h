// HAGIWO MOD2 01 Metal Hit RP2350 Ver1.0 (modulove/MOD2, Firmware/metal/metal.ino), CC0 1.0
// Universal: a struck-metal one-shot, six partials with a noise strike and shimmer. Its engine is kept
// as published, at its own 48 kHz with 10-bit PWM. As in the original, the controls are read 5 ms
// after the trigger, so a V/oct CV sent with it has settled; IN2 is read at the edge.
// The port (the class around it) is MIT, Copyright (c) 2026 David Baghurst.

#ifndef MOD2_VOICES_METAL_H
#define MOD2_VOICES_METAL_H

#include "../voice.h"

namespace mod2 {

class Metal final : public Voice {
  static constexpr uint32_t SAMPLE_RATE = 48000;
  static constexpr uint16_t PWM_WRAP = 1023;
  static constexpr uint32_t SINE_TABLE_BITS = 10;
  static constexpr uint32_t SINE_TABLE_SIZE = 1u << SINE_TABLE_BITS;
  static constexpr uint32_t SINE_TABLE_SHIFT = 32u - SINE_TABLE_BITS;
  static constexpr uint32_t PARTIAL_COUNT = 6;
  static constexpr float TAU_F = 6.28318530718f;
  static constexpr float OUTPUT_GAIN = 0.72f;
  static constexpr float STRIKE_TUNE_BASE_HZ = 40.0f;
  static constexpr float ADC2_VOLTAGE_RANGE = 5.0f;
  static constexpr float METAL_SPREAD_MIN = 0.00f;
  static constexpr float METAL_SPREAD_MAX = 4.20f;
  static constexpr uint16_t ADC1_CONSONANT_MAX = 100;
  static constexpr float FIXED_HARDNESS = 2600.0f / 4095.0f;
  static constexpr float FIXED_DAMPING = 0.12f + (1.0f - 0.12f) * (2500.0f / 4095.0f);
  static constexpr uint32_t LED_FLASH_SAMPLES = SAMPLE_RATE * 20 / 1000;
  static constexpr uint32_t TRIGGER_ADC_DELAY_SAMPLES = SAMPLE_RATE * 5 / 1000;
  static constexpr uint32_t VOICE_ATTACK_SAMPLES = SAMPLE_RATE / 2000;
  static constexpr uint32_t VOICE_RELEASE_SAMPLES = (SAMPLE_RATE * 3) / 1000;
  static constexpr uint32_t RETRIGGER_FADE_SAMPLES = (SAMPLE_RATE * 3) / 1000;

 public:
  float Rate() const override { return SAMPLE_RATE; }
  uint16_t Wrap() const override { return PWM_WRAP; }

  void Init() override {
    for (uint32_t i = 0; i < SINE_TABLE_SIZE; i++) {
      float phase = ((float)i / (float)SINE_TABLE_SIZE) * TAU_F;
      sine_table[i] = (int16_t)(sinf(phase) * 32767.0f);
    }
  }

  // The PUSH / SW state, as the original keeps it in EEPROM
  size_t Save(uint8_t *out) const override {
    out[0] = push_octave_on ? 1 : 0;
    return 1;
  }
  void Load(const uint8_t *in, size_t n) override {
    if (n >= 1 && in[0] <= 1) push_octave_on = in[0] != 0;
  }

  // trigger_metal_hit_after_adc_settle(): IN2 and the LED now, the controls 5 ms on
  void Trigger() override {
    accent_half = panel.in2;
    led_samples = LED_FLASH_SAMPLES;
    settle = TRIGGER_ADC_DELAY_SAMPLES;
  }
  // update_button_toggle(): each press toggles +2 octaves
  void Button(bool pressed, uint32_t heldMs) override {
    (void)heldMs;
    if (!pressed) return;
    push_octave_on = !push_octave_on;
    saveRequested = true;
  }

  uint16_t Render() override {
    if (led_samples) led_samples--;
    if (settle && --settle == 0) {
      // request_metal_hit(): A0 and A1 as averaged, A2 read now (inverted by the circuit)
      start_voice(Adc12(0), Adc12(1), 4095 - Adc12(2), accent_half, push_octave_on);
    }
    float raw = 0.0f;
    raw += render_voice_raw_sample(voice);
    raw += render_voice_raw_sample(tail_voice);
    return audio_to_pwm(soft_clip_fast(raw) * OUTPUT_GAIN);
  }

  bool Active() const override { return voice.active || tail_voice.active || settle; }
  float Led() const override { return led_samples ? 1.0f : 0.0f; }  // 20 ms on each trigger
  void Label(char *out) const override { strcpy(out, push_octave_on ? "METAL +2" : "METAL"); }

 private:
  struct PartialState {
    uint32_t phase;
    uint32_t phase_inc;
    float env;
    float env_mul;
    float amp;
  };
  struct MetalVoice {
    bool active;
    uint32_t sample_index;
    uint32_t duration_samples;
    uint32_t rng;
    PartialState partials[PARTIAL_COUNT];
    float strike_env, strike_env_mul, strike_alpha, strike_lp, strike_amp;
    float shimmer_env, shimmer_env_mul, shimmer_alpha, shimmer_lp, shimmer_amp;
    float accent_gain;
    uint32_t forced_release_remaining;
  };

  static float exp_map(uint16_t adc, float low, float high) { return low * powf(high / low, adc / 4095.0f); }
  static float metal_spread_from_adc1(uint16_t adc) {
    if (adc <= ADC1_CONSONANT_MAX) return 0.0f;
    float norm = (adc - ADC1_CONSONANT_MAX) / (4095.0f - ADC1_CONSONANT_MAX);
    return METAL_SPREAD_MIN + (METAL_SPREAD_MAX - METAL_SPREAD_MIN) * norm;
  }
  static float v_oct_pitch_from_adc(uint16_t adc) {
    float volts = (adc / 4095.0f) * ADC2_VOLTAGE_RANGE;
    return STRIKE_TUNE_BASE_HZ * powf(2.0f, volts);
  }
  static uint32_t rand_u32(uint32_t &state) {
    state = state * 1664525u + 1013904223u;
    return state;
  }
  static float rand_bipolar(uint32_t &state) {
    uint32_t value = rand_u32(state);
    return ((value >> 8) * (1.0f / 16777215.0f)) * 2.0f - 1.0f;
  }
  static uint32_t seed_from_request(uint16_t adc0, uint16_t adc1, uint16_t adc2, bool push_on) {
    uint32_t seed = 101u;
    seed ^= (uint32_t)adc0 * 73856093u;
    seed ^= (uint32_t)adc1 * 19349663u;
    seed ^= (uint32_t)adc2 * 83492791u;
    if (push_on) seed ^= 0x9E3779B9u;
    return seed;
  }
  static float env_multiplier(float tau_seconds) {
    tau_seconds = fmaxf(tau_seconds, 0.0002f);
    return expf(-1.0f / (tau_seconds * (float)SAMPLE_RATE));
  }
  static float one_pole_alpha(float cutoff_hz) {
    cutoff_hz = Clamp(cutoff_hz, 10.0f, SAMPLE_RATE * 0.46f);
    return 1.0f - expf(-TAU_F * cutoff_hz / (float)SAMPLE_RATE);
  }
  static uint32_t phase_inc_from_hz(float hz) {
    hz = Clamp(hz, 0.0f, SAMPLE_RATE * 0.43f);
    return (uint32_t)((hz * 4294967296.0f) / (float)SAMPLE_RATE);
  }
  float sine_from_phase(uint32_t phase) const { return sine_table[phase >> SINE_TABLE_SHIFT] * (1.0f / 32768.0f); }
  static float soft_clip_fast(float x) {
    x *= 1.35f;
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (x * x * x) * (1.0f / 6.0f);
  }
  // audio_to_pwm_cc(): -1..1 to the 10-bit duty, rounded
  static uint16_t audio_to_pwm(float x) {
    x = Clamp(x, -1.0f, 1.0f);
    float y = x * 0.5f + 0.5f;
    uint16_t duty = (uint16_t)(y * PWM_WRAP + 0.5f);
    return duty > PWM_WRAP ? PWM_WRAP : duty;
  }

  static float attack_release_gain(const MetalVoice &target_voice) {
    float gain = 1.0f;
    if (target_voice.sample_index < VOICE_ATTACK_SAMPLES)
      gain *= (float)target_voice.sample_index / (float)VOICE_ATTACK_SAMPLES;
    uint32_t remaining = target_voice.duration_samples - target_voice.sample_index;
    if (remaining <= VOICE_RELEASE_SAMPLES) {
      if (remaining <= 1) gain = 0.0f;
      else gain *= (float)(remaining - 1) / (float)VOICE_RELEASE_SAMPLES;
    }
    if (target_voice.forced_release_remaining > 0) {
      if (target_voice.forced_release_remaining <= 1) gain = 0.0f;
      else gain *= (float)(target_voice.forced_release_remaining - 1) / (float)RETRIGGER_FADE_SAMPLES;
    }
    return gain;
  }

  void start_voice(uint16_t adc0, uint16_t adc1, uint16_t adc2, bool accent, bool push_on) {
    if (voice.active) {
      tail_voice = voice;
      tail_voice.forced_release_remaining = RETRIGGER_FADE_SAMPLES;
    }
    float decay = exp_map(adc0, 0.040f, 1.200f);
    float spread = metal_spread_from_adc1(adc1);
    float pitch = v_oct_pitch_from_adc(adc2);
    if (push_on) pitch *= 4.0f;
    float duration_seconds = decay * (2.55f - FIXED_DAMPING * 0.80f) + 0.12f;
    if (duration_seconds > 3.5f) duration_seconds = 3.5f;

    voice.active = true;
    voice.sample_index = 0;
    voice.duration_samples = (uint32_t)(duration_seconds * SAMPLE_RATE);
    voice.rng = seed_from_request(adc0, adc1, adc2, push_on);
    voice.accent_gain = accent ? 0.5f : 1.0f;
    voice.forced_release_remaining = 0;

    const bool use_consonant_ratios = adc1 <= ADC1_CONSONANT_MAX;
    const float consonant_ratios[PARTIAL_COUNT] = {1.00f, 2.00f, 3.00f, 4.00f, 5.00f, 6.00f};
    const float base_ratios[PARTIAL_COUNT] = {1.00f, 1.39f, 2.16f, 3.08f, 4.37f, 5.91f};
    const float spread_amounts[PARTIAL_COUNT] = {0.00f, 0.07f, 0.23f, 0.39f, 0.61f, 0.83f};
    for (uint32_t i = 0; i < PARTIAL_COUNT; i++) {
      float ratio = use_consonant_ratios ? consonant_ratios[i] : base_ratios[i] + spread * spread_amounts[i];
      float partial_freq = Clamp(pitch * ratio, 35.0f, SAMPLE_RATE * 0.43f);
      float partial_decay = decay * (1.42f - FIXED_DAMPING * 0.72f);
      partial_decay /= 1.0f + i * (0.16f + FIXED_DAMPING * 0.13f);
      float amp_random = 1.0f + rand_bipolar(voice.rng) * 0.10f;
      float amp = (0.55f / (1.0f + i * 0.62f)) * amp_random;
      voice.partials[i].phase = rand_u32(voice.rng);
      voice.partials[i].phase_inc = phase_inc_from_hz(partial_freq);
      voice.partials[i].env = 1.0f;
      voice.partials[i].env_mul = env_multiplier(fmaxf(0.012f, partial_decay));
      voice.partials[i].amp = amp;
    }
    float strike_cutoff = 2600.0f + FIXED_HARDNESS * 9000.0f;
    float strike_tau = 0.0018f + (1.0f - FIXED_HARDNESS) * 0.010f;
    voice.strike_env = 1.0f;
    voice.strike_env_mul = env_multiplier(strike_tau);
    voice.strike_alpha = one_pole_alpha(strike_cutoff);
    voice.strike_lp = 0.0f;
    voice.strike_amp = 0.13f + FIXED_HARDNESS * 0.34f;
    float shimmer_cutoff = 1100.0f + spread * 1700.0f;
    float shimmer_tau = fmaxf(0.025f, decay * (0.18f + spread * 0.05f));
    voice.shimmer_env = 1.0f;
    voice.shimmer_env_mul = env_multiplier(shimmer_tau);
    voice.shimmer_alpha = one_pole_alpha(shimmer_cutoff);
    voice.shimmer_lp = 0.0f;
    voice.shimmer_amp = 0.04f + spread * 0.035f;
  }

  float render_voice_raw_sample(MetalVoice &target_voice) {
    if (!target_voice.active) return 0.0f;
    if (target_voice.sample_index >= target_voice.duration_samples) {
      target_voice.active = false;
      return 0.0f;
    }
    bool forced_release_active = target_voice.forced_release_remaining > 0;
    float voice_gain = attack_release_gain(target_voice) * target_voice.accent_gain;
    float raw = 0.0f;
    for (uint32_t i = 0; i < PARTIAL_COUNT; i++) {
      PartialState &partial = target_voice.partials[i];
      raw += sine_from_phase(partial.phase) * partial.env * partial.amp;
      partial.phase += partial.phase_inc;
      partial.env *= partial.env_mul;
    }
    float noise = rand_bipolar(target_voice.rng);
    target_voice.strike_lp += target_voice.strike_alpha * (noise - target_voice.strike_lp);
    float strike_hp = noise - target_voice.strike_lp;
    raw += strike_hp * target_voice.strike_env * target_voice.strike_amp;
    target_voice.strike_env *= target_voice.strike_env_mul;
    target_voice.shimmer_lp += target_voice.shimmer_alpha * (noise - target_voice.shimmer_lp);
    float shimmer_hp = noise - target_voice.shimmer_lp;
    raw += shimmer_hp * target_voice.shimmer_env * target_voice.shimmer_amp;
    target_voice.shimmer_env *= target_voice.shimmer_env_mul;
    target_voice.sample_index++;
    if (target_voice.forced_release_remaining > 0) target_voice.forced_release_remaining--;
    if (target_voice.sample_index >= target_voice.duration_samples ||
        (forced_release_active && target_voice.forced_release_remaining == 0))
      target_voice.active = false;
    return raw * voice_gain;
  }

  int16_t sine_table[SINE_TABLE_SIZE];
  MetalVoice voice = {};
  MetalVoice tail_voice = {};
  bool push_octave_on = false;
  bool accent_half = false;
  uint32_t settle = 0;
  uint32_t led_samples = 0;
};

}  // namespace mod2

#endif
