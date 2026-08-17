// oled.cc — SH1106 128x64 OLED driver (I2C1, PB8/PB9)
//
// Page-at-a-time update: call oled_update() from main loop.
// Full refresh in 8 calls (~8 ms total at 400 kHz I2C).
//
// Every transfer is checked. An abandoned transfer can leave the SH1106 mid-byte with
// SDA held low, which jams the bus for good: the peripheral reports BUSY for ever after
// and the screen is dead until the module is power-cycled. Since the transfers are
// polled, and the audio ISR has priority, a heavy DSP load is enough to starve one past
// its timeout and trigger exactly that. So a failure now unjams the bus and re-inits the
// display instead of being discarded, and oled_fault_count() records that it happened.

#include "oled.h"
#include "font5x7.h"
#include "stm32f4xx_hal.h"
#include <string.h>

// SH1106 I2C address (7-bit 0x3C, HAL wants 8-bit left-shifted)
#define SH1106_ADDR  (0x3C << 1)

// Display geometry
#define OLED_W  128
#define OLED_H  64
#define PAGES   (OLED_H / 8)

// SH1106 has 132-column RAM but 128-column display — offset by 2
#define COL_OFFSET 2

// Per-transfer timeout, measured by the HAL across the whole call rather than per byte.
// A page is 130 bytes once the address and control byte are counted; at 400 kHz that is
// 2.9 ms before the audio ISR steals any time, so anything near 3 ms abandons every
// transfer mid-page - which is the very thing that jams the bus. 10 ms is the proven
// value and there is no longer any reason to shave it: the buttons moved to the audio
// ISR, so a slow pass through the main loop no longer costs playability.
#define OLED_I2C_TIMEOUT  10u

// How long to leave a jammed bus alone before trying to recover it. Recovery clocks the
// bus by hand and re-runs the init sequence, so hammering it every pass would be worse
// than the fault.
#define OLED_RETRY_MS     250u

static I2C_HandleTypeDef hi2c1;
static uint8_t fb[PAGES * OLED_W];  // 1024-byte framebuffer
static int current_page = 0;

// What the display is believed to be showing. A page is only sent when it differs, so
// a screen that is not changing puts nothing on the bus - and a transfer that never
// happens cannot jam. fb_force counts pages that must go out regardless, used after a
// re-init when the display's real contents are unknown.
static uint8_t fb_sent[PAGES * OLED_W];
static int     fb_force = PAGES;

static uint32_t oled_faults = 0;      // recovered jams, shown in the UI
static bool     oled_jammed = false;  // bus is known bad; leave it alone until retry_at
static uint32_t oled_retry_at = 0;

static void oled_hw_init(void);
static void oled_bus_recover(void);

// --- Low-level I2C helpers ---

static bool oled_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };  // Co=0, D/C#=0 (command)
    return HAL_I2C_Master_Transmit(&hi2c1, SH1106_ADDR, buf, 2, OLED_I2C_TIMEOUT)
           == HAL_OK;
}

// Rough microsecond delay for the manual bus-clocking in oled_bus_recover(). It only has
// to be slow enough to stay inside the I2C spec, so a calibrated spin is fine and avoids
// depending on DWT, which is not started until after oled_init() runs.
static void oled_delay_us(uint32_t us)
{
    volatile uint32_t n = us * 28u;   // ~6 cycles an iteration at 168 MHz
    while (n--) { __NOP(); }
}

static bool oled_send_page(int page)
{
    // Set page address
    if (!oled_cmd(0xB0 | page)) return false;
    // Set column address (low nibble, high nibble) with SH1106 offset
    if (!oled_cmd(0x00 | (COL_OFFSET & 0x0F))) return false;
    if (!oled_cmd(0x10 | (COL_OFFSET >> 4))) return false;

    // Send 128 bytes of pixel data for this page
    // I2C data write: 0x40 prefix byte then 128 data bytes
    uint8_t buf[1 + OLED_W];
    buf[0] = 0x40;  // Co=0, D/C#=1 (data)
    for (int i = 0; i < OLED_W; i++) {
        buf[1 + i] = fb[page * OLED_W + i];
    }
    return HAL_I2C_Master_Transmit(&hi2c1, SH1106_ADDR, buf, 1 + OLED_W,
                                   OLED_I2C_TIMEOUT) == HAL_OK;
}

// Release the bus from a slave that is holding SDA low.
//
// Nine clock pulses is one byte plus its ACK slot - enough for any slave to finish the
// byte it believed it was in the middle of and let go of the line. Then a manual STOP
// puts it back at a known idle, and the peripheral and display are re-initialised.
static void oled_bus_recover(void)
{
    HAL_I2C_DeInit(&hi2c1);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {};
    g.Pin   = GPIO_PIN_8 | GPIO_PIN_9;
    g.Mode  = GPIO_MODE_OUTPUT_OD;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);  // both released
    oled_delay_us(10);

    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
        oled_delay_us(5);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
        oled_delay_us(5);
    }

    // STOP: SDA low while SCL is high, then release SDA.
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
    oled_delay_us(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
    oled_delay_us(5);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
    oled_delay_us(5);

    oled_hw_init();
}

// --- Public API ---

// Peripheral and display configuration only - deliberately does not touch the
// framebuffer, so recovery can re-run it and then re-send the picture already in hand.
static void oled_hw_init(void)
{
    // Enable I2C1 and GPIOB clocks
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // PB8 = SCL, PB9 = SDA — AF4 (I2C1)
    GPIO_InitTypeDef g = {};
    g.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &g);

    // I2C1 at 400 kHz (fast mode)
    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 400000;
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);

    // Short delay for display power-up
    HAL_Delay(20);

    // SH1106 init sequence
    oled_cmd(0xAE);  // Display OFF
    oled_cmd(0xD5);  // Set display clock
    oled_cmd(0x80);  //   default ratio
    oled_cmd(0xA8);  // Set multiplex ratio
    oled_cmd(0x3F);  //   64 lines
    oled_cmd(0xD3);  // Set display offset
    oled_cmd(0x00);  //   no offset
    oled_cmd(0x40);  // Set start line = 0
    oled_cmd(0x8D);  // Charge pump (SSD1306 compat — SH1106 usually has external)
    oled_cmd(0x14);  //   enable
    oled_cmd(0xA1);  // Segment remap (flip horizontal)
    oled_cmd(0xC8);  // COM scan direction (flip vertical)
    oled_cmd(0xDA);  // Set COM pins
    oled_cmd(0x12);  //   alternative, no remap
    oled_cmd(0x81);  // Set contrast
    oled_cmd(0xCF);  //   high
    oled_cmd(0xD9);  // Set pre-charge period
    oled_cmd(0xF1);  //   phase1=1, phase2=15
    oled_cmd(0xDB);  // Set VCOMH deselect level
    oled_cmd(0x40);  //   ~0.77 x Vcc
    oled_cmd(0xA4);  // Entire display ON (follow RAM)
    oled_cmd(0xA6);  // Normal display (not inverted)
    oled_cmd(0xAF);  // Display ON
}

void oled_init(void)
{
    oled_hw_init();

    // Clear framebuffer and push all pages
    oled_clear();
    bool ok = true;
    for (int p = 0; p < PAGES; p++) {
        if (!oled_send_page(p)) { ok = false; break; }
    }

    if (ok) {
        memcpy(fb_sent, fb, sizeof(fb_sent));
        fb_force = 0;
    } else {
        // A display that will not take its first frame is in exactly the state
        // oled_update() knows how to dig out of, so hand it over rather than starting
        // up believing the screen is fine. fb_force is left at PAGES: nothing is on
        // the display, so everything has to go out once the bus is back.
        oled_faults++;
        oled_jammed = true;
        oled_retry_at = HAL_GetTick() + OLED_RETRY_MS;
    }
}

uint32_t oled_fault_count(void)
{
    return oled_faults;
}

void oled_update(void)
{
    if (oled_jammed) {
        // Leave a jammed bus completely alone between attempts: recovery clocks the bus
        // by hand and re-runs the init sequence, which is far too expensive to repeat
        // every pass, and a display that is genuinely dead would otherwise bog the whole
        // loop down - taking the pots and buttons with it.
        if ((int32_t)(HAL_GetTick() - oled_retry_at) < 0) return;
        oled_bus_recover();
        oled_jammed = false;
        fb_force = PAGES;    // the display's contents are unknown after a re-init
    }

    // One page per call at most, and only if it has changed.
    for (int n = 0; n < PAGES; n++) {
        const int page = current_page;
        current_page = (current_page + 1) % PAGES;

        if (fb_force == 0 &&
            memcmp(&fb[page * OLED_W], &fb_sent[page * OLED_W], OLED_W) == 0) {
            continue;
        }

        if (oled_send_page(page)) {
            memcpy(&fb_sent[page * OLED_W], &fb[page * OLED_W], OLED_W);
            if (fb_force) fb_force--;
        } else {
            oled_faults++;
            oled_jammed = true;
            oled_retry_at = HAL_GetTick() + OLED_RETRY_MS;
        }
        return;
    }
}

void oled_clear(void)
{
    for (int i = 0; i < PAGES * OLED_W; i++) {
        fb[i] = 0;
    }
}

void oled_pixel(int x, int y, int on)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    int page = y / 8;
    int bit  = y % 8;
    if (on)
        fb[page * OLED_W + x] |=  (1 << bit);
    else
        fb[page * OLED_W + x] &= ~(1 << bit);
}

void oled_hline(int x, int y, int w)
{
    for (int i = 0; i < w; i++) {
        oled_pixel(x + i, y, 1);
    }
}

void oled_char(int x, int y, char c)
{
    if (c < 32 || c > 126) c = ' ';
    const uint8_t* glyph = font5x7[c - 32];

    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                oled_pixel(x + col, y + row, 1);
            }
        }
    }
}

void oled_str(int x, int y, const char* s)
{
    while (*s) {
        oled_char(x, y, *s);
        x += 6;  // 5px glyph + 1px spacing
        s++;
    }
}

void oled_str_inv(int x, int y, const char* s)
{
    while (*s) {
        // Fill 6x8 white block, then punch out glyph pixels
        for (int col = 0; col < 6; col++)
            for (int row = 0; row < 8; row++)
                oled_pixel(x + col, y + row, 1);

        if (*s >= 32 && *s <= 126) {
            const uint8_t* glyph = font5x7[*s - 32];
            for (int col = 0; col < 5; col++) {
                uint8_t bits = glyph[col];
                for (int row = 0; row < 7; row++) {
                    if (bits & (1 << row))
                        oled_pixel(x + col, y + row, 0);
                }
            }
        }
        x += 6;
        s++;
    }
}

void oled_bar(int x, int y, int w, int h, float fill)
{
    if (fill < 0.0f) fill = 0.0f;
    if (fill > 1.0f) fill = 1.0f;
    int filled = (int)(fill * w);

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            oled_pixel(x + col, y + row, col < filled ? 1 : 0);
        }
    }
}

uint8_t* oled_framebuffer(void)
{
    return fb;
}
