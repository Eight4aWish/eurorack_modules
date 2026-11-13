// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// The small SSD1306 on the Rev B Computer panel: one cell wide, so a 0.49" 64x32 or a 0.66"
// 64x48. Both sit in the middle of the controller's 128 columns, hence the column offset. A
// frame buffer, and a service() that sends one changed 16-byte piece per call, so no pass of the
// background loop waits more than about half a millisecond on the I2C bus it shares with the
// expander and the EEPROM.

#pragma once

#include <stdint.h>
#include <string.h>

#include "hardware/i2c.h"
#include "../../ksoloti_elements/font5x7.h"

#ifndef REVB_SCREEN_W
#define REVB_SCREEN_W 64
#endif
#ifndef REVB_SCREEN_H
#define REVB_SCREEN_H 32
#endif
#ifndef REVB_SCREEN_COL_OFFSET
#define REVB_SCREEN_COL_OFFSET 32
#endif
#ifndef REVB_SCREEN_ADDR
#define REVB_SCREEN_ADDR 0x3C
#endif

namespace revb {

class Screen {
 public:
  static constexpr int W = REVB_SCREEN_W, H = REVB_SCREEN_H, PAGES = H / 8;
  static constexpr int PIECE = 16, PIECES = W / PIECE;

  bool begin(i2c_inst_t *bus) {
    i2c_ = bus;
    static const uint8_t init[] = {
        0xAE,                    // display off
        0xD5, 0x80,              // clock divide
        0xA8, (uint8_t)(H - 1),  // multiplex
        0xD3, 0x00,              // display offset
        0x40,                    // start line 0
        0x8D, 0x14,              // charge pump on
        0x20, 0x00,              // horizontal addressing
        0xA1, 0xC8,              // segment remap, COM scan reversed
        0xDA, 0x12,              // COM pins: alternative
        0x81, 0xCF,              // contrast
        0xD9, 0xF1,              // pre-charge
        0xDB, 0x40,              // VCOMH
        0xA4, 0xA6,              // RAM content, not inverted
        0xAF};                   // display on
    for (uint8_t c : init)
      if (!command(c)) return false;
    memset(fb_, 0, sizeof(fb_));
    memset(dirty_, 0xFF, sizeof(dirty_));
    return true;
  }

  void clear() { memset(fb_, 0, sizeof(fb_)); }

  void pixel(int x, int y, bool on) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    uint8_t &b = fb_[(y >> 3) * W + x];
    if (on)
      b |= 1 << (y & 7);
    else
      b &= ~(1 << (y & 7));
  }

  void rect(int x, int y, int w, int h, bool fill) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++)
        if (fill || i == 0 || j == 0 || i == w - 1 || j == h - 1) pixel(x + i, y + j, true);
  }

  // 5x7 characters in 6x8 cells. Text is clipped at the right edge.
  void text(int x, int y, const char *s) {
    for (; *s && x < W; s++, x += 6) {
      char c = *s;
      if (c < 32 || c > 126) c = '?';
      for (int col = 0; col < 5; col++) {
        uint8_t bits = font5x7[c - 32][col];
        for (int row = 0; row < 7; row++)
          if (bits & (1 << row)) pixel(x + col, y + row, true);
      }
    }
  }

  // Compare the frame just drawn with what the panel shows and mark the pieces that differ
  void commit() {
    for (int p = 0; p < PAGES; p++)
      for (int k = 0; k < PIECES; k++) {
        const uint8_t *a = fb_ + p * W + k * PIECE, *b = shown_ + p * W + k * PIECE;
        if (memcmp(a, b, PIECE) != 0) dirty_[p] |= 1 << k;
      }
  }

  // Send at most one changed piece. True if anything went out.
  bool service() {
    for (int p = 0; p < PAGES; p++) {
      if (!dirty_[p]) continue;
      int k = __builtin_ctz(dirty_[p]);
      int col = REVB_SCREEN_COL_OFFSET + k * PIECE;
      const uint8_t addr[] = {0x00, 0x21, (uint8_t)col, (uint8_t)(col + PIECE - 1), 0x22, (uint8_t)p, (uint8_t)p};
      if (i2c_write_timeout_us(i2c_, REVB_SCREEN_ADDR, addr, sizeof(addr), false, 2000) != (int)sizeof(addr)) {
        dirty_[p] = 0;  // no screen fitted: stop trying
        return false;
      }
      uint8_t data[1 + PIECE];
      data[0] = 0x40;
      memcpy(data + 1, fb_ + p * W + k * PIECE, PIECE);
      i2c_write_timeout_us(i2c_, REVB_SCREEN_ADDR, data, sizeof(data), false, 4000);
      memcpy(shown_ + p * W + k * PIECE, fb_ + p * W + k * PIECE, PIECE);
      dirty_[p] &= ~(1 << k);
      return true;
    }
    return false;
  }

 private:
  bool command(uint8_t c) {
    const uint8_t b[2] = {0x00, c};
    return i2c_write_timeout_us(i2c_, REVB_SCREEN_ADDR, b, 2, false, 2000) == 2;
  }

  i2c_inst_t *i2c_ = nullptr;
  uint8_t fb_[W * PAGES] = {};
  uint8_t shown_[W * PAGES] = {};
  uint8_t dirty_[PAGES] = {};
};

}  // namespace revb
