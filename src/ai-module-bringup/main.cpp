// AI Module — bring-up sketch
//
// Tests:
//   - All 7 button + LED pairs (master + 6 channels). Press any button,
//     paired LED lights while held.
//   - Audio listening tap: live DC level and peak-to-peak on A0 (L) and
//     A1 (R), reported on serial.
//   - Clock input on D9: interrupt-driven pulse count + last interval.
//   - 3× MCP4822 over SPI: writes a unique fixed test voltage per output.
//     Measure with a meter on each chip's Vout A (pin 8) and Vout B (pin 6).
//
// Expected voltages per channel (raw MCP output, before bipolar shift):
//   CV1 (MCP1 Vout A) = 0.5 V
//   CV2 (MCP1 Vout B) = 1.0 V
//   CV3 (MCP2 Vout A) = 1.5 V
//   CV4 (MCP2 Vout B) = 2.0 V
//   CV5 (MCP3 Vout A) = 2.5 V
//   CV6 (MCP3 Vout B) = 3.0 V
//
// Pin map per docs/AI_MODULES_HARDWARE.md §3:
//   master : btn D12, LED D10
//   ch 1   : btn D8,  LED D7
//   ch 2   : btn A2,  LED A3      (A2 = GPIO3 strapping pin — see note)
//   ch 3   : btn D6,  LED D5
//   ch 4   : btn A4,  LED A5
//   ch 5   : btn D4,  LED D3
//   ch 6   : btn A6,  LED A7
//   audio  : L on A0, R on A1     (both ADC1)
//
// Buttons: switch-to-GND, internal pull-up enabled (active LOW).
// LEDs:    anode-via-1kΩ-to-GPIO, cathode-to-GND (active HIGH).
// Audio:   AC-coupled inverting tap, output centred at ~1.65 V (~2048
//          on 12-bit ADC). Peak-to-peak rises with input amplitude.
//
// IMPORTANT: use the D/A label macros, not raw GPIO numbers. The Nano ESP32
// defaults to Arduino-compatible pin remap; raw GPIO numbers won't work.
//
// Note on ch 2 button (A2 / GPIO3): GPIO3 is a strapping pin selecting the
// JTAG signal source. Holding ch 2 down DURING reset will switch JTAG to
// external pins (USB-CDC Serial still works). Normal pressing during runtime
// is unaffected.

#include <Arduino.h>
#include <SPI.h>

// Use SPI3 (HSPI) instead of the default SPI2 (FSPI). FSPI has IOMUX
// default pins (GPIO9, 11, 12, 13, 14) for HD/WP/CLK/Q/D that collide
// with our button and LED assignments. SPI3 has no IOMUX defaults —
// only the pins we specify get claimed.
SPIClass HSpi(HSPI);

// MCP4822 chip-select pins.
constexpr int CS_MCP1 = D2;
constexpr int CS_MCP2 = D0;   // RX0/D0 — UART RX sacrificed
constexpr int CS_MCP3 = D1;   // TX0/D1 — UART TX sacrificed

// Per-channel test codes (12-bit DAC code; output ≈ code / 1000 volts).
constexpr uint16_t TEST_CODE_CV1 =  500;  // ~0.5 V
constexpr uint16_t TEST_CODE_CV2 = 1000;  // ~1.0 V
constexpr uint16_t TEST_CODE_CV3 = 1500;  // ~1.5 V
constexpr uint16_t TEST_CODE_CV4 = 2000;  // ~2.0 V
constexpr uint16_t TEST_CODE_CV5 = 2500;  // ~2.5 V
constexpr uint16_t TEST_CODE_CV6 = 3500;  // ~3.5 V (diagnostic — chip 3 ch B fault)

// MCP4822 16-bit command:
//   bit 15 : A/B select (0 = ch A, 1 = ch B)
//   bit 14 : ignored
//   bit 13 : GA (0 = 2× gain → 0..4.095 V, 1 = 1× gain → 0..2.048 V)
//   bit 12 : SHDN (1 = active, 0 = shutdown)
//   bits 11..0 : 12-bit value
void mcp4822_write(int cs_pin, bool channel_b, uint16_t value) {
  uint16_t cmd = 0x1000;                        // GA=0 (2×), SHDN=1
  if (channel_b) cmd |= 0x8000;                 // AB=1
  cmd |= (value & 0x0FFF);

  HSpi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cs_pin, LOW);
  HSpi.transfer16(cmd);
  digitalWrite(cs_pin, HIGH);
  HSpi.endTransaction();
}

void write_all_cvs(uint16_t code) {
  mcp4822_write(CS_MCP1, false, code);  // CV1
  mcp4822_write(CS_MCP1, true,  code);  // CV2
  mcp4822_write(CS_MCP2, false, code);  // CV3
  mcp4822_write(CS_MCP2, true,  code);  // CV4
  mcp4822_write(CS_MCP3, false, code);  // CV5
  mcp4822_write(CS_MCP3, true,  code);  // CV6
}

// All-channel stepped pattern.
//
// Cycles through fixed DAC codes with multi-second hold per step so a
// multimeter can settle. All 6 CVs receive the same code at the same time.
const uint16_t cv_step_codes[] = {500, 2000, 3500};
constexpr int N_STEPS = sizeof(cv_step_codes) / sizeof(cv_step_codes[0]);
int      cv_step_idx     = 0;
uint32_t cv_step_last_ms = 0;
constexpr uint32_t CV_STEP_HOLD_MS = 3000;

struct Channel {
  const char* name;
  int button_pin;
  int led_pin;
  char short_name; // for the compact serial line: M, 1, 2, ...
};

const Channel channels[] = {
  {"master", D12, D10, 'M'},
  {"ch1",    D8,  D7,  '1'},
  {"ch2",    A2,  A3,  '2'},
  {"ch3",    D6,  D5,  '3'},
  {"ch4",    A4,  A5,  '4'},
  {"ch5",    D4,  D3,  '5'},
  {"ch6",    A6,  A7,  '6'},
};
constexpr int N_CHANNELS = sizeof(channels) / sizeof(channels[0]);

struct AdcStats {
  uint16_t min;
  uint16_t max;
  uint32_t sum;
  uint32_t count;

  void reset() {
    min = 4095;
    max = 0;
    sum = 0;
    count = 0;
  }
  void add(uint16_t v) {
    if (v < min) min = v;
    if (v > max) max = v;
    sum += v;
    count++;
  }
  uint16_t mean() const { return count ? (uint16_t)(sum / count) : 0; }
  uint16_t pp()   const { return max > min ? (max - min) : 0; }
};

AdcStats stats_l, stats_r;

// Clock input: rising-edge interrupt counter + interval.
volatile uint32_t clk_count = 0;
volatile uint32_t clk_last_us = 0;
volatile uint32_t clk_interval_us = 0;

void IRAM_ATTR clk_isr() {
  const uint32_t now_us = micros();
  if (clk_last_us != 0) {
    clk_interval_us = now_us - clk_last_us;
  }
  clk_last_us = now_us;
  clk_count++;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { /* wait up to 3 s for USB-CDC */ }
  Serial.println("\n=== AI Module bring-up: buttons + LEDs + audio ADC ===");
  Serial.println("Press buttons to light paired LEDs.");
  Serial.println("Audio: DC ~2048 at silence; pp grows with input amplitude.");

  for (int i = 0; i < N_CHANNELS; i++) {
    pinMode(channels[i].button_pin, INPUT_PULLUP);
    pinMode(channels[i].led_pin,    OUTPUT);
    digitalWrite(channels[i].led_pin, LOW);
  }

  analogReadResolution(12);            // 0..4095
  analogSetAttenuation(ADC_11db);      // ~0..3.3 V input range

  stats_l.reset();
  stats_r.reset();

  // Clock input: hardware divider already pulls toward GND when idle, but
  // INPUT_PULLDOWN gives a defined state if the conditioning isn't connected.
  pinMode(D9, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(D9), clk_isr, RISING);

  // SPI3 bus (HSpi) + MCP4822 chip selects. SPI3 always uses GPIO-matrix
  // routing — only the explicit pins are claimed, no IOMUX defaults.
  // MISO/SS = -1 because MCP4822 is write-only and we manage CS manually.
  pinMode(CS_MCP1, OUTPUT); digitalWrite(CS_MCP1, HIGH);
  pinMode(CS_MCP2, OUTPUT); digitalWrite(CS_MCP2, HIGH);
  pinMode(CS_MCP3, OUTPUT); digitalWrite(CS_MCP3, HIGH);
  HSpi.begin(D13, -1, D11, -1);

  write_all_cvs(cv_step_codes[0]);
  Serial.println("All 6 CVs stepped pattern (3 s hold each):");
  Serial.println("  code  500 -> DAC 0.50V -> op-amp ~+4.16 V");
  Serial.println("  code 2000 -> DAC 2.00V -> op-amp ~+0.13 V");
  Serial.println("  code 3500 -> DAC 3.50V -> op-amp ~-3.91 V");
  Serial.println("Cycles continuously. Probe each CV output through one cycle.");
}

void loop() {
  // Buttons → LEDs.
  bool pressed[N_CHANNELS];
  for (int i = 0; i < N_CHANNELS; i++) {
    pressed[i] = (digitalRead(channels[i].button_pin) == LOW);
    digitalWrite(channels[i].led_pin, pressed[i] ? HIGH : LOW);
  }

  // Sample audio L/R on every loop tick (~few kHz aggregate).
  stats_l.add(analogRead(A0));
  stats_r.add(analogRead(A1));

  // All-CV stepped pattern: hold each code for CV_STEP_HOLD_MS.
  const uint32_t now_ms = millis();
  if (now_ms - cv_step_last_ms >= CV_STEP_HOLD_MS) {
    cv_step_last_ms = now_ms;
    cv_step_idx = (cv_step_idx + 1) % N_STEPS;
    const uint16_t code = cv_step_codes[cv_step_idx];
    write_all_cvs(code);

    const float v_dac   = code / 1000.0f;
    const float v_opamp = 2.69f * (2.048f - v_dac);
    Serial.printf("CV step: code=%4u  DAC=%.3fV  expect_opamp=%+.2fV\n",
                  code, v_dac, v_opamp);
  }

  // Periodic report.
  static uint32_t last_print = 0;
  const uint32_t now = millis();
  if (now - last_print >= 250) {
    last_print = now;

    Serial.print("btn: ");
    for (int i = 0; i < N_CHANNELS; i++) {
      Serial.print(pressed[i] ? channels[i].short_name : '.');
      Serial.print(' ');
    }

    Serial.printf("  audio: L %4u/%4u  R %4u/%4u",
                  stats_l.mean(), stats_l.pp(),
                  stats_r.mean(), stats_r.pp());

    // Clock readout. Snapshot volatile counters once.
    noInterrupts();
    const uint32_t cnt = clk_count;
    const uint32_t last_us = clk_last_us;
    const uint32_t interval_us = clk_interval_us;
    interrupts();

    Serial.printf("  clk: cnt=%lu", (unsigned long)cnt);
    if (cnt < 2 || (micros() - last_us) > 2000000UL) {
      Serial.print(" int= -");
    } else {
      Serial.printf(" int=%lums", (unsigned long)(interval_us / 1000));
    }
    Serial.println();

    stats_l.reset();
    stats_r.reset();
  }
}
