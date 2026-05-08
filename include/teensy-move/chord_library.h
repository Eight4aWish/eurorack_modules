#pragma once
#include <stdint.h>

// Chord voicing: 4 notes as semitone intervals from the root
struct ChordVoicing {
    int8_t intervals[4];  // semitones from root (can be negative for inversions)
};

// A single chord progression (8 chords, mapped to white keys C D E F G A B + high C)
struct ChordProgression {
    const char* name;           // Short name for display (max 16 chars)
    ChordVoicing chords[8];     // 8 chords triggered by keys
};

// Category of progressions
struct ChordCategory {
    const char* name;           // Category name (max 10 chars)
    const ChordProgression* progressions;
    uint8_t count;
};

// ============================================================================
// POP PROGRESSIONS
// ============================================================================
static const ChordProgression kPopProgressions[] = {
    // 1. I - V - vi - IV (Axis of Awesome)
    {
        "I-V-vi-IV",
        {
            {{ 0,  4,  7, 12 }},  // C  - I   (C)
            {{ 7, 11, 14, 19 }},  // D  - V   (G)
            {{ 9, 12, 16, 21 }},  // E  - vi  (Am)
            {{ 5,  9, 12, 17 }},  // F  - IV  (F)
            {{ 0,  4,  7, 12 }},  // G  - I   (repeat)
            {{ 7, 11, 14, 19 }},  // A  - V
            {{ 9, 12, 16, 21 }},  // B  - vi
            {{ 5,  9, 12, 17 }},  // C2 - IV
        }
    },
    // 2. I - vi - IV - V (50s/Doo-wop)
    {
        "I-vi-IV-V",
        {
            {{ 0,  4,  7, 12 }},  // I   (C)
            {{ 9, 12, 16, 21 }},  // vi  (Am)
            {{ 5,  9, 12, 17 }},  // IV  (F)
            {{ 7, 11, 14, 19 }},  // V   (G)
            {{ 0,  4,  7, 12 }},  // I
            {{ 9, 12, 16, 21 }},  // vi
            {{ 5,  9, 12, 17 }},  // IV
            {{ 7, 11, 14, 19 }},  // V
        }
    },
    // 3. vi - IV - I - V (Sensitive Female / Minor feel)
    {
        "vi-IV-I-V",
        {
            {{ 9, 12, 16, 21 }},  // vi  (Am)
            {{ 5,  9, 12, 17 }},  // IV  (F)
            {{ 0,  4,  7, 12 }},  // I   (C)
            {{ 7, 11, 14, 19 }},  // V   (G)
            {{ 9, 12, 16, 21 }},  // vi
            {{ 5,  9, 12, 17 }},  // IV
            {{ 0,  4,  7, 12 }},  // I
            {{ 7, 11, 14, 19 }},  // V
        }
    },
    // 4. I - IV - vi - V
    {
        "I-IV-vi-V",
        {
            {{ 0,  4,  7, 12 }},  // I   (C)
            {{ 5,  9, 12, 17 }},  // IV  (F)
            {{ 9, 12, 16, 21 }},  // vi  (Am)
            {{ 7, 11, 14, 19 }},  // V   (G)
            {{ 0,  4,  7, 12 }},  // I
            {{ 5,  9, 12, 17 }},  // IV
            {{ 9, 12, 16, 21 }},  // vi
            {{ 7, 11, 14, 19 }},  // V
        }
    },
    // 5. I - V - vi - iii - IV - I - IV - V (Canon / Pachelbel)
    {
        "Canon",
        {
            {{ 0,  4,  7, 12 }},  // I   (C)
            {{ 7, 11, 14, 19 }},  // V   (G)
            {{ 9, 12, 16, 21 }},  // vi  (Am)
            {{ 4,  7, 11, 16 }},  // iii (Em)
            {{ 5,  9, 12, 17 }},  // IV  (F)
            {{ 0,  4,  7, 12 }},  // I   (C)
            {{ 5,  9, 12, 17 }},  // IV  (F)
            {{ 7, 11, 14, 19 }},  // V   (G)
        }
    },
    // 6. I - iii - IV - V
    {
        "I-iii-IV-V",
        {
            {{ 0,  4,  7, 12 }},  // I   (C)
            {{ 4,  7, 11, 16 }},  // iii (Em)
            {{ 5,  9, 12, 17 }},  // IV  (F)
            {{ 7, 11, 14, 19 }},  // V   (G)
            {{ 0,  4,  7, 12 }},  // I
            {{ 4,  7, 11, 16 }},  // iii
            {{ 5,  9, 12, 17 }},  // IV
            {{ 7, 11, 14, 19 }},  // V
        }
    },
    // 7. I - V/7 - vi - IV (with inversions)
    {
        "I-V/B-vi-IV",
        {
            {{ 0,  4,  7, 12 }},  // I   (C)
            {{-1,  2,  7, 11 }},  // V/7 (G/B - B in bass)
            {{ 9, 12, 16, 21 }},  // vi  (Am)
            {{ 5,  9, 12, 17 }},  // IV  (F)
            {{ 0,  4,  7, 12 }},  // I
            {{-1,  2,  7, 11 }},  // V/7
            {{ 9, 12, 16, 21 }},  // vi
            {{ 5,  9, 12, 17 }},  // IV
        }
    },
    // 8. vi - V - IV - V (Andalusian minor flavor)
    {
        "vi-V-IV-V",
        {
            {{ 9, 12, 16, 21 }},  // vi  (Am)
            {{ 7, 11, 14, 19 }},  // V   (G)
            {{ 5,  9, 12, 17 }},  // IV  (F)
            {{ 7, 11, 14, 19 }},  // V   (G)
            {{ 9, 12, 16, 21 }},  // vi
            {{ 7, 11, 14, 19 }},  // V
            {{ 5,  9, 12, 17 }},  // IV
            {{ 7, 11, 14, 19 }},  // V
        }
    },
};

// ============================================================================
// JAZZ PROGRESSIONS (with 7ths)
// ============================================================================
static const ChordProgression kJazzProgressions[] = {
    // 1. ii7 - V7 - Imaj7 - vi7 (Basic jazz)
    {
        "ii-V-I-vi",
        {
            {{ 2,  5,  9, 12 }},  // ii7   (Dm7)
            {{ 7, 11, 14, 17 }},  // V7    (G7)
            {{ 0,  4,  7, 11 }},  // Imaj7 (Cmaj7)
            {{ 9, 12, 16, 19 }},  // vi7   (Am7)
            {{ 2,  5,  9, 12 }},  // ii7
            {{ 7, 11, 14, 17 }},  // V7
            {{ 0,  4,  7, 11 }},  // Imaj7
            {{ 9, 12, 16, 19 }},  // vi7
        }
    },
    // 2. Imaj7 - vi7 - ii7 - V7 (Rhythm changes A section)
    {
        "RhythmChg",
        {
            {{ 0,  4,  7, 11 }},  // Imaj7 (Cmaj7)
            {{ 9, 12, 16, 19 }},  // vi7   (Am7)
            {{ 2,  5,  9, 12 }},  // ii7   (Dm7)
            {{ 7, 11, 14, 17 }},  // V7    (G7)
            {{ 0,  4,  7, 11 }},  // Imaj7
            {{ 9, 12, 16, 19 }},  // vi7
            {{ 2,  5,  9, 12 }},  // ii7
            {{ 7, 11, 14, 17 }},  // V7
        }
    },
    // 3. iii7 - vi7 - ii7 - V7 (Circle of fourths)
    {
        "Circle4ths",
        {
            {{ 4,  7, 11, 14 }},  // iii7  (Em7)
            {{ 9, 12, 16, 19 }},  // vi7   (Am7)
            {{ 2,  5,  9, 12 }},  // ii7   (Dm7)
            {{ 7, 11, 14, 17 }},  // V7    (G7)
            {{ 0,  4,  7, 11 }},  // Imaj7 (Cmaj7)
            {{ 5,  9, 12, 16 }},  // IVmaj7(Fmaj7)
            {{ 2,  5,  9, 12 }},  // ii7
            {{ 7, 11, 14, 17 }},  // V7
        }
    },
    // 4. Imaj7 - bVII7 - bVImaj7 - V7 (Backdoor)
    {
        "Backdoor",
        {
            {{ 0,  4,  7, 11 }},  // Imaj7  (Cmaj7)
            {{10, 14, 17, 20 }},  // bVII7  (Bb7)
            {{ 8, 12, 15, 19 }},  // bVImaj7(Abmaj7)
            {{ 7, 11, 14, 17 }},  // V7     (G7)
            {{ 0,  4,  7, 11 }},  // Imaj7
            {{10, 14, 17, 20 }},  // bVII7
            {{ 8, 12, 15, 19 }},  // bVImaj7
            {{ 7, 11, 14, 17 }},  // V7
        }
    },
    // 5. Imaj7 - #Idim7 - ii7 - #IIdim7 - iii7 - V7 (Chromatic walkup)
    {
        "ChromWalk",
        {
            {{ 0,  4,  7, 11 }},  // Imaj7  (Cmaj7)
            {{ 1,  4,  7, 10 }},  // #Idim7 (C#dim7)
            {{ 2,  5,  9, 12 }},  // ii7    (Dm7)
            {{ 3,  6,  9, 12 }},  // #IIdim7(D#dim7)
            {{ 4,  7, 11, 14 }},  // iii7   (Em7)
            {{ 7, 11, 14, 17 }},  // V7     (G7)
            {{ 0,  4,  7, 11 }},  // Imaj7
            {{ 7, 11, 14, 17 }},  // V7
        }
    },
    // 6. Imaj9 - IVmaj9 (Two chord vamp)
    {
        "Maj9Vamp",
        {
            {{ 0,  4, 11, 14 }},  // Imaj9  (Cmaj9 - no 5th)
            {{ 5,  9, 16, 19 }},  // IVmaj9 (Fmaj9 - no 5th)
            {{ 0,  4, 11, 14 }},  // Imaj9
            {{ 5,  9, 16, 19 }},  // IVmaj9
            {{ 0,  4, 11, 14 }},  // Imaj9
            {{ 5,  9, 16, 19 }},  // IVmaj9
            {{ 0,  4, 11, 14 }},  // Imaj9
            {{ 5,  9, 16, 19 }},  // IVmaj9
        }
    },
    // 7. Minor ii-V-i (iim7b5 - V7 - im7)
    {
        "MinoriiVi",
        {
            {{ 2,  5,  8, 12 }},  // iim7b5 (Dm7b5)
            {{ 7, 11, 14, 17 }},  // V7     (G7)
            {{ 0,  3,  7, 10 }},  // im7    (Cm7)
            {{ 0,  3,  7, 10 }},  // im7
            {{ 2,  5,  8, 12 }},  // iim7b5
            {{ 7, 11, 14, 17 }},  // V7
            {{ 0,  3,  7, 10 }},  // im7
            {{ 5,  8, 12, 15 }},  // ivm7   (Fm7)
        }
    },
    // 8. So What voicings (quartal)
    {
        "SoWhat",
        {
            {{ 0,  5, 10, 15 }},  // Dm11 (quartal from D)
            {{ 0,  5, 10, 15 }},  // Dm11
            {{ 2,  7, 12, 17 }},  // Ebm11 (up half step)
            {{ 2,  7, 12, 17 }},  // Ebm11
            {{ 0,  5, 10, 15 }},  // Dm11
            {{ 0,  5, 10, 15 }},  // Dm11
            {{ 2,  7, 12, 17 }},  // Ebm11
            {{ 2,  7, 12, 17 }},  // Ebm11
        }
    },
};

// ============================================================================
// EDM / ELECTRONIC PROGRESSIONS
// ============================================================================
static const ChordProgression kEdmProgressions[] = {
    // 1. i - VI - III - VII (Epic minor / Trance)
    {
        "EpicMinor",
        {
            {{ 0,  3,  7, 12 }},  // i   (Cm)
            {{ 8, 12, 15, 20 }},  // VI  (Ab)
            {{ 3,  7, 10, 15 }},  // III (Eb)
            {{10, 14, 17, 22 }},  // VII (Bb)
            {{ 0,  3,  7, 12 }},  // i
            {{ 8, 12, 15, 20 }},  // VI
            {{ 3,  7, 10, 15 }},  // III
            {{10, 14, 17, 22 }},  // VII
        }
    },
    // 2. i - i - VI - VII
    {
        "DarkDrive",
        {
            {{ 0,  3,  7, 12 }},  // i   (Cm)
            {{ 0,  3,  7, 12 }},  // i
            {{ 8, 12, 15, 20 }},  // VI  (Ab)
            {{10, 14, 17, 22 }},  // VII (Bb)
            {{ 0,  3,  7, 12 }},  // i
            {{ 0,  3,  7, 12 }},  // i
            {{ 8, 12, 15, 20 }},  // VI
            {{10, 14, 17, 22 }},  // VII
        }
    },
    // 3. vi - IV - I - V (with added 7ths, EDM style)
    {
        "Euphoric",
        {
            {{ 9, 12, 16, 21 }},  // vi  (Am)
            {{ 5,  9, 12, 17 }},  // IV  (F)
            {{ 0,  4,  7, 12 }},  // I   (C)
            {{ 7, 11, 14, 19 }},  // V   (G)
            {{ 9, 12, 16, 21 }},  // vi
            {{ 5,  9, 12, 17 }},  // IV
            {{ 0,  4,  7, 12 }},  // I
            {{ 7, 11, 14, 19 }},  // V
        }
    },
    // 4. i - iv - i - VII (Minimal techno)
    {
        "Minimal",
        {
            {{ 0,  3,  7, 12 }},  // i   (Cm)
            {{ 5,  8, 12, 17 }},  // iv  (Fm)
            {{ 0,  3,  7, 12 }},  // i
            {{10, 14, 17, 22 }},  // VII (Bb)
            {{ 0,  3,  7, 12 }},  // i
            {{ 5,  8, 12, 17 }},  // iv
            {{ 0,  3,  7, 12 }},  // i
            {{10, 14, 17, 22 }},  // VII
        }
    },
    // 5. I - I - I - IV (House build)
    {
        "HouseBld",
        {
            {{ 0,  4,  7, 12 }},  // I (C)
            {{ 0,  4,  7, 12 }},  // I
            {{ 0,  4,  7, 12 }},  // I
            {{ 5,  9, 12, 17 }},  // IV (F)
            {{ 0,  4,  7, 12 }},  // I
            {{ 0,  4,  7, 12 }},  // I
            {{ 5,  9, 12, 17 }},  // IV
            {{ 7, 11, 14, 19 }},  // V (G)
        }
    },
    // 6. Power chord progression (5ths only)
    {
        "Power5ths",
        {
            {{ 0,  7, 12, 19 }},  // C5
            {{ 5, 12, 17, 24 }},  // F5
            {{ 7, 14, 19, 26 }},  // G5
            {{ 5, 12, 17, 24 }},  // F5
            {{ 0,  7, 12, 19 }},  // C5
            {{ 3, 10, 15, 22 }},  // Eb5
            {{ 5, 12, 17, 24 }},  // F5
            {{ 7, 14, 19, 26 }},  // G5
        }
    },
    // 7. Stab chords (short, punchy voicings)
    {
        "Stabs",
        {
            {{ 0,  7, 12, 16 }},  // C(add9 no3)
            {{ 5, 12, 17, 21 }},  // F(add9 no3)
            {{ 7, 14, 19, 23 }},  // G(add9 no3)
            {{10, 17, 22, 26 }},  // Bb(add9 no3)
            {{ 0,  7, 12, 16 }},  // C
            {{ 3, 10, 15, 19 }},  // Eb
            {{ 5, 12, 17, 21 }},  // F
            {{ 7, 14, 19, 23 }},  // G
        }
    },
    // 8. Trance gate (simple for gating effects)
    {
        "TranceGt",
        {
            {{ 0,  3,  7, 12 }},  // Cm
            {{ 0,  3,  7, 12 }},  // Cm
            {{ 8, 12, 15, 20 }},  // Ab
            {{ 8, 12, 15, 20 }},  // Ab
            {{10, 14, 17, 22 }},  // Bb
            {{10, 14, 17, 22 }},  // Bb
            {{ 3,  7, 10, 15 }},  // Eb
            {{ 3,  7, 10, 15 }},  // Eb
        }
    },
};

// ============================================================================
// CINEMATIC / AMBIENT PROGRESSIONS
// ============================================================================
static const ChordProgression kCinematicProgressions[] = {
    // 1. Suspended/Unresolved
    {
        "Suspended",
        {
            {{ 0,  5,  7, 12 }},  // Csus4
            {{ 0,  2,  7, 12 }},  // Csus2
            {{ 5,  7, 12, 17 }},  // Fsus4 (no 3rd)
            {{ 7,  9, 14, 19 }},  // Gsus2
            {{ 0,  5,  7, 12 }},  // Csus4
            {{ 9, 12, 14, 21 }},  // Asus4
            {{ 5, 10, 12, 17 }},  // Fsus4
            {{ 7, 12, 14, 19 }},  // Gsus4
        }
    },
    // 2. Cluster/Close voicings
    {
        "Clusters",
        {
            {{ 0,  2,  4,  7 }},  // C add9 (close)
            {{ 5,  7,  9, 12 }},  // F add9 (close)
            {{ 7,  9, 11, 14 }},  // G add9 (close)
            {{ 9, 11, 14, 16 }},  // Am add9 (close)
            {{ 0,  2,  4,  7 }},  // C
            {{ 4,  7,  9, 11 }},  // Em add9
            {{ 5,  7,  9, 12 }},  // F
            {{ 7, 11, 14, 16 }},  // G7 (close)
        }
    },
    // 3. Epic wide voicings
    {
        "EpicWide",
        {
            {{-5,  0,  7, 16 }},  // C (bass G below)
            {{-7,  0,  5, 12 }},  // F/C
            {{-5,  2,  7, 14 }},  // G/D
            {{-3,  0,  9, 16 }},  // Am/E
            {{-5,  0,  7, 16 }},  // C
            {{-7,  5,  9, 17 }},  // F
            {{-5,  4,  7, 16 }},  // C/E
            {{-7,  2,  7, 14 }},  // G
        }
    },
    // 4. Minor drone
    {
        "MinorDrn",
        {
            {{ 0,  7, 12, 15 }},  // Cm (wide)
            {{ 0,  5, 12, 17 }},  // Fm/C
            {{ 0,  7, 10, 15 }},  // Eb/C
            {{ 0,  3,  8, 15 }},  // Ab/C
            {{ 0,  7, 12, 15 }},  // Cm
            {{ 0,  5, 10, 15 }},  // Bbsus/C
            {{ 0,  7, 12, 15 }},  // Cm
            {{ 0,  3, 10, 15 }},  // Cm7
        }
    },
    // 5. Ethereal (major 7ths, 9ths)
    {
        "Ethereal",
        {
            {{ 0,  4, 11, 14 }},  // Cmaj9
            {{ 9, 12, 19, 23 }},  // Am9
            {{ 5,  9, 16, 19 }},  // Fmaj9
            {{ 7, 11, 14, 21 }},  // G9 (no 5th)
            {{ 0,  4, 11, 14 }},  // Cmaj9
            {{ 4,  7, 14, 18 }},  // Em9
            {{ 5,  9, 16, 19 }},  // Fmaj9
            {{ 0,  4, 11, 14 }},  // Cmaj9
        }
    },
    // 6. Tension/Release
    {
        "Tension",
        {
            {{ 0,  1,  7,  8 }},  // Cluster tension
            {{ 0,  4,  7, 12 }},  // C (release)
            {{ 5,  6, 12, 13 }},  // Cluster tension
            {{ 5,  9, 12, 17 }},  // F (release)
            {{ 7,  8, 14, 15 }},  // Cluster tension
            {{ 7, 11, 14, 19 }},  // G (release)
            {{ 0,  1,  4,  7 }},  // Soft tension
            {{ 0,  4,  7, 12 }},  // C (final release)
        }
    },
    // 7. Ascending emotional
    {
        "Ascending",
        {
            {{ 0,  4,  7, 12 }},  // C
            {{ 2,  5,  9, 14 }},  // Dm
            {{ 4,  7, 11, 16 }},  // Em
            {{ 5,  9, 12, 17 }},  // F
            {{ 7, 11, 14, 19 }},  // G
            {{ 9, 12, 16, 21 }},  // Am
            {{11, 14, 17, 23 }},  // Bdim
            {{12, 16, 19, 24 }},  // C (octave up)
        }
    },
    // 8. Descending melancholy
    {
        "Descend",
        {
            {{12, 16, 19, 24 }},  // C (high)
            {{11, 14, 17, 23 }},  // B dim
            {{ 9, 12, 16, 21 }},  // Am
            {{ 7, 11, 14, 19 }},  // G
            {{ 5,  9, 12, 17 }},  // F
            {{ 4,  7, 11, 16 }},  // Em
            {{ 2,  5,  9, 14 }},  // Dm
            {{ 0,  4,  7, 12 }},  // C
        }
    },
};

// ============================================================================
// LOFI / NEO-SOUL PROGRESSIONS
// ============================================================================
static const ChordProgression kLofiProgressions[] = {
    // 1. ii9 - V13 - Imaj9 - vi7
    {
        "NeoSoul1",
        {
            {{ 2,  5, 12, 16 }},  // Dm9
            {{ 7, 11, 17, 21 }},  // G13 (no 5th)
            {{ 0,  4, 11, 14 }},  // Cmaj9
            {{ 9, 12, 16, 19 }},  // Am7
            {{ 2,  5, 12, 16 }},  // Dm9
            {{ 7, 11, 17, 21 }},  // G13
            {{ 0,  4, 11, 14 }},  // Cmaj9
            {{ 9, 12, 16, 19 }},  // Am7
        }
    },
    // 2. Imaj7 - iii7 - vi9 - IVmaj7
    {
        "NeoSoul2",
        {
            {{ 0,  4,  7, 11 }},  // Cmaj7
            {{ 4,  7, 11, 14 }},  // Em7
            {{ 9, 12, 19, 23 }},  // Am9
            {{ 5,  9, 12, 16 }},  // Fmaj7
            {{ 0,  4,  7, 11 }},  // Cmaj7
            {{ 4,  7, 11, 14 }},  // Em7
            {{ 9, 12, 19, 23 }},  // Am9
            {{ 5,  9, 12, 16 }},  // Fmaj7
        }
    },
    // 3. Chill hop (simple 7ths)
    {
        "ChillHop",
        {
            {{ 0,  4,  7, 10 }},  // C7
            {{ 5,  9, 12, 15 }},  // Fm7
            {{ 7, 11, 14, 17 }},  // G7
            {{ 0,  3,  7, 10 }},  // Cm7
            {{ 0,  4,  7, 10 }},  // C7
            {{ 8, 12, 15, 18 }},  // Abmaj7
            {{ 5,  9, 12, 15 }},  // Fm7
            {{ 7, 11, 14, 17 }},  // G7
        }
    },
    // 4. Jazzy lofi
    {
        "JazzyLofi",
        {
            {{ 0,  4, 11, 14 }},  // Cmaj9
            {{10, 14, 17, 20 }},  // Bb7
            {{ 9, 12, 16, 19 }},  // Am7
            {{ 8, 12, 15, 18 }},  // Abmaj7
            {{ 7, 11, 14, 17 }},  // G7
            {{ 5,  9, 12, 16 }},  // Fmaj7
            {{ 4,  7, 11, 14 }},  // Em7
            {{ 2,  5,  9, 12 }},  // Dm7
        }
    },
    // 5. Minor 9 vibes
    {
        "Minor9s",
        {
            {{ 0,  3, 10, 14 }},  // Cm9
            {{ 5,  8, 15, 19 }},  // Fm9
            {{ 7, 10, 17, 21 }},  // Gm9
            {{ 0,  3, 10, 14 }},  // Cm9
            {{10, 14, 17, 20 }},  // Bb7
            {{ 8, 12, 15, 19 }},  // Ab9
            {{ 5,  8, 15, 19 }},  // Fm9
            {{ 7, 11, 14, 17 }},  // G7
        }
    },
    // 6. Dreamy pads
    {
        "DreamPad",
        {
            {{ 0,  7, 11, 16 }},  // Cmaj7 (spread)
            {{ 9, 16, 19, 24 }},  // Am7 (spread)
            {{ 5, 12, 16, 21 }},  // Fmaj7 (spread)
            {{ 7, 14, 17, 23 }},  // G7 (spread)
            {{ 0,  7, 11, 16 }},  // Cmaj7
            {{ 4, 11, 14, 19 }},  // Em7 (spread)
            {{ 5, 12, 16, 21 }},  // Fmaj7
            {{ 2,  9, 12, 17 }},  // Dm7 (spread)
        }
    },
    // 7. RnB ballad
    {
        "RnBBallad",
        {
            {{ 0,  4,  7, 11 }},  // Cmaj7
            {{ 0,  4,  9, 14 }},  // C6/9
            {{ 9, 12, 16, 21 }},  // Am7
            {{ 9, 14, 16, 21 }},  // Am9
            {{ 5,  9, 12, 16 }},  // Fmaj7
            {{ 5,  9, 14, 17 }},  // Fmaj9
            {{ 7, 11, 14, 19 }},  // G7
            {{ 7, 11, 17, 21 }},  // G9
        }
    },
    // 8. Tape wobble (with slight variations)
    {
        "TapeWobl",
        {
            {{ 0,  4,  7, 11 }},  // Cmaj7
            {{ 0,  5,  7, 12 }},  // Csus4
            {{ 0,  4,  7, 10 }},  // C7
            {{ 0,  4,  7, 11 }},  // Cmaj7
            {{ 5,  9, 12, 16 }},  // Fmaj7
            {{ 5, 10, 12, 17 }},  // Fsus4
            {{ 5,  9, 12, 15 }},  // F7
            {{ 5,  9, 12, 16 }},  // Fmaj7
        }
    },
};

// ============================================================================
// CATEGORY ARRAY
// ============================================================================
static const ChordCategory kChordCategories[] = {
    { "Pop",       kPopProgressions,       sizeof(kPopProgressions)      / sizeof(kPopProgressions[0]) },
    { "Jazz",      kJazzProgressions,      sizeof(kJazzProgressions)     / sizeof(kJazzProgressions[0]) },
    { "EDM",       kEdmProgressions,       sizeof(kEdmProgressions)      / sizeof(kEdmProgressions[0]) },
    { "Cinematic", kCinematicProgressions, sizeof(kCinematicProgressions)/ sizeof(kCinematicProgressions[0]) },
    { "LoFi",      kLofiProgressions,      sizeof(kLofiProgressions)     / sizeof(kLofiProgressions[0]) },
};

static const uint8_t kNumCategories = sizeof(kChordCategories) / sizeof(kChordCategories[0]);

// ============================================================================
// HELPER: Map input note to chord index (0-7)
// White keys C,D,E,F,G,A,B map to 0-6, next C maps to 7
// Black keys map to nearest white key below
// ============================================================================
inline uint8_t noteToChordIndex(uint8_t midiNote) {
    uint8_t noteInOctave = midiNote % 12;
    // Map: C=0, D=1, E=2, F=3, G=4, A=5, B=6
    // Black keys: C#->0, D#->1, F#->3, G#->4, A#->5
    static const uint8_t mapping[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    uint8_t idx = mapping[noteInOctave];
    // Use octave info to wrap to 0-7 (octave 0-1 give 0-6, higher octaves give 7 for the 8th chord)
    // Simple: just return 0-6, chord 7 is accessed by playing higher octave C
    if (noteInOctave == 0 && (midiNote / 12) > 3) {
        // C in octave 4+ triggers chord 7
        return 7;
    }
    return idx;
}

// ============================================================================
// VOICING TRANSFORMS
// ============================================================================
enum VoicingType : uint8_t {
    VOICING_ROOT = 0,      // Root position
    VOICING_INV1,          // 1st inversion
    VOICING_INV2,          // 2nd inversion
    VOICING_DROP2,         // Drop-2 voicing
    VOICING_SPREAD,        // Spread (wide)
    VOICING_COUNT
};

inline void applyVoicing(int8_t* intervals, VoicingType voicing) {
    switch (voicing) {
        case VOICING_ROOT:
            // No change
            break;
        case VOICING_INV1:
            // Move lowest note up an octave
            intervals[0] += 12;
            // Sort to maintain ascending order
            if (intervals[0] > intervals[1]) { int8_t t = intervals[0]; intervals[0] = intervals[1]; intervals[1] = t; }
            if (intervals[1] > intervals[2]) { int8_t t = intervals[1]; intervals[1] = intervals[2]; intervals[2] = t; }
            if (intervals[2] > intervals[3]) { int8_t t = intervals[2]; intervals[2] = intervals[3]; intervals[3] = t; }
            break;
        case VOICING_INV2:
            // Move two lowest notes up an octave
            intervals[0] += 12;
            intervals[1] += 12;
            // Sort
            for (int i = 0; i < 3; i++) {
                for (int j = i+1; j < 4; j++) {
                    if (intervals[i] > intervals[j]) { int8_t t = intervals[i]; intervals[i] = intervals[j]; intervals[j] = t; }
                }
            }
            break;
        case VOICING_DROP2:
            // Drop the 2nd highest note down an octave
            intervals[2] -= 12;
            // Sort
            for (int i = 0; i < 3; i++) {
                for (int j = i+1; j < 4; j++) {
                    if (intervals[i] > intervals[j]) { int8_t t = intervals[i]; intervals[i] = intervals[j]; intervals[j] = t; }
                }
            }
            break;
        case VOICING_SPREAD:
            // Spread: bass down octave, top up octave
            intervals[0] -= 12;
            intervals[3] += 12;
            break;
        default:
            break;
    }
}

static const char* kVoicingNames[VOICING_COUNT] = {
    "Root", "Inv1", "Inv2", "Drop2", "Spread"
};
