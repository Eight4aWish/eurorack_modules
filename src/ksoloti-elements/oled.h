// oled.h — SH1106 128x64 OLED driver for Ksoloti Big Genes
//
// I2C1 on PB8 (SCL) / PB9 (SDA), 400 kHz, address 0x3C.
// Uses a 1024-byte framebuffer with page-at-a-time update
// to avoid blocking the audio ISR.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialise I2C1 and the SH1106 display.
void oled_init(void);

// Push one page (128 bytes) to the display.
// Call once per main-loop tick; cycles through pages 0-7 automatically.
// Full refresh completes every 8 calls (~8 ms at 1 kHz loop).
void oled_update(void);

// Clear the entire framebuffer.
void oled_clear(void);

// Draw a single character at pixel position (x, y). 5x7 font, 6px wide.
void oled_char(int x, int y, char c);

// Draw a null-terminated string at pixel position (x, y).
void oled_str(int x, int y, const char* s);

// Draw a filled horizontal bar at pixel position (x, y), w pixels wide,
// h pixels tall, filled to 'fill' fraction (0.0 .. 1.0).
void oled_bar(int x, int y, int w, int h, float fill);

// Set or clear a single pixel.
void oled_pixel(int x, int y, int on);

// Draw a horizontal line.
void oled_hline(int x, int y, int w);

// Access the raw framebuffer (1024 bytes, page-major: 8 pages x 128 cols).
uint8_t* oled_framebuffer(void);

#ifdef __cplusplus
}
#endif
