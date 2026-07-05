````markdown
# Eurorack Firmware — Pico 2 W Guide (OC)

This guide covers navigation, controls, and behavior for the current functional patches on Pico 2 W. The OLED UI uses a compact fixed grid optimized for split (yellow/blue) SSD1306 displays.

## Panel & Interaction Conventions

The front panel is a 2×5 grid; the firmware's conventions follow it:

```
Pot1   BTN     <- navigation row
Pot2   Pot3    <- parameter row
IN1    IN2
OUT1   OUT2    <- pitch row (two-voice patches)
OUT3   OUT4    <- gate row
```

- **Navigation row (Pot1 + BTN)**: in the menu, Pot1 scrolls the highlight and BTN selects. In a patch, Pot1 is the "global" parameter (tempo / level / size) and BTN cycles the patch's page/mode.
- **Columns are voices/channels**: column A = Pot2 + IN1 + OUT1/OUT3; column B = Pot3 + IN2 + OUT2/OUT4. Two-voice patches put voice A's pitch on OUT1 and gate on OUT3 (left column), voice B on OUT2/OUT4 (right column) — so OUT1/OUT2 form the pitch row and OUT3/OUT4 the gate row.
- **Jack names**: user-facing labels are `IN1/IN2` and `OUT1..OUT4` (panel reading order). Code aliases live in `include/pico2w_oc/pins.h`; calibration indices `voltsToDac(0..3)` correspond to OUT1..OUT4.
- Pots are read inverted so clockwise increases value; most patches smooth them, and page/target switches use soft-takeover ("pot pickup") so stored values aren't clobbered.
- Layout Grid: Title at `y=0`. Content rows at `y=16, 26, 36, 46, 56`. Some patches show mode/status on the right of the title line.

## Navigation Summary

- Menu: **Pot1 scrolls** the highlight (engages after a deliberate move, so knob jitter can't scroll); short press = next (fallback); long press = enter.
- Patch: short = the patch's page/mode/action; long = back to menu. While the menu is up, a still-running background patch's pot values are frozen so scrolling with Pot1 can't disturb it.

## Patches

### Diag
- Purpose: Hardware diagnostics for pots, ADS1115 inputs, and MCP4728 outputs.
- Display:
  - Button state and raw reads for Pot1/2/3.
  - ADS raw codes for IN1/IN2.
  - DAC codes for OUT1..OUT4 (`O1..O4`, mapped via `include/pico2w_oc/pins.h`).
- Controls:
  - Short press: Cycle selected output (OUT1→OUT2→OUT3→OUT4).
  - Pot1: Sets the DAC code (0..4095) for the selected output only; others are set to 0.
  - Pot2/Pot3: No effect.

### Clock
- Purpose: Lightweight 4-channel clock with independent divisions/multiplications and optional external clocking.
- Outputs: 10 ms gates on OUT1..OUT4 using fixed "gate codes" (about 0 V at code ~2047, +5 V at code 0), intentionally ignoring calibration.
- Divisions: /16, /12, /8, /6, /5, /4, /3, /2, 1, x2, x3, x4, x5, x6, x8 (15 options per channel).
- Display: `INT/EXT`, BPM, and `RUN/STOP`; per-channel division labels for CH0..CH3 with `>` indicating the selected channel.
- Controls:
  - Short press: Toggle RUN/STOP.
  - Pot1: BPM (INT mode: 30–300 BPM). If external clock edges are detected on `AD_EXT_CLOCK_CH`, the clock follows the external period.
  - Pot2: Select which channel to edit (CH0 / CH1 / CH2 / CH3).
  - Pot3: Set division/multiplication for the selected channel.
  - Pot pickup: After selecting a different channel with Pot2, Pot3 stays inactive until it crosses (or already matches) that channel's stored division, so divisions aren't clobbered when scanning channels.

### Euclid
- Purpose: Four independent Euclidean rhythm generators, one per output, with a Hagiwo-style polygon display.
- Outputs: 30 ms gates on OUT1..OUT4 (fixed gate codes as above), one jack per generator (CH0..CH3 → OUT1..OUT4).
- Clock: advances one step per rising edge on the external clock input (`AD_EXT_CLOCK_CH`); when no external clock is patched it free-runs at the internal BPM. Title-right shows `EXT`/`INT`.
- Pages (short press cycles `TEMPO → CH0 → CH1 → CH2 → CH3 → TEMPO`; title-right shows the active page). Each pot has one fixed job per page:
  - `TEMPO`: Pot1 = internal BPM (30–300). Display shows the BPM large plus a 2×2 overview of all four channels as `hits/steps r<rotation>`.
  - `CH0..CH3` (edit one channel): Pot1 = Steps (vertices, 1–16), Pot2 = Pulses (active vertices, 0–Steps), Pot3 = Rotation (0–Steps−1).
- Polygon display (on each channel page): vertices are spaced evenly round a circle with step 0 at the top; active steps are filled discs joined by edges into the polygon; inactive steps are dots. A tick above the top vertex marks the step-0 start, so turning Rotation visibly rotates the pattern against it. A ring rides the current step as the playhead. The right column lists Steps / Hits / Rot and the clock source.
- Pot pickup: switching pages arms soft-takeover, so each pot stays inactive until it crosses (or already matches) the new page parameter's stored value — values aren't clobbered when paging. Within a page the pots track immediately.

### Env (Dual Envelopes)
- Purpose: Two independent macro ADSR-style envelopes.
- Outputs: Env1→OUT1, Env2→OUT2 (column convention: envelope 1 = left column, 2 = right). 0 V baseline at code ~2047, up to ~+5 V at code 0. Each envelope has its own velocity scaling.
- Triggers: Env1 from IN1; Env2 from IN2 (rising-edge detection).
- Controls (smoothed):
  - Pot1: Velocity (amplitude) for the selected envelope.
  - Pot2: AD macro — Attack and Decay are linked; turning clockwise lengthens both. Short settings are punchy.
  - Pot3: S/R macro — Sustain level and Release time together; higher values raise sustain and lengthen release.
  - Short press: Toggle which envelope is edited (E1/E2). Header shows the active target; first row displays `Vel <n>%` for the selected envelope.
  - Pot pickup: After switching envelopes, each pot stays inactive until it crosses (or already matches) the selected envelope's stored value, so the other envelope's settings aren't clobbered.
- Envelope Model:
  - Attack(ms) = 1 + (AD²) × 2000.
  - Decay(ms) = Attack × (0.15 + 0.85 × Sustain) for percussive response.
  - Release(ms) = 1 + (SR²) × 2000.
  - Output = Level × Velocity, mapped to DAC codes for ~0..+5 V.

### Quant
- Purpose: 2-channel semitone quantizer (1 V/oct).
- Inputs: IN1, IN2 (mapped to volts via calibration if available).
- Outputs: OUT1, OUT2 (columns: IN1→OUT1, IN2→OUT2; mapped back to DAC codes via calibration).
- Display: In0/Out0 and In1/Out1 values plus current DAC codes.
- Controls: None (always-on behavior).

### Scope
- Purpose: Simple oscilloscope for IN1.
- Display: Waveform trace under the title; title-right shows `Vx<gain> H<samples> M<mid>`.
- Controls:
  - Pot1: Vertical gain (~0.25x to 4x).
  - Pot2: Horizontal window (32..128 samples).
  - Pot3: Midpoint offset.

### MIDI-to-CV modes (UsbMIDI and NetMIDI)

Both MIDI patches share one CV engine with **two modes, toggled by short-press** (the title-right shows `DUO`/`CLK`). They differ only in transport — UsbMIDI over USB, NetMIDI over WiFi. Channels are routed in software, so both receive all channels (OMNI) and split by channel per voice.

Outputs follow the panel's column convention (voice A = left column under Pot2/IN1, voice B or clock = right column under Pot3/IN2):

| Mode | OUT1 | OUT2 | OUT3 | OUT4 | Pot2 | Pot3 |
|------|------|------|------|------|------|------|
| **DUO** (dual voice) | voice A pitch | voice B pitch | voice A gate | voice B gate | voice A channel (1–16) | voice B channel (1–16) |
| **CLK** (clock + voice) | voice pitch | clock | voice gate | reset | voice channel (1–16) | clock division (1/16, 1/8, 1/4, 1/2) |

- **DUO** gives two independent monophonic gate/pitch voices, each with its own last-note-priority stack, on two MIDI channels — voice A on OUT1/OUT3, voice B on OUT2/OUT4.
- **CLK** derives clock + reset from MIDI System-Realtime on the right column: OUT2 pulses per clock division (24 PPQN → 1/4 = 24 ticks, etc.), OUT4 pulses on Start/Continue; the left column stays a gate/pitch voice.
- Pitch is 1 V/oct, MIDI note 36/C2 = 0 V, calibrated via `voltsToDac()`. The display shows each row's jacks as `o1+3` / `o2+4`.

### UsbMIDI (USB MIDI-to-CV)
- The engine above, over USB MIDI. The device enumerates as "Pico2W OC MIDI".
- Display: title `UsbMIDI` + mode, then per-voice channel/note/gate (DUO) or clock division + voice (CLK).

### NetMIDI (Network MIDI-to-CV over WiFi)
- The same engine, over RTP-MIDI ("AppleMIDI" / Apple's Network MIDI) received on the Pico 2 W's WiFi.
- Display:
  - Joining: shows `WiFi..` and the target SSID while it associates; if you see the placeholder SSID, `secrets.h` is missing (see Setup below).
  - Connected: the module's IP address, session state (`LINK` once a controller has an active RTP session, `wait` otherwise), plus the mode body.
- Connecting a controller:
  - **macOS**: Audio MIDI Setup → MIDI Studio → Network. Add the module by IP (port 5004) under "Directory", or connect once it appears, then create a session.
  - **Windows**: Tobias Erichsen's [rtpMIDI](https://www.tobias-erichsen.de/software/rtpmidi.html) → add a new session → enter the module's IP and port 5004.
  - The session name the module advertises is `NET_MIDI_NAME` from `secrets.h` (default `Pico2W-OC`).
- Notes:
  - The Pico 2 W radio is **2.4 GHz only** — point `WIFI_SSID` at a 2.4 GHz network.
  - Inbound MIDI is only serviced while this patch is on screen (its `tick()` pumps the RTP session). Navigate away and the session idles/drops; return and it re-establishes. WiFi association itself persists across patch switches once it has connected.
  - If a connected controller disconnects, any held gate is released so a note can't get stuck on.

### Turing (Dual Turing Machine)
- Purpose: Two independent Music-Thing-style looping shift registers generating quantised pitch + variable gates (TB3PO-ish), clocked and reset from the CV inputs.
- Inputs: IN1 = clock in (rising edge advances both voices), IN2 = reset in (rising edge realigns both loop phases to the top of the loop). An internal 120 BPM clock runs only until the first external clock edge is seen (bench fallback); once externally clocked the module follows that clock and **holds when the clock stops** (no internal free-run takeover). Re-enter the patch to get the internal clock back.
- Outputs (column convention): V1 → pitch OUT1, gate OUT3; V2 → pitch OUT2, gate OUT4. Pitch is 1 V/oct via calibration; gates use fixed gate codes.
- Engine: Each clock, the per-voice register rotates and the bit that falls off is fed back, flipped with probability set by Randomness — fully CCW = locked loop (length N), centre = maximum randomness, fully CW = locked but inverted (length 2N). Note randomness peaks at the **centre** and locks at **both** ends. Pitch comes from the low 8 register bits mapped across `Range` scale degrees and quantised to the global scale/root. The gate fires on a register tap (pattern-locked density); its length is half the measured clock interval (tempo-dependent only, independent of Length/pattern).
- Pages (short press cycles `V1 → V2 → KEY`; title-right shows the active page):
  - `V1`/`V2`: Pot1 Randomness, Pot2 Length (2–16), Pot3 Range (1–24 scale degrees) — per voice.
  - `KEY` (global to both voices): Pot1 Scale (Chromatic / Major / Minor / MinPent / MajPent / Dorian), Pot2 Root note (C…B), Pot3 Octave base (0–4).
- Pot pickup: After switching page, each pot stays inactive until it crosses (or already matches) the stored value for the newly-selected page/voice.
- Display: Selected voice shows randomness as an amount that peaks at centre with a `Loop`/`Inv` tag for the locked end (e.g. `Rnd 0% Loop`, `Rnd 100%`, `Rnd 0% Inv`), plus Len/Rng and the register bits as squares; KEY shows Scale/Root/Octave. Bottom row shows both voices' current note + gate (`*`) and the clock source/tempo (`INT`/`EXT` BPM, or `EXT -- (stop)` when an external clock has stopped).

### Acid
- Purpose: Seed-based acid-line generator (TB-303-ish pitch/gate/accent/slide), clocked from the CV inputs like Turing.
- Outputs (column convention): voice = pitch OUT1 / gate OUT3 (left column); modifiers = accent OUT2 / slide OUT4 (right column). Pitch is 1 V/oct via calibration; gates use fixed gate codes.
- Pages (short press cycles `PLAY → KEY`): PLAY has Pot1 = pattern seed (with a deadband anchor so it doesn't re-roll on entry); KEY mirrors Turing's global scale/root/octave controls.

### Calibration
- Approach: Use the `Diag` patch for DMM-first calibration. Record raw ADC codes vs known volts, and raw DAC codes vs measured volts, then fit straight lines per channel.
- Integration: Static fits are compiled in (see `include/pico2w_oc/calib_static.h`). Diagnostics remain raw-only.

### LFO
- Purpose: 4 independent LFOs with per-LFO amplitude, rate, and shape.
- Outputs: OUT1..OUT4 emit LFOs (L0..L3 in order); bipolar ±amp mapped via calibration to DAC codes.
- Controls (smoothed, inverted):
  - Pot1: Amplitude (0..~5 V peak per LFO).
  - Pot2: Rate (≈0.05–20 Hz with squared mapping for fine low-end control).
  - Pot3: Shape (Sin/Tri/Sq/Up/Down).
  - Display: First row and the per-LFO rows list fields in pot order — Amp, Rate, Shape — so they read left-to-right to match the knobs.
  - Short press: Cycle edited LFO target (L0→L1→L2→L3). Title right shows `L<idx>`.
  - Pot pickup: After switching LFOs, each pot stays inactive until it crosses (or already matches) the selected LFO's stored value, so the other LFOs' settings aren't clobbered.
  - Long press: Return to menu.
 - Notes: All LFOs run continuously; editing only affects the selected LFO’s parameters.

## Tips

- I2C Buses: OLED is on Wire (I2C0, GP20/GP21). ADS1115 + MCP4728 are on Wire1 (I2C1, GP18/GP19). This eliminates bus contention between display updates and CV I/O.
- Physical Mapping: DAC channels use the panel macros `OUT1_DA_CH..OUT4_DA_CH` and inputs `IN1_AD_CH`/`IN2_AD_CH` (aliases over the underlying `CVx_DA_CH`/`ADx_CH` wiring map) in `include/pico2w_oc/pins.h`.
- External Clocking: Provide clean rising edges into IN1 (`AD_EXT_CLOCK_CH`) for reliable detection.
- OLED Grid: Keep titles at `y=0`; use rows `16/26/36/46/56` for content.
- Menu: `Clock`, `Quant`, `Euclid`, `LFO`, `Env`, `Scope`, `UsbMIDI`, `NetMIDI`, `Turing`, `Acid`, `Diag`.

## WiFi Setup (for the `NetMIDI` patch)

The network MIDI patch needs your WiFi credentials. They live in a gitignored
`secrets.h`:

```sh
cp include/pico2w_oc/secrets.h.example include/pico2w_oc/secrets.h
# then edit include/pico2w_oc/secrets.h with your 2.4 GHz SSID/password
```

`secrets.h` defines `WIFI_SSID`, `WIFI_PASS`, and the advertised session name
`NET_MIDI_NAME`. If the file is absent the firmware still builds (so CI stays
green) — the `NetMIDI` patch just falls back to a placeholder SSID and reports that
it can't join. Every other patch works with or without WiFi credentials.

## PlatformIO Quick Commands

```sh
# Build default environment
pio run

# Build Pico 2 W explicitly
pio run -e pico2w_oc

# Upload to Pico 2 W
pio run -e pico2w_oc -t upload

# Monitor serial at 115200
pio device monitor -b 115200
```

## Contributing

- Keep per-patch UIs compact and consistent with the grid.
- Use physical channel macros for all I/O; avoid legacy aliases.

````