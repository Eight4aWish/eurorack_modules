// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// The MOD2 app: HAGIWO's MOD2 voices on the analogue board Rev B (Pico 2 W), behind the MOD2/Melon
// panel, one at a time from a menu on the encoder. docs/REVB_MOD2.md has the panel and the voices.
//
// Each voice in voices/ is one MOD2 sketch (CC0), its engine kept as published. This file is the
// MOD2 around it:
// - the panel read as the MOD2 reads it (voice.h's Panel): POT1 and POT2 as they are, POT3 and the
//   CV summed and inverted as the MOD2's input stage does;
// - the voice run at its own sample rate (31.25, 36.6 or 48 kHz) and its PWM level carried to the
//   codec, then the MOD2's output stage in software (frame.h);
// - the voice menu, and the settings each sketch keeps in EEPROM, here in one flash sector.

#include <math.h>
#include <new>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <type_traits>

#define COMPUTERCARD_SAMPLE_RATE_DIV 1
#define REVB_APP_NAME "MOD2"
#include "ComputerCard.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

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

namespace {
using namespace mod2;

// The menu, in the order the encoder steps through it
enum VoiceId { KICK, RETRO_KICK, SNARE, CLAP, HIHAT, CLAVES, FM_DRUM, METAL, LASER, VOICE_COUNT };

// One voice at a time lives in the arena: the largest (the hi-hat's noise table) sets its size
template <typename... T>
constexpr size_t MaxSize() {
  size_t m = 0;
  ((m = sizeof(T) > m ? sizeof(T) : m), ...);
  return m;
}
template <typename... T>
constexpr bool AllTrivial() {
  return (std::is_trivially_destructible<T>::value && ...);
}
alignas(8) uint8_t arena[MaxSize<Kick, RetroKick, Snare, Clap, HiHat, Claves, FmDrum, Metal, Laser>()];
static_assert(AllTrivial<Kick, RetroKick, Snare, Clap, HiHat, Claves, FmDrum, Metal, Laser>(),
              "a voice is replaced without being destroyed");

Voice *Make(int id) {
  switch (id) {
    case RETRO_KICK: return new (arena) RetroKick;
    case SNARE: return new (arena) Snare;
    case CLAP: return new (arena) Clap;
    case HIHAT: return new (arena) HiHat;
    case CLAVES: return new (arena) Claves;
    case FM_DRUM: return new (arena) FmDrum;
    case METAL: return new (arena) Metal;
    case LASER: return new (arena) Laser;
    default: return new (arena) Kick;
  }
}

// The settings: the voice last chosen and each voice's EEPROM. They live in the sector below the one
// Bends uses, so flashing one app doesn't lose the other's.
constexpr uint32_t SETTINGS_OFFSET = PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE;
constexpr uint32_t SETTINGS_MAGIC = 0x32444F4Du;  // "MOD2"
constexpr uint8_t SETTINGS_VERSION = 1;
struct Settings {
  uint32_t magic;
  uint8_t version;
  uint8_t voice;
  uint8_t len[VOICE_COUNT];
  uint8_t blob[VOICE_COUNT][SAVE_BYTES];
  uint16_t crc;
};
constexpr size_t SETTINGS_BYTES = (sizeof(Settings) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE;
// A save waits until the settings have been still this long and the voice is quiet: writing flash
// stops the audio for a few tens of milliseconds, as EEPROM.commit() does on a MOD2.
constexpr uint32_t SAVE_DELAY_MS = 1500;

constexpr float FS = ComputerCard::SampleRate();
constexpr uint32_t CONTROL_DIV = 48;                    // the voices' loop(): about 1 kHz
constexpr uint32_t IN2_HOLD = (uint32_t)(FS * 0.015f);  // 15 ms

}  // namespace

class Mod2Card : public ComputerCard {
 protected:
  void ProcessSample() override {
    // The panel as the MOD2 reads it. POT3 and the CV share its A2: 0 to +5 V spans the pot.
    float y = KnobVal(Y) * (1.0f / 4095.0f) + CVIn1() * (6.0f / 2048.0f) * (1.0f / 5.0f);
    panel.a[0] = KnobVal(Main) * (1.0f / 4095.0f);
    panel.a[1] = KnobVal(X) * (1.0f / 4095.0f);
    panel.a[2] = 1.0f - Clamp(y, 0.0f, 1.0f);
    bool in2 = PulseIn2();
    if (in2) in2Hold = IN2_HOLD;
    else if (in2Hold) in2Hold--;
    panel.in2 = in2;
    panel.in2Recent = in2 || in2Hold;
    bool trig = PulseIn1RisingEdge();

    float duty = restDuty;
    if (!switching) {
      Voice *v = voice;
      if (pushPress) {
        pushPress = false;
        v->Button(true, 0);
      }
      if (pushRelease) {
        pushRelease = false;
        v->Button(false, pushHeldMs);
      }
      if (trig) v->Trigger();
      if (++controlTick >= CONTROL_DIV) {
        controlTick = 0;
        v->Controls();
      }
      duty = runner.Next(v);
      LedBrightness(0, (uint16_t)(v->Led() * 4095.0f));
    }

    bool bright = SwitchVal() != Down;  // JP1: up (or middle) 15.9 kHz, down 5.0 kHz
    if (bright != out.bright) out.SetFilter(bright);
    out.Process(duty);
    AudioOutVolts(0, out.ac);  // OUT
    AudioOutVolts(1, out.dc);  // OUT DC
  }

  void BackgroundLoop() override {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!voice) {
      LoadSettings();
      SwitchTo(settingsValid && settings.voice < VOICE_COUNT ? (int)settings.voice : (int)KICK);
    }

    int32_t turns = EncoderTurns();
    if (turns) {
      SwitchTo(((current + turns) % VOICE_COUNT + VOICE_COUNT) % VOICE_COUNT);
      dirty = true;
      dirtyAt = now;
    }

    // The MOD2's BUTTON: press and release, with how long it was held (the hi-hat's long press)
    bool held = PushHeld();
    if (held != lastHeld) {
      lastHeld = held;
      if (held) {
        pressAt = now;
        pushPress = true;
      } else {
        pushHeldMs = now - pressAt;
        pushRelease = true;
      }
    }

    voice->Background();
    if (voice->TakeSaveRequest()) {
      dirty = true;
      dirtyAt = now;
    }
    if (dirty && now - dirtyAt >= SAVE_DELAY_MS && !voice->Active()) {
      dirty = false;
      WriteSettings();
    }

    voice->Label(label);
    DisplayName(label);
  }

 private:
  // Swap voices from the background loop. The interrupt plays the rest level meanwhile, and runs on
  // this core, so it never sees a voice half built.
  void SwitchTo(int id) {
    if (voice) KeepBlob();
    switching = true;
    __compiler_memory_barrier();
    Voice *v = Make(id);
    current = id;
    v->Init();
    if (settingsValid && settings.len[id]) v->Load(settings.blob[id], settings.len[id]);
    restDuty = runner.Start(v, FS);
    controlTick = 0;
    pushPress = pushRelease = false;
    voice = v;
    __compiler_memory_barrier();
    switching = false;
  }

  void KeepBlob() {
    uint8_t buf[SAVE_BYTES];
    size_t n = voice->Save(buf);
    if (n > SAVE_BYTES) n = SAVE_BYTES;
    memcpy(settings.blob[current], buf, n);
    settings.len[current] = (uint8_t)n;
  }

  void LoadSettings() {
    memcpy(&settings, (const void *)(XIP_BASE + SETTINGS_OFFSET), sizeof settings);
    settingsValid = settings.magic == SETTINGS_MAGIC && settings.version == SETTINGS_VERSION &&
                    settings.crc == CRCencode((const uint8_t *)&settings, offsetof(Settings, crc));
    if (!settingsValid) memset(&settings, 0, sizeof settings);
  }

  void WriteSettings() {
    KeepBlob();
    settings.magic = SETTINGS_MAGIC;
    settings.version = SETTINGS_VERSION;
    settings.voice = (uint8_t)current;
    settings.crc = CRCencode((const uint8_t *)&settings, offsetof(Settings, crc));
    settingsValid = true;
    static uint8_t page[SETTINGS_BYTES];
    memset(page, 0xFF, sizeof page);
    memcpy(page, &settings, sizeof settings);
    if (memcmp(page, (const void *)(XIP_BASE + SETTINGS_OFFSET), sizeof page) == 0) return;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_OFFSET, page, sizeof page);
    restore_interrupts(ints);
  }

  // shared with the interrupt
  Voice *volatile voice = nullptr;
  volatile bool switching = true;
  volatile bool pushPress = false, pushRelease = false;
  volatile uint32_t pushHeldMs = 0;
  int current = KICK;
  Runner runner;
  OutputStage out{FS};
  float restDuty = 0.5f;
  uint32_t controlTick = 0, in2Hold = 0;

  // the background loop's
  Settings settings = {};
  bool settingsValid = false;
  bool dirty = false, lastHeld = false;
  uint32_t dirtyAt = 0, pressAt = 0;
  char label[11] = "MOD2";
};

Mod2Card card;

int main() {
  card.Run();
}
