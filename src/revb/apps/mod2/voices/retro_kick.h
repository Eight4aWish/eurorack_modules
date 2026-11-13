// HAGIWO MOD2 Retro Game Kick Ver1.1 (modulove/MOD2, Firmware/retro_game_kick/retro_game_kick.ino),
// CC0 1.0 Universal: a retro-game-style one-shot kick, a triangle body, a pulse body and a short
// click. Its engine is kept as published, at its own 48 kHz with 8-bit PWM. The original takes a
// trigger once per 512-sample buffer, up to about 11 ms late; here it starts on the next sample.
// The port (the class around it) is MIT, Copyright (c) 2026 David Baghurst.

#ifndef MOD2_VOICES_RETRO_KICK_H
#define MOD2_VOICES_RETRO_KICK_H

#include "../voice.h"

namespace mod2 {

class RetroKick final : public Voice {
  static constexpr uint32_t SAMPLE_RATE = 48000;
  static constexpr uint16_t PWM_WRAP = 255;
  static constexpr float OUTPUT_GAIN = 0.82f;

 public:
  float Rate() const override { return SAMPLE_RATE; }
  uint16_t Wrap() const override { return PWM_WRAP; }

  // request_kick(): the ADC values latched at the trigger (A2 is inverted by the circuit)
  void Trigger() override { start_kick(Adc12(0), Adc12(1), 4095 - Adc12(2)); }
  void Button(bool pressed, uint32_t heldMs) override {
    (void)heldMs;
    if (pressed) Trigger();
  }

  uint16_t Render() override { return audio_to_pwm(render_kick_sample()); }

  bool Active() const override { return voice.active; }
  float Led() const override { return voice.active ? 1.0f : 0.0f; }  // unused on a MOD2
  void Label(char *out) const override { strcpy(out, "RETRO KICK"); }

 private:
  struct KickVoice {
    bool active;
    uint32_t sample_index;
    uint32_t duration_samples;
    uint32_t phase;
    float decay;
    float base_pitch;
    float morph;
    float sweep_octaves;
    float duty;
    float drive;
    float click_decay;
    float click_level;
  };

  static float exp_map(uint16_t adc, float low, float high) { return low * powf(high / low, adc / 4095.0f); }
  static float linear_map(uint16_t adc, float low, float high) { return low + (high - low) * (adc / 4095.0f); }
  static float phase_to_unit(uint32_t phase) { return phase * (1.0f / 4294967296.0f); }
  static uint16_t audio_to_pwm(float x) {
    x = Clamp(x, -1.0f, 1.0f);
    float y = x * 0.5f + 0.5f;
    return (uint16_t)(y * PWM_WRAP) & PWM_WRAP;
  }
  static float osc_triangle(uint32_t phase) {
    float t = phase_to_unit(phase);
    return 2.0f * fabsf(2.0f * t - 1.0f) - 1.0f;
  }
  static float osc_pulse(uint32_t phase, float duty) { return (phase_to_unit(phase) < duty) ? 1.0f : -1.0f; }
  static float osc_pulse_float_phase(float phase_unit, float duty) {
    float t = phase_unit - floorf(phase_unit);
    return (t < duty) ? 1.0f : -1.0f;
  }

  void start_kick(uint16_t adc0, uint16_t adc1, uint16_t adc2) {
    float decay = exp_map(adc0, 0.020f, 0.120f);
    float base_pitch = exp_map(adc1, 32.0f, 300.0f);
    float morph = adc2 / 4095.0f;
    float sweep_octaves = linear_map(adc2, 0.25f, 3.80f);
    float duty = Clamp(0.50f - morph * 0.30f, 0.08f, 0.92f);
    float drive = 1.35f + morph * 0.90f;
    float duration_seconds = decay * 2.7f + 0.08f;
    if (duration_seconds > 2.8f) duration_seconds = 2.8f;

    voice.active = true;
    voice.sample_index = 0;
    voice.duration_samples = (uint32_t)(duration_seconds * SAMPLE_RATE);
    voice.phase = 0u;
    voice.decay = decay;
    voice.base_pitch = base_pitch;
    voice.morph = morph;
    voice.sweep_octaves = sweep_octaves;
    voice.duty = duty;
    voice.drive = drive;
    voice.click_decay = 0.0045f;
    voice.click_level = 0.20f;
  }

  float render_kick_sample() {
    if (!voice.active) return 0.0f;
    if (voice.sample_index >= voice.duration_samples) {
      voice.active = false;
      return 0.0f;
    }
    float t = voice.sample_index / (float)SAMPLE_RATE;
    float sweep_time = 0.012f + voice.morph * 0.075f;
    float sweep_env = expf(-t / sweep_time);
    float freq = voice.base_pitch * powf(2.0f, voice.sweep_octaves * sweep_env);
    freq = Clamp(freq, 18.0f, SAMPLE_RATE * 0.40f);
    uint32_t phase_inc = (uint32_t)((freq * 4294967296.0f) / SAMPLE_RATE);
    voice.phase += phase_inc;
    float body_env = expf(-t / fmaxf(voice.decay, 0.006f));
    float click_env = expf(-t / voice.click_decay);
    float tri = osc_triangle(voice.phase);
    float pulse_body = osc_pulse(voice.phase, voice.duty);
    float phase_unit = phase_to_unit(voice.phase);
    float click_phase = phase_unit * (2.0f + voice.morph * 2.0f);
    float pulse_click = osc_pulse_float_phase(click_phase, 0.22f);
    float raw = tri * body_env * (0.72f - voice.morph * 0.12f) +
                pulse_body * body_env * (0.22f + voice.morph * 0.16f) + pulse_click * click_env * voice.click_level;
    float out = tanhf(raw * voice.drive) * OUTPUT_GAIN;
    if (panel.in2) out *= 0.5f;  // IN2 high while playing: level x 0.5
    voice.sample_index++;
    return out;
  }

  KickVoice voice = {false, 0, 0, 0u, 0.05f, 100.0f, 0.0f, 1.0f, 0.5f, 1.5f, 0.0045f, 0.20f};
};

}  // namespace mod2

#endif
