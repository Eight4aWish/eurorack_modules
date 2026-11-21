// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Boy's hardware: the analogue board Rev A (Teensy 4.1, teensy_core_midi), as Boy wires it.
// Drivers for the parts Boy uses, taken from eurorack_electronics/firmware/analogue_board_test,
// which proved each one on this board. The pin map, the chips and the start-up rules they follow
// (spec §15, "requirement n" below) are in eurorack_electronics/docs/analogue_board_reva_firmware.md.

#pragma once

#include <Arduino.h>

namespace board {

// The panel jacks on the six CV channels: DAC8568 channel n is board jack CV(n+1). PITCH1 and PITCH2
// follow the deck as found on 2026-11-21 (pitch1 on CV2, pitch2 on CV5), the other way round from the
// bring-up plan; the calibration table is per DAC channel, so it holds either way.
enum Cv : uint8_t { GATE3 = 0, PITCH1 = 1, PITCH3 = 2, GATE1 = 3, PITCH2 = 4, GATE2 = 5 };
constexpr uint8_t CV_ALL = 0x3F;

// The 74HCT595 gate word: bit n drives GOUTn+1, and 1 = +5 V
enum GateBit : uint8_t { CLK = 0, DRUM2 = 1, DRUM1 = 2, RST = 3 };

// Probe the EEPROM (requirement 1), then the expander, the DAC start-up (requirement 3), the gate
// word (requirement 4) and the encoder. Every jack switch stays off and the gate outputs disabled:
// each mode turns on what it uses. False if the EEPROM doesn't answer, so the board is unpowered.
bool begin();

// CV out. cv_write() holds a channel at `volts` at the jack; close its switch with cv_outputs() only
// once it holds the wanted value (requirement 3.6).
void cv_write(uint8_t ch, float volts);
void cv_outputs(uint8_t mask);  // bit n = DAC channel n's jack switch on

// CV out calibration (spec requirement 10, as O&C: a point per volt per channel), kept in the EEPROM.
// For each of the six channels, the DAC code that puts each whole volt from -5 to +5 V on the jack
// with nothing patched; cv_write() interpolates between them. The CALIBRATE mode finds the codes by
// loopback, each jack read by the ADC, after a meter has set the ADC: two readings on one jack, which
// serve all six because the ADS8688's channels match to 0.05 % and 0.75 mV (SBAS582, 25 C). Without a
// table cv_write() uses the design formula and one measured gain.
constexpr int8_t CAL_VMIN = -5, CAL_VMAX = 5;
constexpr uint8_t CAL_POINTS = CAL_VMAX - CAL_VMIN + 1, CAL_CHANNELS = 6;
struct CvCal {
  float adc_gain, adc_offset;  // jack volts = adc_gain * cv_read() + adc_offset: the meter's truth
  uint16_t code[CAL_CHANNELS][CAL_POINTS];
};
bool cv_cal_valid();                    // a table came from the EEPROM, or was stored this session
const CvCal &cv_cal();
bool cv_cal_store(const CvCal &c);      // to the EEPROM, with a CRC; cv_write() uses it from then on
void cv_cal_suspend(bool off);          // true: cv_write() uses the design formula (while calibrating)
uint16_t cv_code_nominal(float volts);  // the design formula's code
void cv_write_code(uint8_t ch, uint16_t code);
float cv_read_avg(uint8_t ch, uint8_t n);  // one channel, n conversions averaged, volts as cv_read()
float cv_adc_true(float v);                // a cv_read() value through the meter's correction

// Load correction (spec requirement 9). A jack driving a 100 kOhm input loses about 1 % through its
// 1 kOhm output resistor: 12 cents an octave. Each switched-on channel is read back while it plays and
// its level corrected, within +-3 %; beyond that the jack is taken to be driven by another output or
// shorted, left uncorrected, and flagged. Needs the calibration. Call every pass from a mode with CV
// outputs; it reads one channel each 2 ms.
void cv_service();
uint8_t cv_driven();  // bit n: channel n read outside the +-3 % a load can explain

// CV in. With its switch off a jack is an input: the ADS8688 reads it on ±10.24 V. cv_in_begin()
// sets the ranges, false if the ADC doesn't echo them. cv_read() takes one conversion of each
// channel listed (Cv numbering), as volts at the jack.
bool cv_in_begin();
void cv_read(const uint8_t *chs, uint8_t n, float *volts);
// What an open input reads: the ADS8688's bias, 1 MOhm to its 2.25 V against 100 kOhm to ground
// (spec requirement 8). Measured on Boy (2026-11-01): +0.206 to +0.209 V on all six.
constexpr float CV_OPEN = 0.207f;

// Gate out: a whole word, shifted and latched (requirements 4 and 14)
void gates_write(uint8_t word);
void gate_outputs(bool on);  // GATE_OE_N: latch a word before turning them on

// The encoder: whole detents turned since the last call, clockwise positive
int32_t encoder_turns();

// The encoder's push. Call every pass; it reads the expander at most every 20 ms.
//   SHORT  released inside 0.6 s
//   LONG   released after 0.6 s, before MENU
//   MENU   held for 2 s, sent while still held
enum class Push : uint8_t { NONE, SHORT, LONG, MENU };
Push poll_push();

// The mode for the session, kept in the EEPROM
uint8_t saved_mode();
void save_mode(uint8_t mode);
bool take_menu_request();  // true once after back_to_menu()
// A mode's own settings: up to 32 bytes in an EEPROM page of its own
bool mode_load(uint8_t mode, void *d, uint8_t n);
bool mode_store(uint8_t mode, const void *d, uint8_t n);

// Every jack switch off, gate outs low and disabled, DAC at 0 V: the board as at power-on
void safe();
// End the session: safe(), then restart into the chooser, which waits for a choice
[[noreturn]] void back_to_menu();

}  // namespace board

// The 128x32 SSD1306 as four rows of 21 characters. A changed row goes out in 32-byte pieces, one
// per service() call, so no pass of the loop waits more than about 1 ms on the I2C bus it shares
// with the expander.
namespace screen {
void begin();
void row(uint8_t r, const char *text, bool inverted = false);  // inverted: black on a lit row
void service();
}  // namespace screen
