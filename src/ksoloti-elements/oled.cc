// oled.cc — SH1106 128x64 OLED driver (I2C1, PB8/PB9)
//
// Page-at-a-time update: call oled_update() from main loop.
// Full refresh in 8 calls (~8 ms total at 400 kHz I2C).

#include "oled.h"
#include "font5x7.h"
#include "stm32f4xx_hal.h"

// SH1106 I2C address (7-bit 0x3C, HAL wants 8-bit left-shifted)
#define SH1106_ADDR  (0x3C << 1)

// Display geometry
#define OLED_W  128
#define OLED_H  64
#define PAGES   (OLED_H / 8)

// SH1106 has 132-column RAM but 128-column display — offset by 2
#define COL_OFFSET 2

static I2C_HandleTypeDef hi2c1;
static uint8_t fb[PAGES * OLED_W];  // 1024-byte framebuffer
static int current_page = 0;

// --- Low-level I2C helpers ---

static void oled_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };  // Co=0, D/C#=0 (command)
    HAL_I2C_Master_Transmit(&hi2c1, SH1106_ADDR, buf, 2, 10);
}

static void oled_send_page(int page)
{
    // Set page address
    oled_cmd(0xB0 | page);
    // Set column address (low nibble, high nibble) with SH1106 offset
    oled_cmd(0x00 | (COL_OFFSET & 0x0F));
    oled_cmd(0x10 | (COL_OFFSET >> 4));

    // Send 128 bytes of pixel data for this page
    // I2C data write: 0x40 prefix byte then 128 data bytes
    uint8_t buf[1 + OLED_W];
    buf[0] = 0x40;  // Co=0, D/C#=1 (data)
    for (int i = 0; i < OLED_W; i++) {
        buf[1 + i] = fb[page * OLED_W + i];
    }
    HAL_I2C_Master_Transmit(&hi2c1, SH1106_ADDR, buf, 1 + OLED_W, 10);
}

// --- Public API ---

void oled_init(void)
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

    // Clear framebuffer and push all pages
    oled_clear();
    for (int p = 0; p < PAGES; p++) {
        oled_send_page(p);
    }
}

void oled_update(void)
{
    oled_send_page(current_page);
    current_page = (current_page + 1) % PAGES;
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
