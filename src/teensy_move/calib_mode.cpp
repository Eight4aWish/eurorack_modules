// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// CALIBRATE: the six CV outputs' calibration table (spec requirement 10; board.h, CvCal).
//
// 1. Unpatch every CV jack, and put a meter on PITCH1 (tip to the jack, common to the rack's 0 V).
// 2. Boy puts about +4 V on PITCH1: turn the encoder until the meter reads 4.000 V, press. Then -4.000 V.
//    Those two readings set the ADC to agree with the meter (gain and offset). One jack serves all six:
//    the ADS8688's channels match to +-0.05 % and +-0.75 mV (SBAS582, 25 C).
// 3. Unpatch the meter, press. Each output is stepped through every whole volt from -5 to +5 V, read back
//    through the ADC at its jack, and its code adjusted until the jack reads that volt: 66 points, a
//    couple of seconds. The table goes to the EEPROM with a CRC, and is read back before it's trusted.
// 4. CHECK: every output holds the same whole volt; turn to step it, and check it with the meter on any
//    jack. Hold the push for 2 s to leave.
//
// Calibration is with nothing patched, so the table is the unloaded jack; the load correction
// (requirement 9, board::cv_service) takes it from there.

#include "calib_mode.h"

#include <Arduino.h>

#include "board.h"

namespace calib_mode {

constexpr uint8_t METER_CH = board::PITCH1;
constexpr float METER_HI = 4.0f, METER_LO = -4.0f;
constexpr float CODES_PER_VOLT = 65536 / (4.3f * 2.5f * 0.982f);  // the stage's slope, near enough to converge
constexpr float LOOP_TOL = 0.0002f;                                 // V: done when the jack reads within this
constexpr float POINT_WARN = 0.002f;                                // V: a point that ends further off is reported
// the panel's name for a DAC channel, from board::Cv so it follows the map
static const char *name(uint8_t ch) {
  switch (ch) {
    case board::GATE1: return "GATE1";
    case board::GATE2: return "GATE2";
    case board::GATE3: return "GATE3";
    case board::PITCH1: return "PITCH1";
    case board::PITCH2: return "PITCH2";
    case board::PITCH3: return "PITCH3";
    default: return "?";
  }
}

enum State : uint8_t { INTRO, METER_HI_STEP, METER_LO_STEP, LOOP_PROMPT, LOOPBACK, CHECK, FAILED };
static State state;
static board::CvCal cal;
static float adc_hi, adc_lo;  // the ADC's readings while the meter read +4.000 and -4.000 V
static int32_t code;          // the code being trimmed
static uint8_t lp_ch, lp_pt, worst_ch, worst_pt;
static float worst_err;
static int8_t check_v;
static uint32_t last_draw;
static char failed_why[22];

static float raw_avg(uint8_t ch, uint8_t n) { return board::cv_read_avg(ch, n); }

static void rows(const char *a, const char *b, const char *c, const char *d) {
  screen::row(0, a, true);
  screen::row(1, b);
  screen::row(2, c);
  screen::row(3, d);
}

static void fail(const char *why) {
  strlcpy(failed_why, why, sizeof(failed_why));
  state = FAILED;
}

void begin() {
  // every output at the design formula's 0 V, then all six switches on: calibration reads each jack
  board::cv_cal_suspend(true);
  for (uint8_t ch = 0; ch < board::CAL_CHANNELS; ch++) board::cv_write_code(ch, board::cv_code_nominal(0));
  board::cv_outputs(board::CV_ALL);
  if (!board::cv_in_begin()) {
    fail("ADC didn't answer");
    return;
  }
  state = INTRO;
}

// One whole point of the loopback: step the code until the jack reads the volt, as the meter would.
static void loopback_point() {
  float v = board::CAL_VMIN + lp_pt;
  int32_t c = board::cv_code_nominal(v);
  float err = 0;
  for (uint8_t it = 0; it < 8; it++) {
    board::cv_write_code(lp_ch, (uint16_t)constrain(c, 0, 65535));
    delay(3);  // the output settles in microseconds; the ADC's input filter in about a millisecond
    float jack = cal.adc_gain * raw_avg(lp_ch, 32) + cal.adc_offset;
    err = v - jack;
    if (fabsf(err) < LOOP_TOL) break;
    c += lroundf(err * CODES_PER_VOLT);
  }
  cal.code[lp_ch][lp_pt] = (uint16_t)constrain(c, 0, 65535);
  if (fabsf(err) > worst_err) worst_err = fabsf(err), worst_ch = lp_ch, worst_pt = lp_pt;
}

void loop() {
  char a[32], b[32], c[32], d[32];
  board::Push push = board::poll_push();
  if (push == board::Push::MENU) board::back_to_menu();  // also restores every output to safe
  int32_t turns = board::encoder_turns();

  switch (state) {
    case INTRO:
      rows("CALIBRATE CV outs", "Unpatch all CV jacks", "meter on PITCH1", "press to start");
      if (push == board::Push::SHORT) {
        code = board::cv_code_nominal(METER_HI);
        state = METER_HI_STEP;
      }
      break;

    case METER_HI_STEP:
    case METER_LO_STEP: {
      bool hi = state == METER_HI_STEP;
      // a detent is 6 codes, about 1 mV; a quick turn moves ten times as far
      if (turns) code = constrain(code + turns * (abs(turns) > 1 ? 60 : 6), 0, 65535);
      board::cv_write_code(METER_CH, (uint16_t)code);
      if (millis() - last_draw > 150) {
        last_draw = millis();
        snprintf(a, sizeof(a), "PITCH1 meter %+.3fV", hi ? METER_HI : METER_LO);
        snprintf(c, sizeof(c), "ADC %+.4f V", raw_avg(METER_CH, 16));
        snprintf(d, sizeof(d), "code %ld", (long)code);
        rows(a, "turn to trim, press", c, d);
      }
      if (push == board::Push::SHORT) {
        delay(20);
        float r = raw_avg(METER_CH, 64);
        if (hi) {
          adc_hi = r;
          code = board::cv_code_nominal(METER_LO);
          state = METER_LO_STEP;
        } else {
          adc_lo = r;
          cal.adc_gain = (METER_HI - METER_LO) / (adc_hi - adc_lo);
          cal.adc_offset = METER_HI - cal.adc_gain * adc_hi;
          // the ADS8688 is good to 0.05 % and 1 mV, and the board's own path to it a little more: a meter
          // that disagrees by more than 3 % or 50 mV was misread, or on the wrong jack
          if (!(cal.adc_gain > 0.97f && cal.adc_gain < 1.03f) || fabsf(cal.adc_offset) > 0.05f) {
            snprintf(failed_why, sizeof(failed_why), "ADC fit %.3f %+.0fmV", cal.adc_gain, cal.adc_offset * 1000);
            state = FAILED;
          } else {
            board::cv_write_code(METER_CH, board::cv_code_nominal(0));
            state = LOOP_PROMPT;
          }
        }
      }
      break;
    }

    case LOOP_PROMPT:
      snprintf(c, sizeof(c), "ADC x%.4f %+.1fmV", cal.adc_gain, cal.adc_offset * 1000);
      rows("CALIBRATE CV outs", "Unpatch the meter", c, "press to measure");
      if (push == board::Push::SHORT) {
        lp_ch = lp_pt = 0;
        worst_err = 0;
        state = LOOPBACK;
      }
      break;

    case LOOPBACK:
      loopback_point();
      snprintf(b, sizeof(b), "%-6s %+dV", name(lp_ch), board::CAL_VMIN + lp_pt);
      rows("CALIBRATING", b, "", "");
      if (++lp_pt == board::CAL_POINTS) {
        lp_pt = 0;
        board::cv_write_code(lp_ch, board::cv_code_nominal(0));
        if (++lp_ch == board::CAL_CHANNELS) {
          if (!board::cv_cal_store(cal)) {
            fail("EEPROM write failed");
            break;
          }
          board::cv_cal_suspend(false);
          check_v = 1;
          for (uint8_t ch = 0; ch < board::CAL_CHANNELS; ch++) board::cv_write(ch, check_v);
          state = CHECK;
        }
      }
      break;

    case CHECK:
      if (turns) {
        check_v = constrain(check_v + turns, board::CAL_VMIN, board::CAL_VMAX);
        for (uint8_t ch = 0; ch < board::CAL_CHANNELS; ch++) board::cv_write(ch, check_v);
      }
      snprintf(b, sizeof(b), "all jacks %+d.000 V", check_v);
      snprintf(c, sizeof(c), "worst %s %+dV %.1fmV", name(worst_ch), board::CAL_VMIN + worst_pt, worst_err * 1000);
      rows(worst_err > POINT_WARN ? "SAVED: check worst" : "SAVED: meter check", b, c, "turn: volts, hold:out");
      break;

    case FAILED:
      rows("CALIBRATION FAILED", failed_why, "nothing saved", "hold to leave");
      break;
  }
  screen::service();
}

}  // namespace calib_mode
