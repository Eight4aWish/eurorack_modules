// main.cc — Ksoloti Big Genes: Mutable Instruments Elements
//
// Elements DSP running on STM32F429 + ADAU1961 codec at 32 kHz.
// 8 pots, 6 CV jacks, gate button, and LED wired to Elements parameters.
//
// Pot/CV mapping:
//   POT1-4 (+CV P1-P4): resonator geometry/brightness/damping/position
//   POT5-8:             bow level / blow level / strike level / space
//   CV A-B:             blow meta / strike meta
//   CV C:               (unassigned)
//   CV D:               gate + strength (velocity from voltage)
//   CV X:               V/Oct pitch
//   CV Y:               FM modulation
//   S1 (ENC1 push):     cycle resonator model (modal/string/chords)
//   ENC1 rotate:        envelope shape (always active)
//   S2 (ENC2 push):     toggle scroll/edit mode (page 2)
//   ENC2 rotate:        scroll or edit secondary params (page 2)
//   S3 button:          manual gate (fixed strength 0.7)
//   S4 button:          cycle OLED page (main / params)
//   LED1 green (PG6):   gate active
//   LED2 red (PC6):     CPU overload
//   LED4 (PB6/PB7):     resonator model (green=modal, red=string, both=strings)
//   Gate1 (PD3):        gate echo output
//
// Audio: In L = blow exciter, In R = strike exciter
//        Out L = main, Out R = aux (reverb)

#include "adc.h"
#include "codec.h"
#include "oled.h"
#include "stm32f4xx_hal.h"
#include "elements/dsp/part.h"
#include <cstdio>

extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
}

// --- Static allocations ---

static elements::Part part;

// Reverb buffer: 64 KB of uint16_t.
// TODO: move to CCM RAM (0x10000000) via linker section if main SRAM is tight.
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

    // De-interleave stereo input -> mono blow/strike buffers
    for (int i = 0; i < BUFSIZE; i++) {
        blow_in[i]   = static_cast<float>(inp[i * 2])     * kInScale;
        strike_in[i] = static_cast<float>(inp[i * 2 + 1]) * kInScale;
    }

    // Run Elements DSP
    part.Process(perf, blow_in, strike_in, main_out, aux_out, BUFSIZE);

    // Re-interleave mono outputs -> stereo int32
    for (int i = 0; i < BUFSIZE; i++) {
        outp[i * 2]     = static_cast<int32_t>(main_out[i] * kOutScale);
        outp[i * 2 + 1] = static_cast<int32_t>(aux_out[i]  * kOutScale);
    }

    // 16 samples @ 32 kHz = 500 µs. At 168 MHz = 84000 cycles budget.
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

// CV mapped to unipolar 0.0 .. 1.0 (centered at 0.5 when unpatched)
static inline float cv_uni(int ch)
{
    return cv(ch) * 0.5f + 0.5f;
}

// --- Display helpers ---

static const char* model_names[] = { "MODAL", "STRING", "CHORDS" };
static const char* note_names[]  = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

// Secondary parameter table
struct SecParam {
    const char* name;
    float* ptr;
    float default_val;
    float min_val;
    float max_val;
};

#define NUM_SEC_PARAMS 10
static SecParam sec_params[NUM_SEC_PARAMS];  // initialized in main()

static int oled_page = 0;       // 0 = main, 1 = secondary params
static int sec_cursor = 0;      // which param is selected (0..NUM_SEC_PARAMS-1)

// Format a float 0.0-1.0 as "0.XX" into buf (max 5 chars + null)
static void fmt_val(char* buf, float v)
{
    int pct = (int)(v * 100.0f + 0.5f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    buf[0] = '0' + (pct / 100);
    buf[1] = '.';
    buf[2] = '0' + ((pct / 10) % 10);
    buf[3] = '0' + (pct % 10);
    buf[4] = '\0';
}

static void draw_header(int model, float env_shape)
{
    // "MODAL (S1)  E1:Env 0.50"
    char hdr[22];
    char vbuf[6];
    fmt_val(vbuf, env_shape);
    snprintf(hdr, sizeof(hdr), "%s(S1) E1:Env%s", model_names[model], vbuf);
    oled_str(0, 0, hdr);

    oled_hline(0, 9, 128);
}

static void draw_page_main(int model, float env_shape)
{
    oled_clear();
    draw_header(model, env_shape);

    oled_str(0, 12, "P1-4: Geo Brt Dmp Pos");
    oled_str(0, 22, "P5-8: Bow Blw Str Spc");

    oled_hline(0, 31, 128);

    oled_str(0, 34, "CvAD: Blw Str - Gte");
    oled_str(0, 44, "CvX: V/Oct  CvY: FM");

    oled_str(0, 56, "S3: ManGte S4: Page2");
}

static void draw_page_params(int model, float env_shape)
{
    oled_clear();
    draw_header(model, env_shape);

    // Two-column layout: 5 rows × 2 columns = 10 params
    // Left column: params 0-4, right column: params 5-9
    // Each entry: cursor(1) + name(6) + value(4) = ~66px per column
    const int col_x[2] = {0, 66};
    const int rows = 5;

    for (int col = 0; col < 2; col++) {
        for (int row = 0; row < rows; row++) {
            int idx = col * rows + row;
            if (idx >= NUM_SEC_PARAMS) break;

            int x = col_x[col];
            int y = 12 + row * 10;

            if (idx == sec_cursor) {
                oled_str(x, y, ">");
            }

            oled_str(x + 6, y, sec_params[idx].name);

            char vbuf[6];
            fmt_val(vbuf, *sec_params[idx].ptr);
            oled_str(x + 42, y, vbuf);
        }
    }
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

    // Secondary parameters — adjustable via OLED encoder UI
    p->exciter_bow_timbre             = 0.5f;
    p->exciter_blow_timbre            = 0.5f;
    p->exciter_strike_timbre          = 0.5f;
    p->exciter_envelope_shape         = 0.5f;
    p->exciter_signature              = 0.5f;
    p->resonator_modulation_frequency = 0.5f;
    p->resonator_modulation_offset    = 0.5f;
    p->reverb_diffusion               = 0.7f;
    p->reverb_lp                      = 0.8f;

    // CV A-B base values (CV modulates around these)
    static float blow_meta_base   = 0.5f;
    static float strike_meta_base = 0.5f;

    // Build secondary param table (name, pointer, default, min, max)
    // BlwMod/StrMod first (most useful), then timbres, then modulation, then reverb
    sec_params[0] = {"BlwMod",  &blow_meta_base,                     0.5f, 0.0f, 1.0f};
    sec_params[1] = {"StrMod",  &strike_meta_base,                   0.5f, 0.0f, 1.0f};
    sec_params[2] = {"BowTim",  &p->exciter_bow_timbre,             0.5f, 0.0f, 1.0f};
    sec_params[3] = {"BlwTim",  &p->exciter_blow_timbre,            0.5f, 0.0f, 1.0f};
    sec_params[4] = {"StrTim",  &p->exciter_strike_timbre,          0.5f, 0.0f, 1.0f};
    sec_params[5] = {"Sig",     &p->exciter_signature,              0.5f, 0.0f, 1.0f};
    sec_params[6] = {"ModFrq",  &p->resonator_modulation_frequency, 0.5f, 0.0f, 1.0f};
    sec_params[7] = {"ModOfs",  &p->resonator_modulation_offset,    0.5f, 0.0f, 1.0f};
    sec_params[8] = {"RvDiff",  &p->reverb_diffusion,               0.7f, 0.0f, 1.0f};
    sec_params[9] = {"RvLP",    &p->reverb_lp,                      0.8f, 0.0f, 1.0f};

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
    bool enc1_push_prev = button_enc1();   // read actual state to avoid spurious edge
    uint32_t enc1_push_last = 0;
    bool s4_prev = button_s4();            // read actual state to avoid startup page flip
    uint32_t s4_last = 0;
    bool enc2_push_prev = button_enc2();
    uint32_t enc2_push_last = 0;

    // --- Main control loop (~1 kHz) ---
    while (1) {
        adc_poll();   // Update pots 5-8 (ADC3)
        enc_poll();   // Update encoder rotation

        // --- Pot -> Patch mapping ---
        p->resonator_geometry   = pot(ADC_POT1);
        p->resonator_brightness = pot(ADC_POT2);
        p->resonator_damping    = pot(ADC_POT3);
        p->resonator_position   = pot(ADC_POT4);
        p->exciter_bow_level    = pot(ADC_POT5);
        p->exciter_blow_level   = pot(ADC_POT6);
        p->exciter_strike_level = pot(ADC_POT7);
        p->space                = pot(ADC_POT8) * 2.0f;

        // --- CV -> Patch/Perf mapping ---
        // CV A-B modulate around encoder-set base values (clamped 0..1)
        float bm = blow_meta_base + cv(ADC_CV_A) * 0.5f;
        p->exciter_blow_meta = (bm < 0.0f) ? 0.0f : (bm > 1.0f) ? 1.0f : bm;
        float sm = strike_meta_base + cv(ADC_CV_B) * 0.5f;
        p->exciter_strike_meta = (sm < 0.0f) ? 0.0f : (sm > 1.0f) ? 1.0f : sm;
        // CV C unassigned — envelope shape now on ENC1 rotate

        float gate_cv = cv(ADC_CV_D);
        bool cv_gate = gate_cv > 0.2f;
        bool manual_gate = button_s3();
        perf.gate = cv_gate || manual_gate;

        if (cv_gate) {
            perf.strength = (gate_cv > 1.0f) ? 1.0f : gate_cv;
        } else if (manual_gate) {
            perf.strength = 0.7f;
        }

        perf.note = 60.0f + cv(ADC_CV_X) * 30.0f;
        perf.modulation = cv(ADC_CV_Y);

        // --- ENC1 push: cycle resonator model (debounced 200ms) ---
        bool enc1_now = button_enc1();
        if (enc1_now && !enc1_push_prev && (HAL_GetTick() - enc1_push_last > 200)) {
            resonator_model = (resonator_model + 1) % 3;
            part.set_resonator_model(
                static_cast<elements::ResonatorModel>(resonator_model));
            enc1_push_last = HAL_GetTick();
        }
        enc1_push_prev = enc1_now;

        // --- S4: cycle OLED page (debounced 200ms) ---
        bool s4_now = button_s4();
        if (s4_now && !s4_prev && (HAL_GetTick() - s4_last > 200)) {
            oled_page = (oled_page + 1) % 2;
            s4_last = HAL_GetTick();
        }
        s4_prev = s4_now;

        // --- ENC1 rotate: envelope shape (always active, both pages) ---
        int enc1_delta = enc1_read();
        if (enc1_delta != 0) {
            p->exciter_envelope_shape += enc1_delta * 0.02f;
            if (p->exciter_envelope_shape < 0.0f) p->exciter_envelope_shape = 0.0f;
            if (p->exciter_envelope_shape > 1.0f) p->exciter_envelope_shape = 1.0f;
        }

        // --- Page 2: S2 steps cursor, E2 adjusts selected param ---
        if (oled_page == 1) {
            // S2 (ENC2 push) = step to next param (debounced 200ms)
            bool enc2_now = button_enc2();
            if (enc2_now && !enc2_push_prev && (HAL_GetTick() - enc2_push_last > 200)) {
                sec_cursor = (sec_cursor + 1) % NUM_SEC_PARAMS;
                enc2_push_last = HAL_GetTick();
            }
            enc2_push_prev = enc2_now;

            // E2 rotate = adjust selected param value
            int enc2_delta = enc2_read();
            if (enc2_delta != 0) {
                SecParam& sp = sec_params[sec_cursor];
                float step = (sp.max_val - sp.min_val) * 0.02f;
                *sp.ptr += enc2_delta * step;
                if (*sp.ptr < sp.min_val) *sp.ptr = sp.min_val;
                if (*sp.ptr > sp.max_val) *sp.ptr = sp.max_val;
            }
        } else {
            enc2_read();  // drain accumulator
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

        // --- OLED: redraw every 8 ticks, push 1 page per tick ---
        static int oled_tick = 0;
        if (oled_tick == 0) {
            if (oled_page == 0)
                draw_page_main(resonator_model, p->exciter_envelope_shape);
            else
                draw_page_params(resonator_model, p->exciter_envelope_shape);
        }
        oled_update();
        oled_tick = (oled_tick + 1) % 8;

        HAL_Delay(1);
    }
}
