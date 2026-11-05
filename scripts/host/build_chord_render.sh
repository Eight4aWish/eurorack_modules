#!/usr/bin/env bash
# Builds scripts/host/chord_render, the chord engine and Warps' DSP on this Mac.
set -eu
cd "$(dirname "$0")/../.."
E=third_party/eurorack
c++ -std=c++14 -O2 -DTEST -w -Isrc/teensy_move -I$E -o "${1:-scripts/host/chord_render}" \
  scripts/host/chord_render.cpp src/teensy_move/chord_engine.cpp \
  $E/warps/dsp/filter_bank.cc $E/warps/dsp/modulator.cc $E/warps/dsp/oscillator.cc $E/warps/dsp/vocoder.cc \
  $E/warps/resources.cc $E/stmlib/dsp/units.cc $E/stmlib/utils/random.cc
