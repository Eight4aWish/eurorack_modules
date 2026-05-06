"""CortHex LLM proxy.

Mac-side FastAPI service that turns natural-language prompts into
Plaits-voice patches via the Claude API. Runs on the local network;
the iPad page calls /generate, gets back a patch, then either applies
it itself or asks this proxy to forward it to the module.

Run:  ANTHROPIC_API_KEY=sk-... uvicorn proxy:app --host 0.0.0.0 --port 8000
"""
from __future__ import annotations

import json
import logging
import os
import urllib.error
import urllib.request
from collections import defaultdict
from typing import Any

import anthropic
from anthropic.types import MessageParam, ToolParam
from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

# Load .env from the proxy directory (same dir as this file) before any
# os.environ reads. Lets us keep ANTHROPIC_API_KEY etc. in a gitignored file
# instead of `export`ing on every shell session.
load_dotenv(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env"))

# --- Config ------------------------------------------------------------------
MODEL = os.environ.get("ANTHROPIC_MODEL", "claude-opus-4-7")
MAX_TOKENS = int(os.environ.get("ANTHROPIC_MAX_TOKENS", "4096"))
EFFORT = os.environ.get("ANTHROPIC_EFFORT", "high")
THINKING = os.environ.get("ANTHROPIC_THINKING", "adaptive")  # "adaptive" or "off"
MODULE_HOST = os.environ.get("MODULE_HOST")  # e.g. http://CortHex-ESP32.local
APPLY_TO_MODULE = os.environ.get("APPLY_TO_MODULE", "0") == "1"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)
log = logging.getLogger("corthex-proxy")

client = anthropic.Anthropic()
app = FastAPI(title="CortHex LLM proxy")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# --- In-memory state ---------------------------------------------------------
# session_id -> conversation history. Reset via POST /sessions/{id}/reset.
_sessions: dict[str, list[MessageParam]] = defaultdict(list)


# --- The set_patch tool ------------------------------------------------------
def _macro_schema(description: str) -> dict[str, Any]:
    return {
        "type": "object",
        "description": description,
        "properties": {
            "v": {"type": "number"},
            "rise_ms": {"type": "integer", "minimum": 0, "maximum": 5000},
            "fall_ms": {"type": "integer", "minimum": 0, "maximum": 5000},
        },
        "required": ["v", "rise_ms", "fall_ms"],
        "additionalProperties": False,
    }


_PATCH_OBJECT_SCHEMA: dict[str, Any] = {
    "type": "object",
    "properties": {
        "name": {
            "type": "string",
            "description": "Short label, ≤ 14 chars (shown on the iPad card).",
        },
        "model": {
            "type": "integer",
            "minimum": 0,
            "maximum": 23,
            "description": "Plaits engine index 0-23.",
        },
        "timbre": _macro_schema("CV2 — Plaits Timbre, 0 to +5 V."),
        "harmonics": _macro_schema("CV3 — Plaits Harmonics, 0 to +5 V."),
        "swords_freq": _macro_schema(
            "CV4 — Swords filter cutoff, -5 to +5 V (0 V = mid)."
        ),
        "swords_res": _macro_schema(
            "CV5 — Swords filter resonance, -5 to +5 V (> +3 V self-oscillates)."
        ),
        "vca": _macro_schema(
            "CV6 — Four Play VCA gain, 0 to +5 V (0 = silent, 5 = full open)."
        ),
        "rationale": {
            "type": "string",
            "description": "1-2 sentence explanation of THIS variation's design choice.",
        },
    },
    "required": [
        "name",
        "model",
        "timbre",
        "harmonics",
        "swords_freq",
        "swords_res",
        "vca",
        "rationale",
    ],
    "additionalProperties": False,
}


SET_PATCH_BANK_TOOL: ToolParam = {
    "name": "set_patch_bank",
    "description": (
        "Generate SIX distinct variations on the user's request and load them "
        "into the panel-button bank. The user will audition them by pressing "
        "physical buttons 1-6 on the module — so the variations should differ "
        "in MEANINGFUL ways (e.g. different engines, different envelope shapes, "
        "different filter behaviours), not just slight parameter wiggles. "
        "Order them roughly from most-conventional to most-experimental. Each "
        "patch needs its own short name (≤ 14 chars, like 'sub thump' or "
        "'scratch hat') and its own rationale."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "concept": {
                "type": "string",
                "description": "1 sentence describing what these 6 share.",
            },
            "patches": {
                "type": "array",
                "minItems": 6,
                "maxItems": 6,
                "description": "Exactly 6 patches.",
                "items": _PATCH_OBJECT_SCHEMA,
            },
        },
        "required": ["concept", "patches"],
        "additionalProperties": False,
    },
}


# --- System prompt -----------------------------------------------------------
SYSTEM_PROMPT = """\
You are a synthesizer programmer driving a hardware Eurorack voice from a Mac
proxy. The user describes a sound; you pick parameters and emit a `set_patch`
tool call.

# Voice architecture

Six CV outputs feed three modules wired in series:

    Plaits (oscillator, 24 engines) → Swords (filter) → Four Play (VCA) → audio out

The user's gate fires both Plaits' TRIG (excites drum/physical models) and the
firmware's envelope engine on D9 (drives any rise/fall envelopes you set on
CV2-6).

# CV channels

| CV | Destination          | Range          | Notes                                    |
|----|----------------------|----------------|------------------------------------------|
| 1  | Plaits Model         | discrete 0-23  | Engine index. Always static — never envelope. |
| 2  | Plaits Timbre        | 0 to +5 V      | Per-engine macro (see engine table).     |
| 3  | Plaits Harmonics     | 0 to +5 V      | Per-engine macro.                        |
| 4  | Swords FREQUENCY     | -5 to +5 V     | Filter cutoff. 0 V = mid (~1 kHz).       |
| 5  | Swords RESONANCE     | -5 to +5 V     | Q. > +3 V tends toward self-oscillation. |
| 6  | Four Play VCA        | 0 to +5 V      | Final amplitude. 0 = silent, 5 = full open. |

Plaits MORPH is panel-only — out of your control.
Plaits' V/oct (pitch) is set externally — out of your control.

# Macros: static vs enveloped

Each of CV2-6 is a {v, rise_ms, fall_ms} object:

- rise_ms = 0 AND fall_ms = 0 → static hold at v.
- otherwise → idles at 0 V; on gate-on, rises to v over rise_ms;
  on gate-off, falls to 0 V over fall_ms. Classic AR shape.

Useful patterns:

- Pluck/percussive: VCA fast attack (1-5 ms), short release (100-400 ms).
- Pad: VCA slow rise (500-2000 ms), longer release.
- Filter snap: SWORDS_FREQ enveloped from 0 V (mid cutoff) up to ~+3 V on gate-on,
  fall ~200-400 ms; SWORDS_RES static around 0 to +1 V for body.
- Drone/sustained: leave VCA open (5 V static); rely on Plaits' internal LPG
  via TRIG (the user handles patching).
- Drum engines (21-23, Bass Drum / Snare / Hi-Hat) have their own internal
  envelope and disable Plaits' LPG — use VCA static at 5 V or a short release
  of ~200-500 ms; don't double-envelope.

# Plaits engine table

Bank order on this firmware (low CV → high CV): orange (0-7) → green (8-15) → red (16-23).

## Orange bank — alt-firmware extensions

0. Classic + Filter
   H: filter / resonance character (CCW: gentle 24 dB/oct, CW: harsh 12 dB/oct)
   T: filter cutoff       M: waveform and sub level
   OUT: low-pass output   AUX: 12 dB/oct high-pass

1. Phase Dist
   H: distortion frequency  T: distortion amount  M: distortion asymmetry
   OUT: sync'd carrier (phase distortion)  AUX: free-running carrier (phase modulation)

2. 6-op FM I  (DX7 SysEx, 32 presets per mode)
   H: preset selection (32 positions)  T: modulator levels
   M: envelope & modulation stretch / time-travel
   OUT: sync'd carrier  AUX: free-running carrier
   NOTE: 6-op DX7 patches; two voices alternated by TRIG. Without TRIG, single-voice
         drone with MORPH time-travelling. LEVEL acts as velocity.

3. 6-op FM II  (second DX7 SysEx bank — otherwise same as 6-op FM I)

4. 6-op FM III (third DX7 SysEx bank — otherwise same as 6-op FM I)

5. Wave Terrain
   H: terrain selection (continuous interp between 8 2-D terrains)
   T: path radius  M: path offset
   OUT: terrain height (z)  AUX: phase-distortion sin(y+z)

6. String Machine
   H: chord  T: chorus / filter amount  M: waveform
   OUT: voices 1+3 (predominantly)  AUX: voices 2+4 (predominantly)
   NOTE: Solina-style string machine with stereo filter and chorus.

7. 4-voice Sqr  (variable square voices for chords/arps)
   H: chord  T: arpeggio type or chord inversion  M: pulse-width sync
   OUT: square wave voices  AUX: NES triangle voice
   NOTE: TRIG clocks the arpeggiator. TIMBRE attenuverter sets the envelope shape.

## Green bank — original Plaits 1.0 bank 1 (pitched / synthesis)

8. Pair VA  (virtual analog, two waves)
   H: detuning between the two waves
   T: variable square (narrow pulse → full square → hardsync formants)
   M: variable saw (triangle → saw with widening notch)
   OUT: pair of classic VA waveforms  AUX: two hardsync'ed waveforms
   NOTE: Narrow pulse or wide notch will sound silent.

9. Waveshaper
   H: waveshaper waveform  T: wavefolder amount  M: waveform asymmetry
   OUT: asymmetric triangle through waveshaper + wavefolder
   AUX: variant with another wavefolder curve

10. 2-op FM  (sine-on-sine)
    H: frequency ratio  T: modulation index
    M: feedback (op2 self past 12 o'clock; op1 phase before 12)
    OUT: phase-modulated sine waves  AUX: sub-oscillator

11. Formant  (granular formant oscillator)
    H: formant 1/2 frequency ratio  T: formant frequency
    M: formant width and shape
    OUT: granular formant oscillator (sine-segment formants)
    AUX: windowed-sine filtered waveforms — HARMONICS picks the filter

12. Harmonic  (additive)
    H: number of bumps in spectrum  T: index of most prominent harmonic
    M: bump shape (flat/wide → peaked/narrow)
    OUT: additive harmonic oscillator
    AUX: subset of harmonics matching Hammond drawbars

13. Wavetable  (8×8, 4 banks interpolated + 4 not)
    H: bank selection  T: row index (sorted by brightness)  M: column index
    OUT: 8×8 wavetable oscillator  AUX: low-fi output

14. Chords  (four-note chord synth)
    H: chord type  T: chord inversion and transposition
    M: waveform (string-machine raw → wavetable scan past 12 o'clock)
    OUT: four-note chord  AUX: root note of the chord

15. Speech
    H: crossfade across formant filtering / SAM / LPC vowels / LPC words
    T: species selection (Daleks → chipmunks)
    M: phoneme / word segment selection
    OUT: synthesised speech  AUX: unfiltered vocal cords' signal
    NOTE: Patch TRIG to utter words. FM attenuverter = intonation, MORPH attenuverter = speed.

## Red bank — original Plaits 1.0 bank 2 (physical / noise / drums)

16. Granular  (cloud of 8 enveloped sawtooths)
    H: pitch randomisation  T: grain density  M: grain duration / overlap
    OUT: enveloped sawtooth swarm  AUX: variant with sine waves

17. Filtered Noise
    H: filter response (LP → BP → HP)  T: clock frequency  M: filter resonance
    OUT: variable-clock noise through resonant filter
    AUX: two band-pass filters with separation set by HARMONICS

18. Particle  (dust noise through filter network)
    H: frequency randomisation  T: particle density
    M: filter type (all-pass network before 12 o'clock → resonant BP after)
    OUT: dust through filter network  AUX: raw dust noise

19. String  (inharmonic string resonator — mini-Rings)
    H: inharmonicity / material  T: excitation brightness + dust density
    M: decay time
    OUT: inharmonic string resonator  AUX: raw exciter signal
    NOTE: When TRIG is unpatched, the resonator is excited by dust noise.

20. Modal  (modal resonator bank — mini-Rings)
    H: inharmonicity / material  T: excitation brightness + dust density
    M: decay time
    OUT: modal resonator bank  AUX: raw exciter signal
    NOTE: When TRIG is unpatched, the resonator is excited by dust noise.

21. Bass Drum  (analog kick model)
    H: attack sharpness  T: brightness  M: decay time
    OUT: analog bass drum  AUX: emulation of another classic bass drum
    NOTE: Uses its own envelope and filter — internal LPG is disabled.

22. Snare  (analog snare model)
    H: harmonic / noise balance  T: balance between drum modes  M: decay time
    OUT: analog snare drum  AUX: emulation of another classic snare drum
    NOTE: Uses its own envelope and filter — internal LPG is disabled.

23. Hi-Hat  (analog hi-hat model)
    H: metallic / noise balance  T: high-pass filter cutoff  M: decay time
    OUT: analog hi-hat  AUX: ring-modulated tuned-noise variant
    NOTE: Uses its own envelope and filter — internal LPG is disabled.

# Your job

Listen to the user's request. Generate SIX distinct variations and emit them
as a `set_patch_bank` tool call — the user will audition them by pressing
panel buttons 1-6 on the hardware.

The variations should be MEANINGFULLY DIFFERENT, not just parameter wiggles.
Examples of good variation axes for a "kick" prompt:
  1. Tight 808-style sub-thump (Bass Drum engine, short decay)
  2. Punchy analog kick with click (Bass Drum, sharp attack)
  3. Dark filtered noise burst (Filtered Noise, heavy LP)
  4. FM kick (2-op FM, fast pitch envelope)
  5. Particle-dust thud (Particle, low filter)
  6. Resonant Modal kick (Modal, low material)

Order from most-conventional to most-experimental. Each patch gets a short
name (≤14 chars; the iPad shows it on a card) and its own 1-sentence rationale.

If the user's request is genuinely ambiguous and no spread of 6 makes sense,
ask one focused clarifying question instead of patching.
"""


# --- Pydantic models for the HTTP API ---------------------------------------
class GenerateRequest(BaseModel):
    session_id: str
    user_message: str


class MacroPayload(BaseModel):
    v: float
    rise_ms: int
    fall_ms: int


class PatchPayload(BaseModel):
    name: str
    model: int
    timbre: MacroPayload
    harmonics: MacroPayload
    swords_freq: MacroPayload
    swords_res: MacroPayload
    vca: MacroPayload
    rationale: str


class GenerateResponse(BaseModel):
    assistant_text: str
    concept: str | None = None
    patches: list[PatchPayload] | None = None
    applied_to_module: bool = False


# --- Module forwarding ------------------------------------------------------
def _forward_bank_to_module(concept: str, patches: list[dict[str, Any]]) -> bool:
    """POST the bank to the firmware's /api/patch/bank endpoint. Returns True on success."""
    if not (MODULE_HOST and APPLY_TO_MODULE):
        return False
    body = json.dumps({"concept": concept, "patches": patches}).encode()
    req = urllib.request.Request(
        f"{MODULE_HOST.rstrip('/')}/api/patch/bank",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            log.info("forwarded bank to module: HTTP %d", resp.status)
            return resp.status == 200
    except (urllib.error.URLError, OSError) as e:
        log.warning("module forward failed: %s", e)
        return False


# --- Endpoints --------------------------------------------------------------
@app.post("/generate", response_model=GenerateResponse)
def generate(req: GenerateRequest) -> GenerateResponse:
    history = _sessions[req.session_id]
    history.append({"role": "user", "content": req.user_message})

    create_kwargs: dict[str, Any] = {
        "model": MODEL,
        "max_tokens": MAX_TOKENS,
        "system": [
            {
                "type": "text",
                "text": SYSTEM_PROMPT,
                "cache_control": {"type": "ephemeral"},
            }
        ],
        "tools": [SET_PATCH_BANK_TOOL],
        "messages": history,
    }
    if THINKING == "adaptive":
        create_kwargs["thinking"] = {"type": "adaptive"}
        create_kwargs["output_config"] = {"effort": EFFORT}

    try:
        response = client.messages.create(**create_kwargs)
    except anthropic.APIError as e:
        log.exception("Anthropic API error")
        raise HTTPException(status_code=502, detail=f"Anthropic API: {e}")

    history.append({"role": "assistant", "content": response.content})

    text_parts: list[str] = []
    concept: str | None = None
    patches: list[dict[str, Any]] | None = None
    tool_use_ids: list[str] = []
    for block in response.content:
        if block.type == "text":
            text_parts.append(block.text)
        elif block.type == "tool_use" and block.name == "set_patch_bank":
            inp = dict(block.input) if isinstance(block.input, dict) else {}
            concept = inp.get("concept")
            raw = inp.get("patches") or []
            patches = [dict(p) for p in raw if isinstance(p, dict)]
            tool_use_ids.append(block.id)

    # Anthropic conversation invariant: every tool_use must be paired with a
    # tool_result on the next user turn, otherwise the next request 400s.
    if tool_use_ids:
        history.append(
            {
                "role": "user",
                "content": [
                    {
                        "type": "tool_result",
                        "tool_use_id": tid,
                        "content": "Bank applied. User is auditioning slots 1–6.",
                    }
                    for tid in tool_use_ids
                ],
            }
        )

    log.info(
        "[%s] in=%d cache_read=%d cache_write=%d out=%d patches=%s",
        req.session_id,
        response.usage.input_tokens,
        response.usage.cache_read_input_tokens or 0,
        response.usage.cache_creation_input_tokens or 0,
        response.usage.output_tokens,
        len(patches) if patches else 0,
    )

    text = " ".join(t.strip() for t in text_parts).strip()
    if not text and concept:
        text = concept

    applied = (
        _forward_bank_to_module(concept or "", patches)
        if patches and len(patches) == 6
        else False
    )

    return GenerateResponse(
        assistant_text=text,
        concept=concept,
        patches=[PatchPayload(**p) for p in patches] if patches else None,
        applied_to_module=applied,
    )


@app.post("/sessions/{session_id}/reset")
def reset_session(session_id: str) -> dict[str, str]:
    _sessions.pop(session_id, None)
    return {"status": "reset", "session_id": session_id}


@app.get("/health")
def health() -> dict[str, str | bool]:
    return {
        "status": "ok",
        "model": MODEL,
        "thinking": THINKING,
        "effort": EFFORT,
        "module_host": MODULE_HOST or "",
        "apply_to_module": APPLY_TO_MODULE,
    }
