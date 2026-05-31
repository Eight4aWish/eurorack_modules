// ESP32 Clk/Link — Phase 1 (OFF + INTERNAL modes; LINK stubbed)
//
// Hardware: same as the previous esp32oscclk module — ESP32-Dev driving an
// MCP4728 quad DAC at 0x60. Output stages are non-inverting unipolar with
// gain ~2: DAC 0 -> 0 V at jack, DAC 2048 -> +5 V.
//
// Channels:
//   A: clock pulses (INTERNAL or LINK)
//   B: reset pulse (mode entry + external CV trigger; bar boundary in LINK)
//   C: manual CV from pot (LINK mode only; idles at 0 V otherwise)
//   D: unused / not wired
//
// Modes (ON-OFF-ON switch):
//   OFF:      all jacks at 0 V
//   INTERNAL: pot sets BPM (40..300), A clocks, B fires reset on entry and
//             on any external rising edge into the CV input
//   LINK:     not yet implemented — outputs idle, LED slow-blinks to
//             indicate the un-built feature
//
// Phase 1 keeps the scheduler in the main loop (no timer ISR yet). At the
// current PPQN=1 and 40..300 BPM range, the period is 200 ms..1.5 s and
// loop latency is well under 1 ms, so this is more than precise enough.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP4728.h>
#include "esp32-clklink/pins.h"

// --- DAC ------------------------------------------------------------------
Adafruit_MCP4728 mcp;
constexpr uint8_t MCP4728_ADDRESS = 0x60;

// Output stage is non-inverting unipolar gain ~2; DAC 2048 -> +5 V at jack.
constexpr int clock_high = 2048;
constexpr int clock_low  = 0;

static inline void dac_write(MCP4728_channel_t ch, uint16_t val) {
    (void)mcp.setChannelValue(ch, val);
}

// --- Mode -----------------------------------------------------------------
enum Mode { MODE_OFF, MODE_INTERNAL, MODE_LINK };
constexpr int SWITCH_THRESH = 500;  // ADC counts; both throws float low when off

static Mode read_mode() {
    int up   = analogRead(PIN_SW_LINK);
    int down = analogRead(PIN_SW_INT);
    if (up   > SWITCH_THRESH) return MODE_LINK;
    if (down > SWITCH_THRESH) return MODE_INTERNAL;
    return MODE_OFF;
}

// --- Clock state ----------------------------------------------------------
constexpr unsigned long PULSE_WIDTH_US = 10000;  // 10 ms per pulse

static unsigned long next_clock_us  = 0;  // when A should next go high
static unsigned long clock_end_us   = 0;  // when A should go low (after pulse)
static bool          clock_active   = false;

static unsigned long reset_end_us   = 0;  // when B should go low (after pulse)
static bool          reset_active   = false;

static int   last_bpm   = 0;
static float current_bpm = 120.0f;

// Map pot 0..4095 to BPM 40..300 with mild input filtering.
static float bpm_from_pot(int pot) {
    if (pot < 0) pot = 0;
    if (pot > 4095) pot = 4095;
    return 40.0f + (pot / 4095.0f) * 260.0f;
}

static unsigned long period_us_for_bpm(float bpm) {
    return (unsigned long)(60000000.0f / bpm);
}

// --- Reset trigger detection (external CV-in) -----------------------------
constexpr int CV_HIGH_THRESH = 2000;  // ADC counts (>~1.6 V at GPIO)
constexpr int CV_LOW_THRESH  = 500;   // hysteresis lower bound
static bool cv_high_state = false;

static bool external_reset_rising_edge() {
    int v = analogRead(PIN_CV_RESET);
    if (!cv_high_state && v > CV_HIGH_THRESH) {
        cv_high_state = true;
        return true;
    }
    if (cv_high_state && v < CV_LOW_THRESH) {
        cv_high_state = false;
    }
    return false;
}

// --- Pulse scheduling -----------------------------------------------------
static void fire_clock_pulse(unsigned long now) {
    dac_write(MCP4728_CHANNEL_A, clock_high);
    clock_end_us = now + PULSE_WIDTH_US;
    clock_active = true;
}

static void fire_reset_pulse(unsigned long now) {
    dac_write(MCP4728_CHANNEL_B, clock_high);
    reset_end_us = now + PULSE_WIDTH_US;
    reset_active = true;
}

static void update_pulse_decay(unsigned long now) {
    if (clock_active && (long)(now - clock_end_us) >= 0) {
        dac_write(MCP4728_CHANNEL_A, clock_low);
        clock_active = false;
    }
    if (reset_active && (long)(now - reset_end_us) >= 0) {
        dac_write(MCP4728_CHANNEL_B, clock_low);
        reset_active = false;
    }
}

// --- LED status -----------------------------------------------------------
static void update_led(Mode mode, unsigned long now_ms) {
    switch (mode) {
        case MODE_OFF:
            digitalWrite(PIN_LED, LOW);
            break;
        case MODE_INTERNAL:
            digitalWrite(PIN_LED, HIGH);  // solid = clocking
            break;
        case MODE_LINK:
            // Slow heartbeat to signal "feature stubbed, not active".
            digitalWrite(PIN_LED, (now_ms / 500) & 1);
            break;
    }
}

// --- Setup ----------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Clk/Link (Phase 1: INTERNAL only)");

    Wire.begin();
    Wire.setClock(1000000);
    if (!mcp.begin(MCP4728_ADDRESS)) {
        Serial.println("MCP4728 not found. Halt.");
        while (1) delay(10);
    }

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_CV_RESET, INPUT);

    // All channels start at jack-low.
    dac_write(MCP4728_CHANNEL_A, clock_low);
    dac_write(MCP4728_CHANNEL_B, clock_low);
    dac_write(MCP4728_CHANNEL_C, clock_low);
    dac_write(MCP4728_CHANNEL_D, clock_low);

    next_clock_us = micros();
}

// --- Loop -----------------------------------------------------------------
void loop() {
    static Mode prev_mode = MODE_OFF;
    Mode mode = read_mode();
    unsigned long now_us = micros();
    unsigned long now_ms = millis();

    // Mode transition handling.
    if (mode != prev_mode) {
        // Make sure outputs drop when leaving an active mode.
        dac_write(MCP4728_CHANNEL_A, clock_low);
        dac_write(MCP4728_CHANNEL_B, clock_low);
        dac_write(MCP4728_CHANNEL_C, clock_low);
        clock_active = false;
        reset_active = false;

        if (mode == MODE_INTERNAL) {
            // Fire reset on entry, then start clocking at "now + period".
            fire_reset_pulse(now_us);
            current_bpm = bpm_from_pot(analogRead(PIN_POT));
            next_clock_us = now_us + period_us_for_bpm(current_bpm);
        }
        prev_mode = mode;
    }

    if (mode == MODE_INTERNAL) {
        // Update BPM from pot only when it changes appreciably, to avoid
        // jitter from analog read noise affecting the period mid-tick.
        int pot = analogRead(PIN_POT);
        int pot_quantised = pot & ~0x1F;  // ignore noise in the low 5 bits
        if (pot_quantised != last_bpm) {
            current_bpm = bpm_from_pot(pot);
            last_bpm = pot_quantised;
        }

        // External reset trigger realigns the clock to "next pulse at now".
        if (external_reset_rising_edge()) {
            fire_reset_pulse(now_us);
            next_clock_us = now_us;  // fire the first tick immediately
        }

        // Schedule clock pulse if due.
        if ((long)(now_us - next_clock_us) >= 0) {
            fire_clock_pulse(now_us);
            next_clock_us += period_us_for_bpm(current_bpm);
        }
    }

    update_pulse_decay(now_us);
    update_led(mode, now_ms);
}
