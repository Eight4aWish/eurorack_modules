#pragma once
#include <stdint.h>
#include <stddef.h>

// Forward declarations so we don't hard-depend on a specific OLED lib.
class Adafruit_SSD1306;

namespace eurorack_ui {

// Simple, legible, NO font scaling. Bold = inverse box.
struct MenuItem {
  const char* label;         // short label (<= 10 chars ideal)
  float value01;             // 0..1 for bar rendering
};

class OledMenu {
public:
  OledMenu(Adafruit_SSD1306* oled, uint8_t cols = 16, uint8_t rows = 4);
  void attach(Adafruit_SSD1306* oled);
  void setItems(MenuItem* items, size_t count);
  void setSelected(size_t idx);
  void setTitle(const char* title);      // optional
  void draw();                           // call at ~30–60 Hz
  void sleep(bool on);                   // for noise reduction
private:
  void drawTitle_();
  void drawRow_(uint8_t row, const MenuItem& it, bool selected);
  void drawBar_(int16_t x, int16_t y, int16_t w, int16_t h, float v);
  Adafruit_SSD1306* oled_;
  MenuItem* items_;
  size_t count_;
  size_t selected_;
  const char* title_;
  uint8_t cols_, rows_;
  bool sleeping_ = false;
};

} // namespace eurorack_ui
