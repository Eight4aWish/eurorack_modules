// main.cc — Ksoloti Big Genes: Mutable Instruments Elements
//
// Elements DSP running on STM32F429 + ADAU1961 codec at 32 kHz.
// Single-page OLED UI with two pot modes (levels / timbres).
//
// Controls:
//   P1-4 (+CV P1-P4): resonator geometry / brightness / damping / position
//   P5-8 mode 1:      bow level / blow level / strike level / space
//   P5-8 mode 2:      blow timbre / flow(blow_meta) / mallet(strike_meta) / strike timbre
//   E1 mode 1:        contour (envelope shape)
//   E1 mode 2:        bow timbre
//   S1 (ENC1 push):   cycle resonator model (modal / string / chords)
//   S2 (ENC2 push):   select CV for assignment (A / B / C)
//   E2 rotate:        cycle CV target parameter
//   S3:               play (manual gate, strength 0.7)
//   S4:               toggle pot mode (levels / timbres)
//   CV A-C:           assignable modulation (default A=Flow, B=Mallet, C=none)
//   CV D:             gate + strength (velocity from voltage)
//   CV X:             V/Oct pitch
//   CV Y:             FM modulation
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

extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
}

// --- Static allocations ---

static elements::Part part;

// Reverb buffer: 64 KB of uint16_t.
static uint16_t reverb_buffer[32768];

// Per-block float buffers (16 mono samples each = kMaxBlockSize)
static float blow_in[BUFSIZE];
static float strike_in[BUFSIZE];
static float main_out[BUFSIZE];
static float aux_out[BUFSIZE];

static elements::PerformanceState perf;
static volatile bool dsp_ready = false;
static volatile bool cpu_overload = false;

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

    part.Process(perf, blow_in, strike_in, main_out, aux_out, BUFSIZE);

    for (int i = 0; i < BUFSIZE; i++) {
        outp[i * 2]     = static_cast<int32_t>(main_out[i] * kOutScale);
        outp[i * 2 + 1] = static_cast<int32_t>(aux_out[i]  * kOutScale);
    }

    // 16 samples @ 32 kHz = 500 us. At 168 MHz = 84000 cycles budget.
    cpu_overload = (DWT->CYCCNT - t0) > 80000;
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
static inline float cv(int ch)
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

// --- Display helpers ---

static const char* model_names[] = { "Mod", "Str", "Chd" };

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
    part.Init(reverb_buffer);

    // Patch pointer — lives for the duration of the program
    elements::Patch* p = part.mutable_patch();

    // --- Parameter base values ---
    // Mode 1 (levels): written by pots when active, frozen when in mode 2
    float val_bow_level    = 0.0f;
    float val_blow_level   = 0.0f;
    float val_strike_level = 0.0f;
    float val_space        = 0.0f;
    float val_contour      = 0.5f;   // E1 in mode 1

    // Mode 2 (timbres): written by pots/E1 when active, frozen when in mode 1
    float val_bow_tim      = 0.5f;   // E1 in mode 2
    float val_blow_tim     = 0.5f;   // P5 in mode 2
    float val_flow         = 0.5f;   // P6 in mode 2 (blow_meta base)
    float val_mallet       = 0.5f;   // P7 in mode 2 (strike_meta base)
    float val_strike_tim   = 0.5f;   // P8 in mode 2

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

    // Button debounce state
    bool enc1_push_prev = button_enc1();
    uint32_t enc1_push_last = 0;
    bool s4_prev = button_s4();
    uint32_t s4_last = 0;
    bool s2_prev = button_enc2();
    uint32_t s2_last = 0;

    // Pot pickup for P5-8 on mode switch
    bool pot_picked[4] = {true, true, true, true};
    float pickup_target[4] = {0};
    #define PICKUP_THRESH 0.03f

    // Activity tracking for bottom-line display
    // Indices: 0-3 = P1-4, 4-7 = P5-8, 8 = E1
    uint32_t act_ts[9] = {0};
    float act_prev[9] = {0};
    #define ACT_THRESH 0.015f
    #define ACT_DURATION 2000

    // Control names for bottom-line display (6 chars max, %-6s padded)
    static const char* ctrl_name_m0[] = {
        "Geom", "Bright", "Damp", "Posn",
        "BowLvl", "BlwLvl", "StkLvl", "Space",
        "Contr"
    };
    static const char* ctrl_name_m1[] = {
        "Geom", "Bright", "Damp", "Posn",
        "BlwTmb", "Flow", "Mallet", "StkTmb",
        "BowTmb"
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

    // --- Main control loop (~1 kHz) ---
    while (1) {
        adc_poll();
        enc_poll();
        uint32_t now = HAL_GetTick();

        // --- P1-4: always resonator ---
        float cur_pot[8];
        cur_pot[0] = pot(ADC_POT1); cur_pot[1] = pot(ADC_POT2);
        cur_pot[2] = pot(ADC_POT3); cur_pot[3] = pot(ADC_POT4);
        p->resonator_geometry   = cur_pot[0];
        p->resonator_brightness = cur_pot[1];
        p->resonator_damping    = cur_pot[2];
        p->resonator_position   = cur_pot[3];

        // --- P5-8: mode-dependent with pickup ---
        cur_pot[4] = pot(ADC_POT5); cur_pot[5] = pot(ADC_POT6);
        cur_pot[6] = pot(ADC_POT7); cur_pot[7] = pot(ADC_POT8);

        for (int i = 0; i < 4; i++) {
            if (!pot_picked[i]) {
                if (fabsf(cur_pot[4 + i] - pickup_target[i]) < PICKUP_THRESH)
                    pot_picked[i] = true;
            }
        }

        if (pot_mode == 0) {
            if (pot_picked[0]) val_bow_level    = cur_pot[4];
            if (pot_picked[1]) val_blow_level   = cur_pot[5];
            if (pot_picked[2]) val_strike_level = cur_pot[6];
            if (pot_picked[3]) val_space        = cur_pot[7] * 2.0f;
        } else {
            if (pot_picked[0]) val_blow_tim   = cur_pot[4];
            if (pot_picked[1]) val_flow       = cur_pot[5];
            if (pot_picked[2]) val_mallet     = cur_pot[6];
            if (pot_picked[3]) val_strike_tim = cur_pot[7];
        }

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
            if (pot_mode == 0) {
                val_contour += enc1_delta * 0.02f;
                val_contour = clampf(val_contour, 0.0f, 1.0f);
            } else {
                val_bow_tim += enc1_delta * 0.02f;
                val_bow_tim = clampf(val_bow_tim, 0.0f, 1.0f);
            }
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
        perf.note = 60.0f + cv(ADC_CV_X) * 30.0f;
        perf.modulation = cv(ADC_CV_Y);

        // --- S1 (ENC1 push): cycle resonator model (debounced 200ms) ---
        bool enc1_now = button_enc1();
        if (enc1_now && !enc1_push_prev && (now - enc1_push_last > 200)) {
            resonator_model = (resonator_model + 1) % 3;
            part.set_resonator_model(
                static_cast<elements::ResonatorModel>(resonator_model));
            enc1_push_last = now;
        }
        enc1_push_prev = enc1_now;

        // --- S4: toggle pot mode (debounced 200ms) ---
        bool s4_now = button_s4();
        if (s4_now && !s4_prev && (now - s4_last > 200)) {
            pot_mode ^= 1;
            if (pot_mode == 0) {
                pickup_target[0] = val_bow_level;
                pickup_target[1] = val_blow_level;
                pickup_target[2] = val_strike_level;
                pickup_target[3] = val_space * 0.5f;
            } else {
                pickup_target[0] = val_blow_tim;
                pickup_target[1] = val_flow;
                pickup_target[2] = val_mallet;
                pickup_target[3] = val_strike_tim;
            }
            for (int i = 0; i < 4; i++) pot_picked[i] = false;
            s4_last = now;
        }
        s4_prev = s4_now;

        // --- S2 (ENC2 push): cycle selected CV (debounced 200ms) ---
        bool s2_now = button_enc2();
        if (s2_now && !s2_prev && (now - s2_last > 200)) {
            cv_sel = (cv_sel + 1) % 3;
            s2_last = now;
        }
        s2_prev = s2_now;

        // --- E2 rotate: cycle CV target for selected CV ---
        int enc2_delta = enc2_read();
        if (enc2_delta != 0) {
            int t = cv_assign[cv_sel] + enc2_delta;
            if (t < 0) t = NUM_CV_TARGETS - 1;
            if (t >= NUM_CV_TARGETS) t = 0;
            cv_assign[cv_sel] = t;
        }

        // --- LEDs ---
        HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6,
            perf.gate ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6,
            cpu_overload ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6,
            (resonator_model == 0 || resonator_model == 2)
                ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7,
            (resonator_model >= 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3,
            perf.gate ? GPIO_PIN_SET : GPIO_PIN_RESET);

        // --- OLED: single-page display ---
        // Layout:
        //   S1 Mod E1 Con SE2 Cv
        //   P1-4 Geo Brt Dmp Pos
        //   P5-8 Bow Blw Stk Spc      <- "P5-8" underlined when active
        //   P5-8 BlT Flw Mal StT      <- "P5-8" underlined when active
        //   CvAD Flw Mal --- Gte      <- active CV underlined
        //   (bottom: active param display or static reference)
        static int oled_tick = 0;
        if (oled_tick == 0) {
            oled_clear();
            char line[22];

            // Row 0 (y=0): "S1 Mod E1 Con SE2 Cv"
            const char* e1_lbl = (pot_mode == 0) ? "Con" : "BoT";
            snprintf(line, sizeof(line), "S1 %s E1 %s SE2 Cv",
                     model_names[resonator_model], e1_lbl);
            oled_str(0, 0, line);

            oled_hline(0, 9, 128);

            // Row 1 (y=11): "P1-4 Geo Brt Dmp Pos"
            oled_str(0, 11, "P1-4 Geo Brt Dmp Pos");

            // Row 2 (y=21): "P5-8 Bow Blw Stk Spc"
            oled_str(0, 21, "P5-8 Bow Blw Stk Spc");
            if (pot_mode == 0)
                oled_hline(0, 29, 24);  // underline "P5-8" only

            // Row 3 (y=31): "P5-8 BlT Flw Mal StT"
            oled_str(0, 31, "P5-8 BlT Flw Mal StT");
            if (pot_mode == 1)
                oled_hline(0, 39, 24);  // underline "P5-8" only

            // Row 4 (y=42): "CvAD Flw Mal --- Gte"
            snprintf(line, sizeof(line), "CvAD %s %s %s Gte",
                     cv_target_abbr[cv_assign[0]],
                     cv_target_abbr[cv_assign[1]],
                     cv_target_abbr[cv_assign[2]]);
            oled_str(0, 42, line);

            // Underline active CV: "CvAD " = 30px, then 3-char groups at 30, 54, 78
            oled_hline(30 + cv_sel * 24, 50, 18);

            // Row 5 (y=53): activity display or static reference
            // Find the two most recently active controls (within 2s)
            const char** names = (pot_mode == 0) ? ctrl_name_m0 : ctrl_name_m1;
            float ctrl_val[9];
            for (int i = 0; i < 8; i++) ctrl_val[i] = cur_pot[i];
            ctrl_val[8] = (pot_mode == 0) ? val_contour : val_bow_tim;

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
                int v0 = (int)(ctrl_val[s0] * 100.0f + 0.5f);
                if (s1 >= 0) {
                    int v1 = (int)(ctrl_val[s1] * 100.0f + 0.5f);
                    snprintf(line, sizeof(line), "%-6s %3d %-6s %3d",
                             names[s0], v0, names[s1], v1);
                } else {
                    snprintf(line, sizeof(line), "%-6s %3d", names[s0], v0);
                }
                oled_str(0, 53, line);
            } else {
                oled_str(0, 53, "S34 PyPge CvXY VO FM");
            }
        }
        oled_update();
        oled_tick = (oled_tick + 1) % 8;

        HAL_Delay(1);
    }
}
