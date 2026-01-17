#pragma once
#include <Arduino.h>

namespace eurorack_calib {

// Linear mapping: y = a*x + b
struct LinCalib { float a; float b; };

inline LinCalib fromTwoPoints(float x1, float y1, float x2, float y2) {
  LinCalib c{0,0};
  float dx = (x2 - x1);
  if (fabsf(dx) < 1e-9f) return c; // degenerate; caller should guard
  c.a = (y2 - y1) / dx;
  c.b = y1 - c.a * x1;
  return c;
}

inline float apply(const LinCalib &c, float x) { return c.a * x + c.b; }

// ADC calibration: raw_code -> volts
struct AdcCalib { LinCalib raw_to_volts; };

// DAC calibration: volts -> code (12-bit for MCP47xx/48xx)
struct DacCalib { LinCalib volts_to_code; };

// Bundle for a device
struct CalibPack {
  static constexpr uint32_t kMagic = 0x4543414C; // 'ECAL'
  static constexpr uint16_t kVersion = 1;
  uint32_t magic = kMagic;
  uint16_t version = kVersion;
  uint8_t adcCount = 0;
  uint8_t dacCount = 0;
  AdcCalib adc[8];   // up to 8 ADC channels
  DacCalib dac[8];   // up to 8 DAC channels
};

// Serialization: raw byte buffer I/O (POD floats)
inline size_t packSize(const CalibPack &p) {
  return sizeof(p.magic)+sizeof(p.version)+sizeof(p.adcCount)+sizeof(p.dacCount)
    + p.adcCount * sizeof(AdcCalib) + p.dacCount * sizeof(DacCalib);
}

inline void pack(const CalibPack &p, uint8_t *buf, size_t &lenOut) {
  uint8_t *w = buf;
  auto w32=[&](uint32_t v){ memcpy(w,&v,4); w+=4; };
  auto w16=[&](uint16_t v){ memcpy(w,&v,2); w+=2; };
  auto w8=[&](uint8_t v){ *w++ = v; };
  w32(p.magic); w16(p.version); w8(p.adcCount); w8(p.dacCount);
  size_t ac = p.adcCount, dc = p.dacCount;
  memcpy(w, p.adc, ac*sizeof(AdcCalib)); w += ac*sizeof(AdcCalib);
  memcpy(w, p.dac, dc*sizeof(DacCalib)); w += dc*sizeof(DacCalib);
  lenOut = (size_t)(w - buf);
}

inline bool unpack(CalibPack &p, const uint8_t *buf, size_t len) {
  if (len < 4+2+1+1) return false;
  const uint8_t *r = buf; size_t rem = len;
  auto r32=[&](uint32_t &v){ if(rem<4) return false; memcpy(&v,r,4); r+=4; rem-=4; return true; };
  auto r16=[&](uint16_t &v){ if(rem<2) return false; memcpy(&v,r,2); r+=2; rem-=2; return true; };
  auto r8=[&](uint8_t &v){ if(rem<1) return false; v=*r++; rem-=1; return true; };
  if (!r32(p.magic) || !r16(p.version)) return false; if (p.magic!=CalibPack::kMagic) return false;
  if (!r8(p.adcCount) || !r8(p.dacCount)) return false; if (p.adcCount>8||p.dacCount>8) return false;
  size_t need = p.adcCount*sizeof(AdcCalib) + p.dacCount*sizeof(DacCalib);
  if (rem < need) return false;
  memcpy(p.adc, r, p.adcCount*sizeof(AdcCalib)); r += p.adcCount*sizeof(AdcCalib);
  memcpy(p.dac, r, p.dacCount*sizeof(DacCalib)); r += p.dacCount*sizeof(DacCalib);
  return true;
}

} // namespace eurorack_calib
