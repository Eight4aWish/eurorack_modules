// HAGIWO MOD2 FM Drum Ver1.2 (modulove/MOD2, Firmware/fm_drum/fm_drum.ino), CC0 1.0 Universal: a
// two-operator FM percussion voice, six parameters on two pages of three. Its engine is kept as
// published, at its own 36.6 kHz with 10-bit PWM. The original builds a 4096-point table when
// triggered (with the audio held off for the build) and plays it back over 0.3 s; here each table
// point is built just before playback first needs it, by the same code in the same order, so the
// table is the same and the hit starts at once.
// The port (the class around it) is MIT, Copyright (c) 2026 David Baghurst.

#ifndef MOD2_VOICES_FM_DRUM_H
#define MOD2_VOICES_FM_DRUM_H

#include "../voice.h"

namespace mod2 {

class FmDrum final : public Voice {
  static constexpr int TABLE_SIZE = 4096;
  static constexpr int TABLE_MASK = TABLE_SIZE - 1;
  static constexpr float TABLE_INV = 1.0f / (TABLE_SIZE - 1);
  static constexpr float SYS_CLOCK = 150000000.0f;
  static constexpr float NOTE_LEN = 0.3f;
  static constexpr float BASE_INC = (TABLE_SIZE * 4096.0f) / (NOTE_LEN * SYS_CLOCK);
  static constexpr float DT = NOTE_LEN / TABLE_SIZE;
  static constexpr float FULL_SCALE = 1023.0f;
  static constexpr float MID_LEVEL = FULL_SCALE / 2.0f;
  static constexpr float PICKUP_THRESHOLD = 0.05f;

 public:
  float Rate() const override { return FS_36K6; }
  uint16_t Wrap() const override { return 1023; }

  void Init() override {
    paramData.pitch = {200.0f, 0.0f, false, 0.0f};
    paramData.operatorRatio = {2.0f, 0.0f, false, 0.0f};
    paramData.modIndexM0 = {1.0f, 0.0f, false, 0.0f};
    paramData.decayTime = {5.0f, 0.0f, false, 0.0f};
    paramData.ratioEnvelope = {0.0f, 0.0f, false, 0.0f};
    paramData.modIndexM1 = {1.0f, 0.0f, false, 0.0f};
    Apply();
  }

  // EEPROM: pitch, operatorRatio, modIndexM0, decayTime, ratioEnvelope (floats, as put at 0..16)
  size_t Save(uint8_t *out) const override {
    PutF(out, 0, paramData.pitch.value);
    PutF(out, 1, paramData.operatorRatio.value);
    PutF(out, 2, paramData.modIndexM0.value);
    PutF(out, 3, paramData.decayTime.value);
    PutF(out, 4, paramData.ratioEnvelope.value);
    return 20;
  }
  void Load(const uint8_t *in, size_t n) override {
    if (n < 20) return;
    float t = GetF(in, 0);
    if (!isnan(t) && t >= 30.0f && t <= 1200.0f) paramData.pitch.value = t;
    t = GetF(in, 1);
    if (!isnan(t) && t >= 0.5f && t <= 8.0f) paramData.operatorRatio.value = t;
    t = GetF(in, 2);
    if (!isnan(t) && t >= 1.0f && t <= 10.0f) paramData.modIndexM0.value = t;
    t = GetF(in, 3);
    if (!isnan(t) && t >= 0.5f && t <= 10.0f) paramData.decayTime.value = t;
    t = GetF(in, 4);
    if (!isnan(t) && t >= 0.0f && t <= 1.0f) paramData.ratioEnvelope.value = t;
    Apply();
  }

  // the trigger ISR: latch what make_wavetable() and the final-table pass read, and start
  void Trigger() override {
    accentState = panel.in2;
    g_f0 = f0;
    g_opRatio = opRatio;
    g_modIndex = modIndex;
    g_ratioEnv = ratioEnv;
    g_phaseC = g_phaseM = 0.0f;
    g_env = 1.0f;
    g_expStep = expf(-decayRate * (1.0f / (TABLE_SIZE - 1)));
    g_softClipK = softClipK;
    g_clipNorm = 1.0f / tanhf(softClipK);
    g_level = accentState ? 0.5f : 1.0f;
    built = 0;
    phase = 0.0f;
    noteOn = true;
  }

  // the button: switch pages, set the pickup targets and save, as loop() does
  void Button(bool pressed, uint32_t heldMs) override {
    (void)heldMs;
    if (!pressed) return;
    if (!editMode) {
      paramData.pitch.value = f0;
      paramData.operatorRatio.value = opRatio;
      paramData.modIndexM0.value = modIndex;
    } else {
      paramData.decayTime.value = decayRate;
      paramData.ratioEnvelope.value = ratioEnv;
      paramData.modIndexM1.value = modIndex;
    }
    editMode = !editMode;
    if (!editMode) {
      paramData.pitch.targetValue = (paramData.pitch.value - 30.0f) / 1170.0f;
      paramData.pitch.pickupActive = true;
      paramData.operatorRatio.targetValue = (paramData.operatorRatio.value - 0.5f) / 7.5f;
      paramData.operatorRatio.pickupActive = true;
      paramData.modIndexM0.targetValue = 1.0f - ((paramData.modIndexM0.value - 1.0f) / 9.0f);
      paramData.modIndexM0.pickupActive = true;
    } else {
      paramData.decayTime.targetValue = 1.0f - ((paramData.decayTime.value - 0.5f) / 9.5f);
      paramData.decayTime.pickupActive = true;
      paramData.ratioEnvelope.targetValue = 1.0f - paramData.ratioEnvelope.value;
      paramData.ratioEnvelope.pickupActive = true;
      paramData.modIndexM1.targetValue = 1.0f - ((paramData.modIndexM1.value - 1.0f) / 9.0f);
      paramData.modIndexM1.pickupActive = true;
    }
    saveRequested = true;
  }

  // loop()'s pot reading with pickup. The original averages five ADC reads; Rev B's driver smooths.
  void Controls() override {
    float pot1Val = panel.a[0], pot2Val = panel.a[1], pot3Val = panel.a[2];
    if (!editMode) {
      if (checkPickup(&paramData.pitch, pot1Val)) {
        f0 = 30.0f + 1170.0f * pot1Val;
        paramData.pitch.value = f0;
      } else {
        f0 = paramData.pitch.value;
      }
      if (checkPickup(&paramData.operatorRatio, pot2Val)) {
        opRatio = 0.5f + 7.5f * pot2Val;
        paramData.operatorRatio.value = opRatio;
      } else {
        opRatio = paramData.operatorRatio.value;
      }
      if (checkPickup(&paramData.modIndexM0, pot3Val)) {
        modIndex = 1.0f + 9.0f * (1.0f - pot3Val);
        paramData.modIndexM0.value = modIndex;
      } else {
        modIndex = paramData.modIndexM0.value;
      }
      softClipK = modIndex;
    } else {
      if (checkPickup(&paramData.decayTime, pot1Val)) {
        decayRate = 0.5f + 9.5f * (1.0f - pot1Val);
        paramData.decayTime.value = decayRate;
      } else {
        decayRate = paramData.decayTime.value;
      }
      if (checkPickup(&paramData.ratioEnvelope, pot2Val)) {
        ratioEnv = 1.0f - pot2Val;
        paramData.ratioEnvelope.value = ratioEnv;
      } else {
        ratioEnv = paramData.ratioEnvelope.value;
      }
      if (checkPickup(&paramData.modIndexM1, pot3Val)) {
        modIndex = 1.0f + 9.0f * (1.0f - pot3Val);
        paramData.modIndexM1.value = modIndex;
      } else {
        modIndex = paramData.modIndexM1.value;
      }
      softClipK = modIndex;
    }
  }

  // on_pwm_wrap()
  uint16_t Render() override {
    if (!noteOn) return (uint16_t)MID_LEVEL;
    float idx = phase;
    uint32_t i = (uint32_t)idx;
    float frac = idx - i;
    while (built <= (int)i + 1 && built < TABLE_SIZE) BuildPoint(built++);
    uint16_t s1 = finalTable[i];
    uint16_t s2 = finalTable[(i + 1) & TABLE_MASK];
    float y = s1 * (1.0f - frac) + s2 * frac;
    phase += BASE_INC;
    if (phase >= (float)TABLE_SIZE) {
      noteOn = false;
      phase = 0.0f;
      return (uint16_t)MID_LEVEL;
    }
    return (uint16_t)y;
  }

  bool Active() const override { return noteOn; }
  float Led() const override { return editMode ? 1.0f : 0.0f; }  // the mode LED
  void Label(char *out) const override { strcpy(out, editMode ? "FM DRUM 2" : "FM DRUM 1"); }

 private:
  struct ParameterData {
    float value;
    float targetValue;
    bool pickupActive;
    float lastPotValue;
  };

  // setup(): the restored (or default) parameters become the live ones
  void Apply() {
    f0 = paramData.pitch.value;
    opRatio = paramData.operatorRatio.value;
    paramData.modIndexM1.value = paramData.modIndexM0.value;
    modIndex = paramData.modIndexM0.value;
    if (modIndex < 1.0f) modIndex = 1.0f;
    softClipK = modIndex;
    decayRate = paramData.decayTime.value;
    ratioEnv = paramData.ratioEnvelope.value;
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

  // Table point i: make_wavetable()'s loop body, then the final-table pass's (clip, accent, fades)
  void BuildPoint(int i) {
    const float twoPi = (float)(2.0 * PI_D);
    const float stepC = twoPi * g_f0 * DT;
    float x = i * TABLE_INV;
    float envR = 1.0f - g_ratioEnv * x;
    float envI = 1.0f - indexEnv * x;
    float stepM = twoPi * g_f0 * (g_opRatio * envR) * DT;
    g_phaseM += stepM;
    g_phaseC += stepC;
    float sample = sinf(g_phaseC + (g_modIndex * envI) * sinf(g_phaseM));
    uint16_t raw = (uint16_t)((sample + 1.0f) * (FULL_SCALE / 2.0f));  // rawTable[i]

    const int fadeInEnd = (int)(TABLE_SIZE * 0.02f);
    const int fadeOutStart = (int)(TABLE_SIZE * 0.90f);
    const int fadeOutDen = TABLE_SIZE - 1 - fadeOutStart;
    const float halfScale = FULL_SCALE / 2.0f;
    const float invHalfScale = 2.0f / FULL_SCALE;
    float bipolar = (raw - MID_LEVEL) * invHalfScale;
    float clipped = tanhf(g_softClipK * bipolar * g_env) * g_clipNorm;
    clipped *= g_level;
    float y = clipped * halfScale + MID_LEVEL;
    if (i < fadeInEnd) {
      float mu = (float)i / fadeInEnd;
      float mu2 = (1.0f - cosf((float)((double)mu * PI_D))) * 0.5f;
      y = (1.0f - mu2) * MID_LEVEL + mu2 * y;
    } else if (i >= fadeOutStart) {
      float mu = (float)(i - fadeOutStart) / fadeOutDen;
      float mu2 = (1.0f - cosf((float)((double)mu * PI_D))) * 0.5f;
      y = (1.0f - mu2) * y + mu2 * MID_LEVEL;
    }
    finalTable[i] = (uint16_t)y;
    g_env *= g_expStep;
  }

  uint16_t finalTable[TABLE_SIZE];
  struct {
    ParameterData pitch, operatorRatio, modIndexM0, decayTime, ratioEnvelope, modIndexM1;
  } paramData;

  float f0 = 200.0f, opRatio = 2.0f, modIndex = 1.0f, decayRate = 5.0f, ratioEnv = 0.0f;
  const float indexEnv = 0.3f;  // index envelope depth (fixed)
  float softClipK = 1.0f;
  bool accentState = false;
  bool noteOn = false;
  float phase = 0.0f;
  bool editMode = false;  // false: Mode0, true: Mode1

  // the table build's state, latched at the trigger
  int built = 0;
  float g_f0 = 200.0f, g_opRatio = 2.0f, g_modIndex = 1.0f, g_ratioEnv = 0.0f;
  float g_phaseC = 0.0f, g_phaseM = 0.0f, g_env = 1.0f, g_expStep = 1.0f;
  float g_softClipK = 1.0f, g_clipNorm = 1.0f, g_level = 1.0f;
};

}  // namespace mod2

#endif
