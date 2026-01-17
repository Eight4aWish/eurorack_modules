#pragma once
#include <Adafruit_SSD1306.h>
#include <stdint.h>

namespace eurorack_ui {

class OledHomeMenu {
public:
  OledHomeMenu();
  // attach display (must be valid for lifetime)
  void begin(Adafruit_SSD1306* oled, uint8_t top_margin = 12);
  // set items (pointer must remain valid or be static)
  void setItems(const char** items, uint8_t count);
  // draw the menu to the display
  void draw();
  // force next draw even if not dirty
  void invalidate();
  // navigation
  void next();
  void prev();
  // commit selection: returns index of selected item
  uint8_t commit();
  // get currently selected index
  uint8_t selected() const { return index_; }

private:
  Adafruit_SSD1306* oled_ = nullptr;
  const char** items_ = nullptr;
  uint8_t count_ = 0;
  uint8_t index_ = 0;
  bool dirty_ = true;
  uint8_t top_margin_ = 12;
};

} // namespace eurorack_ui
