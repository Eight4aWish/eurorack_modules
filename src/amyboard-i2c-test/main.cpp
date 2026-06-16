// AMYboard I2C peripheral check — quick & dirty
// ------------------------------------------------------------
// Target: AMYboard (ESP32-S3). Arduino-ESP32 core, no extra libraries.
//
// Front-panel I2C bus (the FRONT grove jack, NOT the back "Tulip" jack):
//   SDA = GPIO17, SCL = GPIO18, 400 kHz
//
// Expected devices on the bus:
//   - OLED screen (SSD1327 grayscale 128x128) ........ 0x3D (sometimes 0x3C)
//   - Quad Rotary Encoder (Adafruit #5752, Seesaw) ... 0x49
//   - (single QT Rotary Encoder #4991, Seesaw) ....... 0x36
//
// What this does:
//   1) Scans the whole I2C bus every 2 s and prints every address it finds,
//      annotating the ones we recognise.
//   2) For any Seesaw encoder it finds (0x49 / 0x4A / 0x4B / 0x4C / 0x36),
//      reads the hardware-ID register (proves it's really a Seesaw) and the
//      live position of encoder 0. Turn the knob and the number should move.
//
// You have: Adafruit STEMMA QT Rotary Encoder #4991 -> Seesaw @ 0x36.
//
// Open Serial Monitor at 115200 baud.
// ------------------------------------------------------------

#include <Arduino.h>
#include <Wire.h>

static const int PIN_SDA = 17;
static const int PIN_SCL = 18;
static const uint32_t I2C_HZ = 400000;

// Seesaw protocol bits we need (no library required)
static const uint8_t SEESAW_STATUS_BASE     = 0x00;
static const uint8_t SEESAW_STATUS_HW_ID     = 0x01;
static const uint8_t SEESAW_STATUS_SWRST     = 0x7F;
static const uint8_t SEESAW_ENCODER_BASE     = 0x11;
static const uint8_t SEESAW_ENCODER_POSITION = 0x30; // +n for encoder n on the quad

// Candidate Seesaw addresses to probe for encoder behaviour
static const uint8_t SEESAW_ADDRS[] = {0x49, 0x4A, 0x4B, 0x4C, 0x36};

static const char *describe(uint8_t addr) {
  switch (addr) {
    case 0x3C: return "OLED SSD1327/SH1107 (alt addr)";
    case 0x3D: return "OLED SSD1327 (screen)";
    case 0x49: return "Seesaw Quad Rotary Encoder (default)";
    case 0x4A:
    case 0x4B:
    case 0x4C: return "Seesaw Quad Rotary Encoder (addr-jumpered)";
    case 0x36: return "Seesaw single QT Rotary Encoder";
    default:   return "unknown device";
  }
}

// Write a seesaw register pointer, wait, then read `len` bytes.
// Returns true on success.
static bool seesawRead(uint8_t addr, uint8_t base, uint8_t reg,
                       uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(base);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  delayMicroseconds(500); // seesaw needs a moment before it can answer
  uint8_t got = Wire.requestFrom((int)addr, (int)len);
  if (got != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static bool deviceResponds(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void setup() {
  Serial.begin(115200);
  delay(1500); // give USB-CDC time to enumerate
  Serial.println();
  Serial.println("=== AMYboard I2C peripheral check ===");
  Serial.printf("I2C: SDA=GPIO%d  SCL=GPIO%d  @ %lu Hz\n",
                PIN_SDA, PIN_SCL, (unsigned long)I2C_HZ);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_HZ);
  delay(100);
}

void loop() {
  // --- 1) full bus scan ---
  Serial.println("\n-- scanning bus --");
  int found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    if (deviceResponds(addr)) {
      Serial.printf("  0x%02X  %s\n", addr, describe(addr));
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  (no devices found — check Grove cable / power / SDA-SCL not swapped)");
  } else {
    Serial.printf("  %d device(s) found\n", found);
  }

  // --- 2) try to actually talk to a Seesaw encoder ---
  for (uint8_t i = 0; i < sizeof(SEESAW_ADDRS); i++) {
    uint8_t addr = SEESAW_ADDRS[i];
    if (!deviceResponds(addr)) continue;

    uint8_t hwid = 0;
    if (seesawRead(addr, SEESAW_STATUS_BASE, SEESAW_STATUS_HW_ID, &hwid, 1)) {
      // 0x55=SAMD09, 0x84/0x86/0x87=ATtiny8xx variants
      Serial.printf("  encoder @0x%02X: Seesaw HW_ID=0x%02X\n", addr, hwid);

      uint8_t pos[4];
      if (seesawRead(addr, SEESAW_ENCODER_BASE, SEESAW_ENCODER_POSITION, pos, 4)) {
        int32_t position = ((int32_t)pos[0] << 24) | ((int32_t)pos[1] << 16) |
                           ((int32_t)pos[2] << 8)  | (int32_t)pos[3];
        Serial.printf("  encoder @0x%02X: position[0] = %ld  (turn the knob — this should change)\n",
                      addr, (long)position);
      } else {
        Serial.printf("  encoder @0x%02X: responds but encoder-position read failed\n", addr);
      }
    } else {
      Serial.printf("  encoder @0x%02X: responds to address but HW_ID read failed\n", addr);
    }
  }

  delay(2000);
}
