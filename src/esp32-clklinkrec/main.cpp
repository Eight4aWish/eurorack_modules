// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 David Baghurst
//
// esp32-clklinkrec — Ableton Link → Eurorack clock/reset/run-gate
//                    on the Seeed Xiao ESP32-C5.
//
// This module links against the Ableton Link library (GPL-2.0-or-later);
// the firmware binary is therefore GPL-2.0-or-later. See LICENSE.esp32-clklink.
//
// Behaviour:
//   Boot: Link disabled. WiFi connects in the background; LEDs idle.
//   Link button (D0): toggle Link enable on each press. When enabled the
//                     instance joins the local Link session.
//   Capture button (D9): logs only for now; Mac-side recorder POST is a
//                        separate workstream.
//   Reset In jack (D10): rising edge fires one Reset Out pulse and
//                        re-syncs Link beat phase.
//
// Link → Eurorack mapping (when enabled):
//   Clock Out  — one pulse per beat (PPQN=1) while Link is playing
//   Reset Out  — fires on transport start and on each bar boundary
//   Run Out    — high while Link reports playing, low otherwise
//
// LED encoding (Blue = Link, Red = Capture):
//   Blue off       — Link disabled
//   Blue fast blink — Link enabled, WiFi not connected
//   Blue slow blink — Link enabled, WiFi up, no peers yet
//   Blue solid     — locked to at least one Link peer
//   Red            — currently always off (reserved for Capture state)
//
// Outputs go through a 74HCT14 inverting Schmitt buffer, so "active" at
// the jack/LED corresponds to GPIO LOW from this firmware.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <math.h>
#include <string.h>
#include "abl_link.h"
#include "esp32-clklinkrec/pins.h"
#include "esp32-clklinkrec/secrets.h"

static inline void assert_out(int p)  { digitalWrite(p, LOW);  }
static inline void release_out(int p) { digitalWrite(p, HIGH); }
static inline void led_on(int p)      { digitalWrite(p, LOW);  }
static inline void led_off(int p)     { digitalWrite(p, HIGH); }
static inline bool button_down(int p) { return digitalRead(p) == LOW; }
static inline bool reset_in_active(int p) { return digitalRead(p) == LOW; }

static const char* band_of(uint8_t channel) {
    if (channel >= 1 && channel <= 14)   return "2.4 GHz";
    if (channel >= 32 && channel <= 177) return "5 GHz";
    return "?";
}

// --- Link state -----------------------------------------------------------
constexpr double   LINK_INITIAL_TEMPO = 120.0;
constexpr double   LINK_QUANTUM       = 4.0;     // beats per bar
constexpr uint32_t PULSE_WIDTH_US     = 10000;   // 10 ms per clock/reset pulse
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 8000;

static struct abl_link        link_handle      = {nullptr};
static abl_link_session_state link_state       = {nullptr};
static bool                   link_initialised = false;
static bool                   link_enabled     = false;
static double                 last_link_beat   = -1.0;
static bool                   link_is_playing_prev = false;

// --- Output pulse scheduling ---------------------------------------------
static unsigned long clock_end_us     = 0;
static bool          clock_active     = false;
static unsigned long reset_end_us     = 0;
static bool          reset_out_active = false;

static void fire_clock_pulse(unsigned long now_us) {
    assert_out(PIN_CLK_OUT);
    clock_end_us = now_us + PULSE_WIDTH_US;
    clock_active = true;
}

static void fire_reset_pulse(unsigned long now_us) {
    assert_out(PIN_RST_OUT);
    reset_end_us     = now_us + PULSE_WIDTH_US;
    reset_out_active = true;
}

static void update_pulse_decay(unsigned long now_us) {
    if (clock_active && (long)(now_us - clock_end_us) >= 0) {
        release_out(PIN_CLK_OUT);
        clock_active = false;
    }
    if (reset_out_active && (long)(now_us - reset_end_us) >= 0) {
        release_out(PIN_RST_OUT);
        reset_out_active = false;
    }
}

// --- Link wrappers --------------------------------------------------------
static void link_init() {
    if (link_initialised) return;
    link_handle = abl_link_create(LINK_INITIAL_TEMPO);
    link_state  = abl_link_create_session_state();
    abl_link_enable_start_stop_sync(link_handle, true);
    link_initialised = true;
    Serial.println("[Link] instance created (start/stop sync on)");
}

static void link_set_enabled(bool on) {
    if (!link_initialised || link_enabled == on) return;
    abl_link_enable(link_handle, on);
    link_enabled         = on;
    last_link_beat       = -1.0;
    link_is_playing_prev = false;
    if (!on) {
        // Drop the run gate immediately when Link is turned off.
        release_out(PIN_RUN_OUT);
    }
    Serial.printf("[Link] %s\n", on ? "enabled" : "disabled");
}

static size_t link_peer_count() {
    if (!link_initialised) return 0;
    return abl_link_num_peers(link_handle);
}

// Per-loop Link tick: capture session state, detect transport edges,
// detect beat-integer crossings, fire pulses.
static void link_tick(unsigned long now_us) {
    if (!link_initialised || !link_enabled) return;

    abl_link_capture_audio_session_state(link_handle, link_state);
    int64_t link_time_us = esp_timer_get_time();
    bool    is_playing   = abl_link_is_playing(link_state);

    // Transport edges drive the Run gate and a one-shot Reset.
    if (is_playing != link_is_playing_prev) {
        if (is_playing) {
            fire_reset_pulse(now_us);
            assert_out(PIN_RUN_OUT);   // run high while playing
            last_link_beat = -1.0;
        } else {
            release_out(PIN_RUN_OUT);  // run low when stopped
        }
        link_is_playing_prev = is_playing;
    }
    if (!is_playing) return;

    // Beat-integer crossings drive clock pulses; bar 0 also fires reset.
    double beat = abl_link_beat_at_time(link_state, link_time_us, LINK_QUANTUM);
    if (last_link_beat < 0.0) {
        last_link_beat = beat;
        return;
    }
    int last_int = (int)floor(last_link_beat);
    int beat_int = (int)floor(beat);
    if (beat_int > last_int) {
        fire_clock_pulse(now_us);
        int bar_pos = ((beat_int % (int)LINK_QUANTUM) + (int)LINK_QUANTUM) % (int)LINK_QUANTUM;
        if (bar_pos == 0) {
            fire_reset_pulse(now_us);
        }
    }
    last_link_beat = beat;
}

// --- Buttons --------------------------------------------------------------
static void poll_buttons() {
    static bool link_prev    = false;
    static bool capture_prev = false;
    bool link_now    = button_down(PIN_SW_LINK);
    bool capture_now = button_down(PIN_SW_CAPTURE);
    if (link_now && !link_prev) {
        link_set_enabled(!link_enabled);
    }
    if (capture_now && !capture_prev) {
        Serial.println("[Capture] press (recorder POST not implemented yet)");
    }
    link_prev    = link_now;
    capture_prev = capture_now;
}

static void poll_reset_in(unsigned long now_us) {
    static bool prev = false;
    bool now_active = reset_in_active(PIN_RESET_IN);
    if (now_active && !prev) {
        Serial.println("[Reset In] external trigger");
        fire_reset_pulse(now_us);
        last_link_beat = -1.0;
    }
    prev = now_active;
}

// --- LEDs -----------------------------------------------------------------
static void update_link_leds(unsigned long now_ms) {
    if (!link_enabled) {
        led_off(PIN_BLUE_LED);
    } else if (WiFi.status() != WL_CONNECTED) {
        // Fast blink — WiFi not connected.
        if ((now_ms / 100) & 1) led_on(PIN_BLUE_LED); else led_off(PIN_BLUE_LED);
    } else if (link_peer_count() == 0) {
        // Slow blink — WiFi up, waiting for peers.
        if ((now_ms / 250) & 1) led_on(PIN_BLUE_LED); else led_off(PIN_BLUE_LED);
    } else {
        // Solid — locked to ≥1 peer.
        led_on(PIN_BLUE_LED);
    }
    led_off(PIN_RED_LED);  // reserved for Capture state in a later commit
}

// --- Setup ----------------------------------------------------------------
static void print_chip_info() {
    esp_chip_info_t info = {};
    esp_chip_info(&info);
    Serial.printf("[Chip] ESP32-C5 rev %u, %u core(s), CPU %u MHz, IDF %s\n",
                  (unsigned)info.revision, info.cores,
                  (unsigned)ESP.getCpuFreqMHz(), esp_get_idf_version());
    Serial.printf("[Heap] internal free: %u / %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
}

void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) { delay(10); }
    delay(100);
    Serial.println("\n=== esp32-clklinkrec (Link sync) ===");

    Serial.setDebugOutput(true);
    esp_log_level_set("*",         ESP_LOG_INFO);
    esp_log_level_set("wifi",      ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("phy_init",  ESP_LOG_WARN);

    print_chip_info();

    // Outputs default to "released" (74HCT14 input HIGH → output LOW = inactive).
    pinMode(PIN_CLK_OUT,  OUTPUT); release_out(PIN_CLK_OUT);
    pinMode(PIN_RST_OUT,  OUTPUT); release_out(PIN_RST_OUT);
    pinMode(PIN_RUN_OUT,  OUTPUT); release_out(PIN_RUN_OUT);
    pinMode(PIN_RED_LED,  OUTPUT); led_off(PIN_RED_LED);
    pinMode(PIN_BLUE_LED, OUTPUT); led_off(PIN_BLUE_LED);
    pinMode(PIN_SW_LINK,    INPUT_PULLUP);
    pinMode(PIN_SW_CAPTURE, INPUT_PULLUP);
    pinMode(PIN_RESET_IN,   INPUT);

    // Country code: allows 2.4 GHz ch 12/13 and the UK-legal 5 GHz channels.
    wifi_country_t gb = {};
    memcpy(gb.cc, "GB", 2);
    gb.cc[2]  = 0;
    gb.schan  = 1;
    gb.nchan  = 13;
    gb.policy = WIFI_COUNTRY_POLICY_MANUAL;
    esp_wifi_set_country(&gb);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    Serial.printf("[WiFi] MAC: %s, connecting to \"%s\"...\n",
                  WiFi.macAddress().c_str(), WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
        // Modem-sleep DTIM gaps silently drop Link's multicast discovery
        // frames. Disable both via the Arduino wrapper and the IDF API.
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);

        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            Serial.printf("[WiFi] connected: ch %u (%s), RSSI %d dBm, IP %s\n",
                          ap.primary, band_of(ap.primary), ap.rssi,
                          WiFi.localIP().toString().c_str());
        }

        // Multicast TX self-test on the Link discovery group. If a tcpdump
        // on the Mac (`sudo tcpdump -i en0 host 224.76.78.75`) sees this
        // packet, the C5 can put multicast on the wire and any further
        // Link sync problems are downstream.
        WiFiUDP udp;
        if (udp.beginPacket(IPAddress(224, 76, 78, 75), 20808)) {
            udp.print("clklinkrec-mcast-test");
            int sent = udp.endPacket();
            Serial.printf("[WiFi] multicast TX self-test: endPacket=%d\n", sent);
        } else {
            Serial.println("[WiFi] multicast TX self-test: beginPacket FAILED");
        }
    } else {
        Serial.println("[WiFi] not connected — Link will retry when association completes");
    }

    // Create the Link instance up front; enabling it costs nothing more.
    link_init();

    Serial.println("[Setup] ready. Link starts disabled. Press Link button to enable.");
}

// --- Loop -----------------------------------------------------------------
void loop() {
    unsigned long now_us = micros();
    unsigned long now_ms = millis();

    poll_buttons();
    poll_reset_in(now_us);
    link_tick(now_us);
    update_pulse_decay(now_us);
    update_link_leds(now_ms);

    // Periodic telemetry while Link is enabled: peer count, tempo,
    // play state. Once per second, only when something changes so the
    // console isn't flooded.
    static unsigned long last_log_ms       = 0;
    static size_t        last_peers        = 0xFFFF;
    static double        last_tempo        = -1.0;
    static bool          last_playing      = false;
    if (link_enabled && link_initialised && (now_ms - last_log_ms) >= 1000) {
        last_log_ms = now_ms;
        abl_link_capture_app_session_state(link_handle, link_state);
        size_t peers   = link_peer_count();
        double tempo   = abl_link_tempo(link_state);
        bool   playing = abl_link_is_playing(link_state);
        if (peers != last_peers || fabs(tempo - last_tempo) > 0.05 || playing != last_playing) {
            Serial.printf("[Link] peers=%u tempo=%.2f playing=%d\n",
                          (unsigned)peers, tempo, playing ? 1 : 0);
            last_peers   = peers;
            last_tempo   = tempo;
            last_playing = playing;
        }
    }

    // Yield to FreeRTOS — 1 ms is well below the Link beat budget.
    delay(1);
}
