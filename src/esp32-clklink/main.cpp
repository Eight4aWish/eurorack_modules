// ESP32 Clk/Link — Eurorack clock + reset + manual CV with Ableton Link sync.
//
// Hardware: ESP32-Dev driving an MCP4728 quad DAC at 0x60. Output stages are
// non-inverting unipolar with gain ~2: DAC 0 -> 0 V at jack, DAC 2048 -> +5 V.
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
//   LINK:     sync to an Ableton Link network — A clocks on the beat (PPQN=1),
//             B fires reset on each bar boundary, C drives a manual CV from
//             the pot. Falls back to LED indication if WiFi/Link unavailable.

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Adafruit_MCP4728.h>
#include "abl_link.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp32-clklink/pins.h"
#include "secrets.h"

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
static unsigned long clock_end_us   = 0;
static bool          clock_active   = false;
static unsigned long reset_end_us   = 0;
static bool          reset_active   = false;

static int   last_bpm   = 0;
static float current_bpm = 120.0f;

// Map pot 0..4095 to BPM 40..300. Pot reads inverted at the GPIO (CCW = high
// ADC value), so we negate to get CW = fast / CCW = slow at the panel.
static float bpm_from_pot(int pot) {
    if (pot < 0) pot = 0;
    if (pot > 4095) pot = 4095;
    return 300.0f - (pot / 4095.0f) * 260.0f;
}

static unsigned long period_us_for_bpm(float bpm) {
    return (unsigned long)(60000000.0f / bpm);
}

// --- Reset trigger detection (external CV-in) -----------------------------
constexpr int CV_HIGH_THRESH = 2000;
constexpr int CV_LOW_THRESH  = 500;
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

// --- WiFi + Link ----------------------------------------------------------
constexpr double LINK_INITIAL_TEMPO = 120.0;
constexpr double LINK_QUANTUM       = 4.0;  // beats per bar
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 8000;

static struct abl_link        link_handle    = {nullptr};
static abl_link_session_state link_state     = {nullptr};
static bool                   link_initialised = false;
static bool                   link_enabled   = false;
static double                 last_link_beat = -1.0;  // -1 means "no prior beat"

// Map pot 0..4095 to DAC value (0..4095) for Channel C CV output.
// CW on the pot reads ADC near 0, which we want to be max CV. Invert.
static uint16_t pot_to_cv_dac(int pot) {
    if (pot < 0) pot = 0;
    if (pot > 4095) pot = 4095;
    return (uint16_t)(4095 - pot);
}

static bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

static void wifi_begin_nonblocking() {
    if (WiFi.status() == WL_CONNECTED || WiFi.status() == WL_IDLE_STATUS) {
        return;
    }
    Serial.printf("WiFi: connecting to %s...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

static void link_init() {
    if (link_initialised) return;
    link_handle = abl_link_create(LINK_INITIAL_TEMPO);
    link_state  = abl_link_create_session_state();
    link_initialised = true;
    Serial.println("Link: instance created");
}

static void link_set_enabled(bool on) {
    if (!link_initialised || link_enabled == on) return;
    abl_link_enable(link_handle, on);
    link_enabled = on;
    last_link_beat = -1.0;  // resync on next enable
    Serial.printf("Link: %s\n", on ? "enabled" : "disabled");
}

// Tick the Link sync: capture state, detect beat boundaries, fire pulses.
// Called every loop iteration when in LINK mode and WiFi is up.
static void link_tick(unsigned long now_us) {
    if (!link_initialised || !link_enabled) return;

    abl_link_capture_audio_session_state(link_handle, link_state);
    int64_t  link_time_us = esp_timer_get_time();
    double   beat  = abl_link_beat_at_time(link_state, link_time_us, LINK_QUANTUM);

    if (last_link_beat < 0.0) {
        last_link_beat = beat;
        return;
    }

    // Crossed an integer beat boundary?
    int last_int = (int)floor(last_link_beat);
    int beat_int = (int)floor(beat);
    if (beat_int > last_int) {
        fire_clock_pulse(now_us);
        // Quantum-aligned beat 0 -> bar boundary -> reset pulse.
        int bar_pos = ((beat_int % (int)LINK_QUANTUM) + (int)LINK_QUANTUM) % (int)LINK_QUANTUM;
        if (bar_pos == 0) {
            fire_reset_pulse(now_us);
        }
    }
    last_link_beat = beat;
}

static size_t link_peer_count() {
    if (!link_initialised) return 0;
    return abl_link_num_peers(link_handle);
}

// --- LED status -----------------------------------------------------------
static void update_led(Mode mode, unsigned long now_ms) {
    switch (mode) {
        case MODE_OFF:
            digitalWrite(PIN_LED, LOW);
            break;
        case MODE_INTERNAL:
            digitalWrite(PIN_LED, HIGH);
            break;
        case MODE_LINK:
            if (!wifi_is_connected()) {
                // Fast blink (5 Hz) — WiFi not connected.
                digitalWrite(PIN_LED, (now_ms / 100) & 1);
            } else if (link_peer_count() == 0) {
                // Medium blink (2 Hz) — WiFi up, no Link peers yet.
                digitalWrite(PIN_LED, (now_ms / 250) & 1);
            } else {
                // Solid — locked to a Link session.
                digitalWrite(PIN_LED, HIGH);
            }
            break;
    }
}

// --- Setup ----------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println("\nESP32 Clk/Link (Phase 2)");

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

    dac_write(MCP4728_CHANNEL_A, clock_low);
    dac_write(MCP4728_CHANNEL_B, clock_low);
    dac_write(MCP4728_CHANNEL_C, clock_low);
    dac_write(MCP4728_CHANNEL_D, clock_low);

    next_clock_us = micros();

    // Kick off WiFi connection in the background; check status as we go.
    wifi_begin_nonblocking();
    uint32_t t0 = millis();
    while (!wifi_is_connected() && (millis() - t0) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(100);
    }
    if (wifi_is_connected()) {
        // Disable modem sleep both via Arduino wrapper and the ESP-IDF API,
        // since modem-sleep DTIM gaps cause Link's multicast discovery
        // frames to be silently dropped.
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);

        Serial.printf("WiFi: connected\n  SSID:   %s\n  IP:     %s\n"
                      "  Mask:   %s\n  GW:     %s\n  MAC:    %s\n",
                      WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str(),
                      WiFi.subnetMask().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(),
                      WiFi.macAddress().c_str());

        // Multicast TX self-test: send a single UDP packet to the Link
        // discovery group. If a tcpdump on the Mac (`sudo tcpdump -i en0
        // host 224.76.78.75`) sees this, the ESP32 can put multicast on
        // the wire and the problem is downstream; if not, the issue is
        // in our networking stack.
        WiFiUDP udp;
        if (udp.beginPacket(IPAddress(224, 76, 78, 75), 20808)) {
            udp.print("clklink-mcast-test");
            int sent = udp.endPacket();
            Serial.printf("Multicast TX self-test: endPacket=%d\n", sent);
        } else {
            Serial.println("Multicast TX self-test: beginPacket FAILED");
        }
    } else {
        Serial.println("WiFi: timed out; LINK mode will retry later");
    }

    // Always create the Link instance — it costs little and lets us enable
    // it the instant the switch flips to LINK without setup latency.
    link_init();
}

// --- Loop -----------------------------------------------------------------
void loop() {
    static Mode prev_mode = MODE_OFF;
    Mode mode = read_mode();
    unsigned long now_us = micros();
    unsigned long now_ms = millis();

    if (mode != prev_mode) {
        dac_write(MCP4728_CHANNEL_A, clock_low);
        dac_write(MCP4728_CHANNEL_B, clock_low);
        dac_write(MCP4728_CHANNEL_C, clock_low);
        clock_active = false;
        reset_active = false;

        if (mode == MODE_INTERNAL) {
            fire_reset_pulse(now_us);
            current_bpm = bpm_from_pot(analogRead(PIN_POT));
            next_clock_us = now_us + period_us_for_bpm(current_bpm);
            link_set_enabled(false);
        } else if (mode == MODE_LINK) {
            // Late WiFi retry if we missed the initial window.
            if (!wifi_is_connected()) wifi_begin_nonblocking();
            link_set_enabled(true);
            fire_reset_pulse(now_us);  // also send a reset on LINK entry
        } else {
            link_set_enabled(false);
        }
        prev_mode = mode;
    }

    if (mode == MODE_INTERNAL) {
        int pot = analogRead(PIN_POT);
        int pot_quantised = pot & ~0x1F;
        if (pot_quantised != last_bpm) {
            current_bpm = bpm_from_pot(pot);
            last_bpm = pot_quantised;
        }
        if (external_reset_rising_edge()) {
            fire_reset_pulse(now_us);
            next_clock_us = now_us;
        }
        if ((long)(now_us - next_clock_us) >= 0) {
            fire_clock_pulse(now_us);
            next_clock_us += period_us_for_bpm(current_bpm);
        }
    } else if (mode == MODE_LINK) {
        if (external_reset_rising_edge()) {
            fire_reset_pulse(now_us);
            last_link_beat = -1.0;  // resync next tick
        }
        link_tick(now_us);
        // Channel C: pot -> manual CV. Inverted so CW = high CV.
        static uint16_t last_c_dac = 0xFFFF;
        uint16_t c_dac = pot_to_cv_dac(analogRead(PIN_POT));
        if (c_dac != last_c_dac) {
            dac_write(MCP4728_CHANNEL_C, c_dac);
            last_c_dac = c_dac;
        }
    }

    update_pulse_decay(now_us);
    update_led(mode, now_ms);

    // Once per second in LINK mode, log peer count + tempo so we can see
    // whether discovery is making progress.
    static unsigned long last_link_log_ms = 0;
    if (mode == MODE_LINK && link_initialised && (now_ms - last_link_log_ms) > 1000) {
        size_t peers = link_peer_count();
        abl_link_capture_app_session_state(link_handle, link_state);
        double tempo = abl_link_tempo(link_state);
        Serial.printf("Link: peers=%u tempo=%.2f\n", (unsigned)peers, tempo);
        last_link_log_ms = now_ms;
    }

    // Yield to FreeRTOS so the IDLE task on core 1 gets scheduled and the
    // task watchdog stays happy. 1 ms is well below the clock jitter budget
    // at PPQN=1 (period 200 ms - 1.5 s).
    delay(1);
}
