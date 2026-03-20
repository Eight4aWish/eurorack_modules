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
//   ENC1 push:          cycle resonator model (modal/string/strings)
//   LED1 green (PG6):   gate active
//   LED2 red (PC6):     CPU overload
//   LED4 (PB6/PB7):     resonator model (green=modal, red=string, both=strings)
//   Gate1 (PD3):        gate echo output
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

    // --- Resonator model state ---
    int resonator_model = 0;  // 0=modal, 1=string, 2=strings
    bool enc1_prev = false;
    uint32_t enc1_last_press = 0;

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

        // ENC1 push -> cycle resonator model (debounced, 200 ms lockout)
        bool enc1_now = button_enc1();
        if (enc1_now && !enc1_prev && (HAL_GetTick() - enc1_last_press > 200)) {
            resonator_model = (resonator_model + 1) % 3;
            part.set_resonator_model(
                static_cast<elements::ResonatorModel>(resonator_model));
            enc1_last_press = HAL_GetTick();
        }
        enc1_prev = enc1_now;

        // LED1 green = gate active
        HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6,
            perf.gate ? GPIO_PIN_SET : GPIO_PIN_RESET);

        // LED2 red = CPU overload
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6,
            cpu_overload ? GPIO_PIN_SET : GPIO_PIN_RESET);

        // LED4 = resonator model: green=modal, red=string, both=strings
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6,
            (resonator_model == 0 || resonator_model == 2)
                ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7,
            (resonator_model >= 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);

        // Gate1 output = gate echo for chaining
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3,
            perf.gate ? GPIO_PIN_SET : GPIO_PIN_RESET);

        HAL_Delay(1);
    }
}
