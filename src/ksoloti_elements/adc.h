// adc.h — ADC driver for Ksoloti Big Genes (STM32F429)
//
// ADC1 DMA continuous scan: pots 1-4 (PA0-3, summed with CV P1-P4),
//   CV A-D (PA6-7, PB0-1), CV X-Y (PC1, PC4)
// ADC3 polled: pots 5-8 (PF6-9)
// Button GPIO: S3 (PB12), S4 (PB13)

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Channel indices — order matches ADC1 DMA scan sequence
enum {
    ADC_POT1 = 0,    // PA0  ADC1_IN0   resonator_geometry
    ADC_POT2 = 1,    // PA1  ADC1_IN1   resonator_brightness
    ADC_POT3 = 2,    // PA2  ADC1_IN2   resonator_damping
    ADC_POT4 = 3,    // PA3  ADC1_IN3   resonator_position
    ADC_CV_A = 4,    // PA6  ADC1_IN6   exciter_blow_meta
    ADC_CV_B = 5,    // PA7  ADC1_IN7   exciter_strike_meta
    ADC_CV_C = 6,    // PB0  ADC1_IN8   exciter_envelope_shape
    ADC_CV_D = 7,    // PB1  ADC1_IN9   gate + strength
    ADC_CV_X = 8,    // PC1  ADC1_IN11  V/Oct note
    ADC_CV_Y = 9,    // PC4  ADC1_IN14  FM modulation
    ADC1_NUM_CH = 10,
    ADC_POT5 = 10,   // PF6  ADC3_IN4   exciter_bow_level
    ADC_POT6 = 11,   // PF7  ADC3_IN5   exciter_blow_level
    ADC_POT7 = 12,   // PF8  ADC3_IN6   exciter_strike_level
    ADC_POT8 = 13,   // PF9  ADC3_IN7   space
    ADC_NUM_CH = 14
};

#ifdef __cplusplus
extern "C" {
#endif

// Initialise ADC1 (DMA), ADC3 (polled), and button GPIO.
void adc_init(void);

// Poll ADC3 channels (pots 5-8). Call from main loop.
void adc_poll(void);

// Raw 12-bit ADC value (0-4095).
uint16_t adc_raw(int ch);

// Button S3 (PB12): returns true when pressed.
bool button_s3(void);

// Button S4 (PB13): returns true when pressed.
bool button_s4(void);

// ENC1 push (PB5): returns true when pressed.
bool button_enc1(void);

// ENC2 push (PA10): returns true when pressed.
bool button_enc2(void);

// Encoder rotation — enc_poll() must run at a steady rate fast enough to catch every AB
// transition; it is called from the audio ISR (2 kHz), not the main loop.
// Returns accumulated clicks since last call (positive = CW, negative = CCW).
void enc_init(void);
void enc_poll(void);
int enc1_read(void);   // ENC1 (PG11/PG12)
int enc2_read(void);   // ENC2 (PG10/PA15)

// Momentary presses, edge-detected and debounced at the rate btn_poll() is called.
// Like the encoders these are polled from the audio ISR: an edge is a moment, and the
// main loop is neither fast nor evenly spaced enough to be sure of catching one - a
// single slow pass swallows the press entirely. The ISR latches it instead, and the
// loop collects it whenever it gets there.
// btn_*_pressed() returns true once per press, clearing the latch.
void btn_poll(void);
bool btn_s1_pressed(void);   // ENC1 push - cycle resonator model
bool btn_s2_pressed(void);   // ENC2 push - cycle selected CV
bool btn_s4_pressed(void);   // S4       - cycle P5-P7 state

#ifdef __cplusplus
}
#endif
