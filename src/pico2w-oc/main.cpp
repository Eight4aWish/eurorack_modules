// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_MCP4728.h>
#include <math.h>
#include <Bounce2.h>
#include <EEPROM.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

#include "eurorack_ui/OledHelpers.hpp"
#include "eurorack_ui/OledHomeMenu.hpp"

// -------------------- Pins / Addresses (moved to header) --------------------
#include "pico2w-oc/pins.h"
// Optional static calibration (codes↔volts) from measured fits
#ifdef USE_STATIC_CALIB
#include "pico2w-oc/calib_static.h"
#endif

// -------------------- OLED --------------------
Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);  // OLED on Wire (I2C0)

// -------------------- Devices --------------------
Adafruit_ADS1115  ads;    // ADS on Wire1 (I2C1)
Adafruit_MCP4728  mcp;    // MCP on Wire1 (I2C1)

// -------------------- USB MIDI --------------------
Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// -------------------- Button --------------------
Bounce btn;

// -------------------- Patch recall (EEPROM) --------------------
// Save the last-launched patch so it auto-resumes on next power-on,
// mirroring the EuroPi bootloader behaviour.
#define EEPROM_SIZE       16
#define EEPROM_MAGIC_ADDR  0
#define EEPROM_BANK_ADDR   1
#define EEPROM_PATCH_ADDR  2
#define EEPROM_MAGIC_VAL   0xA5

static void saveLastPatch(uint8_t bank, uint8_t patch) {
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
  EEPROM.write(EEPROM_BANK_ADDR,  bank);
  EEPROM.write(EEPROM_PATCH_ADDR, patch);
  EEPROM.commit();
}
static void clearLastPatch() {
  EEPROM.write(EEPROM_MAGIC_ADDR, 0x00);
  EEPROM.commit();
}

// -------------------- Timing --------------------
static uint32_t lastUiMs = 0;
static uint32_t lastTickMs = 0;
#define UI_FRAME_MS_ACTIVE  50
#define UI_FRAME_MS_SLOW   150   // slower OLED updates for CV-critical patches (Env, LFO)
#define CTRL_TICK_MS         2   // 2 ms = 500 Hz tick; reduces clock phase quantisation

// -------------------- Calibration-ish --------------------
// ADS1115 scale via library's computeVolts()
// Default mapping assumes an INVERTING front-end around ~1.65V midpoint.
// If calibration data is present, that will override these defaults.
static float cvBias = 1.65f;
static float cvGain = -(10.0f / 3.3f);  // inverted mapping: (cvBias - adcV) * |gain| -> ±5V

// MCP4728 DAC pin voltage domain.
// This build assumes the MCP4728 is powered from 3.3V and therefore VOUT pins are ~0..3.3V.
static float mcpVdd = 3.3f; // Set to the measured MCP4728 VDD if different
static float mcpVoltsPerCode = (mcpVdd / 4095.0f);

// ---- Forward declarations for calibration helpers ----
static float mapAdsToCv(float adsV);
static uint16_t voltsToDac(int ch, float v);
// Eurorack output stage scaling (your analog stage 0..Vref -> ±5V)
static float dacToEurorackGain = (10.0f / mcpVdd);
// Gate output codes (ignore calibration for clock-like gates)
static const uint16_t kGateLowCode = 2047; // ~0V baseline
static const uint16_t kGateHighCode = 0;   // ~+5V on your hardware

// Gate detection thresholds (raw ADS1115 codes at GAIN_ONE).
// The inverting front-end means LOWER codes = HIGHER Eurorack voltage.
// With default mapping: code ≈ 13500 at 0V, ≈ 1500 at +5V.
// Gate-ON threshold  ~+1.5V Eurorack (code drops below this).
// Gate-OFF threshold ~+0.6V Eurorack (code rises above this) for hysteresis.
static const int16_t kGateOnThresh  = 10000;
static const int16_t kGateOffThresh = 12000;

// External-clock timeout: if no edge arrives within this many ms, revert to internal mode.
static const uint32_t kExtClockTimeoutMs = 2000;

// -------------------- State --------------------
struct Patch {
  const char* name;
  void (*enter)();
  void (*tick)();
  void (*render)();
  bool slowOled; // true for CV-critical patches that need slower OLED refresh
};

struct Bank {
  const char* name;
  Patch* patches[12];
  uint8_t patchCount;
};

static bool haveSSD = false, haveADS = false, haveMCP = false;
// Diag patch DAC control state
static int diag_sel_dac = 0; // which DAC channel is under manual control
static float pot1 = 0, pot2 = 0, pot3 = 0;
static float adc0V = 0, adc1V = 0;
static float cv0V  = 0, cv1V  = 0;
static int16_t ads_raw0 = 0, ads_raw1 = 0;
static uint16_t mcp_values[4] = {0, 0, 0, 0};

// Helper: write mcp_values[] to the MCP4728.
// mcp_values[] is indexed by PHYSICAL DAC channel (0=A, 1=B, 2=C, 3=D), and
// patches store each logical CV into its physical slot via the CVx_DA_CH macros
// (e.g. mcp_values[CV0_DA_CH] = ...). fastWrite() takes channels in A,B,C,D
// order, so we just pass the physical slots straight through.
static void mcp_writeAll() {
  mcp.fastWrite(mcp_values[0], mcp_values[1], mcp_values[2], mcp_values[3]);
}

static char mcpPhysLetter(uint8_t phys) {
  switch (phys & 0x3) {
    case 0: return 'A';
    case 1: return 'B';
    case 2: return 'C';
    default: return 'D';
  }
}

// ---- MCP4728 readback helpers (debug) ----
static bool mcp4728_readAll(uint8_t *buf24) {
  if (!buf24) return false;
  Wire1.requestFrom((uint8_t)I2C_ADDR_MCP, (uint8_t)24);
  if (Wire1.available() < 24) return false;
  for (int i = 0; i < 24; i++) {
    buf24[i] = (uint8_t)Wire1.read();
  }
  return true;
}

static void mcp4728_decodeInputRegWord(const uint8_t *buf24, int ch /*0..3*/, uint16_t &value12, uint8_t &vref, uint8_t &gain, uint8_t &pd) {
  // Buffer layout matches Adafruit library usage: each channel has 6 bytes:
  // [0..2]=output reg, [3..5]=EEPROM. For input-reg fields, the library uses
  // indices {1,2} within each 6-byte block.
  const int base = 6 * ch;
  const uint16_t w = (uint16_t(buf24[base + 1]) << 8) | uint16_t(buf24[base + 2]);
  vref = (w >> 15) & 0x01;
  pd   = (w >> 13) & 0x03;
  gain = (w >> 12) & 0x01;
  value12 = w & 0x0FFF;
}
static volatile bool patchShortPressed = false;

// Normalized pot read (inverted so clockwise increases value)
float readPotNorm(int pin) {
  int v = analogRead(pin);
  return constrain(1.0f - (v / 4095.0f), 0.0f, 1.0f);
}

// Smoothed pot read (inverted), simple exponential moving average
static float potSmooth[3] = {0.0f, 0.0f, 0.0f};
// Reset smoothed pot values so a patch switch doesn't carry stale state.
static void resetPotSmooth() {
  potSmooth[0] = readPotNorm(PIN_POT1);
  potSmooth[1] = readPotNorm(PIN_POT2);
  potSmooth[2] = readPotNorm(PIN_POT3);
}
float readPotNormSmooth(int pin, int idx) {
  // Light oversampling for stability
  int acc = 0; const int samples = 4;
  for (int i=0;i<samples;i++) acc += analogRead(pin);
  int v = acc / samples;
  float norm = constrain(1.0f - (v / 4095.0f), 0.0f, 1.0f);
  const float alpha = 0.05f; // stronger smoothing
  potSmooth[idx] = (1.0f - alpha) * potSmooth[idx] + alpha * norm;
  return potSmooth[idx];
}

// ---- Pot soft-takeover (pickup) ----------------------------------------
// When a patch switches which voice/target the pots edit, the physical pot
// positions no longer match the newly-selected target's stored values. Without
// pickup the next tick snaps (clobbers) those values to wherever the knobs
// happen to sit. Pickup holds a parameter until its pot crosses (or already
// matches) the stored value, then tracks normally — standard "soft takeover".
struct PotPickup {
  bool  caught[3] = {true, true, true};
  float ref[3]    = {0.0f, 0.0f, 0.0f};
};
// Make all pots immediately live (e.g. on patch entry, where stored values are
// fresh defaults with nothing worth preserving).
static void pickup_setLive(PotPickup& pk) {
  pk.caught[0] = pk.caught[1] = pk.caught[2] = true;
}
// Arm pickup after a target switch: pots go inactive until they re-catch.
// `p0..p2` are the current normalized pot readings (the side to cross from).
static void pickup_arm(PotPickup& pk, float p0, float p1, float p2) {
  pk.ref[0] = p0; pk.ref[1] = p1; pk.ref[2] = p2;
  pk.caught[0] = pk.caught[1] = pk.caught[2] = false;
}
// Returns true once pot `i` should drive its parameter. `target` is that
// parameter's current normalized (0..1) value.
static bool pickup_update(PotPickup& pk, int i, float potNow, float target) {
  if (pk.caught[i]) return true;
  const float eps = 0.02f;
  if (fabsf(potNow - target) <= eps) { pk.caught[i] = true; return true; }
  // Caught once the pot moves to the opposite side of the target value.
  if ((pk.ref[i] - target) * (potNow - target) < 0.0f) { pk.caught[i] = true; return true; }
  return false;
}

void i2cScan(bool &ssd, bool &adsOK, bool &mcpOK) {
  ssd = adsOK = mcpOK = false;
  // OLED is on Wire (I2C0)
  Wire.beginTransmission(I2C_ADDR_SSD1306);
  if (Wire.endTransmission() == 0) ssd = true;
  // ADS + MCP are on Wire1 (I2C1)
  for (uint8_t addr : {I2C_ADDR_ADS, I2C_ADDR_MCP}) {
    Wire1.beginTransmission(addr);
    uint8_t err = Wire1.endTransmission();
    if (addr == I2C_ADDR_ADS && err == 0) adsOK = true;
    if (addr == I2C_ADDR_MCP && err == 0) mcpOK = true;
  }
}

void drawBar(int x, int y, int w, int h, float norm) {
  eurorack_ui::drawBar(oled, x, y, w, h, norm, false);
}

// Small local UI forwarding helpers to keep call sites concise
namespace ui {
static void printClipped(int x, int y, int w, const char* s) { eurorack_ui::printClipped(oled, x, y, w, s); }
static void printClippedBold(int x, int y, int w, const char* s, bool bold) { eurorack_ui::printClippedBold(oled, x, y, w, s, bold); }
static void printLabelOnly(int x, int y, int w, const char* label) { eurorack_ui::printLabelOnly(oled, x, y, w, label); }
static void drawBarF(int x, int y, int w, int h, float norm) { eurorack_ui::drawBar(oled, x, y, w, h, norm, false); }
}

// -------------------- Shared musical key --------------------
// One scale/root/octave shared by every pitched patch (Turing, Acid, Quant...).
// State is module-global and persists across patch switches so the instrument
// keeps a single "key" no matter which patch is on screen. Patches that don't
// need every field (e.g. Quant ignores octave) simply use what applies.
struct Scale { const char* name; uint8_t size; uint8_t semis[12]; };
static const Scale kScales[] = {
  { "Chromatic", 12, {0,1,2,3,4,5,6,7,8,9,10,11} },
  { "Major",      7, {0,2,4,5,7,9,11} },
  { "Minor",      7, {0,2,3,5,7,8,10} },
  { "MinPent",    5, {0,3,5,7,10} },
  { "MajPent",    5, {0,2,4,7,9} },
  { "Dorian",     7, {0,2,3,5,7,9,10} },
};
static const int kScaleCount = sizeof(kScales) / sizeof(kScales[0]);
static const char* kNoteNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

// Module-global key (defaults: C Minor, base octave 1V). Not reset on patch entry.
static uint8_t g_scale  = 2; // Minor
static uint8_t g_root   = 0; // C
static uint8_t g_octave = 1; // base octave (volts)

// Map a scale degree (0..) to output volts using the shared key, clamped 0..5V.
// If `pitchClass` is non-null it receives the 0..11 pitch class for display.
static float key_degreeToVolts(int degree, uint8_t* pitchClass = nullptr) {
  const Scale& sc = kScales[g_scale];
  int oct  = degree / sc.size;
  int idx  = degree % sc.size;
  int semi = oct * 12 + sc.semis[idx];
  if (pitchClass) *pitchClass = (uint8_t)((g_root + semi) % 12);
  float v = (float)g_octave + (float)(g_root + semi) / 12.0f;
  if (v < 0.0f) v = 0.0f; else if (v > 5.0f) v = 5.0f;
  return v;
}

// Snap an input voltage to the nearest note of the shared scale+root, returning
// volts (clamped 0..5V). Used by Quant. Searches scale tones across octaves.
static float key_quantizeVolts(float v) {
  const Scale& sc = kScales[g_scale];
  float s = v * 12.0f;                 // input in semitones from 0V
  float best = 0.0f, bestErr = 1e30f;
  for (int oct = -1; oct <= 6; oct++) {
    for (int i = 0; i < sc.size; i++) {
      float cand = (float)((int)g_root + sc.semis[i] + 12 * oct);
      float err = fabsf(cand - s);
      if (err < bestErr) { bestErr = err; best = cand; }
    }
  }
  float volts = best / 12.0f;
  if (volts < 0.0f) volts = 0.0f; else if (volts > 5.0f) volts = 5.0f;
  return volts;
}

// Shared KEY edit page used by any pitched patch. Pot1 scale, Pot2 root,
// Pot3 octave, all via pickup so they don't snap on arrival. Pass the three
// normalized (0..1) pot readings; `pk` is the caller's PotPickup.
static void key_update(PotPickup& pk, float p1, float p2, float p3) {
  float tScale = ((float)g_scale + 0.5f) / (float)kScaleCount;
  float tRoot  = ((float)g_root + 0.5f) / 12.0f;
  float tOct   = (float)g_octave / 4.0f;
  if (pickup_update(pk, 0, p1, tScale)) {
    int s = (int)(p1 * kScaleCount); if (s >= kScaleCount) s = kScaleCount - 1;
    g_scale = (uint8_t)s;
  }
  if (pickup_update(pk, 1, p2, tRoot)) {
    int rt = (int)(p2 * 12.0f); if (rt > 11) rt = 11;
    g_root = (uint8_t)rt;
  }
  if (pickup_update(pk, 2, p3, tOct)) {
    int oc = (int)(p3 * 4.99f); if (oc > 4) oc = 4;
    g_octave = (uint8_t)oc;
  }
}
// Render the shared KEY page body (rows at y=16/26). Caller draws header/footer.
static void key_draw() {
  oled.setCursor(0, 16); oled.print("Scale "); oled.print(kScales[g_scale].name);
  oled.setCursor(0, 26); oled.print("Root "); oled.print(kNoteNames[g_root]);
  oled.setCursor(64, 26); oled.print("Oct "); oled.print(g_octave);
}

// -------------------- Patch: Util/Diag --------------------
void diag_enter() { resetPotSmooth(); }

void diag_tick() {
  // Pots
  // Use smoothed, inverted reads to reduce jitter in displayed values
  pot1 = readPotNormSmooth(PIN_POT1, 0);
  pot2 = readPotNormSmooth(PIN_POT2, 1);
  pot3 = readPotNormSmooth(PIN_POT3, 2);

  // ADS1115: read A0 and A1 single-ended
  if (haveADS) {
    int16_t a0 = ads.readADC_SingleEnded(AD0_CH);
    int16_t a1 = ads.readADC_SingleEnded(AD1_CH);
    ads_raw0 = a0;
    ads_raw1 = a1;
    adc0V = ads.computeVolts(a0);
    adc1V = ads.computeVolts(a1);
    // Map to Eurorack CV domain using calibration if available (handles inversion),
    // otherwise fall back to default inverted mapping above.
    cv0V  = mapAdsToCv(adc0V);
    cv1V  = mapAdsToCv(adc1V);
  } else {
    adc0V = adc1V = cv0V = cv1V = NAN;
    ads_raw0 = ads_raw1 = 0;
  }

  // DAC manual test: short press cycles logical DAC index (0..3). Pot1 sets ONLY that physical channel using mapping macros.
  if (haveMCP) {
    if (patchShortPressed) {
      diag_sel_dac = (diag_sel_dac + 1) & 0x3; // cycle CV0->CV3
      patchShortPressed = false;
    }
    // Clear all first (physical indices)
        mcp_values[0] = 0; mcp_values[1] = 0; mcp_values[2] = 0; mcp_values[3] = 0;
    // Map selected physical CV (CV0..CV3) to underlying DA channel using pins.h
    const uint8_t cv_phys[4] = { CV0_DA_CH, CV1_DA_CH, CV2_DA_CH, CV3_DA_CH };
    uint8_t phys = cv_phys[diag_sel_dac];
    mcp_values[phys] = (uint16_t)(pot1 * 4095.0f);
    // Write in physical channel order
        mcp_writeAll();
  }
}

void diag_render() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  // Disable wrap so numeric prints don't bleed into next lines
  oled.setTextWrap(false);
  ui::printClipped(0, 0, 64, "Diag");
  // Keep O/A/M status visible on Diag screen
  oled.setCursor(66, 0);
  oled.print(haveSSD ? "O" : "-"); oled.print(' ');
  oled.print(haveADS ? "A" : "-"); oled.print(' ');
  oled.print(haveMCP ? "M" : "-");

  // Show logical CV -> physical MCP4728 channel mapping
  oled.setCursor(0, 8);
  oled.print("CV0"); oled.print(mcpPhysLetter(CV0_DA_CH)); oled.print(' ');
  oled.print("CV1"); oled.print(mcpPhysLetter(CV1_DA_CH)); oled.print(' ');
  oled.print("CV2"); oled.print(mcpPhysLetter(CV2_DA_CH)); oled.print(' ');
  oled.print("CV3"); oled.print(mcpPhysLetter(CV3_DA_CH));

  // Button + Pots (show raw ADC values)
  oled.setCursor(0, 16);
  oled.print("BTN "); oled.print(btn.read() == LOW ? "DOWN" : "UP  ");

  // Mild smoothing for on-screen stability (does not affect logic elsewhere)
  static bool diag_init = false;
  static float potDisp1 = 0, potDisp2 = 0, potDisp3 = 0;
  static float adsDisp0 = 0, adsDisp1 = 0;
  const float alpha = 0.05f; // stronger smoothing for steadier readouts

  int raw1_now = analogRead(PIN_POT1);
  int raw2_now = analogRead(PIN_POT2);
  int raw3_now = analogRead(PIN_POT3);
  if (!diag_init) {
    potDisp1 = raw1_now; potDisp2 = raw2_now; potDisp3 = raw3_now;
    adsDisp0 = ads_raw0; adsDisp1 = ads_raw1; diag_init = true;
  } else {
    potDisp1 = (1.0f - alpha) * potDisp1 + alpha * raw1_now;
    potDisp2 = (1.0f - alpha) * potDisp2 + alpha * raw2_now;
    potDisp3 = (1.0f - alpha) * potDisp3 + alpha * raw3_now;
    adsDisp0 = (1.0f - alpha) * adsDisp0 + alpha * (float)ads_raw0;
    adsDisp1 = (1.0f - alpha) * adsDisp1 + alpha * (float)ads_raw1;
  }

  int raw1 = (int)(potDisp1 + 0.5f);
  int raw2 = (int)(potDisp2 + 0.5f);
  int raw3 = (int)(potDisp3 + 0.5f);
  // Line: Btn <status>    P1 <raw>
  oled.setCursor(64, 16); oled.print("P1 "); oled.print(raw1);
  // Next line: P2 <raw>    P3 <raw>
  oled.setCursor(0, 26); oled.print("P2 "); oled.print(raw2);
  oled.setCursor(64, 26); oled.print("P3 "); oled.print(raw3);

  // CV inputs raw
  // ADS1115 raw codes (16-bit signed), lightly smoothed for display only.
  oled.setCursor(0, 36); oled.print("ADC0 "); oled.print((int)(adsDisp0 + 0.5f));
  oled.setCursor(64, 36); oled.print("ADC1 "); oled.print((int)(adsDisp1 + 0.5f));

  // Show physical CV outputs (CV0..CV3) mapped to their DA channels
  oled.setCursor(0, 46);  oled.print(diag_sel_dac==0?">":" "); oled.print("CV0 "); oled.print(mcp_values[CV0_DA_CH]);
  oled.setCursor(64, 46); oled.print(diag_sel_dac==1?">":" "); oled.print("CV1 "); oled.print(mcp_values[CV1_DA_CH]);
  oled.setCursor(0, 56);  oled.print(diag_sel_dac==2?">":" "); oled.print("CV2 "); oled.print(mcp_values[CV2_DA_CH]);
  oled.setCursor(64, 56); oled.print(diag_sel_dac==3?">":" "); oled.print("CV3 "); oled.print(mcp_values[CV3_DA_CH]);

  oled.display();
}

// -------------------- Registry --------------------
Patch patch_diag = { "Diag", diag_enter, diag_tick, diag_render, false };
// -------------------- Patch: Clock (lightweight) --------------------
static bool clock_running = false;
static uint32_t clock_last_internal_ms = 0;
static uint32_t clock_last_external_edge_ms = 0;
static uint32_t clock_base_interval_ms = 500; // default 120 BPM -> 500ms per beat
static uint32_t clock_ext_interval_ms = 0;
static float clock_ext_interval_smooth = 0.0f; // smoothed external interval for stability
static bool clock_ext_resync = false; // set true on each external rising edge
static bool clock_ext_gate = false; // track gate state for threshold detection
// division / multiplication options
static const char* div_labels[] = { "/16", "/12", "/8", "/6", "/5", "/4", "/3", "/2", "1", "x2", "x3", "x4", "x5", "x6", "x8" };
static const float div_factors[] = { 1.0f/16, 1.0f/12, 1.0f/8, 1.0f/6, 1.0f/5, 0.25f, 0.3333333f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };
static const int kDivCount = sizeof(div_factors) / sizeof(div_factors[0]);
// per-channel scheduling
static uint32_t ch_next_fire_ms[4] = {0,0,0,0};
static uint32_t ch_pulse_end_ms[4] = {0,0,0,0};
static bool ch_state[4] = {false,false,false,false};
// Per-channel division index — default to "1" (index 8)
static int clock_div_idx[4] = {8, 8, 8, 8};
static int clock_sel_ch = 0; // which channel (0..3) is being edited via Pot3
static PotPickup clock_pickup; // soft-takeover for Pot3 across channel selection

// Helper: find nearest division index to a target factor
static int clock_find_nearest_div(float target) {
  int best = 0; float bestDiff = fabsf(div_factors[0] - target);
  for (int i=1;i<kDivCount;i++) {
    float d = fabsf(div_factors[i] - target);
    if (d < bestDiff) { bestDiff = d; best = i; }
  }
  return best;
}

void clock_enter() {
  resetPotSmooth();
  clock_running = false;
  clock_last_internal_ms = millis();
  clock_last_external_edge_ms = 0;
  clock_ext_interval_ms = 0;
  clock_ext_interval_smooth = 0.0f;
  clock_ext_resync = false;
  clock_ext_gate = false;
  for (int i=0;i<4;i++) { ch_next_fire_ms[i]=0; ch_pulse_end_ms[i]=0; ch_state[i]=false; }
  // Arm Pot3 pickup so entering Clock preserves each channel's stored division
  // until the knob is moved across it (divisions persist between visits).
  pickup_arm(clock_pickup, readPotNormSmooth(PIN_POT1, 0),
             readPotNormSmooth(PIN_POT2, 1), readPotNormSmooth(PIN_POT3, 2));
  // Start ADS1115 in continuous mode on the ext-clock channel so
  // clock_tick() can read the latest sample without a blocking conversion.
  if (haveADS) {
    ads.startADCReading(MUX_BY_CHANNEL[AD_EXT_CLOCK_CH], /*continuous=*/true);
  }
}

void clock_tick() {
  // handle start/stop short-press from main handler
  if (patchShortPressed) {
    clock_running = !clock_running;
    patchShortPressed = false;
  }

  // read pots (smoothed, inverted) POT1=BPM, POT2=channel select, POT3=division
  float p_bpm = readPotNormSmooth(PIN_POT1, 0);
  float p_sel = readPotNormSmooth(PIN_POT2, 1);
  float p_div = readPotNormSmooth(PIN_POT3, 2);
  int pot_raw_bpm = (int)(p_bpm * 4095.0f);
  int pot_raw_sel = (int)(p_sel * 4095.0f);
  int pot_raw_div = (int)(p_div * 4095.0f);

  uint32_t now = millis();

  // detect external clock on ADS channel (continuous mode — non-blocking read)
  bool have_ext = false;
  if (haveADS) {
    int16_t a0 = ads.getLastConversionResults(); // instant read; no conversion wait
    // Threshold-based rising-edge detection with hysteresis.
    // Lower ADC code = higher Eurorack voltage (inverting front-end).
    bool gate_now = clock_ext_gate ? (a0 < kGateOffThresh) : (a0 < kGateOnThresh);
    if (gate_now && !clock_ext_gate) {
      // rising edge detected
      if (clock_last_external_edge_ms) {
        uint32_t raw_interval = now - clock_last_external_edge_ms;
        // Reject implausible intervals (< 30 BPM = 2000ms, > ~600 BPM = 100ms)
        if (raw_interval >= 100 && raw_interval <= 2000) {
          clock_ext_interval_ms = raw_interval;
          // Exponential smoothing (alpha ~0.3) to reject jitter from ADS poll timing
          if (clock_ext_interval_smooth < 1.0f)
            clock_ext_interval_smooth = (float)raw_interval; // first valid edge
          else
            clock_ext_interval_smooth = 0.7f * clock_ext_interval_smooth + 0.3f * (float)raw_interval;
        }
      }
      clock_last_external_edge_ms = now;
      have_ext = true;
      clock_ext_resync = true;
    }
    clock_ext_gate = gate_now;
  }

  // Timeout: revert to internal mode if external clock has stopped
  if (clock_ext_interval_ms > 0 && clock_last_external_edge_ms
      && (now - clock_last_external_edge_ms > kExtClockTimeoutMs)) {
    clock_ext_interval_ms = 0;
    clock_ext_interval_smooth = 0.0f;
    clock_ext_resync = false;
  }

  // base interval: external (smoothed) if present, else internal from POT1
  bool ext_mode_active = (clock_ext_interval_ms > 0 && clock_ext_interval_smooth > 0.0f);
  if (ext_mode_active) {
    clock_base_interval_ms = (uint32_t)(clock_ext_interval_smooth + 0.5f);
    // Auto-start when receiving external clock
    if (!clock_running) clock_running = true;
  } else {
    // use POT1 to set tempo 30..300 BPM
    int p = pot_raw_bpm;
    int bpm = 30 + (int)(((long)p * (300-30)) / 4095);
    if (bpm <= 0) bpm = 120;
    clock_base_interval_ms = 60000 / bpm;
  }
  // POT2 selects which channel (0..3) to edit; POT3 sets its division.
  int sel = (int)((uint32_t)pot_raw_sel * 4 / 4096);
  if (sel < 0) sel = 0; else if (sel > 3) sel = 3;
  // When the selected channel changes, arm Pot3 pickup so its division isn't
  // snapped to the knob position until Pot3 crosses the channel's stored value.
  if (sel != clock_sel_ch) {
    clock_sel_ch = sel;
    pickup_arm(clock_pickup, p_bpm, p_sel, p_div);
  }
  // Pot3 division, gated by soft-takeover. Target is the band-centre of the
  // channel's current division index within the kDivCount options.
  float divTargetN = ((float)clock_div_idx[clock_sel_ch] + 0.5f) / (float)kDivCount;
  if (pickup_update(clock_pickup, 2, p_div, divTargetN)) {
    int div_idx = (int)((uint32_t)pot_raw_div * kDivCount / 4096);
    if (div_idx < 0) div_idx = 0; else if (div_idx >= kDivCount) div_idx = kDivCount - 1;
    clock_div_idx[clock_sel_ch] = div_idx;
  }

  // On each external rising edge, resynchronize all channel schedules so
  // outputs are phase-locked to the incoming clock, not free-running.
  if (clock_ext_resync) {
    clock_ext_resync = false;
    for (int ch = 0; ch < 4; ch++) {
      float factor = div_factors[clock_div_idx[ch]];
      uint32_t ch_interval_ms = (uint32_t)(clock_base_interval_ms / factor);
      if (ch_interval_ms == 0) ch_interval_ms = 1;
      ch_next_fire_ms[ch] = now + ch_interval_ms;
      // Channels whose factor >= 1.0 (at or faster than base) fire immediately
      // on the external edge to stay in phase.
      if (factor >= 1.0f) {
        ch_state[ch] = true;
        ch_pulse_end_ms[ch] = now + 10;
      }
    }
  }

  for (int ch=0; ch<4; ch++) {
    float factor = div_factors[clock_div_idx[ch]];
    // compute channel interval (ms)
    uint32_t ch_interval_ms = (uint32_t)(clock_base_interval_ms / factor);
    if (ch_interval_ms == 0) ch_interval_ms = 1;

    if (!clock_running) continue;

    if (now >= ch_next_fire_ms[ch]) {
      // start a short pulse
      ch_state[ch] = true;
      ch_pulse_end_ms[ch] = now + 10; // 10 ms pulse
      ch_next_fire_ms[ch] = now + ch_interval_ms;
    }
    if (ch_state[ch] && now >= ch_pulse_end_ms[ch]) {
      ch_state[ch] = false;
    }
  }

  // All 4 channels independently editable via Pot2/Pot3.

  // write MCP outputs if available (direct codes: low~2047, high~0)
  if (haveMCP) {
    uint16_t out0 = ch_state[0] ? kGateHighCode : kGateLowCode;
    uint16_t out1 = ch_state[1] ? kGateHighCode : kGateLowCode;
    uint16_t out2 = ch_state[2] ? kGateHighCode : kGateLowCode;
    uint16_t out3 = ch_state[3] ? kGateHighCode : kGateLowCode;
    // remember values for display
    mcp_values[CV0_DA_CH] = out0; mcp_values[CV1_DA_CH] = out1; mcp_values[CV2_DA_CH] = out2; mcp_values[CV3_DA_CH] = out3;
    mcp_writeAll();
  }
}

void clock_render() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);
  ui::printClipped(0, 0, 64, "Clock");
  // status area unused on patches per request

  // Mode / BPM / Run state on single line (y=16)
  oled.setCursor(0, 16);
  bool ext_mode = (clock_ext_interval_ms > 0 && clock_ext_interval_smooth > 0.0f);
  int bpm_disp = ext_mode ? (int)(60000.0f / clock_ext_interval_smooth + 0.5f)
                          : (int)(60000.0f / (float)clock_base_interval_ms + 0.5f);
  oled.print(ext_mode ? "EXT " : "INT ");
  oled.print(bpm_disp); oled.print(' '); oled.print(clock_running ? "RUN" : "STOP");

  // Channel divisions rows — highlight selected channel
  for (int ch = 0; ch < 4; ch++) {
    int x = (ch & 1) ? 64 : 0;
    int y = (ch < 2) ? 26 : 36;
    oled.setCursor(x, y);
    if (ch == clock_sel_ch) oled.print(">"); else oled.print(" ");
    oled.print("CH"); oled.print(ch); oled.print(' '); oled.print(div_labels[clock_div_idx[ch]]);
  }

  oled.display();
}

Patch patch_clock = { "Clock", clock_enter, clock_tick, clock_render, true };
// -- Placeholder patch stubs for menu entries (lightweight)

// ---- Euclid patch: Euclidean drum triggers on up to 4 MCP outputs ----
static int euclid_steps = 8;
static int euclid_pulses = 3;
static int euclid_rotation = 0;
static int euclid_prev_rotation = 0; // track previous rotation for rebuild
static int euclid_step_idx = 0;
static int euclid_ch_step_idx[4] = {0,0,0,0}; // per-channel step counters for complex mode
static uint32_t euclid_next_ms = 0;
static uint32_t euclid_pulse_end_ms[4] = {0,0,0,0};
static bool euclid_state[4] = {false,false,false,false};
static bool euclid_patterns[4][16]; // max 16 steps
static int euclid_selected_param = 0; // 0=Steps,1=Pulses,2=Rotation,3=BPM
// Euclid modes: 0=simple (shared params), 1=complex (per-channel params)
static int euclid_mode = 0;
static int euclid_ch_steps[4] = {8,8,8,8};
static int euclid_ch_pulses[4] = {3,3,3,3};
static int euclid_ch_rotation[4] = {0,1,2,3};
// Selection in complex mode: (channel, param)
static int euclid_sel_channel = 0; // 0..3
static int euclid_bpm = 120; // cached for render
static PotPickup euclid_pickup; // soft-takeover for Pot3 across param/mode/channel

void build_euclid_pattern(bool *out, int steps, int pulses, int rotate) {
  // simple even-distribution algorithm
  int acc = 0;
  for (int i = 0; i < steps; i++) {
    acc += pulses;
    if (acc >= steps) {
      out[i] = true;
      acc -= steps;
    } else out[i] = false;
  }
  // rotate
  if (rotate != 0) {
    bool tmp[16];
    for (int i=0;i<steps;i++) tmp[(i+rotate)%steps] = out[i];
    for (int i=0;i<steps;i++) out[i] = tmp[i];
  }
}

void euclid_enter() {
  resetPotSmooth();
  euclid_steps = 8; euclid_pulses = 3; euclid_rotation = 0; euclid_prev_rotation = 0;
  euclid_step_idx = 0; euclid_next_ms = millis(); euclid_bpm = 120;
  for (int c=0;c<4;c++) { euclid_pulse_end_ms[c]=0; euclid_state[c]=false; euclid_ch_step_idx[c]=0; }
  pickup_setLive(euclid_pickup); // Pot3 live for the initial parameter
}

void euclid_tick() {
  // read pots (smoothed, inverted) Pot1=BPM, Pot2=mode, Pot3=param
  float p_bpm  = readPotNormSmooth(PIN_POT1, 0);
  float p_mode = readPotNormSmooth(PIN_POT2, 1);
  float p_param= readPotNormSmooth(PIN_POT3, 2);
  int raw1 = (int)(p_bpm   * 4095.0f);
  int raw2 = (int)(p_mode  * 4095.0f);
  int raw3 = (int)(p_param * 4095.0f);
  // Mode selection via Pot2 (coarse split): <50% simple, >=50% complex.
  // On a mode flip, arm Pot3 pickup — the selected parameter's meaning changes.
  int newMode = (raw2 >= 2048) ? 1 : 0;
  if (newMode != euclid_mode) {
    euclid_mode = newMode;
    pickup_arm(euclid_pickup, p_bpm, p_mode, p_param);
  }

  int steps = euclid_steps;
  int pulses = euclid_pulses;
  int bpm = 30 + (int)(((long)raw1 * (300-30)) / 4095);

  // Normalized (0..1) position of the selected Pot3 parameter, for soft-takeover.
  float p3target;
  if (euclid_mode == 0) {
    switch (euclid_selected_param) {
      case 0:  p3target = (float)(euclid_steps - 1) / 15.0f; break;
      case 1:  p3target = (euclid_steps > 0) ? (float)euclid_pulses   / (float)euclid_steps : 0.0f; break;
      case 2:  p3target = (euclid_steps > 0) ? (float)euclid_rotation / (float)euclid_steps : 0.0f; break;
      default: p3target = p_param; break; // BPM page: Pot3 is a no-op
    }
  } else {
    int ch = euclid_sel_channel;
    int sc = euclid_ch_steps[ch]; if (sc < 1) sc = 1;
    switch (euclid_selected_param) {
      case 0:  p3target = (float)(euclid_ch_steps[ch] - 1) / 15.0f; break;
      case 1:  p3target = (float)euclid_ch_pulses[ch]   / (float)sc; break;
      default: p3target = (float)euclid_ch_rotation[ch] / (float)sc; break; // param 2
    }
  }
  bool p3live = pickup_update(euclid_pickup, 2, p_param, p3target);

  if (euclid_mode == 0) {
    // Simple mode: shared params — selected param edited via Pot3
    switch (euclid_selected_param) {
      case 0: if (p3live) steps  = 1 + (raw3 * 15) / 4095; break; // 1..16
      case 1: if (p3live) pulses = (raw3 * euclid_steps) / 4095; break; // 0..steps
      case 2: if (p3live) euclid_rotation = (raw3 * euclid_steps) / 4095; break; // 0..steps-1 approx
      case 3: /* BPM via Pot1 */ break;
    }
  } else {
    // Complex mode: per-channel params; Pot3 edits selected (channel,param)
    int ch = euclid_sel_channel;
    switch (euclid_selected_param) {
      case 0: if (p3live) euclid_ch_steps[ch]   = 1 + (raw3 * 15) / 4095; break;
      case 1: if (p3live) euclid_ch_pulses[ch]  = (raw3 * euclid_ch_steps[ch]) / 4095; break;
      case 2: if (p3live) euclid_ch_rotation[ch]= (raw3 * euclid_ch_steps[ch]) / 4095; break;
      case 3: /* BPM via Pot1 */ break;
    }
  }
  if (bpm <= 0) bpm = 120;

  // short-press cycles selected parameter (and channel in complex mode); arm
  // Pot3 pickup so it doesn't snap the newly-selected parameter.
  if (patchShortPressed) {
    if (euclid_mode == 0) {
      euclid_selected_param = (euclid_selected_param + 1) % 4;
    } else {
      // cycle Steps/Pulses/Rotation; advance channel when wrapping
      euclid_selected_param = (euclid_selected_param + 1) % 3;
      if (euclid_selected_param == 0) euclid_sel_channel = (euclid_sel_channel + 1) % 4;
    }
    patchShortPressed = false;
    pickup_arm(euclid_pickup, p_bpm, p_mode, p_param);
  }

  euclid_bpm = bpm; // cache for render

  // rebuild patterns if needed (steps, pulses, OR rotation changed)
  if (euclid_mode == 0 && (steps != euclid_steps || pulses != euclid_pulses || euclid_rotation != euclid_prev_rotation)) {
    euclid_steps = steps;
    euclid_pulses = pulses;
    euclid_prev_rotation = euclid_rotation;
    // build a base pattern and make 4 rotated variants
    bool base[16] = {0};
    build_euclid_pattern(base, euclid_steps, euclid_pulses, 0);
    for (int ch=0; ch<4; ch++) {
      int ro = (euclid_rotation + ch) % euclid_steps;
      // copy & rotate into pattern
      for (int i=0;i<euclid_steps;i++) euclid_patterns[ch][i] = base[(i - ro + euclid_steps) % euclid_steps];
    }
  }
  if (euclid_mode == 1) {
    // per-channel patterns
    for (int ch=0; ch<4; ch++) {
      int steps_c = euclid_ch_steps[ch]; if (steps_c < 1) steps_c = 1; if (steps_c > 16) steps_c = 16;
      int pulses_c = euclid_ch_pulses[ch]; if (pulses_c < 0) pulses_c = 0; if (pulses_c > steps_c) pulses_c = steps_c;
      int rot_c = euclid_ch_rotation[ch]; if (rot_c < 0) rot_c = 0; if (rot_c >= steps_c) rot_c = steps_c-1;
      bool base[16] = {0};
      build_euclid_pattern(base, steps_c, pulses_c, 0);
      for (int i=0;i<steps_c;i++) euclid_patterns[ch][i] = base[(i - rot_c + steps_c) % steps_c];
      // zero out remainder to avoid stray triggers
      for (int i=steps_c;i<16;i++) euclid_patterns[ch][i] = false;
    }
  }

  uint32_t now = millis();
  uint32_t interval_ms = 60000 / bpm;
  if (now >= euclid_next_ms) {
    euclid_next_ms = now + interval_ms;
    if (euclid_mode == 0) {
      // Simple mode: single shared step index wrapping at euclid_steps
      euclid_step_idx = (euclid_step_idx + 1) % (euclid_steps > 0 ? euclid_steps : 1);
      for (int ch=0; ch<4; ch++) {
        if (euclid_steps > 0 && euclid_patterns[ch][euclid_step_idx]) {
          euclid_state[ch] = true;
          euclid_pulse_end_ms[ch] = now + 30;
        }
      }
    } else {
      // Complex mode: per-channel step counters wrapping at their own step counts
      for (int ch=0; ch<4; ch++) {
        int sc = euclid_ch_steps[ch]; if (sc < 1) sc = 1;
        euclid_ch_step_idx[ch] = (euclid_ch_step_idx[ch] + 1) % sc;
        if (euclid_patterns[ch][euclid_ch_step_idx[ch]]) {
          euclid_state[ch] = true;
          euclid_pulse_end_ms[ch] = now + 30;
        }
      }
    }
  }

  // expire pulses
  for (int ch=0; ch<4; ch++) {
    if (euclid_state[ch] && now >= euclid_pulse_end_ms[ch]) euclid_state[ch] = false;
  }

  // write MCP outputs (direct codes: low~2047, high~0)
  if (haveMCP) {
    uint16_t out0 = euclid_state[0] ? kGateHighCode : kGateLowCode;
    uint16_t out1 = euclid_state[1] ? kGateHighCode : kGateLowCode;
    uint16_t out2 = euclid_state[2] ? kGateHighCode : kGateLowCode;
    uint16_t out3 = euclid_state[3] ? kGateHighCode : kGateLowCode;
    mcp_values[CV0_DA_CH]=out0; mcp_values[CV1_DA_CH]=out1; mcp_values[CV2_DA_CH]=out2; mcp_values[CV3_DA_CH]=out3;
    mcp_writeAll();
  }
}

void euclid_render() {
  oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE); oled.setTextWrap(false);
  ui::printClipped(0, 0, 70, "Euclid");
  oled.setCursor(78, 0);
  oled.print(euclid_mode == 0 ? "Smp " : "Cpx "); oled.print(euclid_bpm);

  // One row per channel: step boxes (filled = pulse), playhead tick under the
  // current step, live-gate marker, and a '>' on the selected channel (complex).
  const int sz = 5, gap = 1, x0 = 14;
  for (int ch = 0; ch < 4; ch++) {
    int y = 16 + ch * 10; // keep below the 16px yellow title band
    int steps  = (euclid_mode == 0) ? euclid_steps : euclid_ch_steps[ch];
    int curIdx = (euclid_mode == 0) ? euclid_step_idx : euclid_ch_step_idx[ch];
    oled.setCursor(0, y + 1);
    oled.print((euclid_mode == 1 && ch == euclid_sel_channel) ? ">" : " ");
    oled.print(ch);
    if (euclid_state[ch]) oled.fillRect(11, y, 2, sz, SSD1306_WHITE); // live gate
    for (int i = 0; i < steps && i < 16; i++) {
      int x = x0 + i * (sz + gap);
      if (x + sz > OLED_W) break;
      if (euclid_patterns[ch][i]) oled.fillRect(x, y, sz, sz, SSD1306_WHITE);
      else                        oled.drawRect(x, y, sz, sz, SSD1306_WHITE);
      if (i == curIdx) oled.drawFastHLine(x, y + sz + 1, sz, SSD1306_WHITE);
    }
  }

  // Status: the parameter currently being edited + its value.
  oled.setCursor(0, 56);
  if (euclid_mode == 1) { oled.print("CH"); oled.print(euclid_sel_channel); oled.print(" "); }
  oled.print((euclid_selected_param==0)?"Steps ":(euclid_selected_param==1)?"Puls ":(euclid_selected_param==2)?"Rot ":"BPM ");
  int pv;
  if (euclid_selected_param == 3)      pv = euclid_bpm;
  else if (euclid_mode == 0)           pv = (euclid_selected_param==0)?euclid_steps:(euclid_selected_param==1)?euclid_pulses:euclid_rotation;
  else                                 pv = (euclid_selected_param==0)?euclid_ch_steps[euclid_sel_channel]:(euclid_selected_param==1)?euclid_ch_pulses[euclid_sel_channel]:euclid_ch_rotation[euclid_sel_channel];
  oled.print(pv);

  oled.display();
}
Patch patch_euclid = { "Euclid", euclid_enter, euclid_tick, euclid_render, false };

// ---- QuadLFO patch: 4 independent LFOs (Amp / Rate / Shape) ----
// Pots (smoothed, inverted):
//   Pot1: Amplitude (0..1 -> 0..5V peak, bipolar)
//   Pot2: Rate (0.05Hz .. 20Hz, squared mapping for fine low-end control)
//   Pot3: Shape selection (Sine/Tri/Square/Up/Dn)
// Short press: cycle edited LFO (0..3)
// Long press: return to menu (handled globally)

enum LfoShape { LFO_SINE, LFO_TRI, LFO_SQUARE, LFO_RAMP_UP, LFO_RAMP_DOWN, LFO_COUNT };
static const char* kLfoShapeNames[LFO_COUNT] = { "Sin", "Tri", "Sq", "Up", "Dn" };
static float lfo_phase[4]     = {0,0,0,0};   // 0..1 wrapping
static float lfo_rate_hz[4]   = {1,1,1,1};   // Hz
static float lfo_amp[4]       = {1,1,1,1};   // 0..5V peak (amplitude knob scales)
static uint8_t lfo_shape[4]   = {LFO_SINE,LFO_TRI,LFO_SQUARE,LFO_RAMP_UP};
static int lfo_edit_idx       = 0;           // which LFO pots are editing
static uint32_t lfo_last_ms   = 0;           // last tick time
static PotPickup lfo_pickup;                 // soft-takeover across LFO switches

static float quadlfo_shape_eval(uint8_t shape, float ph) {
  // ph in [0,1)
  switch(shape) {
    case LFO_SINE: return sinf(ph * 2.0f * PI); // -1..1
    case LFO_TRI: {
      float t = ph;
      float tri = (t < 0.25f) ? (t * 4.0f) : (t < 0.75f ? 2.0f - t*4.0f : (t*4.0f - 4.0f));
      return tri; // -1..1
    }
    case LFO_SQUARE: return (ph < 0.5f) ? 1.0f : -1.0f;
    case LFO_RAMP_UP: return (ph * 2.0f) - 1.0f;          // -1..1 rising
    case LFO_RAMP_DOWN: return 1.0f - (ph * 2.0f);        // -1..1 falling
    default: return 0.0f;
  }
}

void quadlfo_enter() {
  resetPotSmooth();
  lfo_edit_idx = 0;
  for (int i=0;i<4;i++) { lfo_phase[i]=0.0f; lfo_rate_hz[i]=1.0f; lfo_amp[i]=2.5f; }
  lfo_shape[0]=LFO_SINE; lfo_shape[1]=LFO_TRI; lfo_shape[2]=LFO_SQUARE; lfo_shape[3]=LFO_RAMP_UP;
  lfo_last_ms = millis();
  pickup_setLive(lfo_pickup); // pots live for the initial LFO
}

void quadlfo_tick() {
  uint32_t now = millis();
  float dt_ms = (float)(now - lfo_last_ms);
  // Cap dt to prevent large phase jumps when OLED display() stalls the I2C bus.
  if (dt_ms > 12.0f) dt_ms = 12.0f;
  if (dt_ms < 0.0f)  dt_ms = 0.0f;
  lfo_last_ms = now;
  float dt = dt_ms * 0.001f; // seconds

  // Pots (smoothed, inverted)
  float p_amp  = readPotNormSmooth(PIN_POT1, 0); // amplitude 0..1
  float p_rate = readPotNormSmooth(PIN_POT2, 1); // rate mapping
  float p_shape= readPotNormSmooth(PIN_POT3, 2); // shape select

  // Short press cycles edited LFO index; arm pickup so the pots don't snap the
  // newly-selected LFO's params until each knob crosses its stored value.
  if (patchShortPressed) {
    lfo_edit_idx = (lfo_edit_idx + 1) & 0x3;
    patchShortPressed = false;
    pickup_arm(lfo_pickup, p_amp, p_rate, p_shape);
  }

  // Normalized (0..1) positions of the selected LFO's current params, for the
  // pickup comparison (inverse of each forward mapping below).
  float ampN   = lfo_amp[lfo_edit_idx] / 5.0f;
  float rateN  = sqrtf((lfo_rate_hz[lfo_edit_idx] - 0.05f) / (20.0f - 0.05f));
  if (rateN < 0.0f) rateN = 0.0f; else if (rateN > 1.0f) rateN = 1.0f;
  float shapeN = ((float)lfo_shape[lfo_edit_idx] + 0.5f) / (float)LFO_COUNT;

  // Update selected LFO params only once each pot has taken over.
  // Amplitude: 0..5V peak (bipolar), direct scaling.
  if (pickup_update(lfo_pickup, 0, p_amp, ampN))
    lfo_amp[lfo_edit_idx] = p_amp * 5.0f;
  // Rate: square for resolution at low end -> 0.05 .. 20 Hz
  if (pickup_update(lfo_pickup, 1, p_rate, rateN))
    lfo_rate_hz[lfo_edit_idx] = 0.05f + (p_rate * p_rate) * (20.0f - 0.05f);
  // Shape index
  if (pickup_update(lfo_pickup, 2, p_shape, shapeN)) {
    int sh = (int)(p_shape * (float)LFO_COUNT);
    if (sh < 0) sh = 0; else if (sh >= LFO_COUNT) sh = LFO_COUNT - 1;
    lfo_shape[lfo_edit_idx] = (uint8_t)sh;
  }

  // Advance all LFO phases
  for (int i=0;i<4;i++) {
    float inc = lfo_rate_hz[i] * dt; // cycles per second * seconds = cycles
    lfo_phase[i] += inc;
    // wrap
    if (lfo_phase[i] >= 1.0f) lfo_phase[i] -= floorf(lfo_phase[i]);
    if (lfo_phase[i] < 0.0f) lfo_phase[i] = fmodf(lfo_phase[i], 1.0f);
  }

  // Generate outputs and write DACs (CV0..CV3)
  if (haveMCP) {
    for (int i=0;i<4;i++) {
      float val = quadlfo_shape_eval(lfo_shape[i], lfo_phase[i]); // -1..1
      float volts = val * lfo_amp[i]; // -amp .. +amp (bipolar)
      // Clamp to ±5V domain
      if (volts < -5.0f) volts = -5.0f; else if (volts > 5.0f) volts = 5.0f;
      uint16_t code = voltsToDac(i, volts); // i assumes CVx_DA_CH logical match order 0..3
      // Map logical i to physical macros for safety
      uint8_t physIndex = (i==0)?CV0_DA_CH:(i==1)?CV1_DA_CH:(i==2)?CV2_DA_CH:CV3_DA_CH;
      mcp_values[physIndex] = code;
    }
    mcp_writeAll();
  }
}

void quadlfo_render() {
  oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE); oled.setTextWrap(false);

  // Top line: numeric readout of the LFO being edited (pots = Amp/Rate/Shape).
  oled.setCursor(0, 0);
  oled.print("L"); oled.print(lfo_edit_idx); oled.print(":");
  oled.print(kLfoShapeNames[lfo_shape[lfo_edit_idx]]); oled.print(" ");
  oled.print(lfo_amp[lfo_edit_idx], 1); oled.print("V ");
  oled.print(lfo_rate_hz[lfo_edit_idx], 2); oled.print("Hz");

  // 2x2 grid of live mini-waveforms (one cycle each) with a moving phase dot.
  // Cells stay below the 16px yellow title band.
  const int cw = 62, chh = 23;
  for (int i = 0; i < 4; i++) {
    int cx  = (i % 2) * 64;
    int cy0 = 16 + (i / 2) * 24;          // rows at y=16 and y=40
    int midY = cy0 + chh / 2;
    int ampPx = chh / 2 - 2;              // vertical headroom
    float ascale = lfo_amp[i] / 5.0f;     // 0..1
    if (i == lfo_edit_idx) oled.drawRect(cx, cy0, cw, chh, SSD1306_WHITE); // selected
    for (int x = 2; x < cw - 2; x += 3) oled.drawPixel(cx + x, midY, SSD1306_WHITE); // zero line
    const int px0 = cx + 2, pxw = cw - 4;
    int prevY = midY;
    for (int px = 0; px < pxw; px++) {
      float ph = (float)px / (float)pxw;
      float val = quadlfo_shape_eval(lfo_shape[i], ph);             // -1..1
      int yy = midY - (int)(val * ascale * ampPx);
      if (px > 0) oled.drawLine(px0 + px - 1, prevY, px0 + px, yy, SSD1306_WHITE);
      prevY = yy;
    }
    float cph = lfo_phase[i];
    int dpx = px0 + (int)(cph * (pxw - 1));
    int dpy = midY - (int)(quadlfo_shape_eval(lfo_shape[i], cph) * ascale * ampPx);
    oled.fillCircle(dpx, dpy, 1, SSD1306_WHITE);
    oled.setCursor(cx + 2, cy0 + 1); oled.print("L"); oled.print(i);
  }

  oled.display();
}
Patch patch_mod = { "LFO", quadlfo_enter, quadlfo_tick, quadlfo_render, true };

// ---- Env patch: Dual macro ADSR (per-env AD + SR + Velocity) ----
enum EnvStage { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
static uint32_t env_last_ms = 0;   // last tick time for dt
// Editing selection: which envelope pots are writing to (0=E1,1=E2)
static int env_edit_idx = 0;
// Per-envelope params and runtime state
static float env_params_AD[2]  = {0.5f, 0.5f}; // 0..1 maps to attack/decay pair
static float env_params_SR[2]  = {0.5f, 0.5f}; // 0..1 maps to sustain/release
static float env_params_Vel[2] = {1.0f, 1.0f}; // 0..1 output scaling
static float env_levels[2]     = {0.0f, 0.0f}; // current envelope levels
static EnvStage env_stages[2]  = {ENV_IDLE, ENV_IDLE};
// Gate state tracking for threshold-based detection
static bool env_gate_state[2]  = {false, false};
static uint8_t env_adc_divider = 0;         // tick sub-counter for ADC read decimation
static uint16_t env_prev_code[2] = {0, 0};  // previous DAC codes for slew limiting
static PotPickup env_pickup;                // soft-takeover across E1/E2 switches

void env_enter() {
  resetPotSmooth();
  env_edit_idx = 0;
  env_params_AD[0] = env_params_AD[1] = 0.5f;
  env_params_SR[0] = env_params_SR[1] = 0.5f;
  env_params_Vel[0] = env_params_Vel[1] = 1.0f;
  env_levels[0] = env_levels[1] = 0.0f;
  env_stages[0] = env_stages[1] = ENV_IDLE;
  env_gate_state[0] = env_gate_state[1] = false;
  env_adc_divider = 0;
  env_prev_code[0] = env_prev_code[1] = kGateLowCode;
  env_last_ms = millis();
  pickup_setLive(env_pickup); // pots live for the initial envelope
  // Zero all CV outputs so stale values from previous patch don't persist
  if (haveMCP) {
    mcp_values[CV0_DA_CH] = kGateLowCode;
    mcp_values[CV1_DA_CH] = kGateLowCode;
    mcp_values[CV2_DA_CH] = kGateLowCode;
    mcp_values[CV3_DA_CH] = kGateLowCode;
  }
}

void env_tick() {
  uint32_t now = millis();
  float dt = (float)(now - env_last_ms);
  if (dt < 0) dt = 0;
  // Cap dt to prevent large envelope jumps when OLED display() stalls the bus.
  // At 5ms tick rate, anything above ~12ms indicates I2C contention.
  if (dt > 12.0f) dt = 12.0f;
  env_last_ms = now;

  // Pots inverted with smoothing so CW increases
  float potVel = readPotNormSmooth(PIN_POT1, 0); // 0..1 velocity (amplitude)
  float potAD  = readPotNormSmooth(PIN_POT2, 1); // 0..1
  float potSR  = readPotNormSmooth(PIN_POT3, 2); // 0..1

  // Short press: select which envelope to edit (Env1/Env2). Arm pickup so the
  // pots don't snap the newly-selected envelope's params until each knob
  // crosses its stored value.
  if (patchShortPressed) {
    env_edit_idx ^= 1;
    patchShortPressed = false;
    pickup_arm(env_pickup, potVel, potAD, potSR);
  }

  // Update selected env params from pots, gated by soft-takeover.
  // Pot1->Vel, Pot2->AD, Pot3->SR; params are already stored normalized (0..1).
  if (pickup_update(env_pickup, 0, potVel, env_params_Vel[env_edit_idx]))
    env_params_Vel[env_edit_idx] = potVel;
  if (pickup_update(env_pickup, 1, potAD, env_params_AD[env_edit_idx]))
    env_params_AD[env_edit_idx] = potAD;
  if (pickup_update(env_pickup, 2, potSR, env_params_SR[env_edit_idx]))
    env_params_SR[env_edit_idx] = potSR;

  // External triggers: threshold-based gate detection with hysteresis.
  // Lower ADC code = higher Eurorack voltage (inverting front-end).
  // Only read ADS every 4th tick (~50 Hz) to keep most ticks fast for
  // DAC writes. Gate detection at 50 Hz is sufficient for musical use.
  env_adc_divider++;
  if (haveADS && (env_adc_divider >= 4)) {
    env_adc_divider = 0;
    int16_t a0 = ads.readADC_SingleEnded(AD_EXT_CLOCK_CH);
    int16_t a1 = ads.readADC_SingleEnded(AD1_CH);

    bool gate0_now = env_gate_state[0] ? (a0 < kGateOffThresh) : (a0 < kGateOnThresh);
    bool gate1_now = env_gate_state[1] ? (a1 < kGateOffThresh) : (a1 < kGateOnThresh);

    // Rising edge -> trigger attack (retrigger from any stage)
    if (gate0_now && !env_gate_state[0]) env_stages[0] = ENV_ATTACK;
    if (gate1_now && !env_gate_state[1]) env_stages[1] = ENV_ATTACK;

    // Falling edge -> release if holding at sustain
    if (!gate0_now && env_gate_state[0] && env_stages[0] == ENV_SUSTAIN) env_stages[0] = ENV_RELEASE;
    if (!gate1_now && env_gate_state[1] && env_stages[1] == ENV_SUSTAIN) env_stages[1] = ENV_RELEASE;

    env_gate_state[0] = gate0_now;
    env_gate_state[1] = gate1_now;
  }

  // Advance both envelopes using their own params
  for (int i=0;i<2;i++) {
    float AD = env_params_AD[i];
    float SR = env_params_SR[i];
    float attack_ms = 1.0f + (AD * AD) * 2000.0f;
    float decay_base= attack_ms; // link A and D scaling together
    float sustain   = SR; if (sustain < 0.0f) sustain = 0.0f; if (sustain > 1.0f) sustain = 1.0f;
    // Make percussive settings truly punchy: scale decay by sustain
    // sustain=0 -> ~15% of base (fast), sustain=1 -> 100% of base
    float decay_ms  = decay_base * (0.15f + 0.85f * sustain);
    float release_ms= 1.0f + (SR * SR) * 2000.0f;

    switch (env_stages[i]) {
      case ENV_IDLE: env_levels[i] = 0.0f; break;
      case ENV_ATTACK: {
        float step = (attack_ms <= 0.0f) ? 1.0f : (dt / attack_ms);
        env_levels[i] += step; if (env_levels[i] >= 1.0f) { env_levels[i] = 1.0f; env_stages[i] = ENV_DECAY; }
      } break;
      case ENV_DECAY: {
        float span = (1.0f - sustain); if (span < 1e-6f) span = 1e-6f;
        float step = (decay_ms <= 0.0f) ? span : (dt * span / decay_ms);
        env_levels[i] -= step;
        if (env_levels[i] <= sustain) {
          env_levels[i] = sustain;
          // Hold at sustain while gate is HIGH; release when gate drops
          env_stages[i] = ENV_SUSTAIN;
        }
      } break;
      case ENV_SUSTAIN: {
        // Hold at sustain level; gate-off transitions to RELEASE are
        // handled above in the gate detection block.
        env_levels[i] = sustain;
        // If gate already dropped (e.g. very short trigger), release immediately
        if (!env_gate_state[i]) env_stages[i] = ENV_RELEASE;
      } break;
      case ENV_RELEASE: {
        // Release ramp from current level to 0. Use the current level as
        // the span so the slope is consistent regardless of sustain setting.
        // When sustain is 0 the level is already ~0 after decay, so clamp
        // span to a small minimum to avoid divide-by-zero.
        float span = env_levels[i]; if (span < 1e-4f) span = 1e-4f;
        float step = (release_ms <= 0.0f) ? span : (dt * span / release_ms);
        env_levels[i] -= step; if (env_levels[i] <= 0.0f) { env_levels[i] = 0.0f; env_stages[i] = ENV_IDLE; }
      } break;
    }
  }

  // Output Env1->CV0, Env2->CV1 using their velocities
  if (haveMCP) {
    float v0 = env_levels[0] * env_params_Vel[0]; if (v0 < 0.0f) v0 = 0.0f; if (v0 > 1.0f) v0 = 1.0f;
    float v1 = env_levels[1] * env_params_Vel[1]; if (v1 < 0.0f) v1 = 0.0f; if (v1 > 1.0f) v1 = 1.0f;
    // Map to target volts (0..+5V) then to calibrated DAC codes
    uint16_t c0 = voltsToDac(0, v0 * 5.0f);
    uint16_t c1 = voltsToDac(1, v1 * 5.0f);
    // Slew-limit DAC code changes to avoid abrupt voltage jumps that
    // produce audible clicks. Max step of ~100 codes per tick ≈ 80mV
    // at ~0.8mV/code, giving a ~16V/s slew rate — fast enough for
    // musical envelopes but eliminates staircase artifacts.
    const int16_t maxStep = 100;
    int16_t d0 = (int16_t)c0 - (int16_t)env_prev_code[0];
    int16_t d1 = (int16_t)c1 - (int16_t)env_prev_code[1];
    if (d0 >  maxStep) c0 = env_prev_code[0] + maxStep;
    if (d0 < -maxStep) c0 = env_prev_code[0] - maxStep;
    if (d1 >  maxStep) c1 = env_prev_code[1] + maxStep;
    if (d1 < -maxStep) c1 = env_prev_code[1] - maxStep;
    env_prev_code[0] = c0;
    env_prev_code[1] = c1;
    mcp_values[CV0_DA_CH] = c0;
    mcp_values[CV1_DA_CH] = c1;
    // Keep CV2/CV3 at baseline so stale values from previous patches don't persist
    mcp_values[CV2_DA_CH] = kGateLowCode;
    mcp_values[CV3_DA_CH] = kGateLowCode;
    mcp_writeAll();
  }
}

void env_render() {
  oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE); oled.setTextWrap(false);
  ui::printClipped(0, 0, 64, "Env");
  // Editing indicator in header right
  oled.setCursor(66,0); oled.print(env_edit_idx==0?"E1":"E2");

  // Compute param displays from stored per-env values
  auto calc_params = [](float AD, float SR, float &Ams, float &Dms, int &Sperc, float &Rms){
    Ams = 1.0f + (AD * AD) * 2000.0f;
    float decay_base = Ams; // link A and D scaling together for display
    float S = SR; if (S < 0.0f) S = 0.0f; if (S > 1.0f) S = 1.0f; Sperc = (int)(S * 100.0f + 0.5f);
    Dms = decay_base * (0.15f + 0.85f * S);
    Rms = 1.0f + (SR * SR) * 2000.0f;
  };

  float Ams0, Dms0, Rms0; int S0;
  float Ams1, Dms1, Rms1; int S1;
  calc_params(env_params_AD[0], env_params_SR[0], Ams0, Dms0, S0, Rms0);
  calc_params(env_params_AD[1], env_params_SR[1], Ams1, Dms1, S1, Rms1);

  // Row 1 after title: velocity percentage of the selected envelope
  oled.setCursor(0,16);
  int vperc = (int)(env_params_Vel[env_edit_idx] * 100.0f + 0.5f);
  if (vperc < 0) vperc = 0; if (vperc > 100) vperc = 100;
  oled.print("Vel "); oled.print(vperc); oled.print("%");

  // E1 rows
  oled.setCursor(0,26);  oled.print(env_edit_idx==0?">E1 ":" E1 ");
  oled.print("A "); oled.print((int)Ams0); oled.print(" D "); oled.print((int)Dms0);
  oled.setCursor(0,36);  oled.print("    S "); oled.print(S0); oled.print("% R "); oled.print((int)Rms0);
  // E2 rows
  oled.setCursor(0,46);  oled.print(env_edit_idx==1?">E2 ":" E2 ");
  oled.print("A "); oled.print((int)Ams1); oled.print(" D "); oled.print((int)Dms1);
  oled.setCursor(0,56);  oled.print("    S "); oled.print(S1); oled.print("% R "); oled.print((int)Rms1);

  oled.display();
}
Patch patch_env = { "Env", env_enter, env_tick, env_render, true };

// Calib patch removed: prefer OLED Diagnostics + static fits.
// Implement calibration helpers now that `calib` exists
static float mapAdsToCv(float adsV) {
  return (adsV - cvBias) * cvGain;
}
static uint16_t voltsToDac(int ch, float v) {
  if (isnan(v)) return 0u; if (v < -5.0f) v = -5.0f; if (v > 5.0f) v = 5.0f;
  float codef;
  #ifdef USE_STATIC_CALIB
    // Interpret `ch` as logical CV index (0..3)
    codef = (float)pico2w_oc_calib::dacVoltsToCode(ch, v);
  #else
    float scale = 4095.0f / 10.0f;
    codef = (v * scale) + 2047.0f;
  #endif
  if (codef < 0) codef = 0; if (codef > 4095) codef = 4095;
  return (uint16_t)(codef + 0.5f);
}

// (Quant patch removed at user request)
// ---- Quant patch: quantizes the two CV inputs to the shared musical key ----
// MAIN page shows each channel's input voltage + output note on a pitch-class
// strip (scale tones outlined, current note filled). KEY page edits the shared
// scale/root via key_update. Quantized V/oct goes out on CV0/CV1.

// ---- Quant state for tick/render split ----
static int      quant_page = 0; // 0=MAIN, 1=KEY
static PotPickup quant_pickup;
static int16_t quant_raw0 = 0, quant_raw1 = 0;
static float quant_vin0 = NAN, quant_vin1 = NAN;
static float quant_vq0 = NAN, quant_vq1 = NAN;
static float quant_prev_vq0 = NAN, quant_prev_vq1 = NAN; // previous quantised output for hysteresis
static uint16_t quant_code0 = 0, quant_code1 = 0;
// Hysteresis: require input to move past the semitone midpoint by this margin
// (in volts) before flipping to the next note. ~10 mV ≈ 0.12 semitones.
static const float kQuantHysteresis = 0.010f;
// Each ADS single-ended read blocks ~1.16 ms at 860 SPS; reading both channels
// every 2 ms tick would overrun the control loop. Decimate to every 4th tick
// (~125 Hz), matching the Env patch — far faster than needed for pitch.
static uint8_t quant_adc_divider = 0;

void quant_enter() {
  resetPotSmooth();
  quant_page = 0;
  pickup_setLive(quant_pickup);
  quant_raw0 = quant_raw1 = 0;
  quant_vin0 = quant_vin1 = NAN;
  quant_vq0 = quant_vq1 = NAN;
  quant_prev_vq0 = quant_prev_vq1 = NAN;
  quant_code0 = quant_code1 = kGateLowCode;
  quant_adc_divider = 0;
}
void quant_tick() {
  // Page handling: short-press toggles MAIN / KEY; KEY page edits shared key.
  float p1 = readPotNormSmooth(PIN_POT1, 0);
  float p2 = readPotNormSmooth(PIN_POT2, 1);
  float p3 = readPotNormSmooth(PIN_POT3, 2);
  if (patchShortPressed) {
    quant_page = (quant_page + 1) % 2;
    patchShortPressed = false;
    pickup_arm(quant_pickup, p1, p2, p3);
  }
  if (quant_page == 1) key_update(quant_pickup, p1, p2, p3);

  // Only read/quantise/output every 4th tick to keep the loop responsive.
  if (++quant_adc_divider < 4) return;
  quant_adc_divider = 0;

  if (haveADS) {
    quant_raw0 = ads.readADC_SingleEnded(AD0_CH);
    quant_raw1 = ads.readADC_SingleEnded(AD1_CH);
    #ifdef USE_STATIC_CALIB
      quant_vin0 = pico2w_oc_calib::adcCodeToVolts(0, quant_raw0);
      quant_vin1 = pico2w_oc_calib::adcCodeToVolts(1, quant_raw1);
    #else
      quant_vin0 = mapAdsToCv(ads.computeVolts(quant_raw0));
      quant_vin1 = mapAdsToCv(ads.computeVolts(quant_raw1));
    #endif
  }
  quant_vq0 = isnan(quant_vin0) ? NAN : key_quantizeVolts(quant_vin0);
  quant_vq1 = isnan(quant_vin1) ? NAN : key_quantizeVolts(quant_vin1);
  // Hysteresis: only update the output note if the input has moved past
  // the current note boundary by at least kQuantHysteresis volts.
  if (!isnan(quant_prev_vq0) && !isnan(quant_vin0)
      && fabsf(quant_vin0 - quant_prev_vq0) < (1.0f/24.0f + kQuantHysteresis)) {
    quant_vq0 = quant_prev_vq0;
  } else {
    quant_prev_vq0 = quant_vq0;
  }
  if (!isnan(quant_prev_vq1) && !isnan(quant_vin1)
      && fabsf(quant_vin1 - quant_prev_vq1) < (1.0f/24.0f + kQuantHysteresis)) {
    quant_vq1 = quant_prev_vq1;
  } else {
    quant_prev_vq1 = quant_vq1;
  }

  if (haveMCP) {
    quant_code0 = voltsToDac(0, quant_vq0);
    quant_code1 = voltsToDac(1, quant_vq1);
    mcp_values[CV0_DA_CH] = quant_code0;
    mcp_values[CV1_DA_CH] = quant_code1;
    mcp_writeAll();
  }
}
// Draw one channel's row: input volts, output note name, and a 12-cell
// pitch-class strip (scale tones outlined, the output note filled).
static void quant_draw_channel(int yTop, const char* label, float vin, float vq,
                               const bool* inScale) {
  oled.setCursor(0, yTop);
  oled.print(label); oled.print(' ');
  if (isnan(vin)) oled.print("--.--"); else oled.print(vin, 2);
  oled.print("V");
  if (!isnan(vq)) {
    int pc = ((int)lroundf(vq * 12.0f)) % 12; if (pc < 0) pc += 12;
    oled.setCursor(86, yTop); oled.print(">"); oled.print(kNoteNames[pc]);
  }
  int outPc = isnan(vq) ? -1 : (((int)lroundf(vq * 12.0f)) % 12 + 12) % 12;
  int yBox = yTop + 10;
  for (int pc = 0; pc < 12; pc++) {
    int x = 18 + pc * 9;
    if (pc == outPc)        oled.fillRect(x, yBox, 8, 7, SSD1306_WHITE);
    else if (inScale[pc])   oled.drawRect(x, yBox, 8, 7, SSD1306_WHITE);
    // non-scale tones left blank
  }
}

void quant_render() {
  oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE); oled.setTextWrap(false);
  ui::printClipped(0, 0, 96, "Quant");
  oled.setCursor(104, 0); oled.print(quant_page == 0 ? "QNT" : "KEY");

  if (quant_page == 1) { key_draw(); oled.display(); return; }

  // Pitch classes belonging to the current scale+root (for the strips).
  bool inScale[12] = {false};
  const Scale& sc = kScales[g_scale];
  for (int i = 0; i < sc.size; i++) inScale[((int)g_root + sc.semis[i]) % 12] = true;

  quant_draw_channel(16, "0", quant_vin0, quant_vq0, inScale);
  quant_draw_channel(40, "1", quant_vin1, quant_vq1, inScale);

  oled.display();
}
Patch patch_quant = { "Quant", quant_enter, quant_tick, quant_render, false };

// ---- Scope patch: basic ADC oscilloscope for ADS inputs ----
static const int SCOPE_SAMPLES = 128;
static int16_t scope_buf[SCOPE_SAMPLES];   // AD0 (channel 0)
static int16_t scope_buf2[SCOPE_SAMPLES];  // AD1 (channel 1)
static int scope_idx = 0;
static bool scope_buf_full = false; // true once buffer has wrapped at least once

void scope_enter() {
  resetPotSmooth();
  scope_idx = 0;
  scope_buf_full = false;
  for (int i=0;i<SCOPE_SAMPLES;i++) { scope_buf[i]=0; scope_buf2[i]=0; }
  // Zero DAC outputs so stale values from previous patch don't persist
  if (haveMCP) {
    mcp_values[CV0_DA_CH] = kGateLowCode;
    mcp_values[CV1_DA_CH] = kGateLowCode;
    mcp_values[CV2_DA_CH] = kGateLowCode;
    mcp_values[CV3_DA_CH] = kGateLowCode;
    mcp_writeAll();
  }
}

void scope_tick() {
  if (!haveADS) return;
  // One sample per channel per tick. ADS1115 at 860SPS takes ~1.16ms per read;
  // 2 reads ≈ 2.3ms out of 5ms tick budget, leaving headroom for DAC writes.
  // Both channels are written to the same index so the traces stay time-aligned.
  int16_t a0 = ads.readADC_SingleEnded(AD0_CH);
  int16_t a1 = ads.readADC_SingleEnded(AD1_CH);
  scope_buf[scope_idx]  = a0;
  scope_buf2[scope_idx] = a1;
  scope_idx = (scope_idx + 1) % SCOPE_SAMPLES;
  if (scope_idx == 0) scope_buf_full = true;
}

void scope_render() {
  oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE);
  ui::printClipped(0, 0, 64, "Scope");

  // Zoom controls: Pot1 = vertical gain, Pot2 = horizontal window (visible samples)
  int rawV = 4095 - analogRead(PIN_POT1);
  int rawH = 4095 - analogRead(PIN_POT2);
  int rawM = 4095 - analogRead(PIN_POT3);
  float vgain = 0.25f + (3.75f * (float)rawV / 4095.0f);      // ~0.25x .. 4x
  int visible = 32 + (rawH * (128 - 32)) / 4095; if (visible < 2) visible = 2; // 32..128

  // Compute midpoint (constant for this frame) outside the per-sample loop
  int midpoint = (int)((rawM / 4095.0f) * 32767.0f) - 16384;

  // Show zoom + midpoint status in title bar (right side)
  oled.setCursor(66,0);
  oled.print("Vx"); oled.print(vgain, 1); oled.print(" H"); oled.print(visible); oled.print(" M"); oled.print((int)(rawM/256));

  // plotting area: from y = 16 .. OLED_H-1
  const int y0 = 16;
  const int h = OLED_H - y0 - 1;
  if (h <= 4) { oled.display(); return; }

  // draw center line
  const int cy = y0 + (h/2);
  oled.drawFastHLine(0, cy, OLED_W, SSD1306_WHITE);

  // Determine available sample count
  int avail = scope_buf_full ? SCOPE_SAMPLES : scope_idx;
  if (avail < 2) { oled.display(); return; }
  if (visible > avail) visible = avail;

  // Trigger detection: find a rising Eurorack edge (falling raw ADC code
  // crossing the midpoint) within the buffer so the display is stable.
  // Search backwards from the most recent samples to find the latest trigger point.
  int trigger_idx = -1;
  int search_start = (scope_idx - 1 + SCOPE_SAMPLES) % SCOPE_SAMPLES;
  int search_len = avail - 1; // need pairs, so one fewer than available
  if (search_len > SCOPE_SAMPLES - 1) search_len = SCOPE_SAMPLES - 1;
  for (int k = 0; k < search_len - visible; k++) {
    int cur = (search_start - k + SCOPE_SAMPLES) % SCOPE_SAMPLES;
    int prv = (cur - 1 + SCOPE_SAMPLES) % SCOPE_SAMPLES;
    // Rising Eurorack voltage = falling raw ADC code crossing midpoint
    if (scope_buf[prv] >= midpoint && scope_buf[cur] < midpoint) {
      // Found a trigger point; use it as the start of the visible window
      // so the rising edge appears near the left side.
      trigger_idx = cur;
      break;
    }
  }

  // Fall back to latest samples if no trigger found (free-run mode)
  int start;
  if (trigger_idx >= 0) {
    start = trigger_idx;
  } else {
    start = (scope_idx - visible + SCOPE_SAMPLES) % SCOPE_SAMPLES;
  }

  // Channel 0 (AD0): solid line.
  int prevx = 0; int prevy = cy;
  for (int i=0;i<visible;i++) {
    int s = scope_buf[(start + i) % SCOPE_SAMPLES];
    int centered = s - midpoint; // ~-32767..+32767
    int y = cy - (int)((centered * vgain * (h-1)) / 32767.0f);
    if (y < y0) y = y0; else if (y > y0 + h - 1) y = y0 + h - 1;
    int x = (i * (OLED_W - 1)) / (visible - 1);
    if (i>0) oled.drawLine(prevx, prevy, x, y, SSD1306_WHITE);
    prevx = x; prevy = y;
  }

  // Channel 1 (AD1): dotted, so the two traces are distinguishable on the mono OLED.
  for (int i=0;i<visible;i++) {
    int s = scope_buf2[(start + i) % SCOPE_SAMPLES];
    int centered = s - midpoint;
    int y = cy - (int)((centered * vgain * (h-1)) / 32767.0f);
    if (y < y0) y = y0; else if (y > y0 + h - 1) y = y0 + h - 1;
    int x = (i * (OLED_W - 1)) / (visible - 1);
    if ((i & 1) == 0) oled.drawPixel(x, y, SSD1306_WHITE);
  }

  oled.display();
}
Patch patch_scope = { "Scope", scope_enter, scope_tick, scope_render, false };

// -------------------- Patch: MIDI-to-CV --------------------
// USB MIDI input -> CV0 pitch, CV1 gate, CV2 velocity, CV3 mod wheel
// Monophonic last-note-priority with a note stack for proper release behaviour.

static const uint8_t kMidiNoteStackSize = 16;
static uint8_t midi_note_stack[kMidiNoteStackSize];
static uint8_t midi_note_stack_count = 0;
static uint8_t midi_vel = 0;         // velocity of current note
static uint8_t midi_mod = 0;         // CC1 mod wheel
static uint8_t midi_channel = 0;     // 0 = OMNI, 1-16 = specific channel
static bool    midi_gate = false;    // gate state
static bool    midi_dirty = true;    // flag to update DAC outputs
static uint8_t midi_last_note = 60;  // for display when gate is off

// MIDI note -> Eurorack voltage (1V/oct, C2=0V, range roughly -2V to +8V)
// MIDI note 36 (C2) = 0V, each semitone = 1/12 V
static float midiNoteToVolts(uint8_t note) {
  return (note - 36) / 12.0f;   // C2=0V, C3=1V, C4=2V, etc.
}

static void midi_handleNoteOff(byte channel, byte pitch, byte velocity);

static void midi_push_note(uint8_t note) {
  // Remove if already in stack (re-trigger)
  for (uint8_t i = 0; i < midi_note_stack_count; i++) {
    if (midi_note_stack[i] == note) {
      for (uint8_t j = i; j < midi_note_stack_count - 1; j++)
        midi_note_stack[j] = midi_note_stack[j+1];
      midi_note_stack_count--;
      break;
    }
  }
  if (midi_note_stack_count < kMidiNoteStackSize) {
    midi_note_stack[midi_note_stack_count++] = note;
  }
}

static void midi_remove_note(uint8_t note) {
  for (uint8_t i = 0; i < midi_note_stack_count; i++) {
    if (midi_note_stack[i] == note) {
      for (uint8_t j = i; j < midi_note_stack_count - 1; j++)
        midi_note_stack[j] = midi_note_stack[j+1];
      midi_note_stack_count--;
      return;
    }
  }
}

static void midi_handleNoteOn(byte channel, byte pitch, byte velocity) {
  (void)channel;
  if (velocity == 0) { midi_handleNoteOff(channel, pitch, velocity); return; }
  midi_push_note(pitch);
  midi_vel = velocity;
  midi_gate = true;
  midi_last_note = pitch;
  midi_dirty = true;
}

static void midi_handleNoteOff(byte channel, byte pitch, byte velocity) {
  (void)channel; (void)velocity;
  midi_remove_note(pitch);
  if (midi_note_stack_count == 0) {
    midi_gate = false;
  } else {
    // Last-note priority: switch to the most recent held note
    midi_last_note = midi_note_stack[midi_note_stack_count - 1];
  }
  midi_dirty = true;
}

static void midi_handleCC(byte channel, byte cc, byte value) {
  (void)channel;
  if (cc == 1) { midi_mod = value; midi_dirty = true; } // mod wheel
}

void midi_enter() {
  resetPotSmooth();
  midi_note_stack_count = 0;
  midi_vel = 0;
  midi_mod = 0;
  midi_gate = false;
  midi_dirty = true;
  midi_last_note = 60;
  midi_channel = 0; // OMNI

  // Register MIDI callbacks
  MIDI.setHandleNoteOn(midi_handleNoteOn);
  MIDI.setHandleNoteOff(midi_handleNoteOff);
  MIDI.setHandleControlChange(midi_handleCC);

  // Zero all outputs
  for (int i = 0; i < 4; i++) mcp_values[i] = kGateLowCode;
  if (haveMCP) mcp_writeAll();
}

void midi_tick() {
  // Poll USB MIDI — callbacks fire here
  MIDI.read();

  // Pot2 selects MIDI channel (OMNI or 1-16)
  float p_ch = readPotNormSmooth(PIN_POT2, 1);
  uint8_t newCh = (uint8_t)(p_ch * 16.99f); // 0=OMNI, 1-16
  if (newCh != midi_channel) {
    midi_channel = newCh;
    // Re-begin MIDI on new channel
    MIDI.begin(midi_channel == 0 ? MIDI_CHANNEL_OMNI : midi_channel);
    MIDI.setHandleNoteOn(midi_handleNoteOn);
    MIDI.setHandleNoteOff(midi_handleNoteOff);
    MIDI.setHandleControlChange(midi_handleCC);
    midi_dirty = true;
  }

  if (!haveMCP) return;
  if (!midi_dirty) return;
  midi_dirty = false;

  // CV0 = pitch (1V/oct)
  uint8_t activeNote = midi_gate ? midi_note_stack[midi_note_stack_count - 1] : midi_last_note;
  float pitchV = midiNoteToVolts(activeNote);
  mcp_values[CV0_DA_CH] = voltsToDac(0, pitchV);

  // CV1 = gate (+5V on / 0V off)
  mcp_values[CV1_DA_CH] = midi_gate ? kGateHighCode : kGateLowCode;

  // CV2 = velocity (0-5V)
  float velV = (midi_vel / 127.0f) * 5.0f;
  mcp_values[CV2_DA_CH] = voltsToDac(2, velV);

  // CV3 = mod wheel (0-5V)
  float modV = (midi_mod / 127.0f) * 5.0f;
  mcp_values[CV3_DA_CH] = voltsToDac(3, modV);

  mcp_writeAll();
}

static const char* midiNoteName(uint8_t note) {
  static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  return names[note % 12];
}
static int midiNoteOctave(uint8_t note) { return (note / 12) - 1; }

void midi_render() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);
  ui::printClipped(0, 0, 48, "MIDI");

  // Channel display
  oled.setCursor(50, 0);
  if (midi_channel == 0) oled.print("OMNI");
  else { oled.print("CH"); oled.print(midi_channel); }

  // Gate indicator
  oled.setCursor(100, 0);
  oled.print(midi_gate ? "ON" : "--");

  // Note + octave (large)
  oled.setTextSize(2);
  uint8_t dispNote = midi_gate ? midi_note_stack[midi_note_stack_count - 1] : midi_last_note;
  oled.setCursor(0, 18);
  oled.print(midiNoteName(dispNote));
  oled.setTextSize(1);
  oled.print(midiNoteOctave(dispNote));

  // Voltage readout
  oled.setCursor(50, 18);
  float vDisp = midiNoteToVolts(dispNote);
  oled.print(vDisp, 2); oled.print("V");

  // Velocity bar
  oled.setTextSize(1);
  oled.setCursor(0, 38);
  oled.print("Vel "); oled.print(midi_vel);
  drawBar(40, 38, 86, 6, midi_vel / 127.0f);

  // Mod wheel bar
  oled.setCursor(0, 48);
  oled.print("Mod "); oled.print(midi_mod);
  drawBar(40, 48, 86, 6, midi_mod / 127.0f);

  // Note stack count
  oled.setCursor(0, 58);
  oled.print("Notes: "); oled.print(midi_note_stack_count);

  oled.display();
}

Patch patch_midi = { "MIDI", midi_enter, midi_tick, midi_render, true };

// -------------------- Patch: Turing (dual Turing Machine) --------------------
// Two independent Music-Thing-style looping shift registers producing quantised
// pitch + variable gates. Clock-in on AD0 advances both voices; reset-in on AD1
// realigns both loop phases. Outputs: V1 pitch->CV0 / gate->CV1,
// V2 pitch->CV2 / gate->CV3. An internal clock runs as a fallback when no
// external clock is present so the patch is usable on the bench.
//
// Edit pages cycle on short-press: V1 -> V2 -> KEY (long-press exits to menu).
//   V1/V2: Pot1 Randomness, Pot2 Length (2..16), Pot3 Range (scale degrees).
//   KEY  : Pot1 Scale, Pot2 Root note, Pot3 Octave base (global to both voices).

// Scale/root/octave now live in the shared musical-key block above (kScales,
// kNoteNames, g_scale/g_root/g_octave, key_update/key_draw/key_degreeToVolts),
// so Turing, Acid and Quant all share one key.

static const uint32_t kTmInternalMs = 500;       // 120 BPM internal fallback
static const float    kTmGateFraction = 0.5f;     // gate-high time as fraction of the clock interval

// Per-voice state (index 0 = V1, 1 = V2)
static uint16_t tm_reg[2]      = {0xACE1, 0x5B3A};
static uint8_t  tm_step[2]     = {0, 0};
static float    tm_rand[2]     = {0.0f, 0.0f}; // 0..1 flip probability
static uint8_t  tm_len[2]      = {8, 8};       // 2..16
static uint8_t  tm_range[2]    = {12, 12};     // 1..24 scale degrees
static float    tm_note_v[2]   = {0.0f, 0.0f}; // current pitch volts (display + out)
static uint8_t  tm_note_pc[2]  = {0, 0};       // current pitch class (display)
static bool     tm_gate_state[2] = {false, false};
static uint32_t tm_gate_off_ms[2] = {0, 0};
// Musical key (scale/root/octave) is shared module-wide — see g_scale/g_root/g_octave.
// UI / clock
static int      tm_page = 0; // 0=V1, 1=V2, 2=KEY
static PotPickup tm_pickup;
static uint32_t tm_last_clock_ms = 0;
static float    tm_interval_ms = (float)kTmInternalMs;
static bool     tm_clk_gate = false;
static bool     tm_rst_gate = false;
static bool     tm_ext_ever = false; // has any external clock been seen this session
static uint32_t tm_internal_next_ms = 0;
static uint8_t  tm_rst_div = 0;      // sub-counter: poll reset (AD1) every N ticks
static uint8_t  tm_clk_skip = 0;     // skip one clock sample after resuming continuous mode

static inline uint16_t tm_mask(uint8_t len) {
  return (len >= 16) ? 0xFFFFu : (uint16_t)((1u << len) - 1u);
}

// Quantise the voice's register to a pitch (volts) + cache its display fields,
// and decide this step's gate (on/off + length) from the register bits.
static void tm_compute_output(int v, bool setGate) {
  uint8_t len = tm_len[v];
  int value  = tm_reg[v] & 0xFF;                 // 0..255
  int degree = (value * (int)tm_range[v]) >> 8;  // 0..range-1
  tm_note_v[v]  = key_degreeToVolts(degree, &tm_note_pc[v]);
  if (setGate) {
    bool gateOn = ((tm_reg[v] >> (len - 1)) & 1u) != 0; // pattern-locked density
    if (gateOn) {
      tm_gate_state[v] = true;
      // Gate length depends only on tempo (not Length or pattern bits): half the
      // clock interval, clamped so it always fits inside one step.
      float gms = kTmGateFraction * tm_interval_ms;
      if (gms < 8.0f) gms = 8.0f;
      if (tm_interval_ms > 16.0f && gms > tm_interval_ms - 8.0f) gms = tm_interval_ms - 8.0f;
      tm_gate_off_ms[v] = millis() + (uint32_t)gms;
    }
  }
}

// One Turing-machine step: rotate the len-bit register left, feeding back the
// bit that fell off (flipped with probability = randomness knob), then quantise.
static void tm_step_voice(int v) {
  uint8_t len = tm_len[v];
  uint16_t fb = (tm_reg[v] >> (len - 1)) & 1u;
  float r = (float)random(0, 1000) / 1000.0f;
  if (r < tm_rand[v]) fb ^= 1u; // 0=locked loop, 0.5=random, 1=inverted loop
  tm_reg[v] = (uint16_t)(((tm_reg[v] << 1) | fb) & tm_mask(len));
  tm_step[v] = (uint8_t)((tm_step[v] + 1) % len);
  tm_compute_output(v, /*setGate=*/true);
}

static void tm_clock_step() { tm_step_voice(0); tm_step_voice(1); }

// Reset: rotate each register back to its step-0 orientation so the loop
// restarts from the top (exact for locked loops, phase-realign otherwise).
static void tm_reset_voice(int v) {
  uint8_t len = tm_len[v];
  uint8_t k = tm_step[v] % len;
  if (k) {
    uint16_t m = tm_mask(len);
    tm_reg[v] = (uint16_t)(((tm_reg[v] >> k) | (tm_reg[v] << (len - k))) & m);
  }
  tm_step[v] = 0;
  tm_compute_output(v, /*setGate=*/false);
}

void tm_enter() {
  resetPotSmooth();
  tm_page = 0;
  // Musical key is shared/persistent across patches — don't reset it here.
  randomSeed((uint32_t)micros() ^ (uint32_t)analogRead(PIN_POT1));
  for (int v = 0; v < 2; v++) {
    tm_rand[v] = 0.0f; tm_len[v] = 8; tm_range[v] = 12;
    tm_reg[v] = (uint16_t)random(1, 0x10000); // non-zero seed
    tm_step[v] = 0; tm_gate_state[v] = false; tm_gate_off_ms[v] = 0;
    tm_compute_output(v, /*setGate=*/false);
  }
  tm_last_clock_ms = 0;
  tm_interval_ms = (float)kTmInternalMs;
  tm_clk_gate = tm_rst_gate = false;
  tm_ext_ever = false;
  tm_internal_next_ms = millis();
  tm_rst_div = 0;
  tm_clk_skip = 1;
  // Run the clock channel in continuous mode for low-jitter, non-blocking edge
  // detection (same approach as the Clock patch).
  if (haveADS) ads.startADCReading(MUX_BY_CHANNEL[AD_EXT_CLOCK_CH], /*continuous=*/true);
  pickup_setLive(tm_pickup);
  if (haveMCP) {
    mcp_values[CV0_DA_CH] = voltsToDac(0, tm_note_v[0]);
    mcp_values[CV1_DA_CH] = kGateLowCode;
    mcp_values[CV2_DA_CH] = voltsToDac(2, tm_note_v[1]);
    mcp_values[CV3_DA_CH] = kGateLowCode;
    mcp_writeAll();
  }
}

void tm_tick() {
  uint32_t now = millis();
  float pRand  = readPotNormSmooth(PIN_POT1, 0);
  float pLen   = readPotNormSmooth(PIN_POT2, 1);
  float pRange = readPotNormSmooth(PIN_POT3, 2);

  // Short press cycles edit page; arm pickup so pots don't snap the new page.
  if (patchShortPressed) {
    tm_page = (tm_page + 1) % 3;
    patchShortPressed = false;
    pickup_arm(tm_pickup, pRand, pLen, pRange);
  }

  if (tm_page < 2) {
    int v = tm_page;
    float tRand  = tm_rand[v];
    float tLen   = (float)(tm_len[v] - 2) / 14.0f;
    float tRange = (float)(tm_range[v] - 1) / 23.0f;
    if (pickup_update(tm_pickup, 0, pRand, tRand))   tm_rand[v]  = pRand;
    if (pickup_update(tm_pickup, 1, pLen, tLen))     tm_len[v]   = (uint8_t)(2 + (int)(pLen * 14.0f + 0.5f));
    if (pickup_update(tm_pickup, 2, pRange, tRange)) tm_range[v] = (uint8_t)(1 + (int)(pRange * 23.0f + 0.5f));
  } else {
    key_update(tm_pickup, pRand, pLen, pRange); // shared KEY page (scale/root/octave)
  }

  // ---- Clock + reset CV inputs (lower ADC code = higher Eurorack voltage) ----
  // The clock channel runs in ADS continuous mode so it is polled non-blocking
  // every tick (~0.1ms) for low-jitter edge timing — blocking single-shot reads
  // (~1.16ms each) were coarse enough to miss edges and drag the tempo estimate
  // around. Reset (AD1) needs no such precision, so we borrow the ADS for a
  // single-shot read every 16 ticks, then resume clock continuous mode (and skip
  // one possibly-stale clock sample on the following tick).
  if (haveADS) {
    if (++tm_rst_div >= 16) {
      tm_rst_div = 0;
      int16_t aRst = ads.readADC_SingleEnded(AD1_CH);
      bool rstNow = tm_rst_gate ? (aRst < kGateOffThresh) : (aRst < kGateOnThresh);
      if (rstNow && !tm_rst_gate) { tm_reset_voice(0); tm_reset_voice(1); }
      tm_rst_gate = rstNow;
      ads.startADCReading(MUX_BY_CHANNEL[AD_EXT_CLOCK_CH], /*continuous=*/true);
      tm_clk_skip = 1;
    } else if (tm_clk_skip) {
      tm_clk_skip = 0; // let the resumed continuous conversion settle
    } else {
      int16_t aClk = ads.getLastConversionResults(); // non-blocking
      bool clkNow = tm_clk_gate ? (aClk < kGateOffThresh) : (aClk < kGateOnThresh);
      if (clkNow && !tm_clk_gate) {
        if (tm_last_clock_ms) {
          uint32_t dt = now - tm_last_clock_ms;
          if (dt >= 50 && dt <= 3000) {
            if (tm_interval_ms < 1.0f) {
              tm_interval_ms = (float)dt; // first interval seeds the estimate
            } else {
              // Reject outliers (a missed or doubled edge) so one bad interval
              // can't drag the tempo; we still step on every detected edge.
              float ratio = (float)dt / tm_interval_ms;
              if (ratio > 0.6f && ratio < 1.6f)
                tm_interval_ms = 0.8f * tm_interval_ms + 0.2f * (float)dt;
            }
          }
        }
        tm_last_clock_ms = now;
        tm_ext_ever = true;
        tm_clock_step();
      }
      tm_clk_gate = clkNow;
    }
  }

  // Internal clock runs only until the first external clock is seen (bench
  // fallback). Once externally clocked, the module follows that clock and
  // holds when it stops — no internal free-run takeover.
  if (!tm_ext_ever) {
    tm_interval_ms = (float)kTmInternalMs;
    if (now >= tm_internal_next_ms) {
      tm_internal_next_ms = now + kTmInternalMs;
      tm_clock_step();
    }
  }

  // Expire gates
  for (int v = 0; v < 2; v++) {
    if (tm_gate_state[v] && now >= tm_gate_off_ms[v]) tm_gate_state[v] = false;
  }

  // Write outputs: pitch held on CV0/CV2, gates on CV1/CV3.
  if (haveMCP) {
    mcp_values[CV0_DA_CH] = voltsToDac(0, tm_note_v[0]);
    mcp_values[CV1_DA_CH] = tm_gate_state[0] ? kGateHighCode : kGateLowCode;
    mcp_values[CV2_DA_CH] = voltsToDac(2, tm_note_v[1]);
    mcp_values[CV3_DA_CH] = tm_gate_state[1] ? kGateHighCode : kGateLowCode;
    mcp_writeAll();
  }
}

void tm_render() {
  oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE); oled.setTextWrap(false);
  ui::printClipped(0, 0, 96, "Turing");
  oled.setCursor(104, 0);
  oled.print(tm_page == 0 ? "V1" : tm_page == 1 ? "V2" : "KEY");

  if (tm_page < 2) {
    int v = tm_page;
    // Randomness peaks at the knob centre and locks at both ends; show the
    // amount (0..100%) plus which locked end: Loop (len N) or Inv (len 2N).
    float k = tm_rand[v];
    int amt = (int)((1.0f - fabsf(2.0f * k - 1.0f)) * 100.0f + 0.5f);
    oled.setCursor(0, 16);
    oled.print("Rnd "); oled.print(amt); oled.print("%");
    if (amt < 100) oled.print(k < 0.5f ? " Loop" : " Inv");
    oled.setCursor(0, 26);
    oled.print("Len "); oled.print(tm_len[v]);
    oled.print("  Rng "); oled.print(tm_range[v]);
    // Register bits (MSB..LSB left-to-right), filled = 1.
    int len = tm_len[v];
    const int by = 36, sz = 6;
    for (int i = 0; i < len; i++) {
      int x = i * (sz + 1);
      if (x + sz > OLED_W) break;
      if ((tm_reg[v] >> (len - 1 - i)) & 1u) oled.fillRect(x, by, sz, sz, SSD1306_WHITE);
      else oled.drawRect(x, by, sz, sz, SSD1306_WHITE);
    }
  } else {
    key_draw();
  }

  // Bottom: both voices' current note + gate, and clock source/tempo.
  oled.setCursor(0, 48);
  oled.print("1:"); oled.print(kNoteNames[tm_note_pc[0]]); oled.print(tm_gate_state[0] ? "*" : " ");
  oled.setCursor(54, 48);
  oled.print("2:"); oled.print(kNoteNames[tm_note_pc[1]]); oled.print(tm_gate_state[1] ? "*" : " ");
  oled.setCursor(0, 56);
  if (!tm_ext_ever) {
    oled.print("INT "); oled.print((int)(60000.0f / tm_interval_ms + 0.5f)); oled.print(" BPM");
  } else if (millis() - tm_last_clock_ms > kExtClockTimeoutMs) {
    oled.print("EXT -- (stop)");
  } else {
    oled.print("EXT "); oled.print((int)(60000.0f / tm_interval_ms + 0.5f)); oled.print(" BPM");
  }

  oled.display();
}
Patch patch_turing = { "Turing", tm_enter, tm_tick, tm_render, true };

// -------------------- Patch: Acid (TB-303 style, TB-3PO inspired) --------------------
// Where Turing is a *live* shift register, Acid is *seed-regenerated*: the whole
// pattern is laid out deterministically from a 16-bit seed. Turn the Seed knob to
// morph between patterns; Density is bipolar and sculpts gate busyness AND pitch
// range at once (centre = sparse/narrow, extremes = busy/full scale); Steps sets
// loop length. Scale/root/octave come from the shared KEY page. Mono voice with
// discrete expression outs: CV0 pitch (with 303 slides) / CV1 gate / CV2 accent /
// CV3 slide. Clock-in AD0 advances; reset-in AD1 restarts; internal clock is a
// bench fallback until the first external clock arrives. (Generation rules ported
// from Logarhythm's TB-3PO Hemisphere applet.)

#define ACID_MAX_STEPS 32

static uint16_t ac_seed    = 0x1234;
static uint8_t  ac_density = 7;   // 0..14, read bipolar as (density-7) = -7..+7
static uint8_t  ac_steps   = 16;  // 1..32 loop length
// Seed knob is mapped across the full 16-bit space, so it's very sensitive to pot
// noise. Only re-roll the seed once the knob moves past this deadband from the
// position that last set it — stops noise from constantly regenerating the pattern.
static float ac_seed_ref = 0.0f;
static const float kAcSeedDeadband = 0.012f;

static uint8_t  ac_notes[ACID_MAX_STEPS]; // scale-degree index per step
static uint32_t ac_gates   = 0;           // bit s set => step s gated
static uint32_t ac_slides  = 0;
static uint32_t ac_accents = 0;
static uint32_t ac_octups  = 0;
static uint32_t ac_octdns  = 0;

// What the live pattern was generated from (to know when to regenerate)
static uint16_t ac_cur_seed    = 0xFFFF;
static uint8_t  ac_cur_density = 0xFF;
static uint8_t  ac_cur_scale   = 0xFF;

// Playback state
static int      ac_step         = 0;
static float    ac_pitch_cur    = 0.0f;  // glided output volts
static float    ac_pitch_tgt    = 0.0f;
static uint8_t  ac_note_pc      = 0;
static bool     ac_gate_state   = false;
static bool     ac_accent_state = false;
static bool     ac_slide_state  = false;
static uint32_t ac_gate_off_ms  = 0;

// UI / clock (same scheme as Turing)
static int       ac_page = 0; // 0=PLAY, 1=KEY
static PotPickup ac_pickup;
static uint32_t  ac_last_clock_ms = 0;
static float     ac_interval_ms = (float)kTmInternalMs;
static bool      ac_clk_gate = false;
static bool      ac_rst_gate = false;
static bool      ac_ext_ever = false;
static uint32_t  ac_internal_next_ms = 0;
static uint8_t   ac_rst_div = 0;
static uint8_t   ac_clk_skip = 0;

static inline int ac_prop(int a, int b, int c) { return b ? (int)((long)a * c / b) : 0; }
static inline int ac_rand_bit(int prob) { return ((int)random(1, 100) <= prob) ? 1 : 0; }

static inline bool ac_is_gated(int s)  { return (ac_gates  >> s) & 1u; }
static inline bool ac_is_slid(int s)   { return (ac_slides >> s) & 1u; }
static inline bool ac_is_accent(int s) { return (ac_accents>> s) & 1u; }
static inline bool ac_is_octup(int s)  { return (ac_octups >> s) & 1u; }
static inline bool ac_is_octdn(int s)  { return (ac_octdns >> s) & 1u; }

// Output volts for a step via the shared key, shifted +/- one octave per oct bits.
static float ac_pitch_for_step(int s, uint8_t* pc) {
  float v = key_degreeToVolts(ac_notes[s], pc);
  if (ac_is_octup(s)) v += 1.0f; else if (ac_is_octdn(s)) v -= 1.0f;
  if (v < 0.0f) v = 0.0f; else if (v > 5.0f) v = 5.0f;
  return v;
}

// Deterministically rebuild the whole 32-step pattern from the seed, shaped by
// density + the shared scale.
static void ac_regenerate() {
  randomSeed(ac_seed);
  int scale_size = kScales[g_scale].size;

  // ---- pitches: density's left half narrows the available scale degrees ----
  int pitch_dens = constrain((int)ac_density, 0, 8);
  int available = 0;
  if (scale_size > 0) {
    if (pitch_dens > 7) available = scale_size - 1;
    else if (pitch_dens < 2) available = pitch_dens;       // root only, or root+1
    else {
      int range_from_scale = scale_size - 3; if (range_from_scale < 4) range_from_scale = 4;
      available = 3 + ac_prop(pitch_dens - 3, 4, range_from_scale);
      available = constrain(available, 1, scale_size - 1);
    }
  }
  ac_octups = ac_octdns = 0;
  int force_repeat = 50 - pitch_dens * 6;                  // sparser density repeats notes
  for (int s = 0; s < ACID_MAX_STEPS; s++) {
    if (s > 0 && ac_rand_bit(force_repeat)) {
      ac_notes[s] = ac_notes[s - 1];
    } else {
      ac_notes[s] = (uint8_t)random(0, available + 1);
      if (ac_rand_bit(40)) {                               // occasional octave jump
        if (ac_rand_bit(50)) ac_octups |= (1u << s);
        else                 ac_octdns |= (1u << s);
      }
    }
  }

  // ---- gates / slides / accents: density's distance from centre = busyness ----
  int on_off_dens = abs((int)ac_density - 7);
  int densProb = 10 + on_off_dens * 14;
  ac_gates = ac_slides = ac_accents = 0;
  int last_slide = 0, last_accent = 0;
  for (int s = 0; s < ACID_MAX_STEPS; s++) {
    if (ac_rand_bit(densProb)) ac_gates |= (1u << s);
    last_slide  = ac_rand_bit(last_slide  ? 10 : 18); if (last_slide)  ac_slides  |= (1u << s);
    last_accent = ac_rand_bit(last_accent ? 7  : 16); if (last_accent) ac_accents |= (1u << s);
  }

  ac_cur_seed = ac_seed; ac_cur_density = ac_density; ac_cur_scale = g_scale;
}

static inline void ac_regen_if_needed() {
  if (ac_seed != ac_cur_seed || ac_density != ac_cur_density || g_scale != ac_cur_scale)
    ac_regenerate();
}

static inline int ac_next_step(int s) { return (s + 1 >= ac_steps) ? 0 : s + 1; }

// Advance one clock: latch next step's pitch/gate/accent/slide and arm the slide.
static void ac_clock_step() {
  int prev = ac_step;
  ac_step = ac_next_step(ac_step);

  uint8_t pc;
  ac_pitch_tgt = ac_pitch_for_step(ac_step, &pc);
  ac_note_pc = pc;
  bool slid_in = ac_is_slid(prev);        // previous step slides into this one
  if (!slid_in) ac_pitch_cur = ac_pitch_tgt;  // hard jump unless tied by a slide

  bool gated = ac_is_gated(ac_step) || slid_in;
  ac_gate_state   = gated;
  ac_accent_state = gated && ac_is_accent(ac_step);
  ac_slide_state  = ac_is_slid(ac_step);
  if (gated) {
    float frac = ac_slide_state ? 1.0f : kTmGateFraction; // slides hold across the step
    float gms = frac * ac_interval_ms;
    if (gms < 8.0f) gms = 8.0f;
    if (!ac_slide_state && ac_interval_ms > 16.0f && gms > ac_interval_ms - 8.0f)
      gms = ac_interval_ms - 8.0f;
    ac_gate_off_ms = millis() + (uint32_t)gms;
  }
}

// Reset realigns so the next clock plays step 0.
static void ac_reset() { ac_step = (ac_steps > 0) ? ac_steps - 1 : 0; }

void ac_enter() {
  resetPotSmooth();
  ac_page = 0;
  ac_regenerate();
  ac_step = (ac_steps > 0) ? ac_steps - 1 : 0;
  ac_pitch_cur = ac_pitch_tgt = 0.0f;
  ac_gate_state = ac_accent_state = ac_slide_state = false;
  ac_gate_off_ms = 0;
  ac_last_clock_ms = 0;
  ac_interval_ms = (float)kTmInternalMs;
  ac_clk_gate = ac_rst_gate = false;
  ac_ext_ever = false;
  ac_internal_next_ms = millis();
  ac_rst_div = 0; ac_clk_skip = 1;
  if (haveADS) ads.startADCReading(MUX_BY_CHANNEL[AD_EXT_CLOCK_CH], /*continuous=*/true);
  pickup_setLive(ac_pickup);
  ac_seed_ref = readPotNormSmooth(PIN_POT1, 0); // anchor the seed deadband
  if (haveMCP) {
    mcp_values[CV0_DA_CH] = voltsToDac(0, ac_pitch_cur);
    mcp_values[CV1_DA_CH] = kGateLowCode;
    mcp_values[CV2_DA_CH] = kGateLowCode;
    mcp_values[CV3_DA_CH] = kGateLowCode;
    mcp_writeAll();
  }
}

void ac_tick() {
  uint32_t now = millis();
  float p1 = readPotNormSmooth(PIN_POT1, 0);
  float p2 = readPotNormSmooth(PIN_POT2, 1);
  float p3 = readPotNormSmooth(PIN_POT3, 2);

  if (patchShortPressed) {
    ac_page = (ac_page + 1) % 2;
    patchShortPressed = false;
    pickup_arm(ac_pickup, p1, p2, p3);
    ac_seed_ref = p1; // re-anchor so returning to PLAY doesn't re-roll on its own
  }

  if (ac_page == 0) {
    float tSeed = (float)ac_seed / 65535.0f;
    float tDens = (float)ac_density / 14.0f;
    float tStep = (float)(ac_steps - 1) / 31.0f;
    if (pickup_update(ac_pickup, 0, p1, tSeed)
        && fabsf(p1 - ac_seed_ref) > kAcSeedDeadband) {
      ac_seed_ref = p1;
      int sd = (int)(p1 * 65535.0f + 0.5f); if (sd > 65535) sd = 65535;
      ac_seed = (uint16_t)sd;
    }
    if (pickup_update(ac_pickup, 1, p2, tDens)) {
      int d = (int)(p2 * 14.99f); if (d > 14) d = 14;
      ac_density = (uint8_t)d;
    }
    if (pickup_update(ac_pickup, 2, p3, tStep)) {
      int st = 1 + (int)(p3 * 31.99f); if (st > 32) st = 32;
      ac_steps = (uint8_t)st;
    }
  } else {
    key_update(ac_pickup, p1, p2, p3); // shared KEY page
  }

  ac_regen_if_needed(); // seed/density/scale edits rebuild the pattern

  // ---- Clock + reset CV inputs (same low-jitter scheme as Turing) ----
  if (haveADS) {
    if (++ac_rst_div >= 16) {
      ac_rst_div = 0;
      int16_t aRst = ads.readADC_SingleEnded(AD1_CH);
      bool rstNow = ac_rst_gate ? (aRst < kGateOffThresh) : (aRst < kGateOnThresh);
      if (rstNow && !ac_rst_gate) ac_reset();
      ac_rst_gate = rstNow;
      ads.startADCReading(MUX_BY_CHANNEL[AD_EXT_CLOCK_CH], /*continuous=*/true);
      ac_clk_skip = 1;
    } else if (ac_clk_skip) {
      ac_clk_skip = 0;
    } else {
      int16_t aClk = ads.getLastConversionResults();
      bool clkNow = ac_clk_gate ? (aClk < kGateOffThresh) : (aClk < kGateOnThresh);
      if (clkNow && !ac_clk_gate) {
        if (ac_last_clock_ms) {
          uint32_t dt = now - ac_last_clock_ms;
          if (dt >= 50 && dt <= 3000) {
            if (ac_interval_ms < 1.0f) {
              ac_interval_ms = (float)dt;
            } else {
              float ratio = (float)dt / ac_interval_ms;
              if (ratio > 0.6f && ratio < 1.6f)
                ac_interval_ms = 0.8f * ac_interval_ms + 0.2f * (float)dt;
            }
          }
        }
        ac_last_clock_ms = now;
        ac_ext_ever = true;
        ac_clock_step();
      }
      ac_clk_gate = clkNow;
    }
  }

  if (!ac_ext_ever) {
    ac_interval_ms = (float)kTmInternalMs;
    if (now >= ac_internal_next_ms) { ac_internal_next_ms = now + kTmInternalMs; ac_clock_step(); }
  }

  // Glide the pitch toward target (303-style slide); snap when close.
  if (ac_pitch_cur != ac_pitch_tgt) {
    ac_pitch_cur += (ac_pitch_tgt - ac_pitch_cur) * 0.15f;
    if (fabsf(ac_pitch_tgt - ac_pitch_cur) < 0.001f) ac_pitch_cur = ac_pitch_tgt;
  }

  if (ac_gate_state && now >= ac_gate_off_ms) { ac_gate_state = false; ac_accent_state = false; }

  if (haveMCP) {
    mcp_values[CV0_DA_CH] = voltsToDac(0, ac_pitch_cur);
    mcp_values[CV1_DA_CH] = ac_gate_state   ? kGateHighCode : kGateLowCode;
    mcp_values[CV2_DA_CH] = ac_accent_state ? kGateHighCode : kGateLowCode;
    mcp_values[CV3_DA_CH] = ac_slide_state  ? kGateHighCode : kGateLowCode;
    mcp_writeAll();
  }
}

void ac_render() {
  oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE); oled.setTextWrap(false);
  ui::printClipped(0, 0, 96, "Acid");
  oled.setCursor(104, 0);
  oled.print(ac_page == 0 ? "PLAY" : "KEY");

  if (ac_page == 0) {
    oled.setCursor(0, 16); oled.print("Seed ");
    if (ac_seed < 0x1000) oled.print("0");
    if (ac_seed < 0x100)  oled.print("0");
    if (ac_seed < 0x10)   oled.print("0");
    oled.print(ac_seed, HEX);
    oled.setCursor(0, 26); oled.print("Dns ");
    int dd = (int)ac_density - 7; if (dd >= 0) oled.print("+"); oled.print(dd);
    oled.print("  Stp "); oled.print(ac_steps);
    // Step grid: gates filled, accents tick above, current step underlined.
    const int by = 36, sz = 6;
    for (int i = 0; i < ac_steps; i++) {
      int x = i * (sz + 1);
      if (x + sz > OLED_W) break;
      if (ac_is_gated(i)) oled.fillRect(x, by, sz, sz, SSD1306_WHITE);
      else                oled.drawRect(x, by, sz, sz, SSD1306_WHITE);
      if (ac_is_accent(i)) oled.drawPixel(x + sz / 2, by - 2, SSD1306_WHITE);
      if (i == ac_step)    oled.drawFastHLine(x, by + sz + 1, sz, SSD1306_WHITE);
    }
  } else {
    key_draw();
  }

  oled.setCursor(0, 48);
  oled.print("N:"); oled.print(kNoteNames[ac_note_pc]);
  oled.print(ac_gate_state ? (ac_accent_state ? "!" : "*") : " ");
  if (ac_slide_state) { oled.setCursor(54, 48); oled.print("slide"); }
  oled.setCursor(0, 56);
  if (!ac_ext_ever) {
    oled.print("INT "); oled.print((int)(60000.0f / ac_interval_ms + 0.5f)); oled.print(" BPM");
  } else if (millis() - ac_last_clock_ms > kExtClockTimeoutMs) {
    oled.print("EXT -- (stop)");
  } else {
    oled.print("EXT "); oled.print((int)(60000.0f / ac_interval_ms + 0.5f)); oled.print(" BPM");
  }
  oled.display();
}
Patch patch_acid = { "Acid", ac_enter, ac_tick, ac_render, true };

// Arrange the bank so indexes match the home-menu ordering below.
Bank bank_util = { "Util", { &patch_clock, &patch_quant, &patch_euclid, &patch_mod, &patch_env, &patch_scope, &patch_midi, &patch_turing, &patch_acid, &patch_diag }, 10 };
Bank* banks[] = { &bank_util };
static uint8_t bankIdx  = 0;
static uint8_t patchIdx = 0;

// ---- Home menu + input state ----
// Home menu items (4x2 grid viewport). Order updated to the requested first-8 patches.
static const char* kHomeItems[] = { "Clock", "Quant", "Euclid", "LFO", "Env", "Scope", "MIDI", "Turing", "Acid", "Diag" };
static eurorack_ui::OledHomeMenu homeMenu;
static bool homeMenuActive = true;
static uint32_t menuIgnoreUntil = 0;

// -------------------- Input --------------------
void handleButtons() {
  btn.update();
  static uint32_t downAt = 0;
  if (btn.fell())  downAt = millis();
  if (btn.rose()) {
    uint32_t held = millis() - downAt;
    // If home menu active: short -> next, long -> select
    if (homeMenuActive) {
      // ignore spurious releases immediately after entering menu
      if (millis() < menuIgnoreUntil) return;
      if (held <= 600) {
        // short press -> next
        homeMenu.next();
      } else {
        // long press -> select current
        uint8_t sel = homeMenu.commit();
        // Activate the selected patch if it's registered in the current bank;
        // otherwise ignore the press and stay in the menu (defensive guard for
        // banks that may contain gaps).
        if (sel < banks[bankIdx]->patchCount && banks[bankIdx]->patches[sel] != nullptr) {
          homeMenuActive = false;
          bankIdx = 0; patchIdx = sel;
          saveLastPatch(bankIdx, patchIdx);
          Patch* p = banks[bankIdx]->patches[patchIdx];
          if (p && p->enter) p->enter();
        }
      }
    } else {
      // not in menu: short press -> patch-specific action flag; long press -> return to menu
      if (held <= 600) {
        patchShortPressed = true;
      } else {
        // enter menu from patch: clear patch short flag, force redraw and ignore spurious inputs
        homeMenuActive = true;
        patchShortPressed = false;
        clearLastPatch(); // next boot will show menu instead of auto-launching
        menuIgnoreUntil = millis() + 400;
        lastUiMs = 0; // force next UI tick to redraw immediately
        homeMenu.invalidate();
        homeMenu.draw();
        Serial.print("[UI] Returned to menu from patch\n");
      }
    }
  }
}

// -------------------- Setup / Loop --------------------
void setup() {
  // Serial optional
  Serial.begin(115200);
  delay(50);

  // Initialise EEPROM (flash-backed on RP2350)
  EEPROM.begin(EEPROM_SIZE);

  // USB MIDI init (TinyUSB composite: CDC serial + MIDI)
  usb_midi.setStringDescriptor("Pico2W OC MIDI");
  MIDI.begin(MIDI_CHANNEL_OMNI);
  // Callbacks are registered per-patch in midi_enter(); MIDI.read() is a no-op
  // until the MIDI patch is active, so this is safe to call early.

  pinMode(PIN_BTN, INPUT_PULLUP);
  btn.attach(PIN_BTN);
  btn.interval(5);

  analogReadResolution(12); // 0..4095

  // I2C0 (Wire) — OLED only
  Wire.setSDA(I2C0_SDA);
  Wire.setSCL(I2C0_SCL);
  Wire.begin();
  Wire.setClock(400000);

  // I2C1 (Wire1) — ADS1115 + MCP4728
  Wire1.setSDA(I2C1_SDA);
  Wire1.setSCL(I2C1_SCL);
  Wire1.begin();
  Wire1.setClock(400000);

  // Quick I2C presence check
  i2cScan(haveSSD, haveADS, haveMCP);

  // OLED
  if (haveSSD) {
    haveSSD = oled.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_SSD1306);
    if (haveSSD) {
      oled.clearDisplay();
      oled.setTextSize(1);
      oled.setTextColor(SSD1306_WHITE);
      ui::printClipped(0, 0, OLED_W, "Pico2W Util/Diag");
      oled.display();
      delay(200);
    }
  }

  // Home menu init (use shared menu)
  if (haveSSD) {
    // Reserve a top band for the menu/colour zone so patch info prints below it
    homeMenu.begin(&oled, UI_TOP_MARGIN);
    homeMenu.setItems(kHomeItems, (uint8_t)(sizeof(kHomeItems)/sizeof(kHomeItems[0])));
    homeMenuActive = true;
    homeMenu.draw();
    menuIgnoreUntil = millis() + 400;
  }

  // ADS1115
  if (haveADS) {
    // begin(address, wirePort)
    haveADS = ads.begin(I2C_ADDR_ADS, &Wire1);
    if (haveADS) {
      ads.setGain(GAIN_ONE);                 // ADS gain = 1 (FSR depends on library)
      ads.setDataRate(RATE_ADS1115_860SPS);  // fastest
    }
  }

  // MCP4728
  if (haveMCP) {
    haveMCP = mcp.begin(I2C_ADDR_MCP, &Wire1); // uses default addr 0x60 by itself
    if (haveMCP) {
      // IMPORTANT: `fastWrite()` only updates the 12-bit DAC codes; it does NOT set
      // per-channel Vref/Gain/PowerDown bits. Those come from the chip's current
      // input-register/EEPROM state and can vary between boards/firmware.
      // Force a known configuration so code↔volts calibration behaves predictably.
      // With MCP4728 powered at 3.3V, treat VOUT as 0..VDD and use VDD reference (1x).
      (void)mcp.setChannelValue(MCP4728_CHANNEL_A, kGateLowCode, MCP4728_VREF_VDD, MCP4728_GAIN_1X);
      (void)mcp.setChannelValue(MCP4728_CHANNEL_B, kGateLowCode, MCP4728_VREF_VDD, MCP4728_GAIN_1X);
      (void)mcp.setChannelValue(MCP4728_CHANNEL_C, kGateLowCode, MCP4728_VREF_VDD, MCP4728_GAIN_1X);
      (void)mcp.setChannelValue(MCP4728_CHANNEL_D, kGateLowCode, MCP4728_VREF_VDD, MCP4728_GAIN_1X);
      // Keep our mirror array consistent with what we just wrote.
      mcp_values[0] = mcp_values[1] = mcp_values[2] = mcp_values[3] = kGateLowCode;
    }
  }

  // ---- Auto-restore last patch from EEPROM ----
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VAL) {
    uint8_t savedBank  = EEPROM.read(EEPROM_BANK_ADDR);
    uint8_t savedPatch = EEPROM.read(EEPROM_PATCH_ADDR);
    uint8_t numBanks   = sizeof(banks) / sizeof(banks[0]);
    if (savedBank < numBanks && savedPatch < banks[savedBank]->patchCount
        && banks[savedBank]->patches[savedPatch] != nullptr) {
      bankIdx  = savedBank;
      patchIdx = savedPatch;
      homeMenuActive   = false;
      Patch* p = banks[bankIdx]->patches[patchIdx];
      if (p && p->enter) p->enter();
      Serial.printf("[BOOT] Auto-restored patch %s\n", p ? p->name : "?");
    }
  }
}

void loop() {
  handleButtons();

  uint32_t now = millis();

  if (now - lastTickMs >= CTRL_TICK_MS) {
    lastTickMs = now;
    Patch* p = banks[bankIdx]->patches[patchIdx];
    if (p && p->tick) p->tick();
  }

  // Use slower OLED refresh for CV-critical patches to reduce
  // I2C bus contention that causes audible DAC update jitter.
  Patch* curPatch = banks[bankIdx]->patches[patchIdx];
  bool isCvPatch = (curPatch && curPatch->slowOled);
  uint32_t uiInterval = isCvPatch ? UI_FRAME_MS_SLOW : UI_FRAME_MS_ACTIVE;
  if (haveSSD && (now - lastUiMs >= uiInterval)) {
    lastUiMs = now;
    if (homeMenuActive) {
      homeMenu.draw();
    } else {
      Patch* p = banks[bankIdx]->patches[patchIdx];
      if (p && p->render) p->render();
    }
  }
}

