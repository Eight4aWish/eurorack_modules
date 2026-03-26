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
#ifndef PIN_POT1
#define PIN_POT1 A2   // Chaos / bifurcation
#endif
#ifndef PIN_POT2
#define PIN_POT2 A3   // Rate / frequency
#endif
#ifndef PIN_POT3
#define PIN_POT3 A6   // Character / secondary
#endif
#ifndef PIN_POT4
#define PIN_POT4 A7   // Depth / mix
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
