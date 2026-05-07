// AI Module — bring-up sketch
//
// Tests:
//   - All 7 button + LED pairs (master + 6 channels). Press any button,
//     paired LED lights while held.
//   - Audio listening tap: live DC level and peak-to-peak on A0 (L) and
//     A1 (R), reported on serial.
//   - Clock input on D9: interrupt-driven pulse count + last interval.
//   - 3× MCP4822 over SPI: writes a unique fixed test voltage per output.
//     Measure with a meter on each chip's Vout A (pin 8) and Vout B (pin 6).
//   - WiFi + ArduinoOTA: module joins the configured network and accepts
//     OTA firmware uploads. mDNS advertises as <OTA_HOST>.local. Real-time
//     work continues while WiFi connects in the background.
//
// Expected voltages per channel (raw MCP output, before bipolar shift):
//   CV1 (MCP1 Vout A) = 0.5 V
//   CV2 (MCP1 Vout B) = 1.0 V
//   CV3 (MCP2 Vout A) = 1.5 V
//   CV4 (MCP2 Vout B) = 2.0 V
//   CV5 (MCP3 Vout A) = 2.5 V
//   CV6 (MCP3 Vout B) = 3.0 V
//
// Pin map per docs/AI_MODULES_HARDWARE.md §3:
//   master : btn D12, LED D10
//   ch 1   : btn D8,  LED D7
//   ch 2   : btn A2,  LED A3      (A2 = GPIO3 strapping pin — see note)
//   ch 3   : btn D6,  LED D5
//   ch 4   : btn A4,  LED A5
//   ch 5   : btn D4,  LED D3
//   ch 6   : btn A6,  LED A7
//   audio  : L on A0, R on A1     (both ADC1)
//
// Buttons: switch-to-GND, internal pull-up enabled (active LOW).
// LEDs:    anode-via-1kΩ-to-GPIO, cathode-to-GND (active HIGH).
// Audio:   AC-coupled inverting tap, output centred at ~1.65 V (~2048
//          on 12-bit ADC). Peak-to-peak rises with input amplitude.
//
// IMPORTANT: use the D/A label macros, not raw GPIO numbers. The Nano ESP32
// defaults to Arduino-compatible pin remap; raw GPIO numbers won't work.
//
// Note on ch 2 button (A2 / GPIO3): GPIO3 is a strapping pin selecting the
// JTAG signal source. Holding ch 2 down DURING reset will switch JTAG to
// external pins (USB-CDC Serial still works). Normal pressing during runtime
// is unaffected.

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// secrets.h is gitignored — copy secrets.h.example to secrets.h and fill in
// your WiFi SSID/password and OTA password. See file for details.
#include "secrets.h"

// Web server + WebSocket for the diag page (port 80, files embedded in flash).
AsyncWebServer http_server(80);
AsyncWebSocket ws("/ws");

// Static diag-page assets, embedded at link time via board_build.embed_txtfiles
// in platformio.ini. The _start / _end symbols come from the linker.
extern const char index_html_start[]  asm("_binary_src_nanoesp32_corthex_data_index_html_start");
extern const char index_html_end[]    asm("_binary_src_nanoesp32_corthex_data_index_html_end");
extern const char style_css_start[]   asm("_binary_src_nanoesp32_corthex_data_style_css_start");
extern const char style_css_end[]     asm("_binary_src_nanoesp32_corthex_data_style_css_end");
extern const char app_js_start[]      asm("_binary_src_nanoesp32_corthex_data_app_js_start");
extern const char app_js_end[]        asm("_binary_src_nanoesp32_corthex_data_app_js_end");
extern const char plaits_html_start[] asm("_binary_src_nanoesp32_corthex_data_plaits_html_start");
extern const char plaits_html_end[]   asm("_binary_src_nanoesp32_corthex_data_plaits_html_end");
extern const char plaits_js_start[]   asm("_binary_src_nanoesp32_corthex_data_plaits_js_start");
extern const char plaits_js_end[]     asm("_binary_src_nanoesp32_corthex_data_plaits_js_end");
extern const char llm_html_start[]    asm("_binary_src_nanoesp32_corthex_data_llm_html_start");
extern const char llm_html_end[]      asm("_binary_src_nanoesp32_corthex_data_llm_html_end");
extern const char llm_js_start[]      asm("_binary_src_nanoesp32_corthex_data_llm_js_start");
extern const char llm_js_end[]        asm("_binary_src_nanoesp32_corthex_data_llm_js_end");

// Use SPI3 (HSPI) instead of the default SPI2 (FSPI). FSPI has IOMUX
// default pins (GPIO9, 11, 12, 13, 14) for HD/WP/CLK/Q/D that collide
// with our button and LED assignments. SPI3 has no IOMUX defaults —
// only the pins we specify get claimed.
SPIClass HSpi(HSPI);

// WiFi connect + ArduinoOTA setup. Non-blocking — kicks off connection
// and returns; the loop runs even while WiFi is still connecting.
void setup_wifi_ota() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOST);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi: connecting to '%s' (non-blocking)...\n", WIFI_SSID);

  ArduinoOTA.setHostname(OTA_HOST);
  ArduinoOTA.setPassword(OTA_PASS);

  ArduinoOTA.onStart([]() {
    Serial.println("\nOTA: starting firmware upload");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA: complete — rebooting");
  });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int total) {
    static int last_pct = -1;
    int pct = total ? (p * 100) / total : 0;
    if (pct != last_pct) {
      Serial.printf("OTA: %d%%\r", pct);
      last_pct = pct;
    }
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("OTA error %u\n", e);
  });
}

// Called periodically from loop. Reports WiFi status changes and starts
// the OTA listener once WiFi connects. Safe to call every loop tick.
void poll_wifi_ota() {
  static bool ota_started = false;
  static wl_status_t last_status = WL_NO_SHIELD;

  const wl_status_t now_status = WiFi.status();
  if (now_status != last_status) {
    last_status = now_status;
    if (now_status == WL_CONNECTED) {
      Serial.printf("\nWiFi: connected, IP %s, RSSI %d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      if (!ota_started) {
        if (MDNS.begin(OTA_HOST)) {
          Serial.printf("mDNS: %s.local\n", OTA_HOST);
        }
        ArduinoOTA.begin();
        Serial.printf("OTA: ready (host %s, password set)\n", OTA_HOST);
        ota_started = true;
      }
    } else if (now_status == WL_DISCONNECTED) {
      Serial.println("\nWiFi: disconnected");
    }
  }

  if (ota_started) {
    ArduinoOTA.handle();
  }
}

// MCP4822 chip-select pins.
constexpr int CS_MCP1 = D2;
constexpr int CS_MCP2 = D0;   // RX0/D0 — UART RX sacrificed
constexpr int CS_MCP3 = D1;   // TX0/D1 — UART TX sacrificed

// Per-channel test codes (12-bit DAC code; output ≈ code / 1000 volts).
constexpr uint16_t TEST_CODE_CV1 =  500;  // ~0.5 V
constexpr uint16_t TEST_CODE_CV2 = 1000;  // ~1.0 V
constexpr uint16_t TEST_CODE_CV3 = 1500;  // ~1.5 V
constexpr uint16_t TEST_CODE_CV4 = 2000;  // ~2.0 V
constexpr uint16_t TEST_CODE_CV5 = 2500;  // ~2.5 V
constexpr uint16_t TEST_CODE_CV6 = 3500;  // ~3.5 V (diagnostic — chip 3 ch B fault)

// MCP4822 16-bit command:
//   bit 15 : A/B select (0 = ch A, 1 = ch B)
//   bit 14 : ignored
//   bit 13 : GA (0 = 2× gain → 0..4.095 V, 1 = 1× gain → 0..2.048 V)
//   bit 12 : SHDN (1 = active, 0 = shutdown)
//   bits 11..0 : 12-bit value
void mcp4822_write(int cs_pin, bool channel_b, uint16_t value) {
  uint16_t cmd = 0x1000;                        // GA=0 (2×), SHDN=1
  if (channel_b) cmd |= 0x8000;                 // AB=1
  cmd |= (value & 0x0FFF);

  HSpi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cs_pin, LOW);
  HSpi.transfer16(cmd);
  digitalWrite(cs_pin, HIGH);
  HSpi.endTransaction();
}

void write_all_cvs(uint16_t code) {
  mcp4822_write(CS_MCP1, false, code);  // CV1
  mcp4822_write(CS_MCP1, true,  code);  // CV2
  mcp4822_write(CS_MCP2, false, code);  // CV3
  mcp4822_write(CS_MCP2, true,  code);  // CV4
  mcp4822_write(CS_MCP3, false, code);  // CV5
  mcp4822_write(CS_MCP3, true,  code);  // CV6
}

// =====================================================================
// Patch + CV system (Plaits-helper foundation)
// =====================================================================
//
// Six CV outputs drive a Plaits + Swords + T03 voice. Each CV channel
// has a per-channel calibration (codes for ±5V/0V) plus a semantic range
// that captures whether the destination expects unipolar 0..+5V or bipolar
// ±5V or the offset −1.7..+3.4V used by Plaits Model.
//
// A "patch" is six PatchChannel records. Each channel either holds a
// static voltage forever, or runs a gated rise/fall envelope. v1 ignores
// the LFO `cycle` field (data parsed, renderer falls back to static).

// Per-channel CV calibration + semantic range. Codes from 2026-05-02
// bench measurement (memory: project_ai_modules_cv_calibration). Ranges
// from 2026-05-04 bench measurement (memory: project_ai_modules_plaits_mapping).
struct CvCal {
  uint16_t code_pos5v;
  uint16_t code_0v;
  uint16_t code_neg5v;
  float    output_min_v;   // semantic range — clamps user/LLM input
  float    output_max_v;
  int      cs_pin;         // which MCP4822 chip
  bool     channel_b;      // false = Vout A, true = Vout B
};

const CvCal CV_CAL[6] = {
  // CV 1 — Plaits Model — offset bipolar (-1.7 to +3.4 V), discrete 24 bands
  { 367, 2033, 3700, -1.7f,  3.4f, CS_MCP1, false },
  // CV 2 — Plaits Timbre — unipolar 0..5 V
  { 377, 2035, 3692,  0.0f,  5.0f, CS_MCP1, true  },
  // CV 3 — Plaits Harmonics — unipolar 0..5 V
  { 372, 2018, 3665,  0.0f,  5.0f, CS_MCP2, false },
  // CV 4 — dual-purpose. Plaits-only page treats this as Plaits Morph (0..5 V);
  // LLM-voice page treats it as Swords FREQ (±5 V). Firmware range broadened
  // to ±5 V so both work; per-page UI clamps to its own semantic sub-range.
  { 378, 2025, 3671, -5.0f,  5.0f, CS_MCP2, true  },
  // CV 5 — Swords RES — bipolar ±5 V (LLM-voice only; unused on Plaits-only page)
  { 380, 2045, 3710, -5.0f,  5.0f, CS_MCP3, false },
  // CV 6 — T03 VCA — unipolar 0..5 V (LLM-voice only)
  { 384, 2045, 3706,  0.0f,  5.0f, CS_MCP3, true  },
};
constexpr int N_CV = 6;

// PatchChannel — describes one CV's behaviour in a patch.
//   cycle == 0 (none): rise=fall=0 → pure hold; otherwise gated envelope
//   cycle != 0: free-running LFO (v2 — for now renderer treats as static)
enum CycleShape : uint8_t {
  CYCLE_NONE = 0,
  CYCLE_TRI  = 1,
  CYCLE_SIN  = 2,
  CYCLE_SQR  = 3,
  CYCLE_SAW  = 4,
  CYCLE_RAMP = 5,
};

struct PatchChannel {
  float    hold_v;     // peak voltage
  uint16_t rise_ms;    // 0 → instant
  uint16_t fall_ms;    // 0 → instant
  uint8_t  cycle;      // CycleShape — v1 only renders CYCLE_NONE
};

struct Patch {
  char         name[16];
  char         category[8];
  PatchChannel channels[N_CV];
};

// Convert a target voltage to a DAC code via the per-channel calibration.
// Clamps to channel's semantic range first, then to safe DAC code limits.
uint16_t cv_voltage_to_code(int ch, float v) {
  if (ch < 0 || ch >= N_CV) return 2048;
  const CvCal& c = CV_CAL[ch];
  if (v < c.output_min_v) v = c.output_min_v;
  if (v > c.output_max_v) v = c.output_max_v;
  // Linear interp using the +5V and -5V endpoints (most accurate).
  float code_f = c.code_neg5v
               + (v + 5.0f) * (c.code_pos5v - c.code_neg5v) / 10.0f;
  if (code_f < 50.0f)   code_f = 50.0f;
  if (code_f > 4045.0f) code_f = 4045.0f;
  return (uint16_t)(code_f + 0.5f);
}

// Write a voltage to a CV channel via SPI.
void cv_set_voltage(int ch, float v) {
  if (ch < 0 || ch >= N_CV) return;
  mcp4822_write(CV_CAL[ch].cs_pin, CV_CAL[ch].channel_b,
                cv_voltage_to_code(ch, v));
}

// Write a raw DAC code to a CV channel (bypasses calibration; useful for
// testing or when the caller already knows the code).
void cv_set_code(int ch, uint16_t code) {
  if (ch < 0 || ch >= N_CV) return;
  mcp4822_write(CV_CAL[ch].cs_pin, CV_CAL[ch].channel_b, code);
}

// Plaits Model — per-model centre voltages, empirically measured 2026-05-05.
// Bands aren't perfectly uniform; linear-fit residuals exceed band width in
// the upper red bank, so we use a lookup table instead of a formula.
//
// Each entry is the centre voltage of that model's band, computed as the
// midpoint between consecutive observed lower-edges.
constexpr int PLAITS_MODEL_COUNT = 24;
const float PLAITS_MODEL_CENTRE_V[PLAITS_MODEL_COUNT] = {
  // Orange bank (0–7)
  -1.8f, -1.49f, -1.25f, -1.04f, -0.86f, -0.65f, -0.44f, -0.23f,
  // Green bank (8–15)
   -0.02f,  0.18f,  0.39f,  0.60f,  0.80f,  1.01f,  1.22f,  1.42f,
  // Red bank (16–23)
   1.62f,  1.83f,  2.03f,  2.23f,  2.44f,  2.64f,  2.85f,  3.06f,
};

// Forward decl — body lives below ChannelState definition since it touches
// chan_state[]. Keeps the existing call sites and route handler valid.
void plaits_set_model(int model_index);

// Reverse of plaits_set_model: which model index does this voltage select?
// Uses closest-centre matching, which is mathematically equivalent to
// midpoint-boundary detection. Single source of truth for both directions.
int plaits_voltage_to_model(float v) {
  int best = 0;
  float best_d = fabsf(v - PLAITS_MODEL_CENTRE_V[0]);
  for (int i = 1; i < PLAITS_MODEL_COUNT; i++) {
    const float d = fabsf(v - PLAITS_MODEL_CENTRE_V[i]);
    if (d < best_d) { best_d = d; best = i; }
  }
  return best;
}

// =====================================================================
// Envelope render engine (1 kHz tick, gated by D9 input)
// =====================================================================

// Runtime state for each CV channel — recomputed every tick from the
// loaded patch + the gate state.
struct ChannelState {
  uint8_t  mode;            // 0 = static, 1 = envelope
  float    hold_v;          // patch's hold voltage
  uint16_t rise_ms;
  uint16_t fall_ms;
  // Envelope dynamics
  enum Phase { PH_IDLE = 0, PH_RISING, PH_HOLDING, PH_FALLING };
  uint8_t  phase;
  uint32_t phase_start_ms;
  float    phase_start_v;   // voltage at start of current phase (for smooth transitions)
  float    current_v;       // last value written
};

ChannelState chan_state[N_CV];

// Set a CV to a static voltage and update chan_state so envelope_tick
// doesn't override it. Used by /api/cv, /api/plaits/model, /api/patch.
void cv_set_static(int ch, float v) {
  if (ch < 0 || ch >= N_CV) return;
  ChannelState& st = chan_state[ch];
  st.mode = 0;
  st.phase = ChannelState::PH_IDLE;
  st.hold_v = v;
  st.current_v = v;
  cv_set_voltage(ch, v);
}

void plaits_set_model(int model_index) {
  if (model_index < 0)  model_index = 0;
  if (model_index >= PLAITS_MODEL_COUNT) model_index = PLAITS_MODEL_COUNT - 1;
  cv_set_static(0, PLAITS_MODEL_CENTRE_V[model_index]);
}

// Currently loaded patch. Mutable copy so the live UI can edit it.
Patch current_patch = {};

// Apply a patch: snapshot it into chan_state, then immediately write the
// resting voltage of each channel (static channels hold; envelope channels
// idle at 0 V awaiting gate).
void patch_apply(const Patch& p) {
  current_patch = p;
  const uint32_t now_ms = millis();

  for (int i = 0; i < N_CV; i++) {
    const PatchChannel& pc = p.channels[i];
    ChannelState& st = chan_state[i];

    st.hold_v   = pc.hold_v;
    st.rise_ms  = pc.rise_ms;
    st.fall_ms  = pc.fall_ms;

    const bool is_static = (pc.cycle == CYCLE_NONE)
                        && (pc.rise_ms == 0)
                        && (pc.fall_ms == 0);

    if (is_static) {
      st.mode = 0;
      st.phase = ChannelState::PH_IDLE;
      st.current_v = pc.hold_v;
      cv_set_voltage(i, pc.hold_v);
    } else if (pc.cycle == CYCLE_NONE) {
      // Gated envelope; idle at 0 V until gate.
      st.mode = 1;
      st.phase = ChannelState::PH_IDLE;
      st.current_v = 0.0f;
      st.phase_start_ms = now_ms;
      st.phase_start_v  = 0.0f;
      cv_set_voltage(i, 0.0f);
    } else {
      // v2: LFO. For now, render as static at hold_v.
      st.mode = 0;
      st.phase = ChannelState::PH_IDLE;
      st.current_v = pc.hold_v;
      cv_set_voltage(i, pc.hold_v);
    }
  }

  Serial.printf("Patch loaded: '%s' (%s)\n", p.name, p.category);
}

// Gate input state — driven by D9 ISR.
volatile bool     gate_state         = false;
volatile uint32_t gate_changed_us    = 0;
volatile bool     gate_edge_pending  = false;
volatile uint32_t gate_pulse_count   = 0;

void IRAM_ATTR gate_isr() {
  const bool now = (digitalRead(D9) == HIGH);
  if (now != gate_state) {
    gate_state = now;
    gate_changed_us = micros();
    gate_edge_pending = true;
    if (now) gate_pulse_count++;
  }
}

// Called from the main loop once per millisecond. Updates envelope state
// for each enveloped channel, writes new CV codes when the value moves.
void envelope_tick(uint32_t now_ms) {
  // Pick up any pending gate edge.
  bool gate_now;
  bool edge;
  noInterrupts();
  gate_now = gate_state;
  edge = gate_edge_pending;
  gate_edge_pending = false;
  interrupts();

  for (int i = 0; i < N_CV; i++) {
    ChannelState& st = chan_state[i];
    if (st.mode != 1) continue;  // static channels handled at patch_apply

    // Edge handling
    if (edge) {
      if (gate_now) {
        // Gate on → start rising from current value to hold_v
        st.phase = ChannelState::PH_RISING;
        st.phase_start_ms = now_ms;
        st.phase_start_v  = st.current_v;
      } else {
        // Gate off → start falling from current value to 0 V
        st.phase = ChannelState::PH_FALLING;
        st.phase_start_ms = now_ms;
        st.phase_start_v  = st.current_v;
      }
    }

    float target_v = st.current_v;
    const uint32_t elapsed = now_ms - st.phase_start_ms;

    switch (st.phase) {
      case ChannelState::PH_RISING: {
        if (st.rise_ms == 0 || elapsed >= st.rise_ms) {
          target_v = st.hold_v;
          st.phase = ChannelState::PH_HOLDING;
        } else {
          const float t = (float)elapsed / (float)st.rise_ms;
          target_v = st.phase_start_v + t * (st.hold_v - st.phase_start_v);
        }
        break;
      }
      case ChannelState::PH_HOLDING:
        target_v = st.hold_v;
        break;
      case ChannelState::PH_FALLING: {
        if (st.fall_ms == 0 || elapsed >= st.fall_ms) {
          target_v = 0.0f;
          st.phase = ChannelState::PH_IDLE;
        } else {
          const float t = (float)elapsed / (float)st.fall_ms;
          target_v = st.phase_start_v + t * (0.0f - st.phase_start_v);
        }
        break;
      }
      case ChannelState::PH_IDLE:
      default:
        target_v = 0.0f;
        break;
    }

    if (target_v != st.current_v) {
      st.current_v = target_v;
      cv_set_voltage(i, target_v);
    }
  }
}

// =====================================================================
// Test patches — hardcoded so we can validate the pipeline end-to-end
// =====================================================================
//
// Channel order: [Model, Timbre, Harmonics, Swords RES, Swords FREQ, VCA]

// Boot / reset patch — chosen to be audible regardless of which voice is
// patched up (Plaits-only or Plaits+Swords+T03). All-static, with
// CV6 = +5 V so T03 VCA is fully open if it's in the chain.
const Patch test_mode_patch = { "Test Mode", "test", {
  {  0.0f,  0, 0, CYCLE_NONE },  // CV1 Model — engine ≈8 area at 0 V
  {  2.5f,  0, 0, CYCLE_NONE },  // CV2 Timbre — mid
  {  2.5f,  0, 0, CYCLE_NONE },  // CV3 Harmonics — mid
  {  0.0f,  0, 0, CYCLE_NONE },  // CV4 — bipolar mid (Swords FREQ centre / Plaits Morph CCW)
  {  0.0f,  0, 0, CYCLE_NONE },  // CV5 — bipolar mid (Swords RES centre)
  {  5.0f,  0, 0, CYCLE_NONE },  // CV6 — T03 VCA full open (audible)
}};

const Patch test_patches[] = {
  // Pluck Bass — Pair VA + filter envelope, punchy VCA.
  { "Pluck Bass", "bass", {
    { -0.02f, 0,   0,   CYCLE_NONE },  // Model — engine 8 (Pair VA, green slot 1)
    {  2.5f,  0,   0,   CYCLE_NONE },  // CV2 Timbre — full square (mid)
    {  0.8f,  0,   0,   CYCLE_NONE },  // CV3 Harmonics — light detune for life
    {  4.0f,  5,   300, CYCLE_NONE },  // CV4 Swords FREQ — snap open, decay to mid
    {  1.5f,  0,   0,   CYCLE_NONE },  // CV5 Swords RES — moderate, no self-osc
    {  5.0f,  1,   350, CYCLE_NONE },  // CV6 VCA — punchy attack, ~350 ms release
  }},
  // Slow Pad
  { "Slow Pad", "pad", {
    {  0.80f,  0,    0,    CYCLE_NONE },  // Model — engine 12 (Harmonic, green slot 5)
    {  3.5f,   0,    0,    CYCLE_NONE },  // Timbre
    {  3.0f,   0,    0,    CYCLE_NONE },  // Harmonics
    {  1.5f,   0,    0,    CYCLE_NONE },  // RES
    {  2.0f,   800,  1500, CYCLE_NONE },  // FREQ slow envelope
    {  4.5f,   1200, 2000, CYCLE_NONE },  // VCA slow envelope
  }},
  // Kick
  { "Kick", "perc", {
    {  2.64f, 0, 0,   CYCLE_NONE },  // Model — engine 21 (Bass Drum, red bank slot 6)
    {  3.0f,  0, 0,   CYCLE_NONE },  // Timbre
    {  1.5f,  0, 0,   CYCLE_NONE },  // Harmonics
    {  3.0f,  0, 0,   CYCLE_NONE },  // RES — bumpy
    {  2.5f,  1, 80,  CYCLE_NONE },  // FREQ fast click
    {  5.0f,  1, 120, CYCLE_NONE },  // VCA punchy
  }},
  // Drone
  { "Drone", "drone", {
    { -0.44f, 0,    0,    CYCLE_NONE },  // Model — engine 6 (String Machine, orange slot 7)
    {  2.0f,  0,    0,    CYCLE_NONE },  // Timbre
    {  4.0f,  0,    0,    CYCLE_NONE },  // Harmonics
    {  0.0f,  0,    0,    CYCLE_NONE },  // RES
    {  0.0f,  0,    0,    CYCLE_NONE },  // FREQ static at mid
    {  4.5f,  2000, 3000, CYCLE_NONE },  // VCA slow swell
  }},
};
constexpr int N_TEST_PATCHES = sizeof(test_patches) / sizeof(test_patches[0]);

// =====================================================================
// LLM patch bank — 6 patches selectable via panel buttons 1–6
// =====================================================================
//
// When the LLM voice (via the Mac proxy + /llm page) generates a set of 6
// variations on a prompt, the iPad POSTs them as a "bank" to /api/patch/bank.
// The firmware stores them and applies slot 0 immediately. Panel buttons 1–6
// then select among the 6 — channel-button LED stays lit on the active slot.
// `active_patch_idx == -1` means no LLM bank loaded yet (boot state).

constexpr int PATCH_BANK_SIZE = 6;
Patch patch_bank[PATCH_BANK_SIZE] = {};
int   active_patch_idx = -1;

// =====================================================================
// Panel button + LED + audio + clock telemetry (existing bring-up code)
// =====================================================================

struct PanelChannel {
  const char* name;
  int button_pin;
  int led_pin;
  char short_name; // for the compact serial line: M, 1, 2, ...
};

const PanelChannel panel_channels[] = {
  {"master", D12, D10, 'M'},
  {"ch1",    D8,  D7,  '1'},
  {"ch2",    A2,  A3,  '2'},
  {"ch3",    D6,  D5,  '3'},
  {"ch4",    A4,  A5,  '4'},
  {"ch5",    D4,  D3,  '5'},
  {"ch6",    A6,  A7,  '6'},
};
constexpr int N_PANEL = sizeof(panel_channels) / sizeof(panel_channels[0]);
constexpr int N_CHANNELS = N_PANEL;  // legacy alias used by older code below

struct AdcStats {
  uint16_t min;
  uint16_t max;
  uint32_t sum;
  uint32_t count;

  void reset() {
    min = 4095;
    max = 0;
    sum = 0;
    count = 0;
  }
  void add(uint16_t v) {
    if (v < min) min = v;
    if (v > max) max = v;
    sum += v;
    count++;
  }
  uint16_t mean() const { return count ? (uint16_t)(sum / count) : 0; }
  uint16_t pp()   const { return max > min ? (max - min) : 0; }
};

AdcStats stats_l, stats_r;

// HTTP + WebSocket server for the diag page. Static assets are embedded in
// the firmware (see _binary_*_start/_end symbols above). Live telemetry is
// pushed to clients over /ws as JSON.
void setup_web_server() {
  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      Serial.printf("WS: client %u connected from %s\n",
                    client->id(), client->remoteIP().toString().c_str());
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.printf("WS: client %u disconnected\n", client->id());
    }
  });

  http_server.addHandler(&ws);

  // Embedded static handlers — one per file, no filesystem.
  // embed_txtfiles appends a NUL terminator that we must strip from the
  // served length, otherwise the trailing \0 trips strict JS parsers
  // (Chrome throws SyntaxError on a NUL after the last token).
  http_server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    const size_t len = (size_t)(index_html_end - index_html_start) - 1;
    req->send_P(200, "text/html", (const uint8_t*)index_html_start, len);
  });
  http_server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *req) {
    const size_t len = (size_t)(style_css_end - style_css_start) - 1;
    req->send_P(200, "text/css", (const uint8_t*)style_css_start, len);
  });
  http_server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *req) {
    const size_t len = (size_t)(app_js_end - app_js_start) - 1;
    req->send_P(200, "application/javascript", (const uint8_t*)app_js_start, len);
  });
  http_server.on("/plaits", HTTP_GET, [](AsyncWebServerRequest *req) {
    const size_t len = (size_t)(plaits_html_end - plaits_html_start) - 1;
    req->send_P(200, "text/html", (const uint8_t*)plaits_html_start, len);
  });
  http_server.on("/plaits.js", HTTP_GET, [](AsyncWebServerRequest *req) {
    const size_t len = (size_t)(plaits_js_end - plaits_js_start) - 1;
    req->send_P(200, "application/javascript", (const uint8_t*)plaits_js_start, len);
  });
  http_server.on("/llm", HTTP_GET, [](AsyncWebServerRequest *req) {
    const size_t len = (size_t)(llm_html_end - llm_html_start) - 1;
    req->send_P(200, "text/html", (const uint8_t*)llm_html_start, len);
  });
  http_server.on("/llm.js", HTTP_GET, [](AsyncWebServerRequest *req) {
    const size_t len = (size_t)(llm_js_end - llm_js_start) - 1;
    req->send_P(200, "application/javascript", (const uint8_t*)llm_js_start, len);
  });

  // POST /api/plaits/model — body is a single integer 0..23.
  http_server.on("/api/plaits/model", HTTP_POST,
    [](AsyncWebServerRequest *req) {},  // body handler below provides the payload
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index != 0) return;  // only handle first chunk for tiny payloads
      // Trim and parse: accept either a bare integer or {"model":N} JSON.
      char tmp[16] = {0};
      const size_t copylen = (len < sizeof(tmp) - 1) ? len : sizeof(tmp) - 1;
      memcpy(tmp, data, copylen);
      int model = -1;
      // Look for a digit anywhere in the buffer (handles "5", "{\"model\":5}", "model=5")
      for (size_t i = 0; i < copylen; i++) {
        if (tmp[i] >= '0' && tmp[i] <= '9') {
          model = atoi(&tmp[i]);
          break;
        }
      }
      if (model < 0 || model >= PLAITS_MODEL_COUNT) {
        req->send(400, "text/plain", "invalid model index");
        return;
      }
      plaits_set_model(model);
      Serial.printf("Plaits model -> %d\n", model);
      char resp[48];
      snprintf(resp, sizeof(resp), "model=%d voltage=%.4f",
               model, PLAITS_MODEL_CENTRE_V[model]);
      req->send(200, "text/plain", resp);
    });

  // POST /api/patch/test/:N — load hardcoded test patch by index.
  http_server.on("^/api/patch/test/([0-9]+)$", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      const int n = req->pathArg(0).toInt();
      if (n < 0 || n >= N_TEST_PATCHES) {
        req->send(400, "text/plain", "invalid test patch index");
        return;
      }
      patch_apply(test_patches[n]);
      char resp[64];
      snprintf(resp, sizeof(resp), "patch=%s ok", test_patches[n].name);
      req->send(200, "text/plain", resp);
    });

  // POST /api/patch/bank — load 6 LLM-generated patches into the panel-button
  // bank. Body is JSON; can exceed a single TCP chunk so we accumulate into
  // req->_tempObject (a String*) and parse only once the full body has arrived.
  http_server.on("/api/patch/bank", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      String* buf = static_cast<String*>(req->_tempObject);
      if (!buf || buf->length() == 0) {
        req->send(400, "text/plain", "no body");
        return;
      }
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, *buf);
      if (err) {
        req->send(400, "text/plain", "json parse error");
        return;
      }
      JsonArray arr = doc["patches"].as<JsonArray>();
      if (arr.isNull() || arr.size() != PATCH_BANK_SIZE) {
        req->send(400, "text/plain", "expected 6 patches");
        return;
      }

      auto load_chan = [&](JsonVariant n, PatchChannel& c) {
        if (n.is<JsonObject>()) {
          c.hold_v   = n["v"]       | 0.0f;
          c.rise_ms  = n["rise_ms"] | 0;
          c.fall_ms  = n["fall_ms"] | 0;
        } else {
          c.hold_v   = n.as<float>();
          c.rise_ms  = 0;
          c.fall_ms  = 0;
        }
        c.cycle = CYCLE_NONE;
      };

      const char* needed[] = {"model","timbre","harmonics","swords_freq","swords_res","vca"};
      for (int i = 0; i < PATCH_BANK_SIZE; i++) {
        JsonVariant p = arr[i];
        for (const char* k : needed) {
          if (p[k].isNull()) {
            char m[64];
            snprintf(m, sizeof(m), "patch %d missing field: %s", i, k);
            req->send(400, "text/plain", m);
            return;
          }
        }
        Patch& dst = patch_bank[i];
        memset(&dst, 0, sizeof(dst));
        const char* nm = p["name"] | (const char*)nullptr;
        snprintf(dst.name, sizeof(dst.name), "%s", nm ? nm : "LLM Patch");
        snprintf(dst.category, sizeof(dst.category), "%s", "llm");

        const int model = p["model"] | 0;
        const int model_clamped =
          (model < 0) ? 0 : (model >= PLAITS_MODEL_COUNT ? PLAITS_MODEL_COUNT - 1 : model);
        dst.channels[0] = { PLAITS_MODEL_CENTRE_V[model_clamped], 0, 0, CYCLE_NONE };
        load_chan(p["timbre"],      dst.channels[1]);
        load_chan(p["harmonics"],   dst.channels[2]);
        load_chan(p["swords_freq"], dst.channels[3]);
        load_chan(p["swords_res"],  dst.channels[4]);
        load_chan(p["vca"],         dst.channels[5]);
      }

      // Auto-apply slot 0 so the user immediately hears something.
      active_patch_idx = 0;
      patch_apply(patch_bank[0]);
      Serial.printf("Bank loaded — 6 patches, slot 0 active.\n");
      req->send(200, "text/plain", "bank loaded; slot 0 active");
    },
    nullptr,
    // Body chunk handler — accumulate into req->_tempObject (String*).
    // _tempObject is auto-deleted by AsyncWebServer when the request finishes.
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      String* buf = static_cast<String*>(req->_tempObject);
      if (!buf) {
        buf = new String();
        if (total > 0) buf->reserve(total);
        req->_tempObject = buf;
      }
      buf->concat((const char*)data, len);
    });

  // POST /api/patch/reset — re-apply the boot/test-mode patch. Useful as
  // a "panic" button to get back to a known audible state.
  http_server.on("/api/patch/reset", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      patch_apply(test_mode_patch);
      req->send(200, "text/plain", "reset to test mode");
    });

  // POST /api/patch/trigger — synthesise a gate-on/gate-off cycle from
  // software so the page can audition envelopes without a physical gate.
  http_server.on("/api/patch/trigger", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      // Force a "gate-on" edge.
      noInterrupts();
      gate_state = true;
      gate_edge_pending = true;
      gate_pulse_count++;
      interrupts();
      req->send(200, "text/plain", "trigger sent");
    });

  // POST /api/patch/release — synthesise gate-off (release).
  http_server.on("/api/patch/release", HTTP_POST,
    [](AsyncWebServerRequest *req) {
      noInterrupts();
      gate_state = false;
      gate_edge_pending = true;
      interrupts();
      req->send(200, "text/plain", "release sent");
    });

  // POST /api/cv/:ch — body is the target voltage as a plain decimal number
  // (e.g. "1.234" or "-2.5"). Sets that CV channel directly via the calibrated
  // mapping. Used by the v1.1 model-tuning slider; later by per-CV manual-mode
  // sliders. Updates chan_state so static-channel rendering doesn't fight us.
  http_server.on("^/api/cv/([0-9]+)$", HTTP_POST,
    [](AsyncWebServerRequest *req) {},  // no params handler
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index != 0) return;
      const int ch = req->pathArg(0).toInt();
      if (ch < 0 || ch >= N_CV) {
        req->send(400, "text/plain", "invalid channel");
        return;
      }
      char tmp[24] = {0};
      const size_t copylen = (len < sizeof(tmp) - 1) ? len : sizeof(tmp) - 1;
      memcpy(tmp, data, copylen);
      const float v = atof(tmp);
      cv_set_static(ch, v);
      char resp[40];
      snprintf(resp, sizeof(resp), "cv%d=%.3f V", ch + 1, v);
      req->send(200, "text/plain", resp);
    });

  // POST /api/patch — apply a complete LLM-voice patch in one shot.
  // Routes through patch_apply() so per-channel rise/fall envelopes are honoured;
  // the gate input on D9 then drives them.
  // Body is JSON. Each parameter is an object { "v", "rise_ms", "fall_ms" }.
  // CV1 ("model") is special: it's a discrete index 0..23 (never enveloped).
  //   { "model": 0..23,
  //     "timbre":      { "v": 0..5,   "rise_ms": 0, "fall_ms": 0 },
  //     "harmonics":   { "v": 0..5,   "rise_ms": 0, "fall_ms": 0 },
  //     "swords_freq": { "v": -5..5,  "rise_ms": 0, "fall_ms": 0 },
  //     "swords_res":  { "v": -5..5,  "rise_ms": 0, "fall_ms": 0 },
  //     "vca":         { "v": 0..5,   "rise_ms": 0, "fall_ms": 0 },
  //     "rationale":   "..." (optional; firmware ignores it) }
  // rise=fall=0 → pure static hold. Otherwise gated AR envelope on D9.
  http_server.on("/api/patch", HTTP_POST,
    [](AsyncWebServerRequest *req) {},
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index != 0) return;
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) {
        req->send(400, "text/plain", "json parse error");
        return;
      }
      const char* needed[] = {"model","timbre","harmonics","swords_freq","swords_res","vca"};
      for (const char* k : needed) {
        if (doc[k].isNull()) {
          char m[64];
          snprintf(m, sizeof(m), "missing field: %s", k);
          req->send(400, "text/plain", m);
          return;
        }
      }

      // Helper: pull { v, rise_ms, fall_ms } from a JSON node into a PatchChannel.
      // Tolerates a bare number for v (treats it as a static channel).
      auto load_chan = [&](const char* key, PatchChannel& c) {
        JsonVariant n = doc[key];
        if (n.is<JsonObject>()) {
          c.hold_v   = n["v"]       | 0.0f;
          c.rise_ms  = n["rise_ms"] | 0;
          c.fall_ms  = n["fall_ms"] | 0;
        } else {
          c.hold_v   = n.as<float>();
          c.rise_ms  = 0;
          c.fall_ms  = 0;
        }
        c.cycle = CYCLE_NONE;
      };

      Patch p = {};
      strncpy(p.name, "LLM Patch", sizeof(p.name) - 1);
      strncpy(p.category, "llm", sizeof(p.category) - 1);

      // CV1 — Plaits Model (discrete; always static)
      const int model = doc["model"] | 0;
      const int model_clamped =
        (model < 0) ? 0 : (model >= PLAITS_MODEL_COUNT ? PLAITS_MODEL_COUNT - 1 : model);
      p.channels[0] = { PLAITS_MODEL_CENTRE_V[model_clamped], 0, 0, CYCLE_NONE };

      // CV2..CV6 — macros that may carry envelopes
      load_chan("timbre",      p.channels[1]);
      load_chan("harmonics",   p.channels[2]);
      load_chan("swords_freq", p.channels[3]);
      load_chan("swords_res",  p.channels[4]);
      load_chan("vca",         p.channels[5]);

      patch_apply(p);

      Serial.printf("LLM patch applied: model=%d "
                    "t=%.2f(%u/%u) h=%.2f(%u/%u) f=%.2f(%u/%u) "
                    "r=%.2f(%u/%u) vca=%.2f(%u/%u)\n",
                    model_clamped,
                    p.channels[1].hold_v, p.channels[1].rise_ms, p.channels[1].fall_ms,
                    p.channels[2].hold_v, p.channels[2].rise_ms, p.channels[2].fall_ms,
                    p.channels[3].hold_v, p.channels[3].rise_ms, p.channels[3].fall_ms,
                    p.channels[4].hold_v, p.channels[4].rise_ms, p.channels[4].fall_ms,
                    p.channels[5].hold_v, p.channels[5].rise_ms, p.channels[5].fall_ms);
      req->send(200, "text/plain", "patch applied");
    });

  http_server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "not found");
  });

  http_server.begin();
  Serial.printf("HTTP+WS server: listening on :80 "
                "(index=%u css=%u app=%u plaits-html=%u plaits-js=%u bytes embedded)\n",
                (unsigned)(index_html_end - index_html_start),
                (unsigned)(style_css_end - style_css_start),
                (unsigned)(app_js_end - app_js_start),
                (unsigned)(plaits_html_end - plaits_html_start),
                (unsigned)(plaits_js_end - plaits_js_start));
}

// Build and push one telemetry JSON frame to every connected WS client.
// Per-CV current voltages come from chan_state (the live envelope state).
void broadcast_telemetry(const bool* pressed) {
  if (ws.count() == 0) return;  // no listeners → skip the formatting

  const uint32_t uptime_s = millis() / 1000;

  // Snapshot gate state.
  noInterrupts();
  const bool gate_now = gate_state;
  const uint32_t gate_pulses = gate_pulse_count;
  interrupts();

  const int plaits_model = plaits_voltage_to_model(chan_state[0].current_v);

  static char buf[800];
  const int n = snprintf(buf, sizeof(buf),
    "{"
      "\"btn\":[%d,%d,%d,%d,%d,%d,%d],"
      "\"audio\":{\"l\":{\"dc\":%u,\"pp\":%u},\"r\":{\"dc\":%u,\"pp\":%u}},"
      "\"gate\":{\"state\":%d,\"pulses\":%lu},"
      "\"cv\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f],"
      "\"plaits\":{\"model\":%d},"
      "\"bank\":{\"slot\":%d},"
      "\"patch\":\"%s\","
      "\"sys\":{\"uptime_s\":%lu,\"free_heap\":%u,\"rssi\":%d,\"ip\":\"%s\"}"
    "}",
    pressed[0]?1:0, pressed[1]?1:0, pressed[2]?1:0, pressed[3]?1:0,
    pressed[4]?1:0, pressed[5]?1:0, pressed[6]?1:0,
    stats_l.mean(), stats_l.pp(),
    stats_r.mean(), stats_r.pp(),
    gate_now ? 1 : 0, (unsigned long)gate_pulses,
    chan_state[0].current_v, chan_state[1].current_v, chan_state[2].current_v,
    chan_state[3].current_v, chan_state[4].current_v, chan_state[5].current_v,
    plaits_model,
    active_patch_idx,
    current_patch.name,
    (unsigned long)uptime_s,
    (unsigned)ESP.getFreeHeap(),
    (int)WiFi.RSSI(),
    WiFi.localIP().toString().c_str());

  if (n > 0 && n < (int)sizeof(buf)) {
    ws.textAll(buf, n);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { /* wait up to 3 s for USB-CDC */ }
  Serial.println("\n=== AI Module: Plaits helper v1 ===");
  Serial.println("CV mapping: Plaits Model/Timbre/Harmonics + Swords RES/FREQ + 4Play VCA");

  for (int i = 0; i < N_PANEL; i++) {
    pinMode(panel_channels[i].button_pin, INPUT_PULLUP);
    pinMode(panel_channels[i].led_pin,    OUTPUT);
    digitalWrite(panel_channels[i].led_pin, LOW);
  }

  analogReadResolution(12);            // 0..4095
  analogSetAttenuation(ADC_11db);      // ~0..3.3 V input range

  stats_l.reset();
  stats_r.reset();

  // Gate input on D9: respond to both edges. Internal pull-down keeps the
  // pin defined when the conditioning network isn't connected.
  pinMode(D9, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(D9), gate_isr, CHANGE);

  // SPI3 bus (HSpi) + MCP4822 chip selects. SPI3 always uses GPIO-matrix
  // routing — only the explicit pins are claimed, no IOMUX defaults.
  // MISO/SS = -1 because MCP4822 is write-only and we manage CS manually.
  pinMode(CS_MCP1, OUTPUT); digitalWrite(CS_MCP1, HIGH);
  pinMode(CS_MCP2, OUTPUT); digitalWrite(CS_MCP2, HIGH);
  pinMode(CS_MCP3, OUTPUT); digitalWrite(CS_MCP3, HIGH);
  HSpi.begin(D13, -1, D11, -1);

  // Initialise channel state and load the test-mode patch (all static, safe
  // resonance, VCA open) as the boot default. v1.1 focus: dial in Plaits Model.
  for (int i = 0; i < N_CV; i++) {
    chan_state[i] = {};
  }
  patch_apply(test_mode_patch);

  Serial.printf("Test patches available: %d\n", N_TEST_PATCHES);
  for (int i = 0; i < N_TEST_PATCHES; i++) {
    Serial.printf("  [%d] %s (%s)\n", i, test_patches[i].name, test_patches[i].category);
  }

  setup_wifi_ota();
  setup_web_server();
}

void loop() {
  // WiFi state machine + OTA poll. Non-blocking; safe every tick.
  poll_wifi_ota();

  // Periodically clean up dead WebSocket clients (recommended for
  // AsyncWebSocket; cheap to call every loop tick).
  ws.cleanupClients();

  // Buttons → patch-bank slot select.
  // Channel buttons (1–6) edge-detect to load patch_bank[i-1] and light their
  // LED to indicate the active slot. Master button (M) stays as a diagnostic
  // press-thru indicator.
  static bool prev_pressed[N_PANEL] = { false };
  bool pressed[N_PANEL];
  for (int i = 0; i < N_PANEL; i++) {
    pressed[i] = (digitalRead(panel_channels[i].button_pin) == LOW);
  }
  for (int i = 1; i < N_PANEL; i++) {
    if (pressed[i] && !prev_pressed[i]) {
      const int slot = i - 1;
      if (slot >= 0 && slot < PATCH_BANK_SIZE) {
        active_patch_idx = slot;
        patch_apply(patch_bank[slot]);
        Serial.printf("Panel select: patch slot %d ('%s')\n",
                      slot, patch_bank[slot].name);
      }
    }
  }
  // LED state: master = press-thru; ch 1–6 = active-slot indicator.
  digitalWrite(panel_channels[0].led_pin, pressed[0] ? HIGH : LOW);
  for (int i = 1; i < N_PANEL; i++) {
    const int slot = i - 1;
    digitalWrite(panel_channels[i].led_pin,
                 (slot == active_patch_idx) ? HIGH : LOW);
  }
  memcpy(prev_pressed, pressed, sizeof(prev_pressed));

  // Sample audio L/R on every loop tick (~few kHz aggregate).
  stats_l.add(analogRead(A0));
  stats_r.add(analogRead(A1));

  // Envelope render tick at 1 ms cadence.
  static uint32_t last_env_tick_ms = 0;
  const uint32_t now_ms = millis();
  if (now_ms != last_env_tick_ms) {
    last_env_tick_ms = now_ms;
    envelope_tick(now_ms);
  }

  // Periodic serial print + WebSocket telemetry broadcast.
  static uint32_t last_print = 0;
  if (now_ms - last_print >= 250) {
    last_print = now_ms;

    Serial.print("btn: ");
    for (int i = 0; i < N_PANEL; i++) {
      Serial.print(pressed[i] ? panel_channels[i].short_name : '.');
      Serial.print(' ');
    }

    Serial.printf("  audio: L %4u/%4u  R %4u/%4u",
                  stats_l.mean(), stats_l.pp(),
                  stats_r.mean(), stats_r.pp());

    Serial.printf("  gate:%s pulses=%lu  patch:%s\n",
                  gate_state ? "HI" : "lo",
                  (unsigned long)gate_pulse_count,
                  current_patch.name);

    broadcast_telemetry(pressed);

    stats_l.reset();
    stats_r.reset();
  }
}
