// Noise test firmware for Boy Rev A (Teensy 4.1 on the analogue board Rev A), temporary: for the
// noise comparison in eurorack_electronics/docs/noise_test.md. Reflash Boy's own firmware
// (env teensy41) after.
//
// The schedule is noise_schedule.h, the same as the Daisy boards'. Only audio and the screen run:
// board::begin() sets the board up as Boy does (jack switches off, DAC at 0 V, gates latched),
// then nothing polls the encoder, CV ADC or I2C expander, so the screen is the only bus traffic.
//
// The screen is Boy's 128x32 SSD1306 at 0x3C on Wire, 400 kHz, through the Adafruit library as
// Boy drives it. Off = never started, or display off and charge pump off; still = a 50%
// checkerboard drawn once; redrawing = it and its inverse alternately, a whole frame every 33 ms.

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>

#include "../teensy_move/board.h"
#include "noise_stream.h"

static noise::Player player;
static Adafruit_SSD1306 oled(128, 32, &Wire, -1, 400000, 400000);
static bool started = false, phase = false;
static noise::Screen shown = noise::OFF;
static uint32_t last_frame = 0;

static void frame(bool ph) {
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 128; x++) oled.drawPixel(x, y, ((x + y + ph) & 1) ? SSD1306_WHITE : SSD1306_BLACK);
  oled.display();
}

void setup() {
  if (!board::begin()) Serial.println("noise test: board::begin() failed (no EEPROM answer)");
  player.Begin(true);
  // Audio objects after board::begin(), as Boy's modes make them: begin() returns every board pin,
  // the I2S ones among them, to an input first.
  AudioMemory(8);
  AudioInputI2S *in = new AudioInputI2S();
  NoiseStream *ns = new NoiseStream(&player);
  AudioOutputI2S *out = new AudioOutputI2S();
  new AudioConnection(*in, 0, *ns, 0);
  new AudioConnection(*in, 1, *ns, 1);
  new AudioConnection(*ns, 0, *out, 0);
  new AudioConnection(*ns, 1, *out, 1);
}

void loop() {
  noise::Screen want = player.screen;
  if (want != shown) {
    if (want == noise::OFF) {
      if (started) {
        oled.ssd1306_command(SSD1306_DISPLAYOFF);
        oled.ssd1306_command(SSD1306_CHARGEPUMP);
        oled.ssd1306_command(0x10);
      }
    } else {
      if (!started) {
        oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // its init ends display-on
        started = true;
      } else {
        oled.ssd1306_command(SSD1306_CHARGEPUMP);
        oled.ssd1306_command(0x14);
        oled.ssd1306_command(SSD1306_DISPLAYON);
      }
      phase = false;
      frame(phase);
      last_frame = millis();
    }
    shown = want;
  }
  if (shown == noise::REDRAW && millis() - last_frame >= 33) {
    last_frame = millis();
    phase = !phase;
    frame(phase);
  }
}
