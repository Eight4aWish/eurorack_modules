// Noise test firmware for the Teensy Chaos (Teensy 4.1 on a Teensy Audio Shield, SGTL5000;
// 128x64 SSD1306 on SPI), temporary: the output noise with the screen off, still and redrawing,
// as the five boards in eurorack_electronics/docs/noise_test.md. Reflash env teensy_chaos after.
//
// The schedule is noise_schedule.h, the same as every other board's. The Chaos has no audio
// input, so its "through" segments carry only the codec's idle line-in; the analysis reads the
// output-only segments. The codec is set as src/teensy_chaos/main.cpp sets it (line in 5,
// volume 0.7, line out 13), at 48 kHz like the other boards (Teensy's driver hard-codes 44.1,
// so SYS_FS is set too). Only audio and the screen run: no CV scanning (ADS1115 on Wire1) and
// no CV-out updates (MCP4822), whose chip select is held high, as is pin 10's Audio Shield SD
// card, which shares the screen's chip select.
//
// The screen is driven as Chaos drives it: Adafruit SSD1306 on SPI, the frame at 30 fps.
// Off = never started, or display off and charge pump off; still = a 50% checkerboard drawn
// once; redrawing = it and its inverse alternately, a whole frame every 33 ms.

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Audio.h>
#include <SPI.h>
#include <Wire.h>

#include "pins.h"  // include/teensy_chaos
#include "noise_stream.h"

class Sgtl5000At48k : public AudioControlSGTL5000
{
  public:
    void SetFs48k() { write(0x0004, 0x0008); }  // CHIP_CLK_CTRL: SYS_FS = 48 kHz, MCLK = 256 fs
};

static noise::Player player;
AudioInputI2S        i2s_in;
NoiseStream          ns(&player);
AudioOutputI2S       i2s_out;
AudioConnection      c0(i2s_in, 0, ns, 0), c1(i2s_in, 1, ns, 1), c2(ns, 0, i2s_out, 0), c3(ns, 1, i2s_out, 1);
static Sgtl5000At48k codec;
static Adafruit_SSD1306 display(128, 64, &SPI, OLED_DC, OLED_RST, OLED_CS);

static bool          started = false, phase = false;
static noise::Screen shown   = noise::OFF;
static uint32_t      last_frame = 0;

static void frame(bool ph)
{
    for(int y = 0; y < 64; y++)
        for(int x = 0; x < 128; x++)
            display.drawPixel(x, y, ((x + y + ph) & 1) ? SSD1306_WHITE : SSD1306_BLACK);
    display.display();
}

void setup()
{
    pinMode(10, OUTPUT);
    digitalWriteFast(10, HIGH);  // the Audio Shield's SD card off (it shares pin 10 with the screen)
    pinMode(PIN_CS_DAC, OUTPUT);
    digitalWriteFast(PIN_CS_DAC, HIGH);  // the CV-out DAC idle
    Wire.setSDA(18);
    Wire.setSCL(19);
    Wire.begin();
    Wire.setClock(400000);

    player.Begin(true);
    AudioMemory(8);
    codec.enable();
    codec.SetFs48k();
    codec.inputSelect(AUDIO_INPUT_LINEIN);
    codec.lineInLevel(5);
    codec.volume(0.7f);
    codec.lineOutLevel(13);
}

void loop()
{
    noise::Screen want = player.screen;
    if(want != shown)
    {
        if(want == noise::OFF)
        {
            if(started)
            {
                display.ssd1306_command(SSD1306_DISPLAYOFF);
                display.ssd1306_command(SSD1306_CHARGEPUMP);
                display.ssd1306_command(0x10);
            }
        }
        else
        {
            if(!started)
            {
                display.begin(SSD1306_SWITCHCAPVCC);  // its init ends display-on
                started = true;
            }
            else
            {
                display.ssd1306_command(SSD1306_CHARGEPUMP);
                display.ssd1306_command(0x14);
                display.ssd1306_command(SSD1306_DISPLAYON);
            }
            phase = false;
            frame(phase);
            last_frame = millis();
        }
        shown = want;
    }
    if(shown == noise::REDRAW && millis() - last_frame >= 33)
    {
        last_frame = millis();
        phase      = !phase;
        frame(phase);
    }
}
