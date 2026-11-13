// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Renders every MOD2 voice on a computer, through the same Runner and OutputStage as the board
// (frame.h), so a port can be heard before it is flashed. Each voice plays four hits at different
// pot settings, a hit every 0.6 s; OUT (the AC-coupled jack) goes to <dir>/<voice>.wav, 16-bit at
// the codec's 47 974 Hz, +-10 V full scale.
//
//   c++ -std=c++17 -O2 -o build/mod2_render src/revb/apps/mod2/host_render.cpp
//   build/mod2_render build/mod2_wav

#include <stdio.h>
#include <sys/stat.h>

#include <memory>
#include <vector>

#include "frame.h"
#include "voice.h"
#include "voices/clap.h"
#include "voices/claves.h"
#include "voices/fm_drum.h"
#include "voices/hihat.h"
#include "voices/kick.h"
#include "voices/laser.h"
#include "voices/metal.h"
#include "voices/retro_kick.h"
#include "voices/snare.h"

namespace mod2 {
Panel panel = {};
}
using namespace mod2;

static const float FS = 196500000.0f / (16 * 256);  // the codec's rate on Rev B

static void WriteWav(const char *path, const std::vector<float> &volts) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    perror(path);
    return;
  }
  uint32_t n = (uint32_t)volts.size(), rate = (uint32_t)(FS + 0.5f), bytes = n * 2;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f);
  u32(36 + bytes);
  fwrite("WAVEfmt ", 1, 8, f);
  u32(16);
  u16(1);
  u16(1);
  u32(rate);
  u32(rate * 2);
  u16(2);
  u16(16);
  fwrite("data", 1, 4, f);
  u32(bytes);
  for (float v : volts) {
    float s = Clamp(v / 10.0f, -1.0f, 1.0f);
    int16_t i = (int16_t)lroundf(s * 32767.0f);
    fwrite(&i, 2, 1, f);
  }
  fclose(f);
}

// pot settings for the four hits: POT1, POT2, POT3 (POT3 and the CV together)
static const float HITS[4][3] = {{0.5f, 0.5f, 0.5f}, {0.2f, 0.8f, 0.3f}, {0.8f, 0.2f, 0.7f}, {1.0f, 1.0f, 1.0f}};

template <typename V>
static void Render(const char *dir, const char *name) {
  std::unique_ptr<V> vp(new V);  // on the heap: the larger voices don't fit on the stack
  V &v = *vp;
  v.Init();
  Runner runner;
  OutputStage out(FS);
  runner.Start(&v, FS);
  const uint32_t spacing = (uint32_t)(0.6f * FS);
  std::vector<float> wav;
  uint32_t tick = 0;
  for (int h = 0; h < 4; h++) {
    panel.a[0] = HITS[h][0];
    panel.a[1] = HITS[h][1];
    panel.a[2] = 1.0f - HITS[h][2];  // the MOD2's A2 is inverted
    panel.in2 = panel.in2Recent = false;
    for (uint32_t i = 0; i < spacing; i++) {
      if (++tick >= 48) {
        tick = 0;
        v.Controls();
      }
      if (i == 48) v.Trigger();  // after the first Controls(), as on the board
      out.Process(runner.Next(&v));
      wav.push_back(out.ac);
    }
  }
  float peak = 0, sum = 0;
  for (float s : wav) {
    peak = fmaxf(peak, fabsf(s));
    sum += s * s;
  }
  char path[512];
  snprintf(path, sizeof path, "%s/%s.wav", dir, name);
  WriteWav(path, wav);
  printf("%-12s peak %5.2f V  rms %5.2f V  %s\n", name, peak, sqrtf(sum / wav.size()), path);
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  mkdir(dir, 0755);
  Render<Kick>(dir, "kick");
  Render<RetroKick>(dir, "retro_kick");
  Render<Snare>(dir, "snare");
  Render<Clap>(dir, "clap");
  Render<HiHat>(dir, "hihat");
  Render<Claves>(dir, "claves");
  Render<FmDrum>(dir, "fm_drum");
  Render<Metal>(dir, "metal");
  Render<Laser>(dir, "laser");
  return 0;
}
