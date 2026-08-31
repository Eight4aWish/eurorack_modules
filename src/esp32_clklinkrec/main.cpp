// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 David Baghurst
//
// esp32_clklinkrec — Ableton Link → Eurorack clock/reset/run-gate,
//                    plus Capture → HTTP POST to the seeed-recorder
//                    Mac app, on the Seeed Xiao ESP32-C5.
//
// This module links against the Ableton Link library (GPL-2.0-or-later);
// the firmware binary is therefore GPL-2.0-or-later. See LICENSE.esp32_clklink.
//
// Behaviour:
//   Boot: Link disabled. WiFi connects; mDNS discovers the recorder
//         (with optional RECORDER_HOST fallback from secrets.h);
//         /healthz confirms reachability.
//   Link button (D0): toggle Link enable on each press.
//   Capture button (D9): POST /capture to the resolved recorder. Red LED
//                        is solid for the duration of the request and
//                        sticks if the request fails.
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
#include <Preferences.h>
#include <soc/soc_caps.h>
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
#include "esp32_clklinkrec/pins.h"
#include "esp32_clklinkrec/secrets.h"

// Set to 0 once WiFi bring-up is settled. At 1 the IDF wifi tags log at
// INFO, which shows the scan/auth/assoc timeline — that is how you tell
// scan cost from handshake cost when a connect is slow.
#define WIFI_DEBUG_LOG 1

static inline void assert_out(int p)  { digitalWrite(p, LOW);  }
static inline void release_out(int p) { digitalWrite(p, HIGH); }
static inline void led_on(int p)      { digitalWrite(p, LOW);  }
static inline void led_off(int p)     { digitalWrite(p, HIGH); }
static inline bool button_down(int p) { return digitalRead(p) == LOW; }

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

// Address resolution (mDNS queryService ~3 s + optional .local queryHost
// 2 s) and the healthz GET (up to 1.5 s) are all blocking. Running them
// inline in setup() or loop() freezes the module — no clock pulses, dead
// buttons — for several seconds, which on a flapping WiFi link reads as
// "unresponsive". Do them on a one-shot task instead so loop() keeps
// servicing the clock/buttons throughout.
static volatile bool recorder_resolve_in_flight = false;

static void recorder_resolve_task(void* arg) {
    resolve_recorder_address();
    check_recorder_healthz();
    recorder_resolve_in_flight = false;
    vTaskDelete(NULL);
}

// Kick off a background resolve+healthz. No-op if one is already running,
// so repeated WiFi reconnect edges don't pile up tasks. Falls back to an
// inline resolve only if the task can't be created.
static void start_recorder_resolution() {
    if (recorder_resolve_in_flight) return;
    recorder_resolve_in_flight = true;
    BaseType_t ok = xTaskCreate(recorder_resolve_task, "recresolve",
                                /*stack*/ 8192, nullptr,
                                /*prio*/ 4, nullptr);
    if (ok != pdPASS) {
        Serial.println("[Recorder] resolve task create failed; resolving inline");
        recorder_resolve_in_flight = false;
        resolve_recorder_address();
        check_recorder_healthz();
    }
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

// --- WiFi -----------------------------------------------------------------
// Bring-up ordering matters. Arduino initialises the Wi-Fi driver inside
// WiFi.mode(); any esp_wifi_* setter called before that returns
// ESP_ERR_WIFI_NOT_INIT and is silently discarded. Everything here runs
// after mode() for that reason.
//
// The C5 is dual-band, so a plain connect scans 2.4 GHz *and* the whole
// 5 GHz band — and under the IDF default country ("01", world-safe mode,
// 802.11d on) many of those channels are scanned passively, a full beacon
// interval each. That is where multi-second connects come from. Three
// things cut it down, each with a fallback:
//   1. a real country code    -> active scanning on legal channels
//   2. 5 GHz-only band mode   -> the 2.4 GHz half of the scan disappears
//   3. a cached BSSID+channel -> a known AP is joined with no scan at all
// Stale cache falls back to a scan; no 5 GHz AP falls back to dual band.

constexpr char     WIFI_COUNTRY[]               = "GB";
constexpr uint32_t WIFI_FAST_CONNECT_TIMEOUT_MS = 4000;   // cached-BSSID path
constexpr uint32_t WIFI_RETRY_INTERVAL_MS       = 15000;  // loop() backstop
constexpr bool     WIFI_PREFER_5G               = true;

static bool          wifi_band_5g_only  = false;
static unsigned long wifi_next_retry_ms = 0;

// 5 GHz channels 52–144 are DFS: the AP must vacate them if it detects
// radar, and stations scan them passively. Worth flagging in the log —
// it is a real source of mid-session disconnects.
static bool channel_is_dfs(uint8_t ch) { return ch >= 52 && ch <= 144; }

// --- Last-known-good AP cache (NVS) --------------------------------------
static bool wifi_cache_load(uint8_t bssid[6], uint8_t* channel) {
    Preferences p;
    if (!p.begin("clklinkrec", true)) return false;
    size_t n = p.getBytes("bssid", bssid, 6);
    uint8_t ch = p.getUChar("chan", 0);
    p.end();
    if (n != 6 || ch == 0) return false;
    *channel = ch;
    return true;
}

static void wifi_cache_store(const uint8_t bssid[6], uint8_t channel) {
    Preferences p;
    if (!p.begin("clklinkrec", false)) return;
    p.putBytes("bssid", bssid, 6);
    p.putUChar("chan", channel);
    p.end();
}

static void wifi_cache_clear() {
    Preferences p;
    if (!p.begin("clklinkrec", false)) return;
    p.remove("bssid");
    p.remove("chan");
    p.end();
}

// --- Radio configuration --------------------------------------------------
static void wifi_set_band_mode(bool five_g_only) {
#if defined(SOC_WIFI_SUPPORT_5G) && SOC_WIFI_SUPPORT_5G
    wifi_band_mode_t mode = five_g_only ? WIFI_BAND_MODE_5G_ONLY
                                        : WIFI_BAND_MODE_AUTO;
    esp_err_t err = esp_wifi_set_band_mode(mode);
    if (err == ESP_OK) {
        wifi_band_5g_only = five_g_only;
        Serial.printf("[WiFi] band mode: %s\n",
                      five_g_only ? "5 GHz only" : "2.4 + 5 GHz");
    } else {
        wifi_band_5g_only = false;
        Serial.printf("[WiFi] esp_wifi_set_band_mode failed: %s\n",
                      esp_err_to_name(err));
    }
#else
    wifi_band_5g_only = false;
    (void)five_g_only;
#endif
}

// Everything that must happen after esp_wifi_init() (i.e. after
// WiFi.mode()) but before association.
static void wifi_apply_radio_config() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // Power save off *before* associating, so the association and DHCP
    // exchange don't run through modem-sleep DTIM gaps either. Link's
    // multicast discovery needs this on permanently.
    WiFi.setSleep(false);
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        Serial.printf("[WiFi] esp_wifi_set_ps failed: %s\n", esp_err_to_name(err));
    }

    // Deprecated esp_wifi_set_country() only ever described the 2.4 GHz
    // channel list. The country code call sets the regulatory tables for
    // both bands. ieee80211d_enabled = false so the configured country is
    // always used rather than whatever the AP advertises.
    err = esp_wifi_set_country_code(WIFI_COUNTRY, false);
    if (err != ESP_OK) {
        Serial.printf("[WiFi] esp_wifi_set_country_code(%s) failed: %s\n",
                      WIFI_COUNTRY, esp_err_to_name(err));
    } else {
        char cc[3] = {};
        esp_wifi_get_country_code(cc);
        Serial.printf("[WiFi] country: %s (active scan on %s channels)\n",
                      cc, cc);
    }

    wifi_set_band_mode(WIFI_PREFER_5G);

    // Only valid once the driver is started.
    Serial.printf("[WiFi] MAC: %s\n", WiFi.macAddress().c_str());
}

// --- Association ----------------------------------------------------------
// One association attempt. Passing a bssid/channel skips the scan
// entirely; passing nullptr does a normal scan-and-join.
static bool wifi_attempt(const uint8_t* bssid, uint8_t channel,
                         uint32_t timeout_ms, const char* what) {
    unsigned long t0 = millis();
    // Clear any stored AP config first, otherwise a previously pinned
    // (and now stale) BSSID keeps being retried by the driver.
    WiFi.disconnect(false, true);
    delay(20);

    if (bssid) {
        WiFi.begin(WIFI_SSID, WIFI_PASS, (int32_t)channel, bssid);
    } else {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    }

    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
        delay(25);
    }
    bool ok = (WiFi.status() == WL_CONNECTED);
    Serial.printf("[WiFi] %s: %s in %lu ms\n", what,
                  ok ? "associated" : "no association",
                  millis() - t0);
    return ok;
}

// Log what we actually landed on and refresh the AP cache.
static void wifi_on_connected() {
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        Serial.printf("[WiFi] %s  ch %u (%s)  RSSI %d dBm  "
                      "BSSID %02x:%02x:%02x:%02x:%02x:%02x  IP %s\n",
                      WiFi.SSID().c_str(), ap.primary, band_of(ap.primary),
                      ap.rssi,
                      ap.bssid[0], ap.bssid[1], ap.bssid[2],
                      ap.bssid[3], ap.bssid[4], ap.bssid[5],
                      WiFi.localIP().toString().c_str());
        if (channel_is_dfs(ap.primary)) {
            Serial.println("[WiFi] note: this is a DFS channel — the AP may "
                           "channel-switch under radar, and scans of it are "
                           "passive. A non-DFS channel (36-48 / 149-165) is "
                           "more stable for clock sync.");
        }
        wifi_cache_store(ap.bssid, ap.primary);
    } else {
        Serial.printf("[WiFi] connected, IP %s\n",
                      WiFi.localIP().toString().c_str());
    }
}

// Boot-time connect. Blocking is fine here — nothing else is running yet.
static bool wifi_connect_blocking() {
    wifi_apply_radio_config();

    uint8_t bssid[6];
    uint8_t channel = 0;
    if (wifi_cache_load(bssid, &channel)) {
        Serial.printf("[WiFi] cached AP %02x:%02x:%02x:%02x:%02x:%02x ch %u — "
                      "joining without a scan\n",
                      bssid[0], bssid[1], bssid[2],
                      bssid[3], bssid[4], bssid[5], channel);
        if (wifi_attempt(bssid, channel, WIFI_FAST_CONNECT_TIMEOUT_MS,
                         "cached AP")) {
            wifi_on_connected();
            return true;
        }
        Serial.println("[WiFi] cached AP didn't answer — clearing cache");
        wifi_cache_clear();
    }

    if (wifi_attempt(nullptr, 0, WIFI_CONNECT_TIMEOUT_MS,
                     wifi_band_5g_only ? "scan (5 GHz)" : "scan (2.4 + 5 GHz)")) {
        wifi_on_connected();
        return true;
    }

    if (wifi_band_5g_only) {
        Serial.println("[WiFi] nothing on 5 GHz — retrying across both bands");
        wifi_set_band_mode(false);
        if (wifi_attempt(nullptr, 0, WIFI_CONNECT_TIMEOUT_MS,
                         "scan (2.4 + 5 GHz)")) {
            wifi_on_connected();
            return true;
        }
    }

    Serial.println("[WiFi] not connected — Link + Recorder will retry in "
                   "the background");
    return false;
}

// Backstop retry from loop(). Deliberately non-blocking: a Eurorack clock
// must not stall for seconds because the AP went away, so this only kicks
// off an attempt and lets the status edge in loop() pick up the result.
static void wifi_maintain(unsigned long now_ms) {
    if (WiFi.status() == WL_CONNECTED) {
        wifi_next_retry_ms = 0;
        return;
    }
    if (wifi_next_retry_ms != 0 && (long)(now_ms - wifi_next_retry_ms) < 0) {
        return;
    }
    wifi_next_retry_ms = now_ms + WIFI_RETRY_INTERVAL_MS;

    uint8_t bssid[6];
    uint8_t channel = 0;
    Serial.println("[WiFi] disconnected — re-attempting association");
    if (wifi_cache_load(bssid, &channel)) {
        WiFi.begin(WIFI_SSID, WIFI_PASS, (int32_t)channel, bssid);
    } else {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
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
    Serial.println("\n=== esp32_clklinkrec (Link sync + Recorder) ===");

    Serial.setDebugOutput(true);
    esp_log_level_set("*",         ESP_LOG_INFO);
#if WIFI_DEBUG_LOG
    esp_log_level_set("wifi",      ESP_LOG_INFO);
    esp_log_level_set("wifi_init", ESP_LOG_INFO);
#else
    esp_log_level_set("wifi",      ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
#endif
    esp_log_level_set("phy_init",  ESP_LOG_WARN);

    print_chip_info();

    pinMode(PIN_CLK_OUT,  OUTPUT); release_out(PIN_CLK_OUT);
    pinMode(PIN_RST_OUT,  OUTPUT); release_out(PIN_RST_OUT);
    pinMode(PIN_RUN_OUT,  OUTPUT); release_out(PIN_RUN_OUT);
    pinMode(PIN_RED_LED,  OUTPUT); led_off(PIN_RED_LED);
    pinMode(PIN_BLUE_LED, OUTPUT); led_off(PIN_BLUE_LED);
    pinMode(PIN_SW_LINK,    INPUT_PULLUP);
    pinMode(PIN_SW_CAPTURE, INPUT_PULLUP);

    Serial.printf("[WiFi] target SSID \"%s\"\n", WIFI_SSID);

    if (wifi_connect_blocking()) {
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
        start_recorder_resolution();
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
    link_tick(now_us);
    update_pulse_decay(now_us);
    update_blue_led(now_ms);
    update_red_led(now_ms);

    // WiFi reconnect detection: on the false→true edge, re-resolve the
    // recorder address. The Mac may have moved to a new IP, or we may
    // have come up before the recorder did.
    // Seeded from the real state on first entry: setup() has already done
    // the connect and the recorder lookup, and repeating the (blocking,
    // ~3 s) mDNS query here would just delay the module coming up.
    static bool wifi_was_connected = (WiFi.status() == WL_CONNECTED);
    bool wifi_is_connected = (WiFi.status() == WL_CONNECTED);
    if (wifi_is_connected && !wifi_was_connected) {
        Serial.println("[WiFi] reconnected — re-resolving recorder address");
        wifi_on_connected();
        start_recorder_resolution();
    }
    wifi_was_connected = wifi_is_connected;

    // Backstop for the driver's own auto-reconnect: if we're still down
    // after WIFI_RETRY_INTERVAL_MS, kick off another association attempt.
    wifi_maintain(now_ms);

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
