// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 David Baghurst
//
// esp32-clklinkrec — hardware bring-up self-test.
//
// Not the final firmware; intended for first-flash verification of the
// wiring against the netlist in docs/ESP32_CLKLINKREC.md.
//
// What it does:
//   1. Boot: walks through each LED and output in turn so you can
//      multimeter / scope / LED-tester each jack while it's named on
//      the serial console.
//   2. Rolling test:
//        Clock Out  — pulses every 500 ms (50 ms wide)
//        Reset Out  — fires every 8 clocks (~once every 4 s)
//        Running    — toggles every 4 clocks (~0.25 Hz square wave)
//        Red LED    — follows the Capture button (held = lit)
//        Blue LED   — follows the Link button (held = lit)
//        Reset In   — rising edge at jack echoes one Reset Out pulse
//                     and logs to serial.

#include <Arduino.h>
#include "esp32-clklinkrec/pins.h"

// 74HCT14 inverts everything, so "active" = GPIO LOW.
static inline void assert_out(int p)   { digitalWrite(p, LOW);  }
static inline void release_out(int p)  { digitalWrite(p, HIGH); }
static inline void led_on(int p)       { digitalWrite(p, LOW);  }
static inline void led_off(int p)      { digitalWrite(p, HIGH); }
static inline bool button_down(int p)  { return digitalRead(p) == LOW; }
static inline bool reset_active(int p) { return digitalRead(p) == LOW; }

static void idle_all_outputs() {
    digitalWrite(PIN_CLK_OUT,  HIGH);
    digitalWrite(PIN_RST_OUT,  HIGH);
    digitalWrite(PIN_RUN_OUT,  HIGH);
    digitalWrite(PIN_RED_LED,  HIGH);
    digitalWrite(PIN_BLUE_LED, HIGH);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== esp32-clklinkrec hardware test ===");

    // Drive idle BEFORE configuring as outputs so the first thing the
    // 74HCT14 sees is HIGH (jack 0 V, LED off), avoiding a boot glitch.
    idle_all_outputs();
    pinMode(PIN_CLK_OUT,  OUTPUT);
    pinMode(PIN_RST_OUT,  OUTPUT);
    pinMode(PIN_RUN_OUT,  OUTPUT);
    pinMode(PIN_RED_LED,  OUTPUT);
    pinMode(PIN_BLUE_LED, OUTPUT);
    idle_all_outputs();

    pinMode(PIN_SW_LINK,    INPUT_PULLUP);
    pinMode(PIN_SW_CAPTURE, INPUT_PULLUP);
    pinMode(PIN_RESET_IN,   INPUT);   // R9 already provides the pull-down

    // Boot self-test: each LED and each jack in turn, named on serial.
    Serial.println("[Boot] Red LED");
    led_on(PIN_RED_LED);   delay(300); led_off(PIN_RED_LED);  delay(200);
    Serial.println("[Boot] Blue LED");
    led_on(PIN_BLUE_LED);  delay(300); led_off(PIN_BLUE_LED); delay(200);
    Serial.println("[Boot] Clock Out pulse");
    assert_out(PIN_CLK_OUT); delay(150); release_out(PIN_CLK_OUT); delay(200);
    Serial.println("[Boot] Reset Out pulse");
    assert_out(PIN_RST_OUT); delay(150); release_out(PIN_RST_OUT); delay(200);
    Serial.println("[Boot] Running Out pulse");
    assert_out(PIN_RUN_OUT); delay(150); release_out(PIN_RUN_OUT); delay(200);

    Serial.println("[Boot] Self-test complete. Entering rolling test.");
    Serial.println("       Red  LED <- Capture button");
    Serial.println("       Blue LED <- Link button");
    Serial.println("       Reset In jack -> Reset Out pulse");
}

void loop() {
    static uint8_t  clk_count     = 0;
    static uint32_t last_clk_ms   = 0;
    static bool     reset_in_prev = false;
    static bool     run_state     = false;

    uint32_t now = millis();

    if (now - last_clk_ms >= 500) {
        last_clk_ms = now;
        clk_count++;

        assert_out(PIN_CLK_OUT);
        delay(50);
        release_out(PIN_CLK_OUT);

        if (clk_count % 8 == 0) {
            assert_out(PIN_RST_OUT);
            delay(50);
            release_out(PIN_RST_OUT);
            Serial.printf("[Clk #%u] reset fired\n", clk_count);
        }

        if (clk_count % 4 == 0) {
            run_state = !run_state;
            if (run_state) assert_out(PIN_RUN_OUT);
            else           release_out(PIN_RUN_OUT);
        }
    }

    // LEDs echo buttons live (debounce caps + internal pull-ups handle the rest).
    if (button_down(PIN_SW_CAPTURE)) led_on(PIN_RED_LED);  else led_off(PIN_RED_LED);
    if (button_down(PIN_SW_LINK))    led_on(PIN_BLUE_LED); else led_off(PIN_BLUE_LED);

    // External Reset In: log BOTH edges so we can tell if the line is
    // returning to idle between triggers. Active edge also flashes both
    // LEDs and fires a Reset Out pulse.
    bool reset_in_now = reset_active(PIN_RESET_IN);
    if (reset_in_now && !reset_in_prev) {
        Serial.println("[Reset In] ACTIVE  -> firing Reset Out");
        led_on(PIN_RED_LED);
        led_on(PIN_BLUE_LED);
        assert_out(PIN_RST_OUT);
        delay(120);
        release_out(PIN_RST_OUT);
        led_off(PIN_RED_LED);
        led_off(PIN_BLUE_LED);
    } else if (!reset_in_now && reset_in_prev) {
        Serial.println("[Reset In] released (line back to idle)");
    }
    reset_in_prev = reset_in_now;

    delay(1);  // yield to FreeRTOS
}
