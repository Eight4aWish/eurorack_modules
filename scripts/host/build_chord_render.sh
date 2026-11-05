#!/usr/bin/env bash
# Builds scripts/host/chord_render, the chord engine and Warps' DSP on this Mac.
set -eu
cd "$(dirname "$0")/../.."
P=third_party/parasites
c++ -std=c++14 -O2 -DTEST -w -Isrc/teensy_move -I$P -o "${1:-scripts/host/chord_render}" \
  scripts/host/chord_render.cpp src/teensy_move/chord_engine.cpp \
  $P/warps/dsp/filter_bank.cc $P/warps/dsp/modulator.cc $P/warps/dsp/oscillator.cc $P/warps/dsp/vocoder.cc \
  $P/warps/resources.cc $P/stmlib/dsp/units.cc $P/stmlib/utils/random.cc
