#include "eurorack_ui/OledMenu.hpp"
#include <Adafruit_SSD1306.h>   // concrete at compile time

namespace eurorack_ui {

OledMenu::OledMenu(Adafruit_SSD1306* oled, uint8_t cols, uint8_t rows)
: oled_(oled), items_(nullptr), count_(0), selected_(0), title_(nullptr), cols_(cols), rows_(rows) {}

void OledMenu::attach(Adafruit_SSD1306* oled) { oled_ = oled; }
void OledMenu::setItems(MenuItem* items, size_t count){ items_ = items; count_ = count; }
void OledMenu::setSelected(size_t idx){ if(idx < count_) selected_ = idx; }
void OledMenu::setTitle(const char* t){ title_ = t; }
void OledMenu::sleep(bool on){
  sleeping_ = on;
  if (oled_) { if (on) oled_->ssd1306_command(SSD1306_DISPLAYOFF);
               else     oled_->ssd1306_command(SSD1306_DISPLAYON); }
}

void OledMenu::draw(){
  if(!oled_ || sleeping_) return;
  oled_->clearDisplay();
  drawTitle_();
  if(!items_ || count_ == 0){ oled_->display(); return; }

  // Row layout: align to standard Y grid starting at 16
  int y = (title_ ? 16 : 0);
  for(size_t i=0; i<count_ && i<rows_; ++i){
    bool sel = (i == selected_);
    drawRow_(y, items_[i], sel);
    y += 10; // line height for standard grid
  }
  oled_->display();
}

void OledMenu::drawTitle_(){
  if(!title_) return;
  oled_->setTextSize(1);
  oled_->setTextColor(SSD1306_WHITE);
  oled_->setCursor(0, 0);
  oled_->println(title_);
  // thin separator
  oled_->drawLine(0, 14, oled_->width()-1, 14, SSD1306_WHITE);
}

void OledMenu::drawRow_(uint8_t y, const MenuItem& it, bool sel){
  const int h = 10;
  // "Bold" via inverse box behind the label
  if(sel) oled_->fillRect(0, y, 64, h, SSD1306_WHITE);
  oled_->setTextSize(1);
  oled_->setCursor(2, y+1);
  if(sel) oled_->setTextColor(SSD1306_BLACK);
  else    oled_->setTextColor(SSD1306_WHITE);
  oled_->print(it.label ? it.label : "-");
  // Value bar on right
  drawBar_(68, y+1, oled_->width()-70, h-2, it.value01);
}

void OledMenu::drawBar_(int16_t x, int16_t y, int16_t w, int16_t h, float v){
  if(w <= 2) return;
  oled_->drawRect(x, y, w, h, SSD1306_WHITE);
  int fill = (int)((w-2) * (v < 0 ? 0 : v > 1 ? 1 : v));
  if(fill > 0) oled_->fillRect(x+1, y+1, fill, h-2, SSD1306_WHITE);
}

} // namespace eurorack_ui
