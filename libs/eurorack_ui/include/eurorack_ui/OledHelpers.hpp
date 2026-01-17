#pragma once
#include <Adafruit_SSD1306.h>

namespace eurorack_ui {

// Print a clipped string to fit in a pixel width `w` at (x,y).
void printClipped(Adafruit_SSD1306& oled, int x, int y, int w, const char* s);

// Same as printClipped but optionally draw a bold effect (simple offset redraw).
void printClippedBold(Adafruit_SSD1306& oled, int x, int y, int w, const char* s, bool bold=false);

// Print a short label (no numerals) clipped to fit
void printLabelOnly(Adafruit_SSD1306& oled, int x, int y, int w, const char* s);

// Draw a horizontal value bar (0..1) with optional inversion (filled background)
void drawBar(Adafruit_SSD1306& oled, int x, int y, int w, int h, float v, bool invert=false);

} // namespace eurorack_ui
