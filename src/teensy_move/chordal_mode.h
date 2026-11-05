// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// Mode 2, Chordal: one finger plays four notes that lead smoothly into the next chord.

#pragma once

#include "chord_engine.h"

namespace chordal_mode {

void begin();
void loop();

// What Chordal last saved, for Warps' chord carrier. False leaves the defaults.
bool load_params(chord::Params *p);

}  // namespace chordal_mode
