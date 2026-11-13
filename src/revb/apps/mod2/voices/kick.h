// HAGIWO MOD2 Kick Ver1.2 (modulove/MOD2, Firmware/kick/kick.ino), CC0 1.0 Universal: a sine kick
// with a curved pitch sweep, soft clip and decay, six parameters on two pages of three. Its engine is
// kept as published, at its own 36.6 kHz with 10-bit PWM. The original builds a 2048-point table when
// triggered (with the audio held off for the build) and plays it back with linear interpolation at
// a speed set by POT1; here each table point is built just before playback first needs it, by the
// same code in the same order, so the table is the same and the hit starts at once.
// The port (the class around it) is MIT, Copyright (c) 2026 David Baghurst.

#ifndef MOD2_VOICES_KICK_H
#define MOD2_VOICES_KICK_H

#include "../voice.h"

namespace mod2 {

class Kick final : public Voice {
  static constexpr float sys_clock = 150000000.0f;
  static constexpr float T = 0.3f;  // Kick playback duration (seconds)
  static constexpr float baseIncrement = (2048.0f * 4096.0f) / (T * sys_clock);
  static constexpr float dt = T / 2048.0f;
  static constexpr float FULL_SCALE = 1023.0f;
  static constexpr float MID_LEVEL = FULL_SCALE / 2.0f;
  static constexpr float PICKUP_THRESHOLD = 0.02f;
  static constexpr int NUM_CURVES = 32;
  static constexpr int LUT_SIZE = 256;
  static constexpr int SEGMENTS = 8;

 public:
  float Rate() const override { return FS_36K6; }
  uint16_t Wrap() const override { return 1023; }

  void Init() override {
    initParameterData();
    pitchMultiplier = paramData.pitchMult.value;
    softClipRate = paramData.softClip.value;
    decayRate = paramData.decay.value;
    f0 = paramData.startFreq.value;
    f1 = paramData.endFreq.value;
    selectedCurve = (uint8_t)paramData.curve.value;
    // Build 32 pitch-envelope LUTs
    const float curveMin = 0.1f, curveMax = 2.0f;
    const float step = (curveMax - curveMin) / float(NUM_CURVES - 1);
    for (int c = 0; c < NUM_CURVES; c++) {
      float curveVal = curveMin + step * c;
      for (int i = 0; i < LUT_SIZE; i++) pitchEnvLUTs[c][i] = powf(float(i) / float(LUT_SIZE - 1), curveVal);
    }
  }

  // EEPROM: pitchMult, softClip, decay, startFreq, endFreq, curve (floats, as put at 0..20)
  size_t Save(uint8_t *out) const override {
    PutF(out, 0, paramData.pitchMult.value);
    PutF(out, 1, paramData.softClip.value);
    PutF(out, 2, paramData.decay.value);
    PutF(out, 3, paramData.startFreq.value);
    PutF(out, 4, paramData.endFreq.value);
    PutF(out, 5, paramData.curve.value);
    return 24;
  }
  void Load(const uint8_t *in, size_t n) override {
    if (n < 24) return;
    float t = GetF(in, 0);
    if (!isnan(t) && t >= 0.5f && t <= 2.0f) pitchMultiplier = paramData.pitchMult.value = t;
    t = GetF(in, 1);
    if (!isnan(t) && t >= 0.5f && t <= 10.0f) softClipRate = paramData.softClip.value = t;
    t = GetF(in, 2);
    if (!isnan(t) && t >= 1.0f && t <= 10.0f) decayRate = paramData.decay.value = t;
    t = GetF(in, 3);
    if (!isnan(t) && t >= 3.0f && t <= 1026.0f) f0 = paramData.startFreq.value = t;
    t = GetF(in, 4);
    if (!isnan(t) && t >= 2.0f && t <= 513.0f) f1 = paramData.endFreq.value = t;
    t = GetF(in, 5);
    if (!isnan(t) && t >= 0 && t < NUM_CURVES) {
      paramData.curve.value = t;
      selectedCurve = (uint8_t)t;
    }
  }

  // onTrigger(): latch what make_wavetable() and the finalTable pass read, and start
  void Trigger() override {
    reduce_state = panel.in2;
    float ratio = f1 / f0;
    for (int i = 0; i <= SEGMENTS; i++) ratioLUT[i] = powf(ratio, float(i) / SEGMENTS);
    g_f0 = f0;
    g_curve = selectedCurve;
    g_reduce = 1 - (reduce_state * 0.5f);
    g_phase = 0.0f;
    g_env = 1.0f;
    g_expStep = expf(-decayRate * (1.0f / 2047.0f));
    g_softClip = softClipRate;
    g_clipNorm = 1.0f / tanhf(softClipRate);
    built = 0;
    kickPhase = 0.0f;
    kickPlaying = true;
  }

  // the button: switch pages, set the pickup targets and save, as loop() does
  void Button(bool pressed, uint32_t heldMs) override {
    (void)heldMs;
    if (!pressed) return;
    if (selectMode == 0) {
      paramData.pitchMult.value = pitchMultiplier;
      paramData.softClip.value = softClipRate;
      paramData.decay.value = decayRate;
    } else {
      paramData.startFreq.value = f0;
      paramData.endFreq.value = f1;
      paramData.curve.value = selectedCurve;
    }
    selectMode = !selectMode;
    if (selectMode == 0) {
      paramData.pitchMult.targetValue = (paramData.pitchMult.value - 0.5f) / 1.5f;
      paramData.pitchMult.pickupActive = true;
      paramData.softClip.targetValue = (paramData.softClip.value - 0.5f) / 9.5f;
      paramData.softClip.pickupActive = true;
      paramData.decay.targetValue = (paramData.decay.value - 1.0f) / 9.0f;
      paramData.decay.pickupActive = true;
    } else {
      paramData.startFreq.targetValue = (paramData.startFreq.value - 3.0f) / 1023.0f;
      paramData.startFreq.pickupActive = true;
      paramData.endFreq.targetValue = (paramData.endFreq.value - 2.0f) / 510.5f;
      paramData.endFreq.pickupActive = true;
      paramData.curve.targetValue = paramData.curve.value / float(NUM_CURVES - 1);
      paramData.curve.pickupActive = true;
    }
    saveRequested = true;
  }

  // loop()'s pot reading with pickup. The original averages four ADC reads; Rev B's driver smooths.
  void Controls() override {
    float pot1Val = panel.a[0], pot2Val = panel.a[1], pot3Val = panel.a[2];
    if (firstRun) {  // setup() sets every lastPotValue from the pots
      paramData.pitchMult.lastPotValue = paramData.startFreq.lastPotValue = pot1Val;
      paramData.softClip.lastPotValue = paramData.endFreq.lastPotValue = pot2Val;
      paramData.decay.lastPotValue = paramData.curve.lastPotValue = pot3Val;
    }
    if (selectMode == 0) {
      if (firstRun || checkPickup(&paramData.pitchMult, pot1Val)) {
        pitchMultiplier = 0.5f + 1.5f * pot1Val;
        paramData.pitchMult.value = pitchMultiplier;
      } else {
        pitchMultiplier = paramData.pitchMult.value;
      }
      if (firstRun || checkPickup(&paramData.softClip, pot2Val)) {
        softClipRate = 0.5f + 9.5f * pot2Val;
        paramData.softClip.value = softClipRate;
      } else {
        softClipRate = paramData.softClip.value;
      }
      if (firstRun || checkPickup(&paramData.decay, pot3Val)) {
        decayRate = 1.0f + 9.0f * pot3Val;
        paramData.decay.value = decayRate;
      } else {
        decayRate = paramData.decay.value;
      }
    } else {
      if (firstRun || checkPickup(&paramData.startFreq, pot1Val)) {
        f0 = pot1Val * 1023.0f + 3.0f;
        paramData.startFreq.value = f0;
      } else {
        f0 = paramData.startFreq.value;
      }
      if (firstRun || checkPickup(&paramData.endFreq, pot2Val)) {
        f1 = pot2Val * 510.5f + 2.0f;
        paramData.endFreq.value = f1;
      } else {
        f1 = paramData.endFreq.value;
      }
      if (firstRun || checkPickup(&paramData.curve, pot3Val)) {
        int c = int(pot3Val * NUM_CURVES);
        selectedCurve = c < NUM_CURVES - 1 ? c : NUM_CURVES - 1;
        paramData.curve.value = selectedCurve;
      } else {
        selectedCurve = (uint8_t)paramData.curve.value;
      }
    }
    firstRun = false;
  }

  // on_pwm_wrap()
  uint16_t Render() override {
    if (!kickPlaying) return (uint16_t)MID_LEVEL;
    float currInc = baseIncrement * pitchMultiplier;
    float index = kickPhase;
    uint16_t idx = (uint16_t)index;
    float frac = index - idx;
    while (built <= idx + 1 && built < 2048) BuildPoint(built++);
    uint16_t s1 = finalTable[idx];
    uint16_t s2 = finalTable[(idx + 1) % 2048];
    float interp_sample = s1 * (1.0f - frac) + s2 * frac;
    kickPhase += currInc;
    if (kickPhase >= 2048.0f) {
      kickPlaying = false;
      kickPhase = 0.0f;
      return (uint16_t)MID_LEVEL;
    }
    return (uint16_t)interp_sample;
  }

  bool Active() const override { return kickPlaying; }
  // the mode LED (the original leaves it dark until the first press; here it shows from the start)
  float Led() const override { return selectMode ? 1.0f : 0.0f; }
  void Label(char *out) const override { strcpy(out, selectMode ? "KICK 2" : "KICK 1"); }

 private:
  struct ParameterData {
    float value;
    float targetValue;
    bool pickupActive;
    float lastPotValue;
  };

  void initParameterData() {
    paramData.pitchMult = {1.0f, 0.0f, false, 0.5f};
    paramData.softClip = {1.0f, 0.0f, false, 0.0f};
    paramData.decay = {5.0f, 0.0f, false, 0.444f};
    paramData.startFreq = {250.0f, 0.0f, false, 0.243f};
    paramData.endFreq = {50.0f, 0.0f, false, 0.094f};
    paramData.curve = {0.0f, 0.0f, false, 0.0f};
  }

  static bool checkPickup(ParameterData *param, float currentPotValue) {
    if (!param->pickupActive) return true;
    float normalizedTarget = param->targetValue;
    bool crossedFromBelow = (param->lastPotValue < normalizedTarget - PICKUP_THRESHOLD) &&
                            (currentPotValue >= normalizedTarget - PICKUP_THRESHOLD);
    bool crossedFromAbove = (param->lastPotValue > normalizedTarget + PICKUP_THRESHOLD) &&
                            (currentPotValue <= normalizedTarget + PICKUP_THRESHOLD);
    if (crossedFromBelow || crossedFromAbove || fabsf(currentPotValue - normalizedTarget) < PICKUP_THRESHOLD) {
      param->pickupActive = false;
      return true;
    }
    param->lastPotValue = currentPotValue;
    return false;
  }

  // Table point i: make_wavetable()'s loop body, then the finalTable pass's (soft clip, decay, fade)
  void BuildPoint(int i) {
    float x = float(i) / 2047.0f;
    int lutIdx = int(x * (LUT_SIZE - 1));
    float x_adj = pitchEnvLUTs[g_curve][lutIdx];
    float segF = x_adj * SEGMENTS;
    int seg = int(segF);
    if (seg >= SEGMENTS) seg = SEGMENTS - 1;
    float segFrac = segF - seg;
    float ratio_pow = ratioLUT[seg] * (1.0f - segFrac) + ratioLUT[seg + 1] * segFrac;
    float f = g_f0 * ratio_pow;
    if (i > 0) g_phase = (float)((double)g_phase + 2.0 * PI_D * (double)f * (double)dt);
    float sample = sinf(g_phase) * g_reduce;
    uint16_t kick = uint16_t((sample + 1.0f) * (FULL_SCALE / 2.0f));  // kickTable[i]

    const float halfScale = FULL_SCALE / 2.0f;
    const float invHalfScale = 2.0f / FULL_SCALE;
    const int fadeStart = int(2048 * 0.95f);
    const int fadeDenom = 2047 - fadeStart;
    float bipolar = (kick - MID_LEVEL) * invHalfScale;
    float attenuated = bipolar * g_env;
    float clipped = tanhf(g_softClip * attenuated) * g_clipNorm;
    float sampleOut = clipped * halfScale + MID_LEVEL;
    if (i >= fadeStart) {
      float mu = (i - fadeStart) * (1.0f / fadeDenom);
      float mu2 = (1.0f - cosf((float)((double)mu * PI_D))) * 0.5f;
      sampleOut = (1.0f - mu2) * sampleOut + mu2 * MID_LEVEL;
    }
    finalTable[i] = uint16_t(sampleOut);
    g_env *= g_expStep;
  }

  float pitchEnvLUTs[NUM_CURVES][LUT_SIZE];
  uint16_t finalTable[2048];
  float ratioLUT[SEGMENTS + 1];
  struct {
    ParameterData pitchMult, softClip, decay, startFreq, endFreq, curve;
  } paramData;

  bool kickPlaying = false;
  float kickPhase = 0.0f;
  float pitchMultiplier = 1.0f;
  float softClipRate = 1.0f;
  uint8_t selectedCurve = 0;
  float f0 = 250.0f, f1 = 50.0f;
  float decayRate = 1.0f + 9.0f * (300.0f / 1023.0f);
  bool reduce_state = false;
  bool selectMode = 1;  // 0: pitch/clip/decay, 1: f0/f1/curve (the original starts here)
  bool firstRun = true;

  // the table build's state, latched at the trigger
  int built = 0;
  float g_f0 = 250.0f, g_reduce = 1.0f, g_phase = 0.0f, g_env = 1.0f, g_expStep = 1.0f;
  float g_softClip = 1.0f, g_clipNorm = 1.0f;
  uint8_t g_curve = 0;
};

}  // namespace mod2

#endif
