// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 David Baghurst
//
// esp32-clklinkrec — WiFi diagnostic.
//
// Goal: confirm the Xiao ESP32-C5 can associate to the studio WiFi on
// 5 GHz before we start porting the Ableton Link layer. Prints SSID,
// BSSID, channel, derived band, RSSI, PHY mode (incl. 802.11ax), and
// IPv4 lease once associated. Re-prints on any change.
//
// LED semantics (at-a-glance band check from across the room):
//   Blue blinking      — associating / not yet connected
//   Blue solid         — connected on 2.4 GHz
//   Blue + red solid   — connected on 5 GHz (target state)
//   Red solid alone    — disconnected after previously associating
//
// Buttons + clock generator still live so the hardware test is not
// regressed; the bring-up self-test is intentionally trimmed.
//
// Requires include/esp32-clklinkrec/secrets.h — copy from secrets.h.example.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include "esp32-clklinkrec/pins.h"
#include "esp32-clklinkrec/secrets.h"

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

static const char* phy_str(const wifi_ap_record_t& ap) {
    if (ap.phy_11ax) return "802.11ax (WiFi 6)";
    if (ap.phy_11n)  return "802.11n";
    if (ap.phy_11g)  return "802.11g";
    if (ap.phy_11b)  return "802.11b";
    return "?";
}

static void print_chip_info() {
    esp_chip_info_t info = {};
    esp_chip_info(&info);
    const char* model = "?";
    switch (info.model) {
        case CHIP_ESP32:    model = "ESP32";    break;
        case CHIP_ESP32S2:  model = "ESP32-S2"; break;
        case CHIP_ESP32S3:  model = "ESP32-S3"; break;
        case CHIP_ESP32C2:  model = "ESP32-C2"; break;
        case CHIP_ESP32C3:  model = "ESP32-C3"; break;
        case CHIP_ESP32C6:  model = "ESP32-C6"; break;
        case CHIP_ESP32H2:  model = "ESP32-H2"; break;
        case CHIP_ESP32C5:  model = "ESP32-C5"; break;
        default: break;
    }
    Serial.printf("[Chip] %s rev %u, %u core(s), CPU %u MHz\n",
                  model, (unsigned)info.revision, info.cores,
                  (unsigned)(ESP.getCpuFreqMHz()));
    Serial.printf("[Chip] features: %s%s%s%s%s\n",
                  (info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi(b/g/n) " : "",
                  (info.features & CHIP_FEATURE_BT)       ? "BT "         : "",
                  (info.features & CHIP_FEATURE_BLE)      ? "BLE "        : "",
                  (info.features & CHIP_FEATURE_EMB_FLASH)? "EmbFlash "   : "",
                  (info.features & CHIP_FEATURE_EMB_PSRAM)? "EmbPSRAM"    : "");
    Serial.printf("[Chip] IDF version: %s\n", esp_get_idf_version());
    Serial.printf("[Heap] internal free: %u, total: %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    Serial.printf("[Heap] PSRAM   free: %u, total: %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    int8_t tx_dbm_q = 0;
    if (esp_wifi_get_max_tx_power(&tx_dbm_q) == ESP_OK) {
        Serial.printf("[WiFi] max TX power: %d (0.25 dBm units = %.2f dBm)\n",
                      tx_dbm_q, tx_dbm_q * 0.25f);
    }
}

static const char* wl_status_str(wl_status_t s) {
    switch (s) {
        case WL_IDLE_STATUS:     return "IDLE";
        case WL_NO_SSID_AVAIL:   return "NO_SSID (router not seen)";
        case WL_SCAN_COMPLETED:  return "SCAN_DONE";
        case WL_CONNECTED:       return "CONNECTED";
        case WL_CONNECT_FAILED:  return "CONNECT_FAILED (wrong password?)";
        case WL_CONNECTION_LOST: return "LOST";
        case WL_DISCONNECTED:    return "DISCONNECTED (still trying)";
        default:                 return "?";
    }
}

static void print_association() {
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        Serial.println("[WiFi] esp_wifi_sta_get_ap_info failed");
        return;
    }
    char bssid[18];
    snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
             ap.bssid[0], ap.bssid[1], ap.bssid[2],
             ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    Serial.printf("[WiFi] associated\n");
    Serial.printf("       SSID    : %s\n", ap.ssid);
    Serial.printf("       BSSID   : %s\n", bssid);
    Serial.printf("       channel : %u  (band: %s)\n", ap.primary, band_of(ap.primary));
    Serial.printf("       PHY     : %s\n", phy_str(ap));
    Serial.printf("       RSSI    : %d dBm\n", ap.rssi);
    Serial.printf("       IP      : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("       gateway : %s\n", WiFi.gatewayIP().toString().c_str());
}

void setup() {
    Serial.begin(115200);
    // USB-CDC needs the host to enumerate before any println shows up.
    // Wait up to 3 s for that; if we run headless (no host attached) we
    // still proceed.
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) { delay(10); }
    delay(100);
    Serial.println("\n=== esp32-clklinkrec WiFi diagnostic ===");

    // Route ESP-IDF log output to the USB CDC console so PHY/WiFi driver
    // diagnostics are actually visible. Set verbose on the components
    // that matter for RX-side troubleshooting.
    Serial.setDebugOutput(true);
    esp_log_level_set("*",        ESP_LOG_INFO);
    esp_log_level_set("wifi",     ESP_LOG_VERBOSE);
    esp_log_level_set("wifi_init",ESP_LOG_VERBOSE);
    esp_log_level_set("phy",      ESP_LOG_VERBOSE);
    esp_log_level_set("phy_init", ESP_LOG_VERBOSE);
    esp_log_level_set("net80211", ESP_LOG_VERBOSE);
    esp_log_level_set("pp",       ESP_LOG_VERBOSE);

    print_chip_info();

    pinMode(PIN_CLK_OUT,  OUTPUT); release_out(PIN_CLK_OUT);
    pinMode(PIN_RST_OUT,  OUTPUT); release_out(PIN_RST_OUT);
    pinMode(PIN_RUN_OUT,  OUTPUT); release_out(PIN_RUN_OUT);
    pinMode(PIN_RED_LED,  OUTPUT); led_off(PIN_RED_LED);
    pinMode(PIN_BLUE_LED, OUTPUT); led_off(PIN_BLUE_LED);
    pinMode(PIN_SW_LINK,    INPUT_PULLUP);
    pinMode(PIN_SW_CAPTURE, INPUT_PULLUP);
    pinMode(PIN_RESET_IN,   INPUT);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // Force regulatory domain to GB so 2.4 GHz ch 12/13 and the UK 5 GHz
    // bands are actually scanned. Default "01" worldwide code restricts
    // 2.4 to ch 1-11 and blocks 5 GHz outright -- on a dual-band chip
    // like the C5 that means a scan can come back empty even though the
    // radio and antenna are fine.
    {
        wifi_country_t gb = {};
        memcpy(gb.cc, "GB", 2);
        gb.cc[2]       = 0;
        gb.schan       = 1;
        gb.nchan       = 13;            // UK permits ch 1..13 on 2.4 GHz
        gb.policy      = WIFI_COUNTRY_POLICY_MANUAL;
        esp_err_t cerr = esp_wifi_set_country(&gb);
        Serial.printf("[WiFi] esp_wifi_set_country(GB, manual) -> %s\n",
                      esp_err_to_name(cerr));
    }
    wifi_country_t cur = {};
    if (esp_wifi_get_country(&cur) == ESP_OK) {
        Serial.printf("[WiFi] active country: %c%c  ch %u..%u  policy=%s\n",
                      cur.cc[0], cur.cc[1], cur.schan, cur.schan + cur.nchan - 1,
                      cur.policy == WIFI_COUNTRY_POLICY_MANUAL ? "MANUAL" : "AUTO");
    }

    Serial.printf("[WiFi] MAC: %s\n", WiFi.macAddress().c_str());

    // Two-pass scan: an active scan can miss APs on dwell-tight windows,
    // especially after a cold radio init. Try active first, then a passive
    // scan with longer dwell if the first comes back empty.
    Serial.println("[WiFi] scanning (active) for visible APs...");
    int n = WiFi.scanNetworks(/*async*/false, /*show_hidden*/true,
                              /*passive*/false, /*max_ms_per_chan*/300, /*chan*/0);
    if (n <= 0) {
        Serial.printf("[WiFi] active scan returned %d, retrying passive...\n", n);
        WiFi.scanDelete();
        n = WiFi.scanNetworks(/*async*/false, /*show_hidden*/true,
                              /*passive*/true, /*max_ms_per_chan*/600, /*chan*/0);
    }
    if (n <= 0) {
        Serial.printf("[WiFi] scan returned %d networks — radio may be silent or antenna unconnected\n", n);
    } else {
        Serial.printf("[WiFi] %d AP(s) visible:\n", n);
        for (int i = 0; i < n; i++) {
            uint8_t ch = WiFi.channel(i);
            Serial.printf("       %2d) %-32s ch %3u (%s)  RSSI %4d dBm  %s\n",
                          i,
                          WiFi.SSID(i).length() ? WiFi.SSID(i).c_str() : "<hidden>",
                          ch, band_of(ch),
                          WiFi.RSSI(i),
                          WiFi.BSSIDstr(i).c_str());
        }
        bool target_seen = false;
        for (int i = 0; i < n; i++) {
            if (WiFi.SSID(i) == WIFI_SSID) { target_seen = true; break; }
        }
        Serial.printf("[WiFi] target SSID \"%s\" %s in scan\n",
                      WIFI_SSID, target_seen ? "PRESENT" : "NOT PRESENT");
    }
    WiFi.scanDelete();

    Serial.printf("[WiFi] connecting to \"%s\"...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void loop() {
    static bool     was_connected = false;
    static uint32_t last_blink_ms = 0;
    static uint32_t last_clk_ms   = 0;
    static bool     blue_blink    = false;
    static int      last_channel  = -1;
    static int      last_rssi     = 0;
    static uint32_t last_telem_ms = 0;

    uint32_t now = millis();
    bool connected = (WiFi.status() == WL_CONNECTED);

    // -- LED state ----------------------------------------------------------
    if (connected) {
        wifi_ap_record_t ap = {};
        bool have_ap = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
        led_on(PIN_BLUE_LED);
        if (have_ap && ap.primary >= 32) led_on(PIN_RED_LED);
        else                              led_off(PIN_RED_LED);
    } else if (was_connected) {
        led_off(PIN_BLUE_LED);
        led_on(PIN_RED_LED);
    } else {
        if (now - last_blink_ms >= 250) {
            last_blink_ms = now;
            blue_blink = !blue_blink;
            if (blue_blink) led_on(PIN_BLUE_LED); else led_off(PIN_BLUE_LED);
        }
        led_off(PIN_RED_LED);
    }

    // -- Print on every connect / disconnect transition --------------------
    if (connected && !was_connected) {
        print_association();
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            last_channel = ap.primary;
            last_rssi    = ap.rssi;
        }
    } else if (!connected && was_connected) {
        Serial.printf("[WiFi] disconnected: %s — auto-reconnect armed\n",
                      wl_status_str(WiFi.status()));
    }
    was_connected = connected;

    // -- Heartbeat while not connected so we can see WHY -------------------
    static uint32_t last_heartbeat_ms = 0;
    if (!connected && now - last_heartbeat_ms >= 2000) {
        last_heartbeat_ms = now;
        Serial.printf("[WiFi] still trying: %s\n",
                      wl_status_str(WiFi.status()));
    }

    // -- Periodic telemetry while connected: only print on change ----------
    if (connected && now - last_telem_ms >= 1000) {
        last_telem_ms = now;
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            int rssi_delta = abs((int)ap.rssi - last_rssi);
            if (ap.primary != last_channel || rssi_delta >= 5) {
                Serial.printf("[WiFi] ch %u (%s), RSSI %d dBm\n",
                              ap.primary, band_of(ap.primary), ap.rssi);
                last_channel = ap.primary;
                last_rssi    = ap.rssi;
            }
        }
    }

    // -- Keep the clock generator alive so the hardware test isn't lost --
    if (now - last_clk_ms >= 500) {
        last_clk_ms = now;
        assert_out(PIN_CLK_OUT);
        delay(50);
        release_out(PIN_CLK_OUT);
    }

    delay(10);
}
