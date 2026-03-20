// codec.cc — SAI1 + ADAU1961 driver for Ksoloti Big Genes (STM32F429)
//
// Ported from Ksoloti firmware (codec_ADAU1961_SAI.c, Johannes Taelman / Ksoloti)
// Rewritten as bare-metal STM32Cube HAL — no ChibiOS dependency.
//
// Hardware:
//   MCU  : STM32F429 @ 168 MHz, HSE = 8 MHz
//   Codec: ADAU1961, I2C2 addr 0x70, SAI1 Block A (TX slave) + Block B (RX sync slave)
//   MCLK : PA8 / MCO1 = HSE/1 = 8 MHz
//   SAI  : PE3 SD_B (RX), PE4 FS, PE5 SCK, PE6 SD_A (TX) — all AF6
//   DMA  : DMA2 Stream1 Ch0 (TX), Stream4 Ch1 (RX), int32_t double-buffer

#include "codec.h"
#include "stm32f4xx_hal.h"
#include <string.h>

// ---------------------------------------------------------------------------
// DMA audio buffers — regular SRAM (aligned for DMA)
// ---------------------------------------------------------------------------
int32_t buf[DOUBLE_BUFSIZE]  __attribute__((aligned(4)));
int32_t buf2[DOUBLE_BUFSIZE] __attribute__((aligned(4)));
int32_t rbuf[DOUBLE_BUFSIZE] __attribute__((aligned(4)));
int32_t rbuf2[DOUBLE_BUFSIZE] __attribute__((aligned(4)));

// ---------------------------------------------------------------------------
// I2C handle (I2C2: PB10=SCL, PB11=SDA, AF4, 400 kHz)
// ---------------------------------------------------------------------------
static I2C_HandleTypeDef hi2c2;

#define ADAU1961_ADDR  0x70u   // 8-bit I2C address (7-bit 0x38, shifted left)
#define I2C_TIMEOUT    10u     // ms

// ---------------------------------------------------------------------------
// ADAU1961 register helpers
// ---------------------------------------------------------------------------
static void adau_write(uint16_t reg, uint8_t val)
{
    uint8_t buf3[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val };
    HAL_I2C_Master_Transmit(&hi2c2, ADAU1961_ADDR, buf3, 3, I2C_TIMEOUT);
}

static void adau_write_block(uint16_t reg, const uint8_t *data, uint8_t len)
{
    // max 6 data bytes for PLL block write
    uint8_t pkt[8];
    pkt[0] = (uint8_t)(reg >> 8);
    pkt[1] = (uint8_t)(reg & 0xFF);
    memcpy(&pkt[2], data, len);
    HAL_I2C_Master_Transmit(&hi2c2, ADAU1961_ADDR, pkt, (uint16_t)(len + 2), I2C_TIMEOUT);
}

static uint8_t adau_read_byte(uint16_t reg)
{
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    uint8_t val = 0;
    HAL_I2C_Master_Transmit(&hi2c2, ADAU1961_ADDR, addr, 2, I2C_TIMEOUT);
    HAL_I2C_Master_Receive (&hi2c2, ADAU1961_ADDR | 1u, &val, 1, I2C_TIMEOUT);
    return val;
}

// ---------------------------------------------------------------------------
// System clock: 168 MHz from HSE=8 MHz, MCO1=HSE=8 MHz
// ---------------------------------------------------------------------------
static void clock_config(void)
{
    // Enable power controller so we can scale voltage
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitTypeDef osc = {};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    // HSE=8 MHz → VCO input=2 MHz (÷4) → VCO=336 MHz (×168) → SYSCLK=168 MHz (÷2)
    osc.PLL.PLLM = 4;
    osc.PLL.PLLN = 168;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;   // 48 MHz USB
    HAL_RCC_OscConfig(&osc);

    RCC_ClkInitTypeDef clk = {};
    clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                         RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;    // AHB  = 168 MHz
    clk.APB1CLKDivider = RCC_HCLK_DIV4;      // APB1 = 42 MHz  (I2C2, SAI1 on APB2)
    clk.APB2CLKDivider = RCC_HCLK_DIV2;      // APB2 = 84 MHz
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);

    // MCO1 = HSE / 1 = 8 MHz → PA8 (AF0) drives ADAU1961 MCLK
    HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_1);
}

// ---------------------------------------------------------------------------
// GPIO setup
// ---------------------------------------------------------------------------
static void gpio_config(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitTypeDef g = {};

    // PA8 — MCO1 (AF0), push-pull, MCLK to ADAU1961
    g.Pin       = GPIO_PIN_8;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF0_MCO;
    HAL_GPIO_Init(GPIOA, &g);

    // PB10=I2C2_SCL, PB11=I2C2_SDA — open-drain AF4
    g.Pin       = GPIO_PIN_10 | GPIO_PIN_11;
    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_NOPULL;   // external pull-ups on board
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOB, &g);

    // PE3=SAI1_SD_B (RX), PE4=SAI1_FS, PE5=SAI1_SCK, PE6=SAI1_SD_A (TX) — AF6
    g.Pin       = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOE, &g);
}

// ---------------------------------------------------------------------------
// I2C2 init (400 kHz, APB1=42 MHz)
// ---------------------------------------------------------------------------
static void i2c_config(void)
{
    __HAL_RCC_I2C2_CLK_ENABLE();

    hi2c2.Instance             = I2C2;
    hi2c2.Init.ClockSpeed      = 400000;
    hi2c2.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c2.Init.OwnAddress1     = 0x33 << 1;
    hi2c2.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c2);
}

// ---------------------------------------------------------------------------
// ADAU1961 register initialisation sequence
// Sourced directly from Ksoloti firmware (codec_ADAU1961_SAI.c).
// PLL bytes tuned for MCLK=8 MHz → 48 kHz (1024×FS core clock).
// ---------------------------------------------------------------------------
static void adau_init(void)
{
    // R0_CLKC: PLL clock source, 1024×FS, core disabled while PLL locks
    adau_write(0x4000, 0x0E);

    // R1_PLLC: PLL config — 6 bytes for MCLK=8 MHz → 32 kHz
    // M=125, N=12, R=4, X=1, fractional → core = 8*(4+12/125) = 32.768 MHz = 1024*32kHz
    const uint8_t pll[6] = { 0x00, 0x7D, 0x00, 0x0C, 0x21, 0x01 };
    adau_write_block(0x4002, pll, 6);

    // Wait for PLL lock (byte 5 bit 1 of R1_PLLC readback)
    uint32_t t0 = HAL_GetTick();
    while (!(adau_read_byte(0x4007) & 0x02)) {
        if (HAL_GetTick() - t0 > 200) break;  // 200 ms timeout; proceed anyway
    }

    // R0_CLKC: enable core, stay on PLL
    adau_write(0x4000, 0x0F);

    // Clear record mixer, volumes, ALC (R4–R14)
    for (uint16_t r = 0x400A; r <= 0x4014; r++) adau_write(r, 0x00);

    // R15_SERP0: codec is I2S master (generates BCLK / LRCLK for SAI slave)
    adau_write(0x4015, 0x01);
    // R16_SERP1: 32×fs BCLK (matches Ksoloti reference firmware)
    adau_write(0x4016, 0x00);
    // R17_CON0, R18_CON1
    adau_write(0x4017, 0x00);
    adau_write(0x4018, 0x00);
    // R19_ADCC: ADC config pre-enable, HPF off
    adau_write(0x4019, 0x00);
    // R20_LDVOL, R21_RDVOL: 0 dB digital volume
    adau_write(0x401A, 0x00);
    adau_write(0x401B, 0x00);
    // Clear playback mixers and output blocks (R22–R42)
    for (uint16_t r = 0x401C; r <= 0x4030; r++) adau_write(r, 0x00);

    // R34_POPCLICK: slew 42 ms to suppress power-on click
    adau_write(0x4028, 0x02);
    HAL_Delay(10);

    // Enable ADC and DAC — HPF disabled (bit4=0): avoids low-freq ringing
    adau_write(0x4019, 0x03);  // R19_ADCC: ADC L+R on, HPF off
    adau_write(0x402A, 0x03);  // R36_DACC0: DAC L+R on

    // Line outputs at 0 dB
    adau_write(0x4025, 0xE6);  // R31_PLLVOL: line out L, 0 dB, unmuted
    adau_write(0x4026, 0xE6);  // R32_PLRVOL: line out R, 0 dB, unmuted

    // Playback mixer routing: DAC → line out
    adau_write(0x401C, 0x21);  // R22_PMIXL0: DAC L unmuted
    adau_write(0x401E, 0x41);  // R24_PMIXR0: DAC R unmuted
    adau_write(0x4020, 0x05);  // R26_PLRML: Mixer5 enable, +6 dB (0x01 = muted)
    adau_write(0x4021, 0x11);  // R27_PLRMR: Mixer6 enable, +6 dB (0x01 = muted)

    // Output power management: L+R enabled
    adau_write(0x4029, 0x03);  // R35_PWRMGMT

    // Record mixer routing: line in → PGA → ADC
    adau_write(0x400A, 0x01);  // R4_RMIXL0: Mixer1 enable
    adau_write(0x400B, 0x08);  // R5_RMIXL1: PGA unmuted, 0 dB
    adau_write(0x400C, 0x01);  // R6_RMIXR0: Mixer2 enable
    adau_write(0x400D, 0x08);  // R7_RMIXR1: PGA unmuted, 0 dB
    adau_write(0x400E, 0x43);  // R8_LDIVOL: 0 dB input gain
    adau_write(0x400F, 0x43);  // R9_RDIVOL: 0 dB input gain

    // Capless headphone common-mode output
    adau_write(0x4027, 0x03);  // R33_PMONO: MONOM+MOMODE
    adau_write(0x4022, 0x01);  // R28_PLRMM: MX7EN
    adau_write(0x4023, 0xC3);  // R29_PHPLVOL: HP L, -12 dB
    adau_write(0x4024, 0xC3);  // R30_PHPRVOL: HP R, -12 dB
}

// ---------------------------------------------------------------------------
// SAI1 setup — Block A slave TX, Block B synchronous slave RX, 32-bit stereo
// ---------------------------------------------------------------------------
static void sai_config(void)
{
    __HAL_RCC_SAI1_CLK_ENABLE();

    // Frame: 64-bit frame (FRL=63), FS active for first 32 bits (FSALL=31), FSOFF=1
    const uint32_t frcr = SAI_xFRCR_FRL_0 | SAI_xFRCR_FRL_1 | SAI_xFRCR_FRL_2 |
                          SAI_xFRCR_FRL_3 | SAI_xFRCR_FRL_4 | SAI_xFRCR_FRL_5 |
                          SAI_xFRCR_FSALL_0 | SAI_xFRCR_FSALL_1 | SAI_xFRCR_FSALL_2 |
                          SAI_xFRCR_FSALL_3 | SAI_xFRCR_FSALL_4 |
                          SAI_xFRCR_FSOFF;

    // Slots: 2 slots (NBSLOT=1 means 2 slots), both active (SLOTEN bits [1:0] = 0x3)
    const uint32_t slotr = (0x3u << SAI_xSLOTR_SLOTEN_Pos) | SAI_xSLOTR_NBSLOT_0;

    SAI1_Block_A->FRCR  = frcr;
    SAI1_Block_B->FRCR  = frcr;
    SAI1_Block_A->SLOTR = slotr;
    SAI1_Block_B->SLOTR = slotr;

    // Block A: slave transmitter (MODE=10), 32-bit data (DS=111), DMA, CKSTR=1
    SAI1_Block_A->CR2 = 0;
    SAI1_Block_A->CR1 = SAI_xCR1_DS_0 | SAI_xCR1_DS_1 | SAI_xCR1_DS_2 |
                        SAI_xCR1_MODE_1 |
                        SAI_xCR1_DMAEN  |
                        SAI_xCR1_CKSTR;

    // Block B: synchronous slave receiver (MODE=11, SYNCEN=01), 32-bit, DMA, CKSTR=1
    SAI1_Block_B->CR2 = 0;
    SAI1_Block_B->CR1 = SAI_xCR1_DS_0 | SAI_xCR1_DS_1 | SAI_xCR1_DS_2 |
                        SAI_xCR1_SYNCEN_0 |
                        SAI_xCR1_MODE_1   | SAI_xCR1_MODE_0 |
                        SAI_xCR1_DMAEN    |
                        SAI_xCR1_CKSTR;
}

// ---------------------------------------------------------------------------
// DMA2 setup — double-buffer, 32-bit words
//   Stream1 Ch0 = SAI1_A TX (M→P)
//   Stream4 Ch1 = SAI1_B RX (P→M)
// ---------------------------------------------------------------------------
static void dma_config(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    // Clear audio buffers before arming DMA
    memset(buf,  0, sizeof(buf));
    memset(buf2, 0, sizeof(buf2));
    memset(rbuf, 0, sizeof(rbuf));
    memset(rbuf2,0, sizeof(rbuf2));

    // --- SAI1_A TX: DMA2 Stream1, Channel 0, Memory→Peripheral ---
    DMA2_Stream1->CR = 0;
    while (DMA2_Stream1->CR & DMA_SxCR_EN);

    DMA2->LIFCR = DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1 |
                  DMA_LIFCR_CTEIF1 | DMA_LIFCR_CDMEIF1 | DMA_LIFCR_CFEIF1;

    DMA2_Stream1->PAR  = (uint32_t)&SAI1_Block_A->DR;
    DMA2_Stream1->M0AR = (uint32_t)buf;
    DMA2_Stream1->M1AR = (uint32_t)buf2;
    DMA2_Stream1->NDTR = DOUBLE_BUFSIZE;
    DMA2_Stream1->FCR  = 0;  // Direct mode (no FIFO) — matches Ksoloti reference
    DMA2_Stream1->CR   =
        (0u << DMA_SxCR_CHSEL_Pos) |  // Channel 0
        DMA_SxCR_DBM               |  // Double-buffer mode
        DMA_SxCR_PL_0              |  // Priority medium
        DMA_SxCR_MSIZE_1           |  // Memory word = 32-bit
        DMA_SxCR_PSIZE_1           |  // Peripheral word = 32-bit
        DMA_SxCR_MINC              |  // Memory pointer increments
        (1u << DMA_SxCR_DIR_Pos)   |  // Memory→Peripheral
        DMA_SxCR_TCIE              |  // Transfer-complete IRQ (fires audio callback)
        DMA_SxCR_CIRC;                // Circular (required in DBM mode)

    // --- SAI1_B RX: DMA2 Stream4, Channel 1, Peripheral→Memory ---
    DMA2_Stream4->CR = 0;
    while (DMA2_Stream4->CR & DMA_SxCR_EN);

    DMA2->HIFCR = DMA_HIFCR_CTCIF4 | DMA_HIFCR_CHTIF4 |
                  DMA_HIFCR_CTEIF4 | DMA_HIFCR_CDMEIF4 | DMA_HIFCR_CFEIF4;

    DMA2_Stream4->PAR  = (uint32_t)&SAI1_Block_B->DR;
    DMA2_Stream4->M0AR = (uint32_t)rbuf;
    DMA2_Stream4->M1AR = (uint32_t)rbuf2;
    DMA2_Stream4->NDTR = DOUBLE_BUFSIZE;
    DMA2_Stream4->FCR  = 0;  // Direct mode (no FIFO) — matches Ksoloti reference
    DMA2_Stream4->CR   =
        (1u << DMA_SxCR_CHSEL_Pos) |  // Channel 1
        DMA_SxCR_DBM               |
        DMA_SxCR_PL_0              |
        DMA_SxCR_MSIZE_1           |
        DMA_SxCR_PSIZE_1           |
        DMA_SxCR_MINC              |
        (0u << DMA_SxCR_DIR_Pos)   |  // Peripheral→Memory
        DMA_SxCR_CIRC;

    // Enable DMA IRQ (TX stream only — RX is frame-synchronised to TX via SAI sync)
    HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);

    // Arm RX first, then TX, then enable SAI
    DMA2_Stream4->CR |= DMA_SxCR_EN;
    DMA2_Stream1->CR |= DMA_SxCR_EN;

    // Enable order: A (TX) then B (RX) — matches Ksoloti reference.
    // Block B is synchronous to A, so A must be running first.
    SAI1_Block_A->CR1 |= SAI_xCR1_SAIEN;
    SAI1_Block_B->CR1 |= SAI_xCR1_SAIEN;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
void codec_init(void)
{
    clock_config();
    gpio_config();
    i2c_config();
    adau_init();
    sai_config();
    dma_config();
}

// ---------------------------------------------------------------------------
// DMA ISR — fires every BUFSIZE frames (16 frames @ 48 kHz = ~333 µs)
//
// SAI1_A and SAI1_B are frame-synchronised; we read DMA2_Stream1->CR CT bit
// to determine which buffer pair is idle and safe to process.
//
// CT=1 → DMA switched to Memory1 (buf2/rbuf2); Memory0 (buf/rbuf) is idle.
// CT=0 → DMA switched to Memory0 (buf/rbuf);  Memory1 (buf2/rbuf2) is idle.
// ---------------------------------------------------------------------------
extern "C" void DMA2_Stream1_IRQHandler(void)
{
    if (DMA2->LISR & DMA_LISR_TCIF1) {
        DMA2->LIFCR = DMA_LIFCR_CTCIF1;

        // Buffer mapping matches Ksoloti reference: RX DMA is one buffer
        // ahead of TX DMA, so when TX CT=1, the most recently completed
        // RX buffer is rbuf2 (M1), not rbuf (M0).
        if (DMA2_Stream1->CR & DMA_SxCR_CT) {
            computebufI(rbuf2, buf);
        } else {
            computebufI(rbuf, buf2);
        }
    }
}
