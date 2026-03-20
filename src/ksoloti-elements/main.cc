// main.cc — Ksoloti Big Genes: Mutable Instruments Elements
//
// Elements DSP running on STM32F429 + ADAU1961 codec at 32 kHz.
// Gate is held permanently on at middle C for initial testing.
// Audio inputs: L = blow exciter, R = strike exciter.
// Audio outputs: L = main, R = aux (stereo reverb spread).

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
        // Output silence until Part is initialized
        for (int i = 0; i < DOUBLE_BUFSIZE; i++) outp[i] = 0;
        return;
    }

    const float kInScale  = 1.0f / 2147483648.0f;
    const float kOutScale = 2147483648.0f;

    // De-interleave stereo input → mono blow/strike buffers
    for (int i = 0; i < BUFSIZE; i++) {
        blow_in[i]   = static_cast<float>(inp[i * 2])     * kInScale;
        strike_in[i] = static_cast<float>(inp[i * 2 + 1]) * kInScale;
    }

    // Run Elements DSP
    part.Process(perf, blow_in, strike_in, main_out, aux_out, BUFSIZE);

    // Re-interleave mono outputs → stereo int32
    for (int i = 0; i < BUFSIZE; i++) {
        outp[i * 2]     = static_cast<int32_t>(main_out[i] * kOutScale);
        outp[i * 2 + 1] = static_cast<int32_t>(aux_out[i]  * kOutScale);
    }
}

// --- Entry point ---

int main(void)
{
    HAL_Init();

    // Initialize Elements DSP BEFORE codec — DMA ISR calls Part::Process()
    // as soon as codec_init() enables DMA.
    part.Init(reverb_buffer);

    // Enable bow exciter for sustained sound (strike only fires on gate edge)
    elements::Patch* p = part.mutable_patch();
    p->exciter_bow_level = 0.8f;
    p->exciter_bow_timbre = 0.5f;
    p->exciter_strike_level = 0.8f;
    p->exciter_strike_meta = 0.5f;
    p->exciter_strike_timbre = 0.5f;
    p->space = 0.5f;

    // Set performance state before audio starts
    perf.note = 60.0f;
    perf.modulation = 0.0f;
    perf.strength = 0.7f;
    perf.gate = false;  // start with gate off

    // Now start the codec (DMA ISR will begin calling computebufI)
    codec_init();
    dsp_ready = true;

    // LED blink = system alive
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef g = {};
    g.Pin   = GPIO_PIN_6;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOG, &g);

    // Re-trigger gate every 2 seconds so the internal exciter keeps firing
    while (1) {
        HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_6);

        perf.gate = true;
        HAL_Delay(500);

        perf.gate = false;
        HAL_Delay(1500);
    }
}
