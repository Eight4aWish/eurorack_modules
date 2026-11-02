// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst

#include "board.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <QuadEncoder.h>
#include <SPI.h>
#include <Wire.h>

namespace board {

// ---- pins (Teensy numbering)
constexpr uint8_t PIN_CS_ADC = 10, PIN_CS_DAC = 9;                      // SPI-A: SCK 13, MOSI 11, MISO 12
constexpr uint8_t PIN_SPIB_MISO = 39, PIN_CS_595 = 38, PIN_CS_POT = 37;  // SPI-B (SPI1): SCK 27, MOSI 26
constexpr uint8_t PIN_ENC_A = 4, PIN_ENC_B = 2;                         // gate ins 3 and 1
// every board net except SDA and SCL. Pin 13 is SPI-A's clock, so no LED_BUILTIN on this board.
constexpr uint8_t BOARD_PINS[] = {13, 11, 12, 10, 9, 27, 26, 39, 38, 37, 23, 21, 20, 7, 8, 22, 17, 2, 3, 4, 5, 0, 1};

// ---- I2C
constexpr uint8_t ADDR_TCA = 0x20, ADDR_EE = 0x50;
enum : uint8_t { TCA_IN0, TCA_IN1, TCA_OUT0, TCA_OUT1, TCA_POL0, TCA_POL1, TCA_CFG0, TCA_CFG1 };
// TCA9555 port 0: P00-P05 the CV1-6 jack switches (1 = on), P06 GATE_OE_N, P07 button SW9 (input)
constexpr uint8_t OE_N = 0x40;
constexpr uint8_t PUSH_BIT = 0x20;  // port 1: SW6, the encoder's push, 0 while pressed

// ---- converters
SPISettings SPI_CONV(4000000, MSBFIRST, SPI_MODE1);   // the ADS8688 and DAC8568 latch on falling edges; tested at 4 MHz
SPISettings SPI_PANEL(1000000, MSBFIRST, SPI_MODE0);  // the panel bus: 1 MHz at most (requirement 14)
constexpr uint16_t DAC_ZERO = 32544;                  // the code for 0 V at the jack

// CV in: each ADC pin sees its jack through 1 kOhm, with 100 kOhm to ground and the ADS8688's 1 MOhm
// to its 2.25 V bias behind it. As the bring-up test program reads it, the drop across the 1 kOhm
// is added back.
constexpr float ADC_VBIAS = 2.25f * 100 / 1100, ADC_RLOAD = 100.0f * 1000 / 1100;  // V, kOhm
static float jack_from_adc(float v) { return v + (v - ADC_VBIAS) / ADC_RLOAD; }

// CV out stage (spec §15): Vout = 4.3 x 2.5 x code / 65536 - 3.3 x 2.5 x 22/34. Measured on Boy
// (2026-11-01) with a meter at each jack: all six sit about 1.8 % under that, the 0 V code within
// 13 mV. Most of it is the board's own load, the 1 kOhm output into the ~92 kOhm path to the ADC.
// Per-channel calibration in the EEPROM (requirement 10) replaces this when it's written.
constexpr float CV_GAIN = 0.982f;

// ---- settings: one EEPROM page, clear of the last page the bring-up test uses; each mode's own
// page sits below it
constexpr uint16_t EE_SETTINGS = 0x1FC0, EE_PAGE = 32;
constexpr uint8_t SETTINGS_MAGIC = 0xB2;
struct Settings {
  uint8_t magic, mode, menu;
};

static uint8_t port0 = OE_N;
static QuadEncoder *enc;

static bool i2c_present(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

static bool tca_write(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(ADDR_TCA);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

static bool tca_read(uint8_t reg, uint8_t &v) {
  Wire.beginTransmission(ADDR_TCA);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(ADDR_TCA, (uint8_t)1) != 1) return false;
  v = Wire.read();
  return true;
}

static void set_port0(uint8_t v) {
  port0 = v;
  tca_write(TCA_OUT0, v);
}

static bool ee_write(uint16_t a, const uint8_t *d, uint8_t n) {  // n <= 16, inside one page
  Wire.beginTransmission(ADDR_EE);
  Wire.write(a >> 8);
  Wire.write(a & 0xFF);
  Wire.write(d, n);
  if (Wire.endTransmission() != 0) return false;
  for (uint32_t t = millis(); millis() - t < 20;)  // no acknowledge until the write cycle (5 ms max) ends
    if (i2c_present(ADDR_EE)) return true;
  return false;
}

static bool ee_read(uint16_t a, uint8_t *d, uint8_t n) {
  Wire.beginTransmission(ADDR_EE);
  Wire.write(a >> 8);
  Wire.write(a & 0xFF);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(ADDR_EE, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) d[i] = Wire.read();
  return true;
}

static void dac_frame(uint32_t w) {
  SPI.beginTransaction(SPI_CONV);
  digitalWriteFast(PIN_CS_DAC, LOW);
  SPI.transfer16(w >> 16);
  SPI.transfer16(w & 0xFFFF);
  digitalWriteFast(PIN_CS_DAC, HIGH);
  SPI.endTransaction();
}

static void dac_write(uint8_t ch, uint16_t code) { dac_frame(0x03000000 | (ch << 20) | ((uint32_t)code << 4)); }  // write and update

bool begin() {
  for (uint8_t p : BOARD_PINS) pinMode(p, INPUT);
  Wire.begin();
  Wire.setClock(400000);
  if (!i2c_present(ADDR_EE)) return false;  // requirement 1: every other bus stays off

  // the DAC start-up, in the order of requirement 3
  port0 = OE_N;  // 1. every jack switch off, gate outputs disabled; the output register first
  tca_write(TCA_OUT0, port0);
  tca_write(TCA_CFG0, 0x80);
  tca_write(TCA_CFG1, 0xFF);
  digitalWrite(PIN_CS_ADC, HIGH);  // the ADS8688 shares SPI-A: cv_in_begin() starts it
  pinMode(PIN_CS_ADC, OUTPUT);
  digitalWrite(PIN_CS_DAC, HIGH);
  pinMode(PIN_CS_DAC, OUTPUT);
  SPI.begin();
  for (uint8_t ch = 0; ch < 8; ch++) dac_write(ch, DAC_ZERO);  // 2. 0 V codes while the reference is still off
  dac_frame(0x090A0000);                                       // 3. internal reference on: flexible, always on
  delay(10);                                                   // 4. settle; 5 and 6 are each mode's, 7 never

  digitalWrite(PIN_CS_595, HIGH);
  pinMode(PIN_CS_595, OUTPUT);
  digitalWrite(PIN_CS_POT, HIGH);
  pinMode(PIN_CS_POT, OUTPUT);
  SPI1.setMISO(PIN_SPIB_MISO);
  SPI1.begin();
  gates_write(0);  // a whole word latched before any mode turns the outputs on (requirement 4)

  enc = new QuadEncoder(1, PIN_ENC_A, PIN_ENC_B, 0);  // no internal pull-ups: the LM393s have theirs
  enc->setInitConfig();
  enc->EncConfig.filterCount = 7;  // the longest input filter, against contact bounce
  enc->EncConfig.filterSamplePeriod = 255;
  enc->init();
  return true;
}

void cv_write(uint8_t ch, float volts) {
  float code = (volts / CV_GAIN + 3.3f * 2.5f * 22 / 34) * 65536 / (4.3f * 2.5f);
  dac_write(ch, constrain(lroundf(code), 0, 65535));
}

void cv_outputs(uint8_t mask) { set_port0((port0 & ~CV_ALL) | (mask & CV_ALL)); }

static uint16_t adc_frame(uint16_t cmd) {  // 32 clocks: a command in, the previous frame's conversion out
  SPI.beginTransaction(SPI_CONV);
  digitalWriteFast(PIN_CS_ADC, LOW);
  SPI.transfer16(cmd);
  uint16_t r = SPI.transfer16(0);
  digitalWriteFast(PIN_CS_ADC, HIGH);
  SPI.endTransaction();
  return r;
}

static uint8_t adc_reg(uint8_t reg, int val = -1) {  // read if val < 0; a write echoes the byte written
  SPI.beginTransaction(SPI_CONV);
  digitalWriteFast(PIN_CS_ADC, LOW);
  SPI.transfer16((reg << 9) | (val >= 0 ? 0x100 | val : 0));
  uint8_t r = SPI.transfer(0);
  digitalWriteFast(PIN_CS_ADC, HIGH);
  SPI.endTransaction();
  return r;
}

bool cv_in_begin() {  // range 0x00, ±10.24 V, on every channel (requirement 8), each read back
  bool ok = true;
  for (uint8_t ch = 0; ch < 8; ch++) ok = adc_reg(0x05 + ch, 0x00) == 0x00 && adc_reg(0x05 + ch) == 0x00 && ok;
  return ok;
}

// MAN_Ch_n selects a channel and it converts in the next frame, so each frame returns the channel
// the one before selected: n + 1 frames read n channels
void cv_read(const uint8_t *chs, uint8_t n, float *volts) {
  adc_frame(0xC000 | (chs[0] << 10));
  for (uint8_t i = 0; i < n; i++) {
    uint16_t code = adc_frame(i + 1 < n ? 0xC000 | (chs[i + 1] << 10) : 0x0000);  // the last: NO_OP
    volts[i] = jack_from_adc((code - 32768.0f) * 20.48f / 65536);
  }
}

void gates_write(uint8_t word) {
  SPI1.beginTransaction(SPI_PANEL);
  SPI1.transfer(word);
  SPI1.endTransaction();
  digitalWriteFast(PIN_CS_595, LOW);  // RCLK: latches on the rising edge
  delayMicroseconds(1);
  digitalWriteFast(PIN_CS_595, HIGH);
}

void gate_outputs(bool on) { set_port0(on ? port0 & ~OE_N : port0 | OE_N); }

int32_t encoder_turns() {  // 4 counts a detent; keep the remainder for next time
  static int32_t used = 0;
  int32_t turns = (enc->read() - used) / 4;
  used += turns * 4;
  return turns;
}

Push poll_push() {
  constexpr uint32_t POLL_MS = 20, LONG_MS = 600, MENU_MS = 2000;
  static uint32_t last_poll, down_at;
  static bool raw_prev, down, menu_sent, primed;
  uint32_t now = millis();
  if (down && !menu_sent && now - down_at >= MENU_MS) {
    menu_sent = true;
    return Push::MENU;
  }
  if (now - last_poll < POLL_MS) return Push::NONE;
  last_poll = now;
  uint8_t in1;
  if (!tca_read(TCA_IN1, in1)) return Push::NONE;
  bool raw = !(in1 & PUSH_BIT);
  bool steady = raw == raw_prev;  // two reads 20 ms apart agree: debounced
  raw_prev = raw;
  if (!primed) {  // a push still held from before (the hold that restarted into the chooser) counts for nothing
    primed = steady && !raw;
    return Push::NONE;
  }
  if (!steady || raw == down) return Push::NONE;
  down = raw;
  if (down) {
    down_at = now;
    menu_sent = false;
    return Push::NONE;
  }
  if (menu_sent) return Push::NONE;
  return now - down_at >= LONG_MS ? Push::LONG : Push::SHORT;
}

static Settings load_settings() {
  Settings s;
  if (!ee_read(EE_SETTINGS, (uint8_t *)&s, sizeof(s)) || s.magic != SETTINGS_MAGIC) s = {SETTINGS_MAGIC, 0, 0};
  return s;
}

static void store_settings(const Settings &s) { ee_write(EE_SETTINGS, (const uint8_t *)&s, sizeof(s)); }

uint8_t saved_mode() { return load_settings().mode; }

void save_mode(uint8_t mode) {
  Settings s = load_settings();
  if (s.mode == mode) return;  // write only on a change
  s.mode = mode;
  store_settings(s);
}

bool take_menu_request() {
  Settings s = load_settings();
  if (!s.menu) return false;
  s.menu = 0;
  store_settings(s);
  return true;
}

bool mode_load(uint8_t mode, void *d, uint8_t n) { return ee_read(EE_SETTINGS - EE_PAGE * (mode + 1), (uint8_t *)d, n); }

bool mode_store(uint8_t mode, const void *d, uint8_t n) {
  return ee_write(EE_SETTINGS - EE_PAGE * (mode + 1), (const uint8_t *)d, n);
}

void safe() {
  gates_write(0);
  set_port0((port0 & ~CV_ALL) | OE_N);
  for (uint8_t ch = 0; ch < 8; ch++) dac_write(ch, DAC_ZERO);
}

// A restart, so every mode starts from a clean Teensy: modes set up the audio and MIDI hardware
// their own way, and none has to undo another's.
void back_to_menu() {
  safe();
  Settings s = load_settings();
  s.menu = 1;
  store_settings(s);
  delay(10);
  SCB_AIRCR = 0x05FA0004;  // system reset request
  while (true) {
  }
}

}  // namespace board

namespace screen {

constexpr uint8_t ADDR_OLED = 0x3C, ROWS = 4, PIECE = 32;
// 400 kHz during and after each of the library's transfers: its default drops the bus to 100 kHz
static Adafruit_SSD1306 oled(128, 32, &Wire, -1, 400000, 400000);
static char text[ROWS][22];
static uint8_t dirty[ROWS];  // bit k: piece k of the row (columns 32k to 32k+31) still to send

void begin() {
  oled.begin(SSD1306_SWITCHCAPVCC, ADDR_OLED);
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.display();  // once, at start-up: the whole frame
}

void row(uint8_t r, const char *s) {
  if (r >= ROWS || strncmp(text[r], s, sizeof(text[r]) - 1) == 0) return;
  strlcpy(text[r], s, sizeof(text[r]));
  oled.fillRect(0, r * 8, 128, 8, SSD1306_BLACK);
  oled.setCursor(0, r * 8);
  oled.print(text[r]);
  dirty[r] = 0x0F;
}

void service() {
  for (uint8_t r = 0; r < ROWS; r++) {
    if (!dirty[r]) continue;
    uint8_t k = __builtin_ctz(dirty[r]);
    uint8_t col = k * PIECE;
    Wire.beginTransmission(ADDR_OLED);
    Wire.write((uint8_t)0x00);  // commands follow
    Wire.write(SSD1306_PAGEADDR);
    Wire.write(r);
    Wire.write(r);
    Wire.write(SSD1306_COLUMNADDR);
    Wire.write(col);
    Wire.write((uint8_t)(col + PIECE - 1));
    Wire.endTransmission();
    Wire.beginTransmission(ADDR_OLED);
    Wire.write((uint8_t)0x40);  // data follows
    Wire.write(oled.getBuffer() + r * 128 + col, PIECE);
    Wire.endTransmission();
    dirty[r] &= ~(1 << k);
    return;
  }
}

}  // namespace screen
