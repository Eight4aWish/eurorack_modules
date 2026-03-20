// main.cc — Ksoloti Big Genes: Mutable Instruments Elements
//
// Elements DSP running on STM32F429 + ADAU1961 codec at 32 kHz.
// 8 pots, 6 CV jacks, gate button, and LED wired to Elements parameters.
//
// Pot/CV mapping (see memory/project_biggenes_io_mapping.md for full spec):
//   POT1-4 (+CV P1-P4): resonator geometry/brightness/damping/position
//   POT5-8:             bow level / blow level / strike level / space
//   CV A-C:             blow meta / strike meta / envelope shape
//   CV D:               gate + strength (velocity from voltage)
//   CV X:               V/Oct pitch
//   CV Y:               FM modulation
//   S3 button:          manual gate (fixed strength 0.7)
//
// Audio: In L = blow exciter, In R = strike exciter
//        Out L = main, Out R = aux (reverb)

#include "adc.h"
#include "codec.h"
#include "stm32f4xx_hal.h"
#include "elements/dsp/part.h"

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

// --- Audio callback ---

extern "C" void computebufI(int32_t *inp, int32_t *outp)
{
    if (!dsp_ready) {
        for (int i = 0; i < DOUBLE_BUFSIZE; i++) outp[i] = 0;
        return;
    }

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

// --- Entry point ---

int main(void)
{
    HAL_Init();

    // ADC + buttons (populates initial readings before audio starts)
    adc_init();

    // Initialize Elements DSP BEFORE codec — DMA ISR calls Part::Process()
    // as soon as codec_init() enables DMA.
    part.Init(reverb_buffer);

    // Patch pointer — lives for the duration of the program
    elements::Patch* p = part.mutable_patch();

    // Secondary parameters: fixed defaults until OLED encoder UI is added
    p->exciter_bow_timbre             = 0.5f;
    p->exciter_blow_timbre            = 0.5f;
    p->exciter_strike_timbre          = 0.5f;
    p->exciter_signature              = 0.5f;
    p->resonator_modulation_frequency = 0.5f;
    p->resonator_modulation_offset    = 0.5f;
    p->reverb_diffusion               = 0.7f;
    p->reverb_lp                      = 0.8f;
    p->modulation_frequency           = 0.5f;

    // Performance defaults
    perf.note       = 60.0f;
    perf.modulation = 0.0f;
    perf.strength   = 0.0f;
    perf.gate       = false;

    // Now start the codec (DMA ISR will begin calling computebufI)
    codec_init();
    dsp_ready = true;

    // LED1 (PG6) = gate indicator
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef g = {};
    g.Pin   = GPIO_PIN_6;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOG, &g);

    // --- Main control loop (~1 kHz) ---
    while (1) {
        adc_poll();  // Update pots 5-8 (ADC3)

        // Pots 1-4 (hardware-summed with CV P1-P4) -> Resonator
        p->resonator_geometry   = pot(ADC_POT1);
        p->resonator_brightness = pot(ADC_POT2);
        p->resonator_damping    = pot(ADC_POT3);
        p->resonator_position   = pot(ADC_POT4);

        // Pots 5-8 -> Exciter levels + Space
        p->exciter_bow_level    = pot(ADC_POT5);
        p->exciter_blow_level   = pot(ADC_POT6);
        p->exciter_strike_level = pot(ADC_POT7);
        p->space                = pot(ADC_POT8) * 2.0f;  // 0..2

        // CV A-C -> Exciter modulation (0..1, centered at 0.5 unpatched)
        p->exciter_blow_meta      = cv_uni(ADC_CV_A);
        p->exciter_strike_meta    = cv_uni(ADC_CV_B);
        p->exciter_envelope_shape = cv_uni(ADC_CV_C);

        // CV D -> Gate + Strength (velocity from gate voltage)
        float gate_cv = cv(ADC_CV_D);
        bool cv_gate = gate_cv > 0.2f;       // ~1 V threshold
        bool manual_gate = button_s3();
        perf.gate = cv_gate || manual_gate;

        if (cv_gate) {
            perf.strength = (gate_cv > 1.0f) ? 1.0f : gate_cv;
        } else if (manual_gate) {
            perf.strength = 0.7f;
        }

        // CV X -> V/Oct pitch (centered at middle C, ~+/-2.5 octaves)
        // TODO: calibrate with trimmer for exact 1V/oct tracking
        perf.note = 60.0f + cv(ADC_CV_X) * 30.0f;

        // CV Y -> FM modulation depth (-1..+1, 0 when unpatched)
        perf.modulation = cv(ADC_CV_Y);

        // LED1 = gate active
        HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6,
            perf.gate ? GPIO_PIN_SET : GPIO_PIN_RESET);

        HAL_Delay(1);
    }
}
