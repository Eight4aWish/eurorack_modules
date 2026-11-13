// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// ComputerCard for the analogue board Rev B with a Pico 2 W, behind the "Computer" panel.
//
// A drop-in replacement for Chris Johnson's ComputerCard.h (version 0.3.0, the Music Thing
// Workshop System Computer, MIT): the same class, the same calls and the same value ranges, so a
// card written against it builds for Rev B unchanged. Behind them is Rev B's hardware: the PCM3060
// codec for audio, the ADS8688 and DAC8568 for CV, a 74HCT595 for the pulse outs, an MCP3208 for
// the knobs and Z, a TCA9555 for the jack switches and the encoder's push, and a small SSD1306 in
// place of the six LEDs.
//
// The board: eurorack_electronics/docs/analogue_board_revb.md (pin map §3), the spec's §15
// requirements, and docs/REVB_COMPUTER.md in this repo for the panel and how it is wired.
//
// What differs from a Workshop Computer, by design:
// - The sample rate is 47 973.6 Hz (or half that with COMPUTERCARD_SAMPLE_RATE_DIV 2): clk_sys
//   runs at 196.5 MHz, and MCLK (256 fs) and the I2S clocks are whole divisions of it. 1 cent flat.
// - Inputs and outputs are read and written in blocks of four samples. CV in and out move once a
//   block, which at 48 kHz is 12 kHz.
// - Connected()/Disconnected(): there is no normalisation probe. A CV jack, and an audio jack
//   through its DC tap, reads the ADS8688's bias when open: steady near +0.2 V for 100 ms counts
//   as unpatched. A pulse input counts as patched once an edge has arrived in the last 2 s.
// - The pulse outs go through the 595, latched from the background loop: up to about a
//   millisecond late. PULSE_1_RAW_OUT and PULSE_2_RAW_OUT name GP13 and GP12, which on a Pico board
//   reach only hole J102, so a card that drives them directly harms nothing.
// - The LEDs are drawn on the screen. Calibration is the board's nominal transfer, until Rev B's
//   EEPROM calibration is written.

#ifndef COMPUTERCARD_H
#define COMPUTERCARD_H

#include <initializer_list>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "revb_conv.pio.h"
#include "revb_i2s.pio.h"
#include "revb_screen.h"

#ifndef COMPUTERCARD_SAMPLE_RATE_DIV
#define COMPUTERCARD_SAMPLE_RATE_DIV 2  // ComputerCard 0.3.0's default
#endif

#ifndef REVB_APP_NAME
#define REVB_APP_NAME "COMPUTER"
#endif

// Audio jack detection needs each audio input's DC tap wired to its ADC channel. Build with
// REVB_AUDIO_DETECT 0 on a board without them: the audio inputs then always count as patched.
#ifndef REVB_AUDIO_DETECT
#define REVB_AUDIO_DETECT 1
#endif

#define PULSE_1_RAW_OUT 13
#define PULSE_2_RAW_OUT 12

namespace revb {

// ---- Pico 2 GPIO, Rev B (eurorack_electronics tools/analogue_board/design.py, PINMAP_REVB)
constexpr uint PIN_SDA = 2, PIN_SCL = 3;  // I2C1
constexpr uint PIN_GATE_IN1 = 6, PIN_GATE_IN2 = 7, PIN_GATE_IN3 = 4, PIN_GATE_IN4 = 9;
constexpr uint PIN_SPIB_MISO = 8, PIN_SPIB_SCK = 10, PIN_SPIB_MOSI = 11, PIN_CS_POT = 14, PIN_CS_595 = 15;
constexpr uint PIN_CS_DAC = 16, PIN_SPIA_MOSI = 19, PIN_SPIA_MISO = 20;  // CS_ADC 17, SCK 18: side-set
constexpr uint PIN_MCLK = 21, PIN_I2S_DIN = 22, PIN_BCK = 26, PIN_I2S_DOUT = 28;  // LRCK 27: side-set

// ---- The Computer panel on Rev B's channels (docs/REVB_COMPUTER.md)
constexpr uint PIN_PULSE_IN[2] = {PIN_GATE_IN1, PIN_GATE_IN3};
constexpr uint PIN_ENC_A = PIN_GATE_IN2, PIN_ENC_B = PIN_GATE_IN4;
constexpr uint8_t POT_MAIN = 3, POT_X = 4, POT_Y = 5, POT_Z = 2;  // MCP3208 channel n is POT(n+1)
constexpr uint8_t ADC_CV1 = 1, ADC_CV2 = 4, ADC_AUDIO1 = 3, ADC_AUDIO2 = 5;  // ADS8688 channel n is CV(n+1)
constexpr uint8_t DAC_CV1 = 2, DAC_CV2 = 6;  // CV3 (two-way, switched on as an output) and CVO7
constexpr uint8_t TCA_SW_EN_CV1 = 1 << DAC_CV1;  // port 0 bit n: CV(n+1)'s jack switch
constexpr uint8_t TCA_OE_N = 0x40;               // port 0 bit 6: the 595's output enable, low = on
constexpr uint8_t TCA_PUSH = 0x02;               // port 1 bit 1: SW2, the encoder's push, 0 while held
constexpr uint8_t GATE_BIT[2] = {0, 1};          // 595 QA, QB: GOUT1, GOUT2

// ---- I2C parts
constexpr uint8_t ADDR_TCA = 0x20, ADDR_EE = 0x50;

// ---- Clocks. 196.5 MHz = 12 MHz x 131 / (4 x 2). MCLK and the I2S program both divide it by
// 16 x COMPUTERCARD_SAMPLE_RATE_DIV, so MCLK is 256 fs.
constexpr uint32_t SYS_KHZ = 196500;
constexpr uint32_t I2S_DIV = 16 * COMPUTERCARD_SAMPLE_RATE_DIV;
constexpr float SAMPLE_RATE = SYS_KHZ * 1000.0f / (I2S_DIV * 256);
constexpr uint32_t CONV_DIV = 4;  // SPI-A: 196.5 MHz / 4 / 4 = 12.3 MHz, under the ADS8688's 17
constexpr uint32_t BLOCK = 4;     // frames per DMA block

// ---- Levels. ComputerCard's +-2048 is about +-6 V on a Workshop Computer.
constexpr float VOLTS_PER_UNIT = 6.0f / 2048;
constexpr float CODEC_IN_VOLTS = 11.0f;   // a full-scale codec input is about 22 Vpp at the jack, inverted
constexpr float CODEC_OUT_VOLTS = 10.0f;  // a sample of 1.0 is about +10 V at the jack
// CV out (spec §15, ±10 V variant): Vout = 8.5 x 2.5 x code / 65536 - 10.714 V. The two-way
// channels sag about 1 % into the board's own load (1 kOhm into the path to the ADC); CVO7's
// 1 kOhm sits inside its loop.
constexpr float DAC_SPAN = 8.5f * 2.5f, DAC_OFFSET = 10.714f;
constexpr float DAC_SAG_TWO_WAY = 0.989f;
// CV in: each ADC pin sees its jack through 1 kOhm, with 100 kOhm to ground and the ADS8688's
// 1 MOhm to its 2.25 V bias behind it (Boy's board.cpp, from the bring-up test)
constexpr float ADC_VBIAS = 2.25f * 100 / 1100, ADC_RLOAD = 100.0f * 1000 / 1100;
// What an open jack reads. CV jacks: +0.207 V, measured on Rev A. An audio jack's DC tap also
// has the jack's own 1 MOhm to ground: about +0.19 V, calculated. Measure both on Rev B.
constexpr float OPEN_CV = 0.207f, OPEN_AUDIO = 0.19f, OPEN_WINDOW = 0.025f, OPEN_STEADY = 0.008f;

}  // namespace revb

class ComputerCard {
 public:
  enum Knob { Main, X, Y };
  enum Switch { Down, Middle, Up };
  enum Input { Audio1, Audio2, CV1, CV2, Pulse1, Pulse2 };
  enum HardwareVersion_t { Proto1 = 0x2a, Proto2_Rev1 = 0x30, Rev1_1 = 0x0C, Unknown = 0xFF };
  enum USBPowerState_t { DFP, UFP, Unsupported };

  ComputerCard() { thisptr = this; }

  /// Start audio processing. Never returns. The audio interrupt runs on the core that calls this.
  void Run() {
    thisptr = this;
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(2);
    set_sys_clock_khz(revb::SYS_KHZ, true);
    board_ok = HardwareInit();
    if (board_ok) StartAudio();
    while (true) {
      BackgroundLoop();
      if (board_ok) Service();
    }
  }

  /// Kept for compatibility: Rev B detects jacks without a probe (see the top of this file)
  void EnableNormalisationProbe() {}

  static ComputerCard *ThisPtr() { return thisptr; }
  uint8_t GetAdcDmaChannel() const { return 0xFF; }
  uint8_t GetSpiDmaChannel() const { return 0xFF; }

  /// Rev B: the sample rate ProcessSample() runs at
  static constexpr float SampleRate() { return revb::SAMPLE_RATE; }

 protected:
  /// Callback, called once per sample
  virtual void ProcessSample() = 0;
  /// Optional background loop, called over and over from Run() on the same core
  virtual void BackgroundLoop() {}

  /// Read knob position (returns 0-4095)
  int32_t __not_in_flash_func(KnobVal)(Knob ind) { return knobs[ind]; }
  /// Read switch position
  Switch __not_in_flash_func(SwitchVal)() { return switchVal; }
  bool __not_in_flash_func(SwitchChanged)() { return switchVal != lastSwitchVal; }

  /// Set audio output (values -2048 to 2047)
  void __not_in_flash_func(AudioOut)(int i, int16_t val) { audioOut[i & 1] = val * revb::VOLTS_PER_UNIT; }
  void __not_in_flash_func(AudioOut1)(int16_t val) { AudioOut(0, val); }
  void __not_in_flash_func(AudioOut2)(int16_t val) { AudioOut(1, val); }

  /// Set CV output (values -2048 to 2047)
  void __not_in_flash_func(CVOut)(int i, int16_t val) {
    if (val < -2048) val = -2048;
    if (val > 2047) val = 2047;
    cvOut[i & 1] = val * revb::VOLTS_PER_UNIT;
  }
  void __not_in_flash_func(CVOut1)(int16_t val) { CVOut(0, val); }
  void __not_in_flash_func(CVOut2)(int16_t val) { CVOut(1, val); }

  /// Set CV output (values -262144 to 262143)
  void __not_in_flash_func(CVOutPrecise)(int i, int32_t val) {
    if (val < -262144) val = -262144;
    if (val > 262143) val = 262143;
    cvOut[i & 1] = val * (6.0f / 262144);
  }
  void __not_in_flash_func(CVOut1Precise)(int32_t val) { CVOutPrecise(0, val); }
  void __not_in_flash_func(CVOut2Precise)(int32_t val) { CVOutPrecise(1, val); }

  /// Set CV output to a MIDI note, 1 V/octave, note 60 = 0 V
  void __not_in_flash_func(CVOutMIDINote)(int i, uint8_t noteNum) { cvOut[i & 1] = (noteNum - 60) / 12.0f; }
  void __not_in_flash_func(CVOut1MIDINote)(uint8_t noteNum) { CVOutMIDINote(0, noteNum); }
  void __not_in_flash_func(CVOut2MIDINote)(uint8_t noteNum) { CVOutMIDINote(1, noteNum); }

  /// Set CV output in millivolts. Returns true if the value was limited.
  bool __not_in_flash_func(CVOutMillivolts)(int i, int32_t millivolts) {
    float v = millivolts * 0.001f;
    bool limited = v < -10.0f || v > 10.0f;
    cvOut[i & 1] = v < -10.0f ? -10.0f : v > 10.0f ? 10.0f : v;
    return limited;
  }
  bool __not_in_flash_func(CVOut1Millivolts)(int32_t mv) { return CVOutMillivolts(0, mv); }
  bool __not_in_flash_func(CVOut2Millivolts)(int32_t mv) { return CVOutMillivolts(1, mv); }

  /// Set pulse output (true = on)
  void __not_in_flash_func(PulseOut)(int i, bool val) {
    uint8_t bit = 1 << revb::GATE_BIT[i & 1];
    gateWord = val ? (gateWord | bit) : (gateWord & ~bit);
  }
  void __not_in_flash_func(PulseOut1)(bool val) { PulseOut(0, val); }
  void __not_in_flash_func(PulseOut2)(bool val) { PulseOut(1, val); }

  /// Audio and CV in (-2048 to 2047)
  int16_t __not_in_flash_func(AudioIn)(int i) { return i ? adcInR : adcInL; }
  int16_t __not_in_flash_func(AudioIn1)() { return adcInL; }
  int16_t __not_in_flash_func(AudioIn2)() { return adcInR; }
  int16_t __not_in_flash_func(CVIn)(int i) { return cv[i & 1]; }
  int16_t __not_in_flash_func(CVIn1)() { return cv[0]; }
  int16_t __not_in_flash_func(CVIn2)() { return cv[1]; }

  /// Pulse in
  bool __not_in_flash_func(PulseIn)(int i) { return pulse[i & 1]; }
  bool __not_in_flash_func(PulseInRisingEdge)(int i) { return pulse[i & 1] && !lastPulse[i & 1]; }
  bool __not_in_flash_func(PulseInFallingEdge)(int i) { return !pulse[i & 1] && lastPulse[i & 1]; }
  bool __not_in_flash_func(PulseIn1)() { return pulse[0]; }
  bool __not_in_flash_func(PulseIn1RisingEdge)() { return PulseInRisingEdge(0); }
  bool __not_in_flash_func(PulseIn1FallingEdge)() { return PulseInFallingEdge(0); }
  bool __not_in_flash_func(PulseIn2)() { return pulse[1]; }
  bool __not_in_flash_func(PulseIn2RisingEdge)() { return PulseInRisingEdge(1); }
  bool __not_in_flash_func(PulseIn2FallingEdge)() { return PulseInFallingEdge(1); }

  /// Jack detection (see the top of this file)
  bool __not_in_flash_func(Connected)(Input i) { return connected[i]; }
  bool __not_in_flash_func(Disconnected)(Input i) { return !connected[i]; }

  /// LEDs, drawn on the screen. Numbered
  ///   0 1
  ///   2 3
  ///   4 5
  void __not_in_flash_func(LedBrightness)(uint32_t index, uint16_t value) {
    if (index < 6) leds[index] = value;
  }
  void __not_in_flash_func(LedOn)(uint32_t index, bool value = true) { LedBrightness(index, value ? 4095 : 0); }
  void __not_in_flash_func(LedOff)(uint32_t index) { LedBrightness(index, 0); }

  USBPowerState_t USBPowerState() { return Unsupported; }
  HardwareVersion_t HardwareVersion() const { return Unknown; }
  uint64_t UniqueCardID() const {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    uint64_t r = 0;
    for (int i = 0; i < 8; i++) r = (r << 8) | id.id[i];
    return r;
  }
  bool CVOutsCalibrated() const { return false; }
  void Abort() { aborted = true; }

  uint16_t CRCencode(const uint8_t *data, int length) {  // CRC-16-CCITT, as ComputerCard's
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < length; i++) {
      crc ^= (uint16_t)data[i] << 8;
      for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
  }

  // ---- Rev B extensions, for programs written for this board (not on a Workshop Computer)

  /// Audio output in volts at the jack, -10 to +10, bypassing the 12-bit range
  void __not_in_flash_func(AudioOutVolts)(int i, float volts) { audioOut[i & 1] = volts; }
  /// The encoder: whole detents since the last call, clockwise positive
  int32_t EncoderTurns() {
    int32_t turns = (encCount - encUsed) / 4;
    encUsed += turns * 4;
    return turns;
  }
  /// The encoder's push: held now, and pressed since the last call
  bool PushHeld() const { return pushHeld; }
  bool PushPressed() {
    bool p = pushPressed;
    pushPressed = false;
    return p;
  }
  /// The name at the top of the screen, up to 10 characters
  void DisplayName(const char *name) { displayName = name; }

 private:
  inline static ComputerCard *thisptr = nullptr;

  // ---- what ProcessSample sees and sets
  volatile int32_t knobs[3] = {0, 0, 0};
  volatile Switch switchVal = Middle, lastSwitchVal = Middle;
  int16_t adcInL = 0, adcInR = 0;
  volatile int16_t cv[2] = {0, 0};
  bool pulse[2] = {false, false}, lastPulse[2] = {false, false};
  volatile bool connected[6] = {false, false, false, false, false, false};
  float audioOut[2] = {0, 0};
  volatile float cvOut[2] = {0, 0};
  volatile uint8_t gateWord = 0;
  volatile uint16_t leds[6] = {0, 0, 0, 0, 0, 0};
  volatile bool aborted = false;
  bool board_ok = false;
  const char *displayName = REVB_APP_NAME;

  // ---- the panel
  volatile int32_t encCount = 0;
  int32_t encUsed = 0;
  volatile uint8_t encState = 0;
  bool pushHeld = false;
  volatile bool pushPressed = false;
  int32_t knobAcc[4] = {0, 0, 0, 0};  // knob readings x 16, smoothed
  uint8_t knobNext = 0;
  uint8_t gateShown = 0xFF;
  uint32_t potDue = 0, tcaDue = 0, screenDue = 0;
  uint8_t tcaPort0 = revb::TCA_OE_N;

  // ---- audio: DMA ping-pong on PIO0 (I2S), three TX buffers and two RX, each one block
  PIO pioAudio = pio0, pioConv = pio1;
  uint smAudio = 0, smConv = 0;
  int dmaTx[3] = {-1, -1, -1}, dmaRx[2] = {-1, -1};
  alignas(32) inline static uint32_t txBuf[3][2 * revb::BLOCK];
  alignas(32) inline static uint32_t rxBuf[2][2 * revb::BLOCK];
  uint32_t blockCount = 0;
  uint32_t divPhase = 0;

  // ---- the converter bus: one batch of frames a block, by DMA. Four ADC frames (each returns
  // the channel the frame before it selected) and the two CV outs.
  static constexpr int CONV_FRAMES = 6;
  int dmaConvTx = -1, dmaConvRx = -1;
  inline static uint32_t convTx[2 * CONV_FRAMES];
  inline static uint32_t convRx[CONV_FRAMES];
  bool convPrimed = false;
  float cvInVolts[4] = {0, 0, 0, 0};  // CV1, CV2, Audio1 tap, Audio2 tap

  // ---- jack detection: per ADC input, the range and mean over a window of blocks
  static constexpr uint32_t DETECT_BLOCKS = (uint32_t)(revb::SAMPLE_RATE * 0.1f / revb::BLOCK);
  float detMin[4], detMax[4], detSum[4];
  uint32_t detCount = 0;
  uint32_t pulseQuiet[2] = {0xFFFFFFFF, 0xFFFFFFFF};  // frames since the last edge

  revb::Screen screen;
  bool screenOk = false;

  // ================================================================== hardware set-up

  static bool I2cPresent(uint8_t addr) {
    uint8_t b;
    return i2c_read_timeout_us(i2c1, addr, &b, 1, false, 2000) == 1;
  }

  static bool TcaWrite(uint8_t reg, uint8_t v) {
    uint8_t b[2] = {reg, v};
    return i2c_write_timeout_us(i2c1, revb::ADDR_TCA, b, 2, false, 2000) == 2;
  }

  static bool TcaRead(uint8_t reg, uint8_t &v) {
    if (i2c_write_timeout_us(i2c1, revb::ADDR_TCA, &reg, 1, true, 2000) != 1) return false;
    return i2c_read_timeout_us(i2c1, revb::ADDR_TCA, &v, 1, false, 2000) == 1;
  }

  // A frame on the converter bus, waited for: for set-up only, before the DMA takes the bus over
  uint32_t ConvFrame(bool dac, uint32_t frame) {
    pio_sm_put_blocking(pioConv, smConv, dac ? 1 : 0);
    pio_sm_put_blocking(pioConv, smConv, frame);
    return pio_sm_get_blocking(pioConv, smConv);
  }

  static uint32_t DacFrame(uint8_t ch, uint16_t code) { return 0x03000000u | ((uint32_t)ch << 20) | ((uint32_t)code << 4); }

  static uint16_t DacCode(float volts, bool twoWay) {
    if (twoWay) volts /= revb::DAC_SAG_TWO_WAY;
    float code = (volts + revb::DAC_OFFSET) * 65536.0f / revb::DAC_SPAN;
    return code < 0 ? 0 : code > 65535 ? 65535 : (uint16_t)(code + 0.5f);
  }

  static float AdcVolts(uint32_t frame) {
    float v = ((int32_t)(frame & 0xFFFF) - 32768) * (20.48f / 65536);
    return v + (v - revb::ADC_VBIAS) / revb::ADC_RLOAD;
  }

  bool HardwareInit() {
    // I2C first: the EEPROM answers only when the rack powers the board (requirement 1). Without
    // it every other bus stays off.
    i2c_init(i2c1, 400000);
    gpio_set_function(revb::PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(revb::PIN_SCL, GPIO_FUNC_I2C);
    if (!I2cPresent(revb::ADDR_EE)) return false;

    // The expander: every jack switch off and the gate outputs disabled, output register first
    TcaWrite(6, 0x80);  // CFG0: port 0 outputs, P07 an input
    TcaWrite(7, 0xFF);  // CFG1: port 1 inputs
    TcaWrite(2, tcaPort0);

    // The converter bus on PIO1
    smConv = pio_claim_unused_sm(pioConv, true);
    uint off = pio_add_program(pioConv, &revb_conv_program);
    pio_sm_config c = revb_conv_program_get_default_config(off);
    sm_config_set_sideset_pins(&c, revb::PIN_CS_DAC);
    sm_config_set_out_pins(&c, revb::PIN_SPIA_MOSI, 1);
    sm_config_set_in_pins(&c, revb::PIN_SPIA_MISO);
    sm_config_set_out_shift(&c, false, false, 32);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_clkdiv_int_frac(&c, revb::CONV_DIV, 0);
    pio_sm_set_pins_with_mask(pioConv, smConv, 0b00011u << revb::PIN_CS_DAC, 0b11111u << revb::PIN_CS_DAC);
    pio_sm_set_pindirs_with_mask(pioConv, smConv, 0b01111u << revb::PIN_CS_DAC, 0b11111u << revb::PIN_CS_DAC);
    for (uint p = revb::PIN_CS_DAC; p <= revb::PIN_SPIA_MISO; p++) pio_gpio_init(pioConv, p);
    pio_sm_init(pioConv, smConv, off, &c);
    pio_sm_set_enabled(pioConv, smConv, true);

    // The DAC start-up (requirement 3): 0 V codes while the reference is still off, then the
    // reference on, then settle. CV out 1's switch closes only once its channel holds 0 V.
    for (uint8_t ch = 0; ch < 8; ch++) ConvFrame(true, DacFrame(ch, DacCode(0, ch < 6)));
    ConvFrame(true, 0x090A0000);
    sleep_ms(10);
    tcaPort0 |= revb::TCA_SW_EN_CV1;
    TcaWrite(2, tcaPort0);

    // The ADC: ±10.24 V on every channel (requirement 8)
    for (uint8_t ch = 0; ch < 8; ch++) ConvFrame(false, (uint32_t)((0x05 + ch) << 9 | 0x100) << 16);

    // The panel bus: SPI1 at 1 MHz (requirement 14), the MCP3208 and the 595
    spi_init(spi1, 1000000);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(revb::PIN_SPIB_SCK, GPIO_FUNC_SPI);
    gpio_set_function(revb::PIN_SPIB_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(revb::PIN_SPIB_MISO, GPIO_FUNC_SPI);
    for (uint p : {revb::PIN_CS_POT, revb::PIN_CS_595}) {
      gpio_init(p);
      gpio_put(p, 1);
      gpio_set_dir(p, GPIO_OUT);
    }
    WriteGates(0);  // a whole word latched before the outputs turn on (requirement 4)
    tcaPort0 &= ~revb::TCA_OE_N;
    TcaWrite(2, tcaPort0);
    for (int i = 0; i < 64; i++) ReadNextKnob();  // the knobs read before the first sample

    // Gate inputs: the LM393s have their own pull-ups. The encoder on gate inputs 2 and 4.
    for (uint p : {revb::PIN_GATE_IN1, revb::PIN_GATE_IN2, revb::PIN_GATE_IN3, revb::PIN_GATE_IN4}) {
      gpio_init(p);
      gpio_set_dir(p, GPIO_IN);
      gpio_disable_pulls(p);
    }
    encState = (gpio_get(revb::PIN_ENC_A) << 1) | gpio_get(revb::PIN_ENC_B);
    gpio_set_irq_enabled_with_callback(revb::PIN_ENC_A, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &EncoderIrq);
    gpio_set_irq_enabled(revb::PIN_ENC_B, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

    screenOk = screen.begin(i2c1);
    for (int i = 0; i < 4; i++) ResetDetect(i);
    return true;
  }

  void StartAudio() {
    // MCLK on GPOUT0
    clock_gpio_init(revb::PIN_MCLK, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, revb::I2S_DIV);

    smAudio = pio_claim_unused_sm(pioAudio, true);
    uint off = pio_add_program(pioAudio, &revb_i2s_program);
    pio_sm_config c = revb_i2s_program_get_default_config(off);
    sm_config_set_sideset_pins(&c, revb::PIN_BCK);
    sm_config_set_out_pins(&c, revb::PIN_I2S_DOUT, 1);
    sm_config_set_in_pins(&c, revb::PIN_I2S_DIN);
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_in_shift(&c, false, true, 32);
    sm_config_set_clkdiv_int_frac(&c, revb::I2S_DIV, 0);
    pio_sm_set_pindirs_with_mask(pioAudio, smAudio, (1u << revb::PIN_BCK) | (1u << (revb::PIN_BCK + 1)) | (1u << revb::PIN_I2S_DOUT),
                                 (1u << revb::PIN_BCK) | (1u << (revb::PIN_BCK + 1)) | (1u << revb::PIN_I2S_DOUT) | (1u << revb::PIN_I2S_DIN));
    for (uint p : {revb::PIN_BCK, revb::PIN_BCK + 1, revb::PIN_I2S_DOUT, revb::PIN_I2S_DIN}) pio_gpio_init(pioAudio, p);
    pio_sm_init(pioAudio, smAudio, off + revb_i2s_offset_entry_point, &c);

    memset(txBuf, 0, sizeof(txBuf));
    memset(rxBuf, 0, sizeof(rxBuf));
    for (int i = 0; i < 3; i++) dmaTx[i] = dma_claim_unused_channel(true);
    for (int i = 0; i < 2; i++) dmaRx[i] = dma_claim_unused_channel(true);
    // TX: A -> B -> C -> A, each reading its own buffer through a 32-byte ring, so a channel needs
    // no re-arming and the chain keeps the codec clocked even if this core stops for a while.
    for (int i = 0; i < 3; i++) {
      dma_channel_config d = dma_channel_get_default_config(dmaTx[i]);
      channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
      channel_config_set_read_increment(&d, true);
      channel_config_set_write_increment(&d, false);
      channel_config_set_ring(&d, false, 5);
      channel_config_set_dreq(&d, pio_get_dreq(pioAudio, smAudio, true));
      channel_config_set_chain_to(&d, dmaTx[(i + 1) % 3]);
      dma_channel_configure(dmaTx[i], &d, &pioAudio->txf[smAudio], txBuf[i], 2 * revb::BLOCK, false);
    }
    // RX: A -> B -> A, the same way; their completions are the audio interrupt
    for (int i = 0; i < 2; i++) {
      dma_channel_config d = dma_channel_get_default_config(dmaRx[i]);
      channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
      channel_config_set_read_increment(&d, false);
      channel_config_set_write_increment(&d, true);
      channel_config_set_ring(&d, true, 5);
      channel_config_set_dreq(&d, pio_get_dreq(pioAudio, smAudio, false));
      channel_config_set_chain_to(&d, dmaRx[(i + 1) % 2]);
      dma_channel_configure(dmaRx[i], &d, rxBuf[i], &pioAudio->rxf[smAudio], 2 * revb::BLOCK, false);
      dma_irqn_set_channel_enabled(1, dmaRx[i], true);
    }

    // The converter bus's DMA: started once a block from the interrupt
    dmaConvTx = dma_claim_unused_channel(true);
    dmaConvRx = dma_claim_unused_channel(true);
    {
      dma_channel_config d = dma_channel_get_default_config(dmaConvTx);
      channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
      channel_config_set_read_increment(&d, true);
      channel_config_set_write_increment(&d, false);
      channel_config_set_dreq(&d, pio_get_dreq(pioConv, smConv, true));
      dma_channel_configure(dmaConvTx, &d, &pioConv->txf[smConv], convTx, 2 * CONV_FRAMES, false);
      d = dma_channel_get_default_config(dmaConvRx);
      channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
      channel_config_set_read_increment(&d, false);
      channel_config_set_write_increment(&d, true);
      channel_config_set_dreq(&d, pio_get_dreq(pioConv, smConv, false));
      dma_channel_configure(dmaConvRx, &d, convRx, &pioConv->rxf[smConv], CONV_FRAMES, false);
    }
    // The ADC frames select the channels in this order; each returns the one selected before it
    static const uint8_t order[4] = {revb::ADC_CV1, revb::ADC_CV2, revb::ADC_AUDIO1, revb::ADC_AUDIO2};
    for (int f = 0; f < 4; f++) {
      convTx[2 * f] = 0;
      convTx[2 * f + 1] = (uint32_t)(0xC000 | (order[(f + 1) % 4] << 10)) << 16;
    }
    convTx[8] = convTx[10] = 1;

    irq_set_exclusive_handler(DMA_IRQ_1, &AudioIrq);
    irq_set_enabled(DMA_IRQ_1, true);
    dma_start_channel_mask((1u << dmaTx[0]) | (1u << dmaRx[0]));
    pio_sm_set_enabled(pioAudio, smAudio, true);
  }

  // ================================================================== the audio interrupt

  static void __not_in_flash_func(AudioIrq)() { thisptr->AudioBlock(); }

  static int32_t ToUnits(float volts) {
    int32_t u = (int32_t)lroundf(volts * (1.0f / revb::VOLTS_PER_UNIT));
    return u < -2048 ? -2048 : u > 2047 ? 2047 : u;
  }

  void __not_in_flash_func(AudioBlock)() {
    int rx = -1;
    for (int i = 0; i < 2; i++)
      if (dma_irqn_get_channel_status(1, dmaRx[i])) {
        dma_irqn_acknowledge_channel(1, dmaRx[i]);
        rx = i;
      }
    if (rx < 0) return;
    uint32_t in[2 * revb::BLOCK];
    memcpy(in, rxBuf[rx], sizeof(in));
    uint32_t *out = txBuf[blockCount % 3];  // the buffer that plays three blocks from now
    blockCount++;
    if (aborted) {
      memset(out, 0, sizeof(txBuf[0]));
      return;
    }

    ConverterBatch();

    for (uint32_t f = 0; f < revb::BLOCK; f++) {
      float l = (int32_t)in[2 * f] * (1.0f / 2147483648.0f);
      float r = (int32_t)in[2 * f + 1] * (1.0f / 2147483648.0f);
      adcInL = (connected[Audio1] || !REVB_AUDIO_DETECT) ? ToUnits(-l * revb::CODEC_IN_VOLTS) : 0;
      adcInR = (connected[Audio2] || !REVB_AUDIO_DETECT) ? ToUnits(-r * revb::CODEC_IN_VOLTS) : 0;
      for (int i = 0; i < 2; i++) {
        lastPulse[i] = pulse[i];
        pulse[i] = gpio_get(revb::PIN_PULSE_IN[i]);
        if (pulse[i] != lastPulse[i]) pulseQuiet[i] = 0;
        else if (pulseQuiet[i] != 0xFFFFFFFF) pulseQuiet[i]++;
        connected[Pulse1 + i] = pulseQuiet[i] < (uint32_t)(2.0f * revb::SAMPLE_RATE);
      }
      ProcessSample();
      lastSwitchVal = switchVal;
      for (int i = 0; i < 2; i++) {
        float s = audioOut[i] * (1.0f / revb::CODEC_OUT_VOLTS);
        s = s < -1.0f ? -1.0f : s > 0.99999f ? 0.99999f : s;
        out[2 * f + i] = (uint32_t)((int32_t)(s * 8388608.0f) * 256);
      }
    }
  }

  // Collect the last batch's readings and send the next: the CV outs as they stand, and the
  // ADC channel selections, which repeat
  void __not_in_flash_func(ConverterBatch)() {
    if (dma_channel_is_busy(dmaConvTx) || dma_channel_is_busy(dmaConvRx)) return;  // can't happen at 12 kHz
    if (convPrimed) {
      // frame f returns the channel frame f - 1 selected, so frame 0 the one the last ADC frame of
      // the batch before chose: CV1, CV2, Audio1's tap, Audio2's tap, in order
      for (int f = 0; f < 4; f++) cvInVolts[f] = AdcVolts(convRx[f]);
      cv[0] = connected[CV1] ? ToUnits(cvInVolts[0]) : 0;
      cv[1] = connected[CV2] ? ToUnits(cvInVolts[1]) : 0;
      Detect();
    }
    convPrimed = true;
    convTx[9] = DacFrame(revb::DAC_CV1, DacCode(cvOut[0], true));
    convTx[11] = DacFrame(revb::DAC_CV2, DacCode(cvOut[1], false));
    dma_channel_set_read_addr(dmaConvTx, convTx, false);
    dma_channel_set_write_addr(dmaConvRx, convRx, false);
    dma_channel_set_trans_count(dmaConvTx, 2 * CONV_FRAMES, false);
    dma_channel_set_trans_count(dmaConvRx, CONV_FRAMES, false);
    dma_start_channel_mask((1u << dmaConvTx) | (1u << dmaConvRx));
  }

  void ResetDetect(int i) {
    detMin[i] = 1e9f;
    detMax[i] = -1e9f;
    detSum[i] = 0;
  }

  // An input is open when it has sat within OPEN_WINDOW of its open level, steady to
  // OPEN_STEADY, for a whole window of about 100 ms
  void __not_in_flash_func(Detect)() {
    for (int i = 0; i < 4; i++) {
      float v = cvInVolts[i];
      detMin[i] = v < detMin[i] ? v : detMin[i];
      detMax[i] = v > detMax[i] ? v : detMax[i];
      detSum[i] += v;
    }
    if (++detCount < DETECT_BLOCKS) return;
    static const Input which[4] = {CV1, CV2, Audio1, Audio2};
    for (int i = 0; i < 4; i++) {
      float open = i < 2 ? revb::OPEN_CV : revb::OPEN_AUDIO;
      float mean = detSum[i] / detCount;
      bool isOpen = fabsf(mean - open) < revb::OPEN_WINDOW && detMax[i] - detMin[i] < revb::OPEN_STEADY;
      connected[which[i]] = !isOpen || (i >= 2 && !REVB_AUDIO_DETECT);
      ResetDetect(i);
    }
    detCount = 0;
  }

  // ================================================================== the panel

  static void EncoderIrq(uint gpio, uint32_t events) {
    (void)gpio;
    (void)events;
    ComputerCard *c = thisptr;
    static const int8_t step[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    uint8_t s = (gpio_get(revb::PIN_ENC_A) << 1) | gpio_get(revb::PIN_ENC_B);
    c->encCount += step[(c->encState << 2) | s];
    c->encState = s;
  }

  void WriteGates(uint8_t word) {
    spi_write_blocking(spi1, &word, 1);  // the last byte the 595 sees before its latch (requirement 14)
    gpio_put(revb::PIN_CS_595, 0);
    busy_wait_us_32(1);
    gpio_put(revb::PIN_CS_595, 1);
    gateShown = word;
  }

  void ReadNextKnob() {
    static const uint8_t ch[4] = {revb::POT_MAIN, revb::POT_X, revb::POT_Y, revb::POT_Z};
    uint8_t c = ch[knobNext];
    uint8_t tx[3] = {(uint8_t)(0x06 | (c >> 2)), (uint8_t)((c & 3) << 6), 0}, rx[3];
    gpio_put(revb::PIN_CS_POT, 0);
    spi_write_read_blocking(spi1, tx, rx, 3);
    gpio_put(revb::PIN_CS_POT, 1);
    int32_t raw = ((rx[1] & 0x0F) << 8) | rx[2];
    int32_t &acc = knobAcc[knobNext];
    acc += raw - (acc >> 4);  // a one-pole smoother; acc holds the reading x 16
    int32_t v = acc >> 4;
    if (knobNext < 3) {
      knobs[knobNext] = v;
    } else {  // Z: up to POT_REF, down to ground, middle through a 100k/100k divider
      Switch s = switchVal;
      if (v > 3300) s = Up;
      else if (v < 800) s = Down;
      else if (v > 1500 && v < 2600) s = Middle;
      switchVal = s;
    }
    knobNext = (knobNext + 1) & 3;
  }

  void Service() {
    uint32_t now = time_us_32();
    uint8_t g = gateWord;
    if (g != gateShown) WriteGates(g);
    if ((int32_t)(now - potDue) >= 0) {  // each knob every millisecond
      potDue = now + 250;
      ReadNextKnob();
      if (gateWord != gateShown) WriteGates(gateWord);  // the knob frame shifted through the 595 too
    }
    if ((int32_t)(now - tcaDue) >= 0) {  // the encoder's push every 20 ms, debounced over two reads
      tcaDue = now + 20000;
      static bool prev = false;
      uint8_t in1;
      if (TcaRead(1, in1)) {
        bool raw = !(in1 & revb::TCA_PUSH);
        if (raw == prev && raw != pushHeld) {
          pushHeld = raw;
          if (raw) pushPressed = true;
        }
        prev = raw;
      }
    }
    if (screenOk) {
      if ((int32_t)(now - screenDue) >= 0) {
        screenDue = now + 50000;
        DrawScreen();
      }
      screen.service();
    }
  }

  void DrawScreen() {
    screen.clear();
    screen.text(0, 0, displayName);
    // the LEDs, 2 x 3 as on the Computer's panel: lit, outlined (dim) or empty
    for (int i = 0; i < 6; i++) {
      int x = (i & 1) * 10, y = 10 + (i >> 1) * 7;
      uint16_t b = leds[i];
      if (b >= 2048) screen.rect(x, y, 8, 6, true);
      else if (b > 0) screen.rect(x, y, 8, 6, false);
    }
    static const char *z[3] = {"Z DN", "Z MID", "Z UP"};
    screen.text(24, 10, z[switchVal]);
    char p[8];
    int n = 0;
    if (connected[Audio1]) p[n++] = 'A';
    if (connected[CV1]) p[n++] = 'C';
    if (connected[Pulse1]) p[n++] = 'P';
    p[n] = 0;
    screen.text(24, 18, n ? p : "-");
    n = 0;
    if (connected[Audio2]) p[n++] = 'A';
    if (connected[CV2]) p[n++] = 'C';
    if (connected[Pulse2]) p[n++] = 'P';
    p[n] = 0;
    screen.text(48, 18, n ? p : "-");
    screen.commit();
  }
};

#endif
