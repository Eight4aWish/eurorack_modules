// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Application code that drives Mutable Instruments Elements
// (MIT, Copyright 2014 Émilie Gillet) on the Ksoloti Big Genes board.
// The Elements DSP sources under third_party/eurorack/ retain their
// original MIT headers.
//
// main.cc — Ksoloti Big Genes: Mutable Instruments Elements
//
// Elements DSP running on STM32F429 + ADAU1961 codec at 32 kHz.
// Single-page OLED UI with two pot modes (levels / timbres).
//
// Controls:
//   P1-4 (+CV P1-P4): resonator geometry / brightness / damping / position
//   P5-P7:            one exciter per column - bow / blow / strike - through three
//                     states cycled by S4:
//                       levels   bow level    blow level   strike level
//                       meta     bow timbre   flow         mallet
//                       timbres  bow timbre   blow timbre  strike timbre
//                     level -> meta -> timbre is the order Elements uses across its own
//                     panel, so the states read the same way round
//                     bow has no meta parameter, so P5 carries its timbre into the third
//                     state rather than going dead
//   P8:               space (never pages)
//   E1 rotate:        contour (envelope shape) (never pages)
//   S1 (ENC1 push):   cycle resonator model (modal / string / chords)
//   S2 (ENC2 push):   select CV for assignment (A / B / C)
//   E2 rotate:        cycle CV target parameter
//   S3:               play (manual gate, strength 0.7)
//   S4:               cycle P5-P7 state (levels / meta / timbres)
//   CV A-C:           assignable modulation (default A=Flow, B=Mallet, C=none)
//   CV D:             gate + strength (velocity from voltage)
//   CV X:             V/Oct pitch
//   CV Y:             FM, +/-49.5 semitones clamped at +/-60 (Elements' scaling)
//   LED1 green (PG6): gate active
//   LED2 red (PC6):   CPU overload
//   LED4 (PB6/PB7):   resonator model (green=modal, red=string, both=chords)
//   Gate1 (PD3):      gate echo output
//
// Audio: In L = blow exciter, In R = strike exciter
//        Out L = main, Out R = aux (reverb)

#include "adc.h"
#include "codec.h"
#include "oled.h"
#include "stm32f4xx_hal.h"
#include "elements/dsp/part.h"
#include <cstdio>
#include <cmath>
#include <cstring>

extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
}

// --- Static allocations ---

static elements::Part part;

// Reverb buffer: 64 KB of uint16_t, placed in Core Coupled Memory.
//
// CCM is core-only — DMA cannot reach it — and it sits on its own bus, so reverb traffic
// no longer contends with the SAI audio stream and the free-running 10-channel ADC scan
// for SRAM. Everything else (part, the DMA buffers) stays in SRAM.
//
// The linker script warns that startup does not copy init values into .ccmram, so this
// is cleared by hand in main() before Part::Init() sees it.
// Declared nobits so it occupies no space in the .bin. The linker script gives .ccmram a
// load address, so a plain section attribute puts 64 KB of zeros into the image for a
// buffer nothing ever copies - the GCC startup handles only .data and .bss, and this is
// cleared by hand in main(). The trailing @ opens an assembler comment, swallowing the
// flags GCC would otherwise append.
static uint16_t reverb_buffer[32768]
    __attribute__((section(".ccmram,\"aw\",%nobits @")));

// Per-block float buffers (16 mono samples each = kMaxBlockSize)
static float blow_in[BUFSIZE];
static float strike_in[BUFSIZE];
static float main_out[BUFSIZE];
static float aux_out[BUFSIZE];

static elements::PerformanceState perf;
static volatile bool dsp_ready = false;
// Sticky. A block overrun lasts 500 us and blocks arrive at 2 kHz, but the main loop -
// which drives the OLED over I2C - iterates far slower, so an unlatched flag is almost
// never sampled in the act. The ISR sets this; the main loop clears it and holds the LED
// on long enough to see.
static float cv(int ch);            // defined with the ADC helpers below
static float fm_lp = 0.0f;          // smoothed FM, updated in the audio callback

static volatile bool cpu_overload = false;

// One audio block must be computed in BUFSIZE / SAMPLERATE seconds. At 32 kHz that is
// 500 us, or 84000 cycles at 168 MHz — the same per-block budget Elements itself has.
static const uint32_t kCoreClockHz = 168000000u;
static const uint32_t kCyclesPerBlock = (uint32_t)((uint64_t)kCoreClockHz * BUFSIZE / SAMPLERATE);
static const uint32_t kCpuOverloadCycles = kCyclesPerBlock * 95u / 100u; // warn at 95%

// --- Audio callback ---

extern "C" void computebufI(int32_t *inp, int32_t *outp)
{
    if (!dsp_ready) {
        for (int i = 0; i < DOUBLE_BUFSIZE; i++) outp[i] = 0;
        return;
    }

    uint32_t t0 = DWT->CYCCNT;

    const float kInScale  = 1.0f / 2147483648.0f;
    const float kOutScale = 2147483648.0f;

    for (int i = 0; i < BUFSIZE; i++) {
        blow_in[i]   = static_cast<float>(inp[i * 2])     * kInScale;
        strike_in[i] = static_cast<float>(inp[i * 2 + 1]) * kInScale;
    }

    // Pitch and FM at block rate, matching Elements (cv_scaler.Read once per block) and
    // Joy (ProcessControls inside AudioCallback). The main loop averages a few hundred Hz
    // but stalls ~25 ms whenever the OLED redraws, so sampling there made fast modulation
    // steppy and V/oct lag. Both CVs are in ADC1's DMA buffer: an array read and a few
    // floats against an 84,000-cycle budget.
    // V/oct scale, in semitones across the full cv() swing. The original port used 30,
    // which is 6 semitones per volt - half of 1V/oct - so an octave of CV came out as a
    // tritone. Wrong since March, and easy to miss unless you play more than an octave.
    //
    // 63.9 rather than a round 60 because the input does not swing exactly +/-5V: measured
    // on a trimmed board, 60 gave 11.27 semitones per volt, so the factor is 60 * 12/11.27.
    // Hardcoded deliberately - every Big Genes has its CV input trimmed on the board, so
    // the ADC reading for a given voltage is the same across modules.
    //
    //   0V -> C -4 cents,  1V -> +12.00 semitones,  2V -> +24.01
    static const float kVoltPerOctScale = 63.9f;
    perf.note = 60.0f + cv(ADC_CV_X) * kVoltPerOctScale;
    const float fm = cv(ADC_CV_Y) * 49.5f;
    fm_lp += 0.05f * (fm - fm_lp);      // ~3 ms at 2 kHz, in the spirit of Elements' filter
    perf.modulation = fm_lp < -60.0f ? -60.0f : (fm_lp > 60.0f ? 60.0f : fm_lp);

    part.Process(perf, blow_in, strike_in, main_out, aux_out, BUFSIZE);

    for (int i = 0; i < BUFSIZE; i++) {
        outp[i * 2]     = static_cast<int32_t>(main_out[i] * kOutScale);
        outp[i * 2 + 1] = static_cast<int32_t>(aux_out[i]  * kOutScale);
    }

    // Derived from SAMPLERATE and BUFSIZE rather than hard-coded, so it cannot drift away
    // from the rate the codec actually runs at.
    // Quadrature decoding has to keep up with the knob, not with the UI. The main loop
    // runs at tens of Hz because it pushes the OLED framebuffer over I2C, and at that rate
    // most AB transitions are missed - the decode table maps a skipped step to 0, so the
    // encoder feels like it resists you, and where the aliasing looks like a valid step
    // the other way it counts backwards. Polling here gives a steady 2 kHz for four GPIO
    // reads, which is nothing against the block budget.
    enc_poll();
    // Buttons for the same reason: a press is an edge, and the main loop stalls
    // whenever the OLED is slow. Sampled here, no press can fall down that gap.
    btn_poll();

    if ((DWT->CYCCNT - t0) > kCpuOverloadCycles) cpu_overload = true;
}

// --- ADC helpers ---

// Pot: inverted by op-amp on Big Genes, scaled to 0.0 .. 1.0
static inline float pot(int ch)
{
    float v = 1.0f - adc_raw(ch) * (1.0f / 4095.0f);
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// CV: bipolar, inverted by op-amp, scaled to -1.0 .. +1.0
// With no cable patched, input sits at ~0 V -> ADC mid-range -> returns ~0.0
static float cv(int ch)
{
    float v = 1.0f - adc_raw(ch) * (1.0f / 2047.5f);
    if (v < -1.0f) return -1.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// --- CV assignment system ---

enum CvTarget {
    CVT_FLOW,       // blow_meta
    CVT_MALLET,     // strike_meta
    CVT_CONTOUR,    // envelope_shape
    CVT_BOW_TIM,    // bow_timbre
    CVT_BLOW_TIM,   // blow_timbre
    CVT_STRIKE_TIM, // strike_timbre
    CVT_SIG,        // exciter_signature
    CVT_MOD_FRQ,    // resonator_modulation_frequency
    CVT_MOD_OFS,    // resonator_modulation_offset
    CVT_RV_DIFF,    // reverb_diffusion
    CVT_RV_LP,      // reverb_lp
    CVT_NONE,       // unassigned
    NUM_CV_TARGETS
};

static const char* cv_target_abbr[NUM_CV_TARGETS] = {
    "Flw", "Mal", "Cnt", "BwT", "BlT", "StT",
    "Sig", "MFr", "MOf", "RvD", "RvL", "---"
};

// Spelled out for the bottom line, which shows one thing at a time and has the room
static const char* cv_target_full[NUM_CV_TARGETS] = {
    "Flow", "Mallet", "Contour", "BowTmb", "BlowTmb", "StrikeTmb",
    "Signature", "ModFreq", "ModOffs", "RevDiff", "RevLP", "unassigned"
};

// --- Display helpers ---

static const char* model_names[] = { "Mod", "Str", "Chd" };

// Per-model P1 labels: short for top row, long for bottom status.
// Geometry pot reinterpreted per resonator model (voice.cc Process()):
//   Modal:  physical body geometry
//   String: string dispersion (stiffness)
//   Chords: stepped selector across 11 chord shapes
static const char* p1_label_short[3] = { "Geom", "Disp", "Chrd" };  // 4 chars: row 1 is four columns of five
static const char* p1_label_long [3] = { "Geom", "Dispr", "Chord" };

// Chord shapes, indexed by (int)(geometry * 10 + 0.5).
// Matches the intervals in chords[][] at voice.cc:76.
static const char* chord_name[11] = {
    "Oct", "m7",  "m",   "m9", "m11",
    "5",   "M11", "M9",  "M",  "M7",  "su4"
};

// Pitch note name from MIDI note number
static void fmt_note(char* buf, float note)
{
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int n = (int)(note + 0.5f);
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    int oct = (n / 12) - 1;
    int idx = n % 12;
    if (names[idx][1] == '#')
        snprintf(buf, 5, "%s%d", names[idx], oct);
    else
        snprintf(buf, 5, "%c%d", names[idx][0], oct);
}

// --- Entry point ---

int main(void)
{
    HAL_Init();

    // ADC + buttons (populates initial readings before audio starts)
    adc_init();

    // OLED display (I2C1, PB8/PB9)
    oled_init();

    // Initialize Elements DSP BEFORE codec — DMA ISR calls Part::Process()
    // as soon as codec_init() enables DMA.
    memset(reverb_buffer, 0, sizeof(reverb_buffer));   // .ccmram is not zeroed for us
    part.Init(reverb_buffer);

    // Patch pointer — lives for the duration of the program
    elements::Patch* p = part.mutable_patch();

    // --- Parameter base values ---
    // A pot writes its value only while its state is selected; the others hold.
    float val_bow_level    = 0.0f;   // P5 levels
    float val_blow_level   = 0.0f;   // P6 levels
    float val_strike_level = 0.0f;   // P7 levels
    float val_bow_tim      = 0.5f;   // P5 timbres AND meta - bow has no meta parameter
    float val_blow_tim     = 0.5f;   // P6 timbres
    float val_strike_tim   = 0.5f;   // P7 timbres
    float val_flow         = 0.5f;   // P6 meta  (blow_meta)
    float val_mallet       = 0.5f;   // P7 meta  (strike_meta)
    float val_space        = 0.0f;   // P8, every state
    float val_contour      = 0.5f;   // E1, every state

    // Which base value P5, P6 or P7 drives in a given state. One exciter per column
    // throughout, so a pot never changes which voice it belongs to - only which of that
    // voice's parameters it holds. Returning the address lets S4 compare targets and skip
    // pickup where the parameter has not actually changed.
    auto pot_target = [&](int i, int mode) -> float* {
        switch (i) {
            case 0: return mode == 0 ? &val_bow_level : &val_bow_tim;
            case 1: return mode == 0 ? &val_blow_level
                         : mode == 1 ? &val_flow : &val_blow_tim;
            default: return mode == 0 ? &val_strike_level
                          : mode == 1 ? &val_mallet : &val_strike_tim;
        }
    };

    // Hidden params (CV-modulatable only, sensible defaults)
    float val_sig      = 0.5f;
    float val_mod_frq  = 0.5f;
    float val_mod_ofs  = 0.5f;
    float val_rv_diff  = 0.7f;
    float val_rv_lp    = 0.8f;

    // Set initial patch values
    p->exciter_bow_timbre             = val_bow_tim;
    p->exciter_blow_timbre            = val_blow_tim;
    p->exciter_strike_timbre          = val_strike_tim;
    p->exciter_envelope_shape         = val_contour;
    p->exciter_signature              = val_sig;
    p->resonator_modulation_frequency = val_mod_frq;
    p->resonator_modulation_offset    = val_mod_ofs;
    p->reverb_diffusion               = val_rv_diff;
    p->reverb_lp                      = val_rv_lp;

    // Performance defaults
    perf.note       = 60.0f;
    perf.modulation = 0.0f;
    perf.strength   = 0.0f;
    perf.gate       = false;

    // Now start the codec (DMA ISR will begin calling computebufI)
    codec_init();
    dsp_ready = true;

    // --- GPIO outputs: LEDs and Gate1 ---
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    GPIO_InitTypeDef g = {};
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;

    g.Pin = GPIO_PIN_6;              // LED1 green (PG6) — gate
    HAL_GPIO_Init(GPIOG, &g);

    g.Pin = GPIO_PIN_6;              // LED2 red (PC6) — CPU overload
    HAL_GPIO_Init(GPIOC, &g);

    g.Pin = GPIO_PIN_6 | GPIO_PIN_7; // LED4 dual (PB6 green, PB7 red) — model
    HAL_GPIO_Init(GPIOB, &g);

    g.Pin = GPIO_PIN_3;              // Gate1 output (PD3) — gate echo
    HAL_GPIO_Init(GPIOD, &g);

    // Enable DWT cycle counter for CPU load measurement
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // --- UI state ---
    int resonator_model = 0;  // 0=modal, 1=string, 2=chords
    int pot_mode = 0;         // 0=levels, 1=timbres

    // CV assignment: which target each CV modulates
    int cv_assign[3] = { CVT_FLOW, CVT_MALLET, CVT_NONE };
    int cv_sel = 0;           // currently selected CV for editing (0=A, 1=B, 2=C)

    // Pot pickup for P5-P7 when S4 changes what they drive
    bool pot_picked[3] = {true, true, true};
    float pickup_target[3] = {0};
    #define PICKUP_THRESH 0.03f

    // Activity tracking for bottom-line display
    // Indices: 0-3 = P1-P4, 4-7 = P5-P8, 8 = E1
    uint32_t act_ts[9] = {0};
    uint32_t cv_act_ts = 0;   // last time a CV slot was selected or reassigned
    float act_prev[9] = {0};
    #define ACT_THRESH 0.015f
    #define ACT_DURATION 2000

    // Control names for bottom-line display (6 chars max, %-6s padded)
    // [state][control] — 0-3 = P1-4, 4-6 = P5-7, 7 = P8, 8 = E1
    static const char* ctrl_name[3][9] = {
        { "Geom", "Bright", "Damp", "Posn", "BowLvl", "BlwLvl", "StkLvl", "Space", "Contr" },
        { "Geom", "Bright", "Damp", "Posn", "BowTmb", "Flow",   "Mallet", "Space", "Contr" },
        { "Geom", "Bright", "Damp", "Posn", "BowTmb", "BlwTmb", "StkTmb", "Space", "Contr" },
    };
    // Row 2 of the screen, per state — four columns of five, aligned under the pots
    static const char* pot_row[3] = {
        "BowL BloL StkL Spce",
        "BowT Flow Mall Spce",
        "BowT BloT StkT Spce",
    };

    // Read initial pot positions and set mode 1 values
    adc_poll();
    val_bow_level    = pot(ADC_POT5);
    val_blow_level   = pot(ADC_POT6);
    val_strike_level = pot(ADC_POT7);
    val_space        = pot(ADC_POT8) * 2.0f;

    // Seed activity prev values
    act_prev[0] = pot(ADC_POT1); act_prev[1] = pot(ADC_POT2);
    act_prev[2] = pot(ADC_POT3); act_prev[3] = pot(ADC_POT4);
    act_prev[4] = pot(ADC_POT5); act_prev[5] = pot(ADC_POT6);
    act_prev[6] = pot(ADC_POT7); act_prev[7] = pot(ADC_POT8);
    act_prev[8] = val_contour;

    // --- Main control loop ---
    // The OLED sets the rate: a pass that pushes a page costs a few milliseconds, and one
    // that finds nothing changed costs nothing. Either way the rate is neither fast nor
    // even, so anything needing a steady one - the encoders, and the button edges - is
    // polled from the audio ISR instead.
    while (1) {
        adc_poll();      // encoders are polled in the audio ISR — see computebufI
        uint32_t now = HAL_GetTick();

        // --- P1-4: always resonator ---
        float cur_pot[8];
        cur_pot[0] = pot(ADC_POT1); cur_pot[1] = pot(ADC_POT2);
        cur_pot[2] = pot(ADC_POT3); cur_pot[3] = pot(ADC_POT4);
        p->resonator_geometry   = cur_pot[0];
        p->resonator_brightness = cur_pot[1];
        p->resonator_damping    = cur_pot[2];
        p->resonator_position   = cur_pot[3];

        // --- P5-P7: state-dependent with pickup; P8 is always Space ---
        cur_pot[4] = pot(ADC_POT5); cur_pot[5] = pot(ADC_POT6);
        cur_pot[6] = pot(ADC_POT7); cur_pot[7] = pot(ADC_POT8);

        for (int i = 0; i < 3; i++) {
            if (!pot_picked[i]) {
                if (fabsf(cur_pot[4 + i] - pickup_target[i]) < PICKUP_THRESH)
                    pot_picked[i] = true;
            }
            if (pot_picked[i]) *pot_target(i, pot_mode) = cur_pot[4 + i];
        }
        val_space = cur_pot[7] * 2.0f;   // P8 never pages, so it never needs picking up

        // Activity detection for P1-8
        for (int i = 0; i < 8; i++) {
            if (fabsf(cur_pot[i] - act_prev[i]) > ACT_THRESH) {
                act_ts[i] = now;
                act_prev[i] = cur_pot[i];
            }
        }

        // --- E1 rotate: mode-dependent ---
        int enc1_delta = enc1_read();
        if (enc1_delta != 0) {
            act_ts[8] = now;
            val_contour += enc1_delta * 0.02f;
            val_contour = clampf(val_contour, 0.0f, 1.0f);
        }

        // --- Write all base values to patch ---
        p->exciter_bow_level      = val_bow_level;
        p->exciter_blow_level     = val_blow_level;
        p->exciter_strike_level   = val_strike_level;
        p->space                  = val_space;
        p->exciter_envelope_shape = val_contour;
        p->exciter_bow_timbre     = val_bow_tim;
        p->exciter_blow_timbre    = val_blow_tim;
        p->exciter_blow_meta      = val_flow;
        p->exciter_strike_meta    = val_mallet;
        p->exciter_strike_timbre  = val_strike_tim;
        p->exciter_signature      = val_sig;
        p->resonator_modulation_frequency = val_mod_frq;
        p->resonator_modulation_offset    = val_mod_ofs;
        p->reverb_diffusion       = val_rv_diff;
        p->reverb_lp              = val_rv_lp;

        // --- Apply CV modulation on top of base values ---
        static const int cv_adc[3] = { ADC_CV_A, ADC_CV_B, ADC_CV_C };
        for (int i = 0; i < 3; i++) {
            if (cv_assign[i] == CVT_NONE) continue;
            float mod = cv(cv_adc[i]) * 0.5f;
            float* target = nullptr;
            switch (cv_assign[i]) {
                case CVT_FLOW:       target = &p->exciter_blow_meta; break;
                case CVT_MALLET:     target = &p->exciter_strike_meta; break;
                case CVT_CONTOUR:    target = &p->exciter_envelope_shape; break;
                case CVT_BOW_TIM:    target = &p->exciter_bow_timbre; break;
                case CVT_BLOW_TIM:   target = &p->exciter_blow_timbre; break;
                case CVT_STRIKE_TIM: target = &p->exciter_strike_timbre; break;
                case CVT_SIG:        target = &p->exciter_signature; break;
                case CVT_MOD_FRQ:    target = &p->resonator_modulation_frequency; break;
                case CVT_MOD_OFS:    target = &p->resonator_modulation_offset; break;
                case CVT_RV_DIFF:    target = &p->reverb_diffusion; break;
                case CVT_RV_LP:      target = &p->reverb_lp; break;
                default: break;
            }
            if (target) *target = clampf(*target + mod, 0.0f, 1.0f);
        }

        // --- CV D: gate + strength ---
        float gate_cv = cv(ADC_CV_D);
        bool cv_gate = gate_cv > 0.2f;
        bool manual_gate = button_s3();
        perf.gate = cv_gate || manual_gate;

        if (cv_gate) {
            perf.strength = clampf(gate_cv, 0.0f, 1.0f);
        } else if (manual_gate) {
            perf.strength = 0.7f;
        }

        // --- CV X/Y: pitch and FM ---
        // modulation is added to the MIDI pitch in Part::Process, in semitones. Elements
        // scales its (attenuverted) FM CV by 49.5 and clamps at +/-60; Girl has no
        // attenuverter to spare, so it ships that full depth the way Joy does for Braids -
        // attenuate at the source for less. Left unscaled this was worth one semitone,
        // which reads as a broken input rather than a shallow one.
        // CV X/Y (pitch and FM) are read in the audio callback - see computebufI.

        // --- S1 (ENC1 push): cycle resonator model ---
        // Edge detection and the 200 ms debounce happen in the audio ISR; this collects
        // the latched press whenever the loop next comes round.
        if (btn_s1_pressed()) {
            resonator_model = (resonator_model + 1) % 3;
            part.set_resonator_model(
                static_cast<elements::ResonatorModel>(resonator_model));
        }

        // --- S4: cycle P5-P7 state ---
        if (btn_s4_pressed()) {
            const int prev_mode = pot_mode;
            pot_mode = (pot_mode + 1) % 3;
            for (int i = 0; i < 3; i++) {
                // P5 drives bow timbre in both timbres and meta, so crossing that
                // boundary needs no pickup - the pot already is where it should be.
                if (pot_target(i, prev_mode) == pot_target(i, pot_mode)) {
                    pot_picked[i] = true;
                } else {
                    pot_picked[i] = false;
                    pickup_target[i] = *pot_target(i, pot_mode);
                }
            }
        }

        // --- S2 (ENC2 push): cycle selected CV ---
        if (btn_s2_pressed()) {
            cv_sel = (cv_sel + 1) % 3;
            cv_act_ts = now;
        }

        // --- E2 rotate: cycle CV target for selected CV ---
        int enc2_delta = enc2_read();
        if (enc2_delta != 0) {
            int t = cv_assign[cv_sel] + enc2_delta;
            if (t < 0) t = NUM_CV_TARGETS - 1;
            if (t >= NUM_CV_TARGETS) t = 0;
            cv_assign[cv_sel] = t;
            cv_act_ts = now;
        }

        // --- LEDs ---
        HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6,
            perf.gate ? GPIO_PIN_SET : GPIO_PIN_RESET);
        // Latch and stretch: any overrun since the last pass lights the LED for 150 ms.
        static uint32_t overload_until = 0;
        if (cpu_overload) {
            cpu_overload = false;
            overload_until = HAL_GetTick() + 150;
        }
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6,
            ((int32_t)(HAL_GetTick() - overload_until) < 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6,
            (resonator_model == 0 || resonator_model == 2)
                ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7,
            (resonator_model >= 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3,
            perf.gate ? GPIO_PIN_SET : GPIO_PIN_RESET);

        // --- OLED: single-page display ---
        // Layout:
        //   S1:Mod
        //   Geom Brgt Damp Posn     P1-P4
        //   BowL BloL StkL Spce     P5-P8, contents cycle with S4
        //   Cont Play Page Asgn     E1  S3  S4  E2
        //   A:Flw  B:Mal  C:---     assignable CV, selected slot underlined
        //   (bottom: active param display or static reference)
        static int oled_tick = 0;
        if (oled_tick == 0) {
            oled_clear();
            char line[22];

            // Row 0 (y=0): what S1 selects and where it is. S4's state is not shown
            // here - the row below spells the parameters out, so naming it twice is noise.
            snprintf(line, sizeof(line), "S1:%s", model_names[resonator_model]);
            oled_str(0, 0, line);

            // Screen-link faults, right-aligned, and only once there have been any.
            // A jam and its recovery are otherwise invisible - the screen is the thing
            // that fails, so it cannot report at the time. This says it happened.
            const uint32_t faults = oled_fault_count();
            if (faults) {
                char f[8];
                snprintf(f, sizeof(f), "E%u",
                         (unsigned)(faults > 99u ? 99u : faults));
                oled_str(128 - 6 * (int)strlen(f), 0, f);
            }

            oled_hline(0, 9, 128);

            // Rows 1-3 sit under the controls they belong to: four columns of five
            // characters, matching P1-P4, then P5-P8, then E1 / S3 / S4 / E2.
            snprintf(line, sizeof(line), "%-4s Brgt Damp Posn",
                     p1_label_short[resonator_model]);
            oled_str(0, 11, line);

            oled_str(0, 21, pot_row[pot_mode]);
            oled_str(0, 31, "Cont Play Page Asgn");

            // Row 4 (y=42): only the assignable CV inputs. CV-D is the gate and CV-X is
            // V/oct - both fixed, so neither earns space here.
            snprintf(line, sizeof(line), "A:%s  B:%s  C:%s",
                     cv_target_abbr[cv_assign[0]],
                     cv_target_abbr[cv_assign[1]],
                     cv_target_abbr[cv_assign[2]]);
            oled_str(0, 42, line);

            // Underline the slot E2 is editing. Each group is "X:abc" = 5 chars = 30px,
            // with two spaces (12px) between, so the groups start at 0, 42 and 84.
            oled_hline(cv_sel * 42, 50, 30);

            // Row 5 (y=53): whatever you last touched, and blank when you are not
            // touching anything. Every control is already named on the rows above, so a
            // static reference line here would only repeat them.
            const char** names = ctrl_name[pot_mode];
            float ctrl_val[9];
            for (int i = 0; i < 8; i++) ctrl_val[i] = cur_pot[i];
            ctrl_val[8] = val_contour;

            int s0 = -1, s1 = -1;
            uint32_t t0 = 0, t1 = 0;
            for (int i = 0; i < 9; i++) {
                if (now - act_ts[i] < ACT_DURATION) {
                    if (act_ts[i] > t0) {
                        s1 = s0; t1 = t0;
                        s0 = i;  t0 = act_ts[i];
                    } else if (act_ts[i] > t1) {
                        s1 = i;  t1 = act_ts[i];
                    }
                }
            }

            if (s0 >= 0) {
                // Format a single name+value fragment ("Geom    50" / "Chord  m7")
                // into out. For P1 in CHD mode, show chord name instead of percent.
                auto fmt_slot = [&](int idx, char* out, size_t outsz) {
                    const char* nm = (idx == 0)
                        ? p1_label_long[resonator_model]
                        : names[idx];
                    if (idx == 0 && resonator_model == 2) {
                        int ci = (int)(ctrl_val[0] * 10.0f + 0.5f);
                        if (ci < 0) ci = 0;
                        if (ci > 10) ci = 10;
                        snprintf(out, outsz, "%-6s %3s", nm, chord_name[ci]);
                    } else {
                        int v = (int)(ctrl_val[idx] * 100.0f + 0.5f);
                        snprintf(out, outsz, "%-6s %3d", nm, v);
                    }
                };
                char f0[12], f1[12];
                fmt_slot(s0, f0, sizeof(f0));
                if (s1 >= 0) {
                    fmt_slot(s1, f1, sizeof(f1));
                    snprintf(line, sizeof(line), "%s %s", f0, f1);
                } else {
                    snprintf(line, sizeof(line), "%s", f0);
                }
                oled_str(0, 53, line);
            } else if (now - cv_act_ts < ACT_DURATION) {
                // No pot moving, but a CV slot was just selected or reassigned
                snprintf(line, sizeof(line), "CV %c  %s",
                         (char)('A' + cv_sel), cv_target_full[cv_assign[cv_sel]]);
                oled_str(0, 53, line);
            }
            oled_update();   // sends one page, and only if that page changed
        }
        oled_tick = (oled_tick + 1) % 8;

        HAL_Delay(1);
    }
}
