// adc.cc — ADC driver for Ksoloti Big Genes (STM32F429)
//
// ADC1: 10-channel continuous scan via DMA2 Stream0 Channel0
//   PA0-PA3 (pots 1-4), PA6-PA7 (CV A-B), PB0-PB1 (CV C-D),
//   PC1 (CV X), PC4 (CV Y)
//
// ADC3: 4-channel polled (no DMA — all streams occupied by codec + ADC1)
//   PF6-PF9 (pots 5-8)
//
// Buttons: S3 (PB12), S4 (PB13), active-low with internal pull-up.

#include "adc.h"
#include "stm32f4xx_hal.h"

// --- Raw buffers ---
static volatile uint16_t adc1_buf[ADC1_NUM_CH];
static uint16_t adc3_buf[4];

// --- HAL handles ---
static ADC_HandleTypeDef hadc1;
static ADC_HandleTypeDef hadc3;
static DMA_HandleTypeDef hdma_adc1;

// ADC3 channel numbers for pots 5-8 (PF6=IN4, PF7=IN5, PF8=IN6, PF9=IN7)
static const uint32_t adc3_channels[4] = {
    ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

uint16_t adc_raw(int ch)
{
    if (ch < ADC1_NUM_CH)
        return adc1_buf[ch];
    if (ch < ADC_NUM_CH)
        return adc3_buf[ch - ADC1_NUM_CH];
    return 2048;  // mid-range fallback
}

bool button_s3(void)
{
    return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_RESET;
}

bool button_s4(void)
{
    return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13) == GPIO_PIN_RESET;
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void adc_init(void)
{
    // --- Enable clocks ---
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC3_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    // --- Analog GPIO pins ---
    GPIO_InitTypeDef g = {};
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;

    // ADC1: PA0-PA3 (pots 1-4), PA6-PA7 (CV A-B)
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
            GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &g);

    // ADC1: PB0 (CV C), PB1 (CV D)
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &g);

    // ADC1: PC1 (CV X), PC4 (CV Y)
    g.Pin = GPIO_PIN_1 | GPIO_PIN_4;
    HAL_GPIO_Init(GPIOC, &g);

    // ADC3: PF6-PF9 (pots 5-8)
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOF, &g);

    // --- Button GPIO: S3 (PB12), S4 (PB13) — input with pull-up ---
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Pin  = GPIO_PIN_12 | GPIO_PIN_13;
    HAL_GPIO_Init(GPIOB, &g);

    // --- ADC common prescaler: /4 → 84 MHz APB2 / 4 = 21 MHz ---
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;

    // --- DMA2 Stream0 Channel0 for ADC1 ---
    hdma_adc1.Instance                 = DMA2_Stream0;
    hdma_adc1.Init.Channel             = DMA_CHANNEL_0;
    hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode                = DMA_CIRCULAR;
    hdma_adc1.Init.Priority            = DMA_PRIORITY_LOW;
    hdma_adc1.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_adc1);
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    // --- ADC1: continuous scan, 10 channels, DMA ---
    hadc1.Instance                   = ADC1;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = ENABLE;
    hadc1.Init.ContinuousConvMode    = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = ADC1_NUM_CH;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SEQ_CONV;
    HAL_ADC_Init(&hadc1);

    // Scan sequence — order must match the enum in adc.h
    static const struct { uint32_t channel; uint32_t rank; } adc1_seq[] = {
        { ADC_CHANNEL_0,  1  },  // PA0  POT1
        { ADC_CHANNEL_1,  2  },  // PA1  POT2
        { ADC_CHANNEL_2,  3  },  // PA2  POT3
        { ADC_CHANNEL_3,  4  },  // PA3  POT4
        { ADC_CHANNEL_6,  5  },  // PA6  CV_A
        { ADC_CHANNEL_7,  6  },  // PA7  CV_B
        { ADC_CHANNEL_8,  7  },  // PB0  CV_C
        { ADC_CHANNEL_9,  8  },  // PB1  CV_D
        { ADC_CHANNEL_11, 9  },  // PC1  CV_X
        { ADC_CHANNEL_14, 10 },  // PC4  CV_Y
    };

    ADC_ChannelConfTypeDef chcfg = {};
    chcfg.SamplingTime = ADC_SAMPLETIME_144CYCLES;
    for (int i = 0; i < ADC1_NUM_CH; i++) {
        chcfg.Channel = adc1_seq[i].channel;
        chcfg.Rank    = adc1_seq[i].rank;
        HAL_ADC_ConfigChannel(&hadc1, &chcfg);
    }

    // Start ADC1 continuous DMA
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_buf, ADC1_NUM_CH);

    // --- ADC3: single conversion, polled (no spare DMA stream) ---
    hadc3.Instance                   = ADC3;
    hadc3.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc3.Init.ScanConvMode          = DISABLE;
    hadc3.Init.ContinuousConvMode    = DISABLE;
    hadc3.Init.DiscontinuousConvMode = DISABLE;
    hadc3.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc3.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc3.Init.NbrOfConversion       = 1;
    hadc3.Init.DMAContinuousRequests = DISABLE;
    hadc3.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    HAL_ADC_Init(&hadc3);

    // Initial poll to populate adc3_buf before audio starts
    adc_poll();
}

// ---------------------------------------------------------------------------
// ADC3 polling — call from main loop (~1 ms cadence)
// 4 channels × ~8 µs each = ~32 µs total, negligible overhead.
// ---------------------------------------------------------------------------

void adc_poll(void)
{
    ADC_ChannelConfTypeDef chcfg = {};
    chcfg.SamplingTime = ADC_SAMPLETIME_144CYCLES;
    chcfg.Rank = 1;

    for (int i = 0; i < 4; i++) {
        chcfg.Channel = adc3_channels[i];
        HAL_ADC_ConfigChannel(&hadc3, &chcfg);
        HAL_ADC_Start(&hadc3);
        if (HAL_ADC_PollForConversion(&hadc3, 2) == HAL_OK) {
            adc3_buf[i] = (uint16_t)HAL_ADC_GetValue(&hadc3);
        }
        HAL_ADC_Stop(&hadc3);
    }
}
