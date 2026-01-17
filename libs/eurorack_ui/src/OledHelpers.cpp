#include "eurorack_ui/OledHelpers.hpp"
#include <cstring>

namespace eurorack_ui {

void printClipped(Adafruit_SSD1306& oled, int x, int y, int w, const char* s){
  // approximate char width for default font: 6 px
  int maxChars = (w - 10) / 6;
  if (maxChars <= 0) return;
  int n = 0;
  while (s[n] && n < maxChars) ++n;
  char buf[64];
  if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
  memcpy(buf, s, n); buf[n] = 0;
  oled.setCursor(x, y);
  oled.print(buf);
}

void printClippedBold(Adafruit_SSD1306& oled, int x, int y, int w, const char* s, bool bold){
  printClipped(oled, x, y, w, s);
  if (bold){ oled.setCursor(x+1, y); oled.print(s); }
}

void printLabelOnly(Adafruit_SSD1306& oled, int x, int y, int w, const char* s){
  int maxChars = (w - 10) / 6;
  if (maxChars <= 0) return;
  int n = 0; while (s[n] && n < maxChars) ++n;
  char buf[64]; if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
  memcpy(buf, s, n); buf[n] = 0;
  oled.setCursor(x, y);
  oled.print(buf);
}

void drawBar(Adafruit_SSD1306& oled, int x, int y, int w, int h, float v, bool invert){
  if (w <= 2 || h <= 0) return;
  float vv = v;
  if (vv < 0.f) vv = 0.f; else if (vv > 1.f) vv = 1.f;
  int fill = (int)((w - 2) * vv + 0.5f);
  if (invert) {
    oled.fillRect(x, y, w, h, SSD1306_WHITE);
    if (fill > 0) oled.fillRect(x+1, y+1, fill, h-2, SSD1306_BLACK);
  } else {
    oled.drawRect(x, y, w, h, SSD1306_WHITE);
    if (fill > 0) oled.fillRect(x+1, y+1, fill, h-2, SSD1306_WHITE);
  }
}

} // namespace eurorack_ui
