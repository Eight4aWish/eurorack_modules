// HAGIWO MOD2 Arcade Laser Ver1.0 (modulove/MOD2, Firmware/laser/laser.ino), CC0 1.0 Universal: a
// one-shot arcade laser from a 12-shape wavetable morph oscillator, six parameters on two pages of
// three with soft takeover. Its engine is kept as published, at its own 31.25 kHz with 10-bit PWM.
// The original takes a trigger once per 256-sample buffer, up to about 8 ms late; here it starts on
// the next sample.
// The port (the class around it) is MIT, Copyright (c) 2026 David Baghurst.

#ifndef MOD2_VOICES_LASER_H
#define MOD2_VOICES_LASER_H

#include "../voice.h"

namespace mod2 {

class Laser final : public Voice {
  static constexpr uint32_t SAMPLE_RATE = 31250;
  static constexpr uint16_t PWM_WRAP = 1023;
  static constexpr float OUTPUT_GAIN = 1.13636f;  // 1 / voice.gain: full scale before the final clip
  static constexpr float TWO_PI_F = 6.28318530717958647692f;
  static constexpr int ADC_LUT_SIZE = 4096;
  static constexpr int WAVE_COUNT = 12;
  static constexpr int WAVE_TABLE_SIZE = 256;
  static constexpr int WAVE_TABLE_MASK = WAVE_TABLE_SIZE - 1;
  static constexpr float DECAY_MIN_SECONDS = 0.020f;
  static constexpr float DECAY_MAX_SECONDS = 0.500f;
  static constexpr float AMP_CURVE_MIN = 0.62f;
  static constexpr float AMP_CURVE_MAX = 2.75f;
  static constexpr float PITCH_WIDTH_MIN_OCT = 0.0f;
  static constexpr float PITCH_WIDTH_MAX_OCT = 4.0f;
  static constexpr float PITCH_TAU_MIN_SECONDS = 0.012f;
  static constexpr float PITCH_TAU_MAX_SECONDS = 0.220f;
  static constexpr float END_PITCH_MIN_HZ = 45.0f;
  static constexpr float END_PITCH_MAX_HZ = 2400.0f;
  static constexpr int PICKUP_THRESHOLD = 100;
  static constexpr uint32_t IN2_HOLD_MS = 15;
  static constexpr uint32_t IN2_GRACE_SAMPLES = (SAMPLE_RATE * IN2_HOLD_MS) / 1000;
  static constexpr uint32_t LED_BLINK_SAMPLES = SAMPLE_RATE * 10 / 1000;

 public:
  float Rate() const override { return SAMPLE_RATE; }
  uint16_t Wrap() const override { return PWM_WRAP; }

  void Init() override {
    setup_luts();
    sync_start_pitch(current_params);
  }

  // The six sound parameters, as SavedLaserState keeps them
  size_t Save(uint8_t *out) const override {
    PutF(out, 0, current_params.decay_seconds);
    PutF(out, 1, current_params.amplitude_curve);
    PutF(out, 2, current_params.wave_position);
    PutF(out, 3, current_params.pitch_width_octaves);
    PutF(out, 4, current_params.end_pitch_hz);
    PutF(out, 5, current_params.pitch_tau_seconds);
    return 24;
  }
  // saved_state_is_valid()'s range checks, then restore_params_from_saved_state()
  void Load(const uint8_t *in, size_t n) override {
    if (n < 24) return;
    float d = GetF(in, 0), a = GetF(in, 1), w = GetF(in, 2), pw = GetF(in, 3), ep = GetF(in, 4),
          pt = GetF(in, 5);
    if (!(d >= DECAY_MIN_SECONDS && d <= DECAY_MAX_SECONDS)) return;
    if (!(a >= AMP_CURVE_MIN && a <= AMP_CURVE_MAX)) return;
    if (!(w >= 0.0f && w <= 11.0f)) return;
    if (!(pw >= PITCH_WIDTH_MIN_OCT && pw <= PITCH_WIDTH_MAX_OCT)) return;
    if (!(ep >= END_PITCH_MIN_HZ && ep <= END_PITCH_MAX_HZ)) return;
    if (!(pt >= PITCH_TAU_MIN_SECONDS && pt <= PITCH_TAU_MAX_SECONDS)) return;
    current_params.decay_seconds = d;
    current_params.amplitude_curve = a;
    current_params.wave_position = w;
    current_params.pitch_width_octaves = pw;
    current_params.end_pitch_hz = ep;
    current_params.pitch_tau_seconds = pt;
    sync_start_pitch(current_params);
  }

  // request_laser() and start_laser_from_request()
  void Trigger() override {
    led_samples = LED_BLINK_SAMPLES;
    start_laser(current_params, panel.in2Recent);
  }
  // update_push_page(): each press flips the edit page
  void Button(bool pressed, uint32_t heldMs) override {
    (void)heldMs;
    if (!pressed) return;
    edit_page_on = !edit_page_on;
    pickup_rearm_pending = true;
    saveRequested = true;  // the original also waits until the voice is idle
  }

  // loop()'s ADC handling: lock the pickups on the first reading, re-arm them after a page change,
  // otherwise soft takeover. The original averages 16 ADC reads; Rev B's driver smooths.
  void Controls() override {
    adc_avg[0] = Adc12(0);
    adc_avg[1] = Adc12(1);
    adc_avg[2] = 4095 - Adc12(2);  // A2 is inverted by the circuit
    if (first_adc_block || pickup_rearm_pending) {
      first_adc_block = false;
      pickup_rearm_pending = false;
      for (int ch = 0; ch < 3; ch++) {
        pickup_origin[ch] = adc_avg[ch];
        picked_up[ch] = false;
      }
    } else {
      update_soft_takeover_params();
    }
  }

  uint16_t Render() override {
    if (led_samples) led_samples--;
    if (!voice.active) return audio_to_pwm(0.0f);
    return audio_to_pwm(render_laser_sample());
  }

  bool Active() const override { return voice.active; }
  // a 10 ms blink on each trigger, dim (10 %) on the second page
  float Led() const override { return led_samples ? 1.0f : edit_page_on ? 0.1f : 0.0f; }
  void Label(char *out) const override { strcpy(out, edit_page_on ? "LASER 2" : "LASER 1"); }

 private:
  struct LaserParams {
    float decay_seconds;
    float amplitude_curve;
    float wave_position;
    float pitch_width_octaves;
    float start_pitch_hz;
    float end_pitch_hz;
    float pitch_tau_seconds;
  };
  struct LaserVoice {
    bool active;
    uint32_t sample_index;
    uint32_t duration_samples;
    float phase;
    float wobble_phase;
    float decay_seconds;
    float amplitude_curve;
    float wave_position;
    float start_pitch_hz;
    float end_pitch_hz;
    float pitch_tau_seconds;
    float log_start_pitch;
    float log_pitch_delta;
    float inv_pitch_tau;
    float inv_decay;
    float wobble_phase_inc;
    float gain;
    float volume_scale;
    bool in2_applied;
  };

  static float exp_map_norm(float norm, float low, float high) { return low * powf(high / low, norm); }
  static float smoothstep_local(float x) {
    x = Clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
  }
  static float fast_tanh_approx(float x) {
    x = Clamp(x, -3.0f, 3.0f);
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }
  // audio_to_pwm_cc(): -1..1 to the 10-bit duty, rounded
  static uint16_t audio_to_pwm(float x) {
    x = Clamp(x, -1.0f, 1.0f);
    float y = x * 0.5f + 0.5f;
    uint16_t duty = (uint16_t)(y * (float)PWM_WRAP + 0.5f);
    return duty > PWM_WRAP ? PWM_WRAP : duty;
  }

  void normalize_wave_row(int row) {
    float peak = 0.0f;
    for (int i = 0; i < WAVE_TABLE_SIZE; i++) {
      float a = fabsf(wavetable[row][i]);
      if (a > peak) peak = a;
    }
    if (peak < 0.000001f) peak = 1.0f;
    for (int i = 0; i < WAVE_TABLE_SIZE; i++) wavetable[row][i] /= peak;
    wavetable[row][WAVE_TABLE_SIZE] = wavetable[row][0];
  }

  void setup_wavetables() {
    for (int i = 0; i < WAVE_TABLE_SIZE; i++) {
      float p = (float)i / (float)WAVE_TABLE_SIZE;
      float s = sinf(TWO_PI_F * p);
      wavetable[0][i] = s;
      wavetable[1][i] = 4.0f * fabsf(p - 0.5f) - 1.0f;
      wavetable[2][i] = 2.0f * p - 1.0f;
      wavetable[3][i] = 1.0f - 2.0f * p;
      wavetable[4][i] = (p < 0.5f) ? 1.0f : -1.0f;
      wavetable[5][i] = (p < (1.0f / 3.0f)) ? 1.0f : -1.0f;
      wavetable[6][i] = (p < 0.25f) ? 1.0f : -1.0f;
      wavetable[7][i] = (p < 0.125f) ? 1.0f : -1.0f;
      wavetable[8][i] = tanhf(2.6f * s);
      wavetable[9][i] = 0.72f * s + 0.28f * sinf(TWO_PI_F * 2.0f * p);
      wavetable[10][i] = 0.62f * s + 0.38f * sinf(TWO_PI_F * 3.0f * p);
      wavetable[11][i] = 2.0f * fabsf(s) - 1.0f;
    }
    for (int w = 0; w < WAVE_COUNT; w++) normalize_wave_row(w);
  }

  void setup_luts() {
    setup_wavetables();
    for (int adc = 0; adc < ADC_LUT_SIZE; adc++) {
      float a = (float)adc / 4095.0f;
      decay_lut[adc] = exp_map_norm(a, DECAY_MIN_SECONDS, DECAY_MAX_SECONDS);
      amplitude_curve_lut[adc] = AMP_CURVE_MIN + (AMP_CURVE_MAX - AMP_CURVE_MIN) * a;
      wave_position_lut[adc] = a * 11.0f;
      pitch_width_lut[adc] = PITCH_WIDTH_MIN_OCT + (PITCH_WIDTH_MAX_OCT - PITCH_WIDTH_MIN_OCT) * a;
      pitch_tau_lut[adc] = exp_map_norm(a, PITCH_TAU_MIN_SECONDS, PITCH_TAU_MAX_SECONDS);
      end_pitch_lut[adc] = exp_map_norm(a, END_PITCH_MIN_HZ, END_PITCH_MAX_HZ);
    }
  }

  float read_single_wave(float phase, int wave_index) const {
    phase = phase - floorf(phase);
    float pos = phase * (float)WAVE_TABLE_SIZE;
    int idx = (int)pos;
    float frac = pos - (float)idx;
    idx &= WAVE_TABLE_MASK;
    float a = wavetable[wave_index][idx];
    float b = wavetable[wave_index][idx + 1];
    return a + (b - a) * frac;
  }

  float read_morph_wave(float phase, float position) const {
    position = Clamp(position, 0.0f, 11.0f);
    int i0 = (int)floorf(position);
    if (i0 < 0) i0 = 0;
    if (i0 > 11) i0 = 11;
    int i1 = i0 + 1;
    if (i1 > 11) i1 = 11;
    float blend = smoothstep_local(position - (float)i0);
    float a = read_single_wave(phase, i0);
    float b = read_single_wave(phase, i1);
    return a + (b - a) * blend;
  }

  static void sync_start_pitch(LaserParams &params) {
    params.start_pitch_hz = params.end_pitch_hz * powf(2.0f, params.pitch_width_octaves);
  }

  void apply_in2_effect_to_voice() {
    if (voice.in2_applied) return;
    voice.volume_scale = 0.5f;
    voice.in2_applied = true;
  }

  void start_laser(const LaserParams &params, bool in2_active) {
    float decay_seconds = params.decay_seconds;
    float amplitude_curve = params.amplitude_curve;
    float wave_position = params.wave_position;
    float start_pitch_hz = Clamp(params.start_pitch_hz, 24.0f, (float)SAMPLE_RATE * 0.42f);
    float end_pitch_hz = Clamp(params.end_pitch_hz, 24.0f, (float)SAMPLE_RATE * 0.42f);
    float pitch_tau_seconds = params.pitch_tau_seconds;
    float tail_scale = 2.7f + 1.0f / fmaxf(0.5f, amplitude_curve);
    float duration_seconds = Clamp(decay_seconds * tail_scale + 0.055f, 0.001f, 3.2f);

    voice.active = true;
    voice.sample_index = 0;
    voice.duration_samples = (uint32_t)(duration_seconds * (float)SAMPLE_RATE);
    voice.phase = 0.0f;
    voice.wobble_phase = 0.0f;
    voice.decay_seconds = decay_seconds;
    voice.amplitude_curve = amplitude_curve;
    voice.wave_position = wave_position;
    voice.start_pitch_hz = start_pitch_hz;
    voice.end_pitch_hz = end_pitch_hz;
    voice.pitch_tau_seconds = pitch_tau_seconds;
    voice.log_start_pitch = logf(start_pitch_hz);
    voice.log_pitch_delta = logf(end_pitch_hz) - voice.log_start_pitch;
    voice.inv_pitch_tau = 1.0f / fmaxf(0.006f, pitch_tau_seconds);
    voice.inv_decay = 1.0f / fmaxf(0.012f, decay_seconds);
    float wobble_rate_hz = 18.0f + wave_position * 1.7f;
    voice.wobble_phase_inc = wobble_rate_hz / (float)SAMPLE_RATE;
    voice.gain = 0.88f;
    voice.volume_scale = 1.0f;
    voice.in2_applied = false;
    if (in2_active) apply_in2_effect_to_voice();
  }

  float render_laser_sample() {
    if (!voice.active) return 0.0f;
    if (!voice.in2_applied && voice.sample_index < IN2_GRACE_SAMPLES) {
      if (panel.in2) apply_in2_effect_to_voice();
    }
    if (voice.sample_index >= voice.duration_samples) {
      voice.active = false;
      return 0.0f;
    }
    float t = (float)voice.sample_index / (float)SAMPLE_RATE;
    float progress = Clamp(1.0f - expf(-t * voice.inv_pitch_tau), 0.0f, 1.0f);
    float freq = expf(voice.log_start_pitch + voice.log_pitch_delta * progress);
    float wobble_env = expf(-t / (voice.decay_seconds * 0.75f + 0.060f));
    float wobble = read_single_wave(voice.wobble_phase, 0);
    freq *= 1.0f + wobble * wobble_env * 0.012f;
    freq = Clamp(freq, 24.0f, (float)SAMPLE_RATE * 0.42f);
    voice.phase += freq / (float)SAMPLE_RATE;
    voice.phase = voice.phase - floorf(voice.phase);
    voice.wobble_phase += voice.wobble_phase_inc;
    voice.wobble_phase = voice.wobble_phase - floorf(voice.wobble_phase);

    float main = read_morph_wave(voice.phase, voice.wave_position);
    float upper = read_morph_wave(voice.phase * 2.0f, fminf(11.0f, voice.wave_position + 1.35f));
    float lower = read_single_wave(voice.phase * 0.5f, 0);
    float attack = Clamp(t / 0.0035f, 0.0f, 1.0f);
    float body_env = expf(-powf(t * voice.inv_decay, voice.amplitude_curve));
    float click_env = expf(-t / 0.0045f);
    float spark_env = expf(-t / (0.012f + voice.pitch_tau_seconds * 0.20f));
    if (body_env < 0.00020f && click_env < 0.00020f && spark_env < 0.00020f) {
      voice.active = false;
      return 0.0f;
    }
    float brightness = voice.wave_position * (1.0f / 11.0f);
    float raw = 0.0f;
    raw += main * body_env * attack * (0.76f - brightness * 0.10f);
    raw += upper * body_env * attack * (0.10f + brightness * 0.14f);
    raw += lower * body_env * attack * 0.08f;
    raw += read_single_wave(voice.phase * 3.0f, 0) * spark_env * 0.10f;
    raw += main * click_env * 0.18f;
    float drive = 1.25f + brightness * 0.55f;
    float out = fast_tanh_approx(raw * drive);
    out *= voice.gain * OUTPUT_GAIN * voice.volume_scale;
    voice.sample_index++;
    return out;
  }

  void update_soft_takeover_params() {
    for (int ch = 0; ch < 3; ch++) {
      if (!picked_up[ch]) {
        int delta = (int)adc_avg[ch] - (int)pickup_origin[ch];
        if (abs(delta) < PICKUP_THRESHOLD) continue;
        picked_up[ch] = true;
      }
      uint16_t adc = adc_avg[ch];
      if (adc > 4095) adc = 4095;
      if (!edit_page_on) {
        if (ch == 0) current_params.decay_seconds = decay_lut[adc];
        else if (ch == 1) current_params.amplitude_curve = amplitude_curve_lut[adc];
        else current_params.wave_position = wave_position_lut[adc];
      } else {
        if (ch == 0) {
          current_params.pitch_width_octaves = pitch_width_lut[adc];
          sync_start_pitch(current_params);
        } else if (ch == 1) {
          current_params.pitch_tau_seconds = pitch_tau_lut[adc];
        } else {
          current_params.end_pitch_hz = end_pitch_lut[adc];
          sync_start_pitch(current_params);
        }
      }
    }
  }

  float decay_lut[ADC_LUT_SIZE];
  float amplitude_curve_lut[ADC_LUT_SIZE];
  float wave_position_lut[ADC_LUT_SIZE];
  float pitch_width_lut[ADC_LUT_SIZE];
  float pitch_tau_lut[ADC_LUT_SIZE];
  float end_pitch_lut[ADC_LUT_SIZE];
  float wavetable[WAVE_COUNT][WAVE_TABLE_SIZE + 1];

  LaserVoice voice = {false, 0, 0, 0.0f, 0.0f, 0.100f, 1.35f, 5.5f, 880.0f, 220.0f, 0.080f,
                      0.0f, 0.0f, 12.5f, 10.0f, 0.0f, 0.88f, 1.0f, false};
  LaserParams current_params = {0.100f, 1.35f, 5.5f, 2.0f, 880.0f, 220.0f, 0.080f};
  bool edit_page_on = false;
  uint16_t adc_avg[3] = {0, 0, 0};
  uint16_t pickup_origin[3] = {0, 0, 0};
  bool picked_up[3] = {true, true, true};
  bool first_adc_block = true;
  bool pickup_rearm_pending = false;
  uint32_t led_samples = 0;
};

}  // namespace mod2

#endif
