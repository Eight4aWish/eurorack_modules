#include "eurorack_ui/OledHomeMenu.hpp"
#include <cstring>

namespace eurorack_ui {

OledHomeMenu::OledHomeMenu() {}

void OledHomeMenu::begin(Adafruit_SSD1306* oled, uint8_t top_margin){
  oled_ = oled;
  top_margin_ = top_margin;
  dirty_ = true;
}

void OledHomeMenu::setItems(const char** items, uint8_t count){
  items_ = items; count_ = count; index_ = 0; dirty_ = true;
}

void OledHomeMenu::draw(){
  if(!oled_ || !items_ || count_ == 0) return;
  if(!dirty_) return;
  dirty_ = false;

  oled_->clearDisplay();
  oled_->setTextSize(1);
  oled_->setTextColor(SSD1306_WHITE);

  // Title always drawn at the very top; `top_margin_` reserves space below it
  oled_->setCursor(0, 0);
  oled_->println(F("Home"));

  // Two-column layout when more than 3 items (up to 2 columns)
  uint8_t cols = (count_ > 3) ? 2 : 1;
  uint8_t rows = (count_ + cols - 1) / cols;
  int cellW = oled_->width() / cols;
  for(uint8_t i=0;i<count_;++i){
    uint8_t col = i / rows;
    uint8_t row = i % rows;
    int x = col * cellW + 2;
    // start items at top_margin_ so the top band remains above the list; use 10px grid
    int y = top_margin_ + row * 10;
    if(i == index_){
      oled_->fillRect(col * cellW, y, cellW, 10, SSD1306_WHITE);
      oled_->setTextColor(SSD1306_BLACK);
      oled_->setCursor(x, y+1);
      oled_->print(items_[i]);
      oled_->setTextColor(SSD1306_WHITE);
    } else {
      oled_->setCursor(x, y+1);
      oled_->print(items_[i]);
    }
  }

  oled_->display();
}

void OledHomeMenu::next(){ if(count_==0) return; index_ = (index_ + 1) % count_; dirty_ = true; draw(); }
void OledHomeMenu::prev(){ if(count_==0) return; index_ = (index_ + count_ - 1) % count_; dirty_ = true; draw(); }

uint8_t OledHomeMenu::commit(){ dirty_ = true; draw(); return index_; }

void OledHomeMenu::invalidate(){ dirty_ = true; }

} // namespace eurorack_ui
