#ifndef PICO2W_PINS_H
#define PICO2W_PINS_H

// Pins / Addresses (extracted from src/pico2w/main.cpp)
#define PIN_BTN 6
#define PIN_POT1 28   // ADC0
#define PIN_POT2 27   // ADC1
#define PIN_POT3 26   // ADC2

// I2C1 — ADS1115 + MCP4728 (CV ADC/DAC)
#define I2C1_SDA 18
#define I2C1_SCL 19

// I2C0 — SSD1306 OLED (separate bus to avoid contention)
#define I2C0_SDA 20
#define I2C0_SCL 21

#define I2C_ADDR_SSD1306 0x3C //0x3C
#define I2C_ADDR_ADS     0x48   // change if you wired A0 differently
#define I2C_ADDR_MCP     0x60

// Display dimensions
#define OLED_W 128
#define OLED_H 64

// UI layout
// Height in pixels reserved for the top color/title band. Adjust this to move
// patch content below the yellow zone.
// Increase this value if patch text still overlaps the coloured top band.
#define UI_TOP_MARGIN 16

// ---- Analog / Digital Channel Assignments ----
// Abstraction layer: refer to CV inputs as AD0 / AD1 and outputs as DA0..DA3.
// Underlying hardware channels start at index 0. If you find jacks are
// physically swapped, change ONLY these definitions.

// ADS1115 single-ended channel indices for the two CV input jacks.
#define AD0_CH 1   // Left CV input jack (physical channel 1) AFTER SWAP
#define AD1_CH 0   // Right CV input jack (physical channel 0) AFTER SWAP

// External clock detect uses AD0 by default (adjust if wiring differs).
#define AD_EXT_CLOCK_CH AD0_CH

// MCP4728 physical DAC channel indices are 0..3.
// Physical CV outputs (CV0..CV3) mapped directly to underlying DAC channels.
// Adjust these constants to match wiring.
// Current mapping (per user): CV0=1, CV1=3, CV2=2, CV3=0
#define CV0_DA_CH 1
#define CV1_DA_CH 3
#define CV2_DA_CH 2
#define CV3_DA_CH 0

// ---- Panel-name aliases (preferred in patch code and on screen) ----
// The front panel is a 2x5 grid; jacks are labelled IN1 IN2 / OUT1 OUT2 /
// OUT3 OUT4 in reading order. Calibration indices follow the same order:
// voltsToDac(0..3) = OUT1..OUT4.
//
//   Pot1  BTN     <- navigation row (menu: Pot1 scrolls, BTN selects)
//   Pot2  Pot3    <- parameter row (Pot2 = column A, Pot3 = column B)
//   IN1   IN2
//   OUT1  OUT2    <- pitch row (two-voice patches)
//   OUT3  OUT4    <- gate row
//
// Column convention: voice/channel A = Pot2 + IN1 + OUT1/OUT3 (left column),
// voice/channel B = Pot3 + IN2 + OUT2/OUT4 (right column).
#define IN1_AD_CH  AD0_CH
#define IN2_AD_CH  AD1_CH
#define OUT1_DA_CH CV0_DA_CH
#define OUT2_DA_CH CV1_DA_CH
#define OUT3_DA_CH CV2_DA_CH
#define OUT4_DA_CH CV3_DA_CH

// (Legacy ADS_CH_CVx / MCP_CH_DACx names removed; use ADx_CH / DAx_CH exclusively.)

#endif // PICO2W_PINS_H
