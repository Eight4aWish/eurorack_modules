#pragma once
// Teensy 4.1 analog pot pin mapping for Diagnostics.
// Provided wiring:
//   Pot 4: A0 = 14
//   Pot 3: A1 = 15
//   Pot 2: A2 = 16
//   Pot 1: A3 = 17
// Define macros for use in firmware; override via build flags if needed.
#ifndef PIN_POT1
#define PIN_POT1 A3   // Pot 1 → A3 (numeric 17)
#endif
#ifndef PIN_POT2
#define PIN_POT2 A2   // Pot 2 → A2 (numeric 16)
#endif
#ifndef PIN_POT3
#define PIN_POT3 A1   // Pot 3 → A1 (numeric 15)
#endif
#ifndef PIN_POT4
#define PIN_POT4 A0   // Pot 4 → A0 (numeric 14)
#endif

// Digital pins (main board + expander)
#ifndef PIN_BTN
#define PIN_BTN 2
#endif
#ifndef PIN_CS_DAC1
#define PIN_CS_DAC1 33
#endif
#ifndef PIN_CS_DAC2
#define PIN_CS_DAC2 34
#endif
#ifndef PIN_CLOCK
#define PIN_CLOCK 39
#endif
#ifndef PIN_RESET
#define PIN_RESET 37
#endif
#ifndef PIN_GATE1
#define PIN_GATE1 40
#endif
#ifndef PIN_GATE2
#define PIN_GATE2 38
#endif
#ifndef PIN_595_LATCH
#define PIN_595_LATCH 32
#endif

// Expander DAC channel mapping (A/B) for Mod/Pitch 3 & 4
// Index meaning: 0 -> CH_A, 1 -> CH_B. Adjust these if hardware wiring swaps channels.
#ifndef EXP_MOD3_CH_IDX
#define EXP_MOD3_CH_IDX 1  // swapped: Mod3 uses CH_B
#endif
#ifndef EXP_MOD4_CH_IDX
#define EXP_MOD4_CH_IDX 0  // swapped: Mod4 uses CH_A
#endif
#ifndef EXP_PITCH3_CH_IDX
#define EXP_PITCH3_CH_IDX 1 // swapped: Pitch3 uses CH_B
#endif
#ifndef EXP_PITCH4_CH_IDX
#define EXP_PITCH4_CH_IDX 0 // swapped: Pitch4 uses CH_A
#endif
