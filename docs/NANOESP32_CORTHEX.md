# AI Module (CortHex) — firmware

Arduino Nano ESP32 firmware for the AI module. Runs three web pages on the
module's WiFi address, calibrates and drives six CV outputs to a Plaits +
Swords + Four Play voice, and lets an LLM-generated 6-patch "bank" be
auditioned via the panel buttons.

Hardware spec, panel layout, and bring-up history: [AI_MODULES_HARDWARE.md](AI_MODULES_HARDWARE.md).

## Quick start

1. Copy `secrets.h.example` → `secrets.h` and fill in your WiFi SSID, password, OTA hostname (default: `CortHex-ESP32`), and OTA password.
2. First flash via USB / DFU: `pio run -e nanoesp32-corthex -t upload`. The module joins your WiFi and prints its IP on serial.
3. Open `http://<OTA_HOST>.local/` in any browser on the LAN. The diagnostics page should load and start streaming telemetry.
4. Subsequent flashes happen OTA: `AI_MODULE_OTA_PASS='...' pio run -e nanoesp32-corthex-ota -t upload`.

## The three pages

All served by the firmware. Top-nav switches between them.

### `/` — Diagnostics

Live telemetry over WebSocket: panel buttons, audio L/R DC + peak-to-peak, gate state and pulse count, all six CV output voltages, system info (uptime, free heap, RSSI, IP). Plus four named sample patches (Pluck Bass, Slow Pad, Kick, Drone), software trigger/release buttons, and a "Reset to test mode" panic button.

### `/plaits` — Plaits-only control

Treats the AI module as a manual control surface for Plaits alone. CV1 selects engine (24-cell picker organised by the on-firmware bank order: orange 0–7, green 8–15, red 16–23). CV2/CV3/CV4 are continuous sliders for Timbre, Harmonics, and Morph (Morph here is repurposed from the LLM voice's CV4 = Swords FREQ). Tap a model and the page shows the verbatim macro / OUT / AUX descriptions from the Plaits v1.2 manual plus per-engine notes (e.g. drum engines disable the internal LPG; Speech needs TRIG patched). Slider hints update per-engine so you know what each macro does on whichever engine is active.

### `/llm` — Natural-language patch generation

Chat with a Mac-side proxy. The proxy turns prompts into a "bank" of 6 distinct named patches via Claude (`tools/llm-proxy/` — see its README for setup). The page displays the bank as 6 cards; the firmware loads them into `patch_bank[]`. Audition with **physical panel buttons 1–6 on the module** — the channel-button LED stays lit on the active slot, and the page highlights the active card from telemetry.

## Architecture

```
src/nanoesp32-corthex/
  main.cpp           — single firmware: WiFi/OTA, MCP4822 chain, AR envelope
                       engine, patch bank, HTTP/WebSocket server, panel-button
                       slot select.
  data/
    index.html, app.js  — Diagnostics page
    plaits.html, plaits.js  — Plaits-only control page
    llm.html,    llm.js     — LLM page
    style.css       — Shared dark theme, iPad-optimised
  secrets.h.example — Copy to secrets.h (gitignored) and fill in WiFi/OTA creds
```

Static assets are embedded in flash at link time via `board_build.embed_txtfiles`. No filesystem; one OTA flash ships everything.

### Audio chain (LLM voice)

```
Plaits (osc, 24 engines) → Swords (filter) → Four Play (VCA) → audio out
```

Six CV outputs:

| CV | Wired to        | Range          | Notes                                      |
|----|-----------------|----------------|--------------------------------------------|
| 1  | Plaits Model    | discrete 0–23  | Always static. Set via lookup table; closest-centre voltage→model is firmware-side, so the page never duplicates the table. |
| 2  | Plaits Timbre   | 0 to +5 V      | Per-engine macro.                          |
| 3  | Plaits Harmonics| 0 to +5 V      | Per-engine macro.                          |
| 4  | Swords FREQ     | −5 to +5 V     | Filter cutoff. The Plaits-only page treats this as Plaits Morph (0–5 V) — UI-side range constraint, not firmware. |
| 5  | Swords RES      | −5 to +5 V     | Filter Q.                                  |
| 6  | Four Play VCA   | 0 to +5 V      | Final amplitude.                           |

CV calibration (per-channel +5 V / 0 V / −5 V codes) lives in the `CV_CAL[]` table in `main.cpp`. Bench-confirmed 2026-05-02; re-measure if Vdd or any bipolar-shift resistor changes.

### AR envelope engine

Every CV channel has a `PatchChannel { hold_v, rise_ms, fall_ms, cycle }` struct. Static channels (`rise=fall=0`) are written once. Enveloped channels idle at 0 V, rise to `hold_v` over `rise_ms` on gate-on (D9 rising edge), fall to 0 V over `fall_ms` on gate-off. `envelope_tick()` runs at 1 ms cadence in the main loop. The `cycle` field is reserved for v2 LFO support — currently rendered as static at `hold_v`.

### Patch bank (LLM voice)

Six `Patch` slots. Panel buttons 1–6 edge-detect to:

1. Set `active_patch_idx` to that slot.
2. Call `patch_apply(patch_bank[slot])`.
3. Light only that channel's LED (others go off).

Master button (`M`) stays as a diagnostic press-thru indicator.

`active_patch_idx == -1` at boot means no LLM bank loaded yet — all channel LEDs are dark, and `test_mode_patch` is the active sound.

## HTTP API

| Endpoint                  | Method | Body                                                | What it does                                                   |
|---------------------------|--------|-----------------------------------------------------|----------------------------------------------------------------|
| `/`, `/plaits`, `/llm`    | GET    | —                                                   | Embedded HTML pages.                                           |
| `/style.css`              | GET    | —                                                   | Shared CSS.                                                    |
| `/app.js`, `/plaits.js`, `/llm.js` | GET | —                                          | Per-page JS.                                                   |
| `/ws`                     | WS     | —                                                   | Telemetry stream (~250 ms cadence).                            |
| `/api/cv/{ch}`            | POST   | text/plain decimal voltage                          | Set one CV channel directly. Promotes channel to static.       |
| `/api/plaits/model`       | POST   | text/plain integer 0–23                             | Set Plaits engine via lookup table; firmware writes the centre voltage to CV1. Returns `model=N voltage=V`. |
| `/api/patch`              | POST   | JSON `{model, timbre{v,rise_ms,fall_ms}, harmonics, swords_freq, swords_res, vca, rationale?}` | Apply one full patch. Routes through `patch_apply`. |
| `/api/patch/bank`         | POST   | JSON `{concept, patches:[6 patches]}`               | Load the 6-slot LLM bank. Auto-applies slot 0; LED 1 lights.   |
| `/api/patch/test/{0..3}`  | POST   | —                                                   | Load one of the four hardcoded sample patches.                 |
| `/api/patch/trigger`      | POST   | —                                                   | Synthesise a gate-on edge (no physical D9 needed).             |
| `/api/patch/release`      | POST   | —                                                   | Synthesise a gate-off edge.                                    |
| `/api/patch/reset`        | POST   | —                                                   | Re-apply `test_mode_patch` (panic button).                     |

## Telemetry JSON

Pushed over `/ws` ~4 Hz:

```json
{
  "btn":  [0,0,0,0,0,0,0],          // master + ch 1..6
  "audio": {"l":{"dc":1967,"pp":12}, "r":{"dc":1965,"pp":11}},
  "gate":  {"state":0, "pulses":42},
  "cv":    [-0.02, 2.50, 1.00, 3.50, 0.00, 5.00],   // CV1..CV6 in volts
  "plaits": {"model": 8},                            // closest-centre lookup
  "bank":   {"slot": 0},                             // active LLM-bank slot, -1 if none
  "patch":  "Test Mode",
  "sys":    {"uptime_s":12, "free_heap":215360, "rssi":-62, "ip":"192.168.1.42"}
}
```

## Things to know

- **Two voices, one CV4**: the firmware's CV4 calibration is broad (±5 V) so it serves both the Plaits-only page (using 0–5 V as Plaits Morph) and the LLM page (using full ±5 V as Swords FREQ). The two pages enforce their own UI sub-ranges.
- **Plaits 1.2 alt-firmware bank order**: this firmware assumes orange (0–7) → green (8–15) → red (16–23) as the CV-band ordering, which is what the user's installed Plaits 1.2 alt firmware shows. Stock Plaits 1.0 uses orange-green only and a different ordering — if you swap firmwares, update `MODELS[]` in `data/llm.js` and `data/plaits.js` (and the engine-table in `tools/llm-proxy/proxy.py`).
- **Drum engines (red 21–23)** have their own internal envelope and disable Plaits' LPG. Don't double-envelope on VCA — leave VCA static at +5 V or use a short release.
- **Pages are duplicated** rather than sharing a SPA shell. Three small pages per task is friendlier on iPad than one big SPA — and keeps unrelated changes from invalidating each other.

## Memory notes (project context, not in repo)

The user's working memory has additional context on the project: bench measurements, calibration values, bank-mapping confirmations. Those memories live in `~/.claude/projects/.../memory/` and aren't part of this repo. Pointers there if you're picking up the project across sessions.
