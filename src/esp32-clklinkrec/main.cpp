// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 David Baghurst
//
// esp32-clklinkrec — Ableton Link → Eurorack clock/reset/run-gate,
//                    plus Capture → HTTP POST to the seeed-recorder
//                    Mac app, on the Seeed Xiao ESP32-C5.
//
// This module links against the Ableton Link library (GPL-2.0-or-later);
// the firmware binary is therefore GPL-2.0-or-later. See LICENSE.esp32-clklink.
//
// Behaviour:
//   Boot: Link disabled. WiFi connects; mDNS discovers the recorder
//         (with optional RECORDER_HOST fallback from secrets.h);
//         /healthz confirms reachability.
//   Link button (D0): toggle Link enable on each press.
//   Capture button (D9): POST /capture to the resolved recorder. Red LED
//                        is solid for the duration of the request and
//                        sticks if the request fails.
//   Reset In jack (D10): rising edge fires one Reset Out pulse and
//                        re-syncs Link beat phase.
//
// Link → Eurorack mapping (when enabled):
//   Clock Out  — one pulse per beat (PPQN=1) while Link is playing
//   Reset Out  — fires on transport start and on each bar boundary
//   Run Out    — high while Link reports playing, low otherwise
//
// LED encoding:
//   Blue = Link status
//     off           — Link disabled
//     fast blink    — Link enabled, WiFi not connected
//     slow blink    — Link enabled, WiFi up, no peers yet
//     solid         — locked to ≥1 Link peer
//   Red  = Recorder Capture status
//     off           — idle
//     solid (1×)    — request in flight (turns off on 200 OK)
//     solid sticky  — last request failed (cleared by next 200 OK)
//     200 ms flash  — pressed but no recorder address resolved
//
// Outputs go through a 74HCT14 inverting Schmitt buffer, so "active" at
// the jack/LED corresponds to GPIO LOW from this firmware.
//
// Protocol contract for the HTTP path lives at
// docs/RECORDER_PROTOCOL.md (v2.0). Read that first if you're changing
// behaviour on the firmware↔Mac interface.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
        release_out(PIN_RUN_OUT);
    }
    Serial.printf("[Link] %s\n", on ? "enabled" : "disabled");
}

static size_t link_peer_count() {
    if (!link_initialised) return 0;
    return abl_link_num_peers(link_handle);
}

static void link_tick(unsigned long now_us) {
    if (!link_initialised || !link_enabled) return;

    abl_link_capture_audio_session_state(link_handle, link_state);
    int64_t link_time_us = esp_timer_get_time();
    bool    is_playing   = abl_link_is_playing(link_state);

    if (is_playing != link_is_playing_prev) {
        if (is_playing) {
            fire_reset_pulse(now_us);
            assert_out(PIN_RUN_OUT);
            last_link_beat = -1.0;
        } else {
            release_out(PIN_RUN_OUT);
        }
        link_is_playing_prev = is_playing;
    }
    if (!is_playing) return;

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

// --- Recorder (HTTP/mDNS capture trigger) --------------------------------
// Wire contract: docs/RECORDER_PROTOCOL.md (v2.0).

constexpr uint32_t MDNS_QUERY_TIMEOUT_MS = 2000;
constexpr uint32_t RECORDER_HTTP_TIMEOUT_MS = 1500;
constexpr uint16_t RECORDER_DEFAULT_PORT = 8765;

enum RecorderLedState : uint8_t {
    RECORDER_LED_IDLE = 0,
    RECORDER_LED_IN_FLIGHT,
    RECORDER_LED_ERROR_STICKY,
};

static char     recorder_host[64]      = "";
static uint16_t recorder_port          = RECORDER_DEFAULT_PORT;
static bool     recorder_resolved      = false;
static volatile bool         recorder_in_flight = false;
static volatile RecorderLedState recorder_led_state = RECORDER_LED_IDLE;
static unsigned long recorder_no_addr_flash_until_ms = 0;

// Parse "host:port" or "host" from a const string into the recorder
// address slots. Returns true on success.
static bool parse_recorder_host(const char* spec) {
    if (!spec || !*spec) return false;
    const char* colon = strrchr(spec, ':');
    if (colon) {
        size_t host_len = colon - spec;
        if (host_len == 0 || host_len >= sizeof(recorder_host)) return false;
        memcpy(recorder_host, spec, host_len);
        recorder_host[host_len] = '\0';
        long p = atol(colon + 1);
        if (p <= 0 || p > 65535) return false;
        recorder_port = (uint16_t)p;
    } else {
        if (strlen(spec) >= sizeof(recorder_host)) return false;
        strcpy(recorder_host, spec);
        recorder_port = RECORDER_DEFAULT_PORT;
    }
    return true;
}

// Resolve the recorder address via mDNS, falling back to RECORDER_HOST
// from secrets.h if mDNS doesn't return a service within ~2 s.
// Sets recorder_resolved on success.
static void resolve_recorder_address() {
    recorder_resolved = false;
    recorder_host[0]  = '\0';
    recorder_port     = RECORDER_DEFAULT_PORT;

    Serial.println("[Recorder] querying mDNS for _recorder._tcp...");
    // ESPmDNS's queryService blocks ~3 s by default; this is acceptable
    // at boot / on reconnect. Returns the count of services seen.
    int n = MDNS.queryService("recorder", "tcp");
    if (n > 0) {
        IPAddress ip = MDNS.address(0);
        uint16_t  p  = MDNS.port(0);
        String    h  = MDNS.hostname(0);
        // Prefer the resolved IPv4 — .local hostnames depend on system
        // mDNS responders we can't guarantee on the HTTPClient side.
        snprintf(recorder_host, sizeof(recorder_host), "%s",
                 ip.toString().c_str());
        recorder_port = p;
        recorder_resolved = true;
        Serial.printf("[Recorder] mDNS resolved: %s (%s) port %u\n",
                      recorder_host, h.c_str(), (unsigned)recorder_port);
        return;
    }
    Serial.println("[Recorder] mDNS returned no services");

#ifdef RECORDER_HOST
    if (parse_recorder_host(RECORDER_HOST)) {
        Serial.printf("[Recorder] fallback RECORDER_HOST: %s port %u\n",
                      recorder_host, (unsigned)recorder_port);
        // HTTPClient does a plain DNS lookup that can't resolve .local
        // hostnames. If the fallback is a .local name, resolve it via
        // mDNS now and replace recorder_host with the resulting IP so
        // the POST path is a pure-IP HTTP call.
        size_t hlen = strlen(recorder_host);
        bool is_local = (hlen >= 6 &&
                         strcmp(recorder_host + hlen - 6, ".local") == 0);
        if (is_local) {
            IPAddress ip = MDNS.queryHost(recorder_host, 2000);
            if (ip != IPAddress(0, 0, 0, 0)) {
                Serial.printf("[Recorder] resolved %s -> %s via mDNS\n",
                              recorder_host, ip.toString().c_str());
                snprintf(recorder_host, sizeof(recorder_host), "%s",
                         ip.toString().c_str());
            } else {
                Serial.printf("[Recorder] mDNS could not resolve %s — "
                              "Capture will be a no-op until mDNS or the "
                              "Mac comes back\n", recorder_host);
                return;  // leave recorder_resolved = false
            }
        }
        recorder_resolved = true;
        return;
    }
    Serial.printf("[Recorder] RECORDER_HOST \"%s\" failed to parse\n",
                  RECORDER_HOST);
#else
    Serial.println("[Recorder] no RECORDER_HOST defined in secrets.h, "
                   "Capture will be a no-op until mDNS works");
#endif
}

// One-shot health check after the recorder address is resolved. Logs the
// response. A 503 is informative (recorder is up but partially failing)
// but doesn't invalidate the cached address.
static void check_recorder_healthz() {
    if (!recorder_resolved) return;
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%u/healthz",
             recorder_host, (unsigned)recorder_port);
    HTTPClient http;
    http.setTimeout(RECORDER_HTTP_TIMEOUT_MS);
    if (!http.begin(url)) {
        Serial.printf("[Recorder] healthz begin() failed for %s\n", url);
        return;
    }
    int code = http.GET();
    String body = http.getString();
    http.end();
    Serial.printf("[Recorder] GET /healthz -> %d  body=%s\n",
                  code, body.c_str());
}

// FreeRTOS task: do the POST /capture round-trip without blocking loop().
// All UI/LED state changes back into the main loop happen via the
// recorder_led_state / recorder_in_flight flags.
static void capture_task(void* arg) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%u/capture",
             recorder_host, (unsigned)recorder_port);

    HTTPClient http;
    http.setTimeout(RECORDER_HTTP_TIMEOUT_MS);
    bool began = http.begin(url);
    int code = -1;
    String body;
    if (began) {
        http.addHeader("Content-Type", "application/json");
        code = http.POST((uint8_t*)nullptr, 0);
        body = http.getString();
        http.end();
    } else {
        Serial.printf("[Recorder] capture begin() failed for %s\n", url);
    }

    if (code == 200) {
        Serial.printf("[Recorder] 200 OK  %s\n", body.c_str());
        recorder_led_state = RECORDER_LED_IDLE;
    } else if (code == 503 && body.indexOf("capture_in_flight") >= 0) {
        Serial.printf("[Recorder] 503 capture_in_flight (Mac busy) — no-op\n");
        // Don't promote the in-flight state into a sticky error; the
        // user can simply press again once the Mac finishes.
        recorder_led_state = RECORDER_LED_IDLE;
    } else if (code > 0) {
        Serial.printf("[Recorder] error: HTTP %d  body=%s\n", code, body.c_str());
        recorder_led_state = RECORDER_LED_ERROR_STICKY;
    } else {
        Serial.printf("[Recorder] error: network/timeout (code=%d)\n", code);
        recorder_led_state = RECORDER_LED_ERROR_STICKY;
    }

    recorder_in_flight = false;
    vTaskDelete(NULL);
}

static void trigger_capture() {
    if (recorder_in_flight) {
        Serial.println("[Recorder] Capture press ignored (own request in flight)");
        return;
    }
    if (!recorder_resolved) {
        Serial.println("[Recorder] Capture press: no recorder address resolved");
        // 200 ms inline flash so the user sees feedback that the press
        // was received but couldn't be served. Acceptable to block the
        // loop briefly — this only happens on misconfiguration.
        led_on(PIN_RED_LED);
        recorder_no_addr_flash_until_ms = millis() + 200;
        return;
    }
    Serial.printf("[Recorder] POST /capture -> %s:%u\n",
                  recorder_host, (unsigned)recorder_port);
    recorder_in_flight = true;
    recorder_led_state = RECORDER_LED_IN_FLIGHT;
    BaseType_t ok = xTaskCreate(capture_task, "capture",
                                /*stack*/ 8192, nullptr,
                                /*prio*/ 5, nullptr);
    if (ok != pdPASS) {
        Serial.println("[Recorder] xTaskCreate failed");
        recorder_in_flight = false;
        recorder_led_state = RECORDER_LED_ERROR_STICKY;
    }
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
        trigger_capture();
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
static void update_blue_led(unsigned long now_ms) {
    if (!link_enabled) {
        led_off(PIN_BLUE_LED);
    } else if (WiFi.status() != WL_CONNECTED) {
        if ((now_ms / 100) & 1) led_on(PIN_BLUE_LED); else led_off(PIN_BLUE_LED);
    } else if (link_peer_count() == 0) {
        if ((now_ms / 250) & 1) led_on(PIN_BLUE_LED); else led_off(PIN_BLUE_LED);
    } else {
        led_on(PIN_BLUE_LED);
    }
}

static void update_red_led(unsigned long now_ms) {
    // Inline "no address" flash takes priority while its window is open.
    if (recorder_no_addr_flash_until_ms != 0) {
        if ((long)(now_ms - recorder_no_addr_flash_until_ms) >= 0) {
            recorder_no_addr_flash_until_ms = 0;
            led_off(PIN_RED_LED);
        }
        return;  // stay on (or just turned off) until the window expires
    }
    switch (recorder_led_state) {
        case RECORDER_LED_IDLE:         led_off(PIN_RED_LED); break;
        case RECORDER_LED_IN_FLIGHT:    led_on(PIN_RED_LED);  break;
        case RECORDER_LED_ERROR_STICKY: led_on(PIN_RED_LED);  break;
    }
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
    Serial.println("\n=== esp32-clklinkrec (Link sync + Recorder) ===");

    Serial.setDebugOutput(true);
    esp_log_level_set("*",         ESP_LOG_INFO);
    esp_log_level_set("wifi",      ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("phy_init",  ESP_LOG_WARN);

    print_chip_info();

    pinMode(PIN_CLK_OUT,  OUTPUT); release_out(PIN_CLK_OUT);
    pinMode(PIN_RST_OUT,  OUTPUT); release_out(PIN_RST_OUT);
    pinMode(PIN_RUN_OUT,  OUTPUT); release_out(PIN_RUN_OUT);
    pinMode(PIN_RED_LED,  OUTPUT); led_off(PIN_RED_LED);
    pinMode(PIN_BLUE_LED, OUTPUT); led_off(PIN_BLUE_LED);
    pinMode(PIN_SW_LINK,    INPUT_PULLUP);
    pinMode(PIN_SW_CAPTURE, INPUT_PULLUP);
    pinMode(PIN_RESET_IN,   INPUT);

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
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);

        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            Serial.printf("[WiFi] connected: ch %u (%s), RSSI %d dBm, IP %s\n",
                          ap.primary, band_of(ap.primary), ap.rssi,
                          WiFi.localIP().toString().c_str());
        }

        WiFiUDP udp;
        if (udp.beginPacket(IPAddress(224, 76, 78, 75), 20808)) {
            udp.print("clklinkrec-mcast-test");
            int sent = udp.endPacket();
            Serial.printf("[WiFi] multicast TX self-test: endPacket=%d\n", sent);
        } else {
            Serial.println("[WiFi] multicast TX self-test: beginPacket FAILED");
        }

        // mDNS responder on our side gives the Mac a name to ping back
        // (not strictly required by the v2 protocol, but a courtesy).
        // Resolve the recorder service + healthz check.
        if (MDNS.begin("clklinkrec")) {
            Serial.println("[mDNS] responder started as clklinkrec.local");
        } else {
            Serial.println("[mDNS] responder failed to start");
        }
        resolve_recorder_address();
        check_recorder_healthz();
    } else {
        Serial.println("[WiFi] not connected — Link + Recorder will retry on reconnect");
    }

    link_init();

    Serial.println("[Setup] ready. Link starts disabled. Press Link button to enable.");
    Serial.println("        Press Capture button to POST /capture to the recorder.");
}

// --- Loop -----------------------------------------------------------------
void loop() {
    unsigned long now_us = micros();
    unsigned long now_ms = millis();

    poll_buttons();
    poll_reset_in(now_us);
    link_tick(now_us);
    update_pulse_decay(now_us);
    update_blue_led(now_ms);
    update_red_led(now_ms);

    // WiFi reconnect detection: on the false→true edge, re-resolve the
    // recorder address. The Mac may have moved to a new IP, or we may
    // have come up before the recorder did.
    static bool wifi_was_connected = false;
    bool wifi_is_connected = (WiFi.status() == WL_CONNECTED);
    if (wifi_is_connected && !wifi_was_connected) {
        Serial.println("[WiFi] reconnected — re-resolving recorder address");
        resolve_recorder_address();
        check_recorder_healthz();
    }
    wifi_was_connected = wifi_is_connected;

    // Periodic Link telemetry, only when something changes.
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

    delay(1);
}
