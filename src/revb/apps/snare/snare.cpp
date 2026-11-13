// HAGIWO MOD2 LFSR Snare, on the analogue board Rev B (Pico 2 W) behind the MOD2/Melon panel.
//
// The voice is HAGIWO's MOD2 "LFSR Snare Ver1.0" (modulove/MOD2, Firmware/snare/snare.ino),
// CC0 1.0 Universal: a retro-game-style one-shot snare from a 15-bit LFSR. Its sound engine is
// kept as published: render_snare_sample(), the lookup tables and the trigger handling. What
// changes is the hardware around it. The original reads its knobs with the RP2350's ADC and plays
// through 8-bit PWM at 31.25 kHz into a two-pole RC filter and an AC-coupled x3.06 amplifier. Here
// it runs at the codec's 48 kHz, and the PWM's 8-bit steps, the RC filter and the output stage
// are reproduced in software, so it keeps its grit. docs/REVB_MOD2.md has the panel.
//
// The rest of this file (the port) is MIT, Copyright (c) 2026 David Baghurst.

#include <math.h>
#include <stdint.h>

#define COMPUTERCARD_SAMPLE_RATE_DIV 1
#define REVB_APP_NAME "SNARE"
#include "ComputerCard.h"

namespace {

// ---- from snare.ino, with SAMPLE_RATE now the codec's
const float SAMPLE_RATE = ComputerCard::SampleRate();
const float OUTPUT_GAIN = 0.92f;
const float PI_F = 3.14159265358979323846f;

const int ADC_LUT_SIZE = 4096;
const int EXP2_LUT_SIZE = 257;

const float DECAY_MIN_SECONDS = 0.015f;
const float DECAY_MAX_SECONDS = 0.400f;

const float IN2_HOLD_SECONDS = 0.015f;
const uint32_t IN2_GRACE_SAMPLES = (uint32_t)(SAMPLE_RATE * IN2_HOLD_SECONDS);

uint32_t duration_samples_lut[ADC_LUT_SIZE];
float noise_coef_lut[ADC_LUT_SIZE];
float base_clock_lut[ADC_LUT_SIZE];
float pitch_coef_lut[ADC_LUT_SIZE];
float highpass_alpha_lut[ADC_LUT_SIZE];
float exp2_lut[EXP2_LUT_SIZE];

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

SnareVoice voice = {false, 0, 0, 0x7FFF, 0.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                    0.99f, 0.99f, 0.99f, 0.2f, 0.22f, 0.78f, 1.25f, 1.0f, false};

float clampf_local(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

float exp_map_norm(float norm, float low, float high) { return low * powf(high / low, norm); }

float lookup_exp2_0_4(float x) {
  x = clampf_local(x, 0.0f, 4.0f);
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
  for (int i = 0; i < EXP2_LUT_SIZE; i++) exp2_lut[i] = powf(2.0f, 4.0f * (float)i / (float)(EXP2_LUT_SIZE - 1));
  for (int adc = 0; adc < ADC_LUT_SIZE; adc++) {
    float a = (float)adc / 4095.0f;
    float decay_seconds = exp_map_norm(a, DECAY_MIN_SECONDS, DECAY_MAX_SECONDS);
    float duration_seconds = clampf_local(decay_seconds * 2.4f + 0.080f, 0.001f, 2.2f);
    duration_samples_lut[adc] = (uint32_t)(duration_seconds * SAMPLE_RATE);
    float decay_for_coef = decay_seconds < 0.004f ? 0.004f : decay_seconds;
    noise_coef_lut[adc] = expf(-1.0f / (decay_for_coef * SAMPLE_RATE));
    base_clock_lut[adc] = exp_map_norm(a, 1000.0f, 32000.0f);
    float pitch_env_time = 0.010f + a * 0.070f;
    pitch_coef_lut[adc] = expf(-1.0f / (pitch_env_time * SAMPLE_RATE));
    float hp_cutoff = 900.0f + a * 2600.0f;
    highpass_alpha_lut[adc] = 1.0f - expf(-2.0f * PI_F * hp_cutoff / SAMPLE_RATE);
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
  // 0.99161 per sample at 31.25 kHz, the same decay time at this rate
  voice.click_coef = powf(0.99161f, 31250.0f / SAMPLE_RATE);
  voice.click_level = 0.22f;
  voice.pitch_coef = pitch_coef_lut[adc2];
  voice.highpass_alpha = highpass_alpha_lut[adc2];
  voice.noise_level = 0.78f + env_amount * 0.10f;
  voice.drive = 1.25f + env_amount * 0.55f;
  voice.volume_scale = 1.0f;
  voice.in2_applied = false;
  if (in2_active) apply_in2_effect_to_voice();
}

float render_snare_sample(bool in2_now) {
  if (!voice.active) return 0.0f;
  if (!voice.in2_applied && voice.sample_index < IN2_GRACE_SAMPLES && in2_now) apply_in2_effect_to_voice();
  if (voice.sample_index >= voice.duration_samples || voice.noise_env < 0.00020f) {
    voice.active = false;
    return 0.0f;
  }
  float pitch_mul = lookup_exp2_0_4(voice.pitch_octaves * voice.pitch_env);
  float current_clock = voice.base_clock_hz * pitch_mul;
  if (current_clock < 120.0f) current_clock = 120.0f;
  // the original caps the LFSR clock at four steps a sample; keep the cap in Hz (125 kHz)
  if (current_clock > 125000.0f) current_clock = 125000.0f;
  voice.clock_phase += current_clock / SAMPLE_RATE;
  while (voice.clock_phase >= 1.0f) {
    advance_lfsr_once();
    voice.clock_phase -= 1.0f;
  }
  float lfsr_out = (voice.lfsr & 1u) ? 1.0f : -1.0f;
  voice.hp_low_state += voice.highpass_alpha * (lfsr_out - voice.hp_low_state);
  float tone_noise = lfsr_out - voice.hp_low_state;
  float raw = tone_noise * voice.noise_env * voice.noise_level;
  raw += lfsr_out * voice.click_env * voice.click_level;
  float out = raw * voice.drive * OUTPUT_GAIN * voice.volume_scale;
  voice.noise_env *= voice.noise_coef;
  voice.click_env *= voice.click_coef;
  voice.pitch_env *= voice.pitch_coef;
  voice.sample_index++;
  return out;
}

// ---- the MOD2's output stage, in software
// PWM duty, 8 bits, as audio_to_pwm_cc(): -1..1 to 0..255
float pwm_quantise(float x) {
  x = clampf_local(x, -1.0f, 1.0f);
  return (float)(uint16_t)((x * 0.5f + 0.5f) * 255.0f) / 255.0f;  // 0..1, the PWM's average
}

}  // namespace

class SnareCard : public ComputerCard {
 public:
  SnareCard() {
    setup_luts();
    SetFilter(true);
  }

 protected:
  void ProcessSample() override {
    // IN1 rising edge or the encoder's push starts the voice; IN2 high around it halves the level
    bool in2 = PulseIn2();
    if (in2) in2_hold = (uint32_t)(SAMPLE_RATE * IN2_HOLD_SECONDS);
    else if (in2_hold) in2_hold--;
    if (PulseIn1RisingEdge() || push_pending) {
      push_pending = false;
      start_snare(KnobVal(Main), KnobVal(X), PitchEnvAmount(), in2 || in2_hold);
    }

    // the filter switch: JP1. Up (or a middle position) 15.9 kHz, down 5.0 kHz
    bool bright = SwitchVal() != Down;
    if (bright != filter_bright) SetFilter(bright);

    float pwm = pwm_quantise(render_snare_sample(in2));  // 0..1 at the GPIO, 3.3 V full scale
    lp1 += rc_alpha * (pwm - lp1);                       // the two RC poles, R17/C13(+C14), R19/C16(+C17)
    lp2 += rc_alpha * (lp1 - lp2);
    float dc = lp2 * 3.3f * 3.06f;  // JP2 closed: DC-coupled, 0 to about +10 V, +5 V at rest
    hp += hp_alpha * (dc - hp);     // JP2 open: C18 into R20, about 1.6 Hz
    AudioOutVolts(0, dc - hp);      // OUT, AC-coupled: about +-5 V
    AudioOutVolts(1, dc);           // OUT DC
    LedOn(0, voice.active);
  }

  void BackgroundLoop() override {
    if (PushPressed()) push_pending = true;
  }

 private:
  // POT3 and the CV jack share the original's A2 (POT3 reads reversed). Here Y and CV in 1 add:
  // 0 to +5 V spans the whole pot.
  uint16_t PitchEnvAmount() {
    int32_t v = KnobVal(Y) + (int32_t)(CVIn1() * (6.0f / 2048) / 5.0f * 4095.0f);
    return (uint16_t)(v < 0 ? 0 : v > 4095 ? 4095 : v);
  }

  void SetFilter(bool bright) {
    filter_bright = bright;
    float fc = 1.0f / (2.0f * PI_F * 1000.0f * (bright ? 10e-9f : 32e-9f));  // 15.9 or 4.97 kHz
    rc_alpha = 1.0f - expf(-2.0f * PI_F * fc / SAMPLE_RATE);
  }

  volatile bool push_pending = false;
  uint32_t in2_hold = 0;
  bool filter_bright = true;
  float rc_alpha = 0, lp1 = 0.5f, lp2 = 0.5f;
  float hp = 0.5f * 3.3f * 3.06f;
  const float hp_alpha = 1.0f - expf(-2.0f * PI_F * 1.59f / SAMPLE_RATE);
};

SnareCard card;

int main() {
  card.Run();
}
