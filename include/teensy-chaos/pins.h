#pragma once
// Teensy 4.1 — teensy-chaos pin assignments (10HP layout).
// Avoid A0 (pin 14, hardware conflict) and A1 (pin 15, Audio Shield volume pot).
//
// Panel layout (6 rows × 3 columns):
//
//   OLED    OLED    BTN
//   OLED    OLED    CVout1
//   CVin1   POT1    CVout2
//   CVin2   POT2    Audio-L-out
//   CVin3   POT3    Audio-R-out
//   CVin4   POT4    Audio-in

// ---------- Analog inputs (pots) ----------
// A6/A7 (pins 20/21) avoided — conflict with Audio Shield I2S LRCLK/BCLK.
// Pots are wired so CCW = max ADC, CW = min ADC — invert to match user expectation.
#define readPot(pin) (1023 - analogRead(pin))
#ifndef PIN_POT1
#define PIN_POT1 A15  // Chaos / bifurcation  (pin 39)
#endif
#ifndef PIN_POT2
#define PIN_POT2 A12  // Rate / frequency     (pin 26)
#endif
#ifndef PIN_POT3
#define PIN_POT3 A14  // Character / secondary (pin 38)
#endif
#ifndef PIN_POT4
#define PIN_POT4 A13  // Depth / mix          (pin 27)
#endif

// ---------- CV inputs ----------
// All CV inputs are analog reads (0–3.3V after scaling).
// External circuit must scale/protect from Eurorack levels (±5V or 0–10V)
// to 0–3.3V. Gate/clock/reset detection uses a software threshold.
#ifndef PIN_CV1
#define PIN_CV1  A8   // Gate / clock / V-Oct
#endif
#ifndef PIN_CV2
#define PIN_CV2  A9   // Reset / trigger
#endif
#ifndef PIN_CV3
#define PIN_CV3  A10  // Chaos modulation
#endif
#ifndef PIN_CV4
#define PIN_CV4  A11  // Assignable
#endif

// Gate threshold (ADC counts, 10-bit): ~1.5V after scaling
#ifndef CV_GATE_THRESH
#define CV_GATE_THRESH 465
#endif

// ---------- CV outputs (MCP4822 dual 12-bit DAC, SPI) ----------
// Shares SPI bus (MOSI=11, SCK=13) with OLED.
#ifndef PIN_CS_DAC
#define PIN_CS_DAC  33  // MCP4822 chip select (active-low, bottom pad)
#endif
// Channel A = CVout1, Channel B = CVout2
// External op-amp scales 0–4.096V DAC output to Eurorack range.
// Both outputs can serve as smooth CV or gate/trigger.

// ---------- Digital ----------
#ifndef PIN_BTN
#define PIN_BTN  2    // Momentary pushbutton (active-low, internal pullup)
#endif

// ---------- OLED (SPI) ----------
// Pin 10 is also the Audio Shield SD card CS.  The SD card is disabled in
// setup() (driven HIGH) so the OLED can use pin 10 without bus contention.
// The Teensy 4.1 built-in SD card uses SDIO and is unaffected.
#ifndef OLED_CS
#define OLED_CS  10
#endif
#ifndef OLED_DC
#define OLED_DC   9
#endif
#ifndef OLED_RST
#define OLED_RST  6
#endif
#ifndef OLED_WIDTH
#define OLED_WIDTH  128
#endif
#ifndef OLED_HEIGHT
#define OLED_HEIGHT 64
#endif

// ---------- Audio Shield (SGTL5000) ----------
// I2S: BCLK=21, LRCLK=20, TX=7, RX=8, MCLK=23
// I2C control: SDA=18, SCL=19 (Wire), addr 0x14
// Audio in via line-in on the shield (no extra pins needed).
// No explicit pin defines — handled by Audio Library.
