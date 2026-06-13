# Behringer Proton — AI Patch Suggester (Feasibility Study)

> **Question:** Is it feasible to build a web app that takes a natural-language
> description of a desired sound and returns a Proton front-panel image showing
> suggested knob/switch states plus the patch-bay cables needed?
>
> **Verdict: Yes — and this project is unusually well-positioned to build it.**
> Two of the three hard problems are already solved in this repo. The remaining
> work is a one-time panel asset + a deterministic renderer, not open research.

---

## 1. Why it's feasible (the short version)

A patch suggester needs three things. Here's where each one stands:

| Need | Status | Where it comes from |
|------|--------|--------------------|
| **A complete, machine-usable model of the instrument** | ✅ Essentially done | [`docs/PROTON_SIGNAL_ROUTING.md`](PROTON_SIGNAL_ROUTING.md) already enumerates every jack, normal, override/sum rule, knob range, switch state, LED meaning, and the full SysEx parameter surface. |
| **An LLM that turns NL → a structured patch** | ✅ Proven pattern in-repo | [`tools/llm-proxy/`](../tools/llm-proxy/) already does exactly this for the CortHex/Plaits voice: FastAPI + Anthropic tool-use with a constrained JSON schema → a 6-patch "bank". |
| **A front-panel image with knobs/switches/cables rendered from that patch** | 🟡 Net-new, but deterministic | Build once: a stylized panel (SVG) + a coordinate map of every control and jack. The LLM emits *values*; the front-end draws them. No image-generation model required. |

The biggest de-risker is the first row. The hard part of an instrument-specific
patch generator is normally encoding the routing semantics — what's normalled,
what *replaces* vs *sums*, which mod paths can't be switched off. The Proton doc
already has that, down to footguns (§8: "Sustain = 0 disables Release",
"patched Freq CV *replaces* the LFO — it does not sum"). That document is, in
effect, the spec for this app.

---

## 2. Recommended architecture

```
  Browser (static page + panel SVG)
        │  POST { session_id, prompt }
        ▼
  Proxy endpoint  (extend tools/llm-proxy, or a serverless fn)
        │  Anthropic tool-use: suggest_proton_patch
        │  system context = distilled Proton model (§3)
        ▼
  Claude returns a structured PatchSpec (JSON)
        │
        ▼
  Deterministic validator (§5)  ── rejects illegal jacks, flags footguns
        │
        ▼
  Browser renderer  ── rotates knob pointers, sets switch/LED colours,
                       draws bezier cables between jack coordinates
```

This reuses the **exact pattern already running** in `tools/llm-proxy` (Anthropic
tool-use returning schema-constrained JSON, per-session history, `.env` key,
launchd autostart). The Proton suggester is a second endpoint + a richer schema +
a renderer. Nothing architecturally new on the AI side.

### Why a static-overlay panel, **not** an image-generation model

Rendering the panel by overlaying values on a fixed asset is the right call:

- **Accuracy is the whole point.** A user must be able to read "Folds at ~2
  o'clock, VCF1 Mode = LPF, cable from ASR 1 OUT → VCA 1 CV." Diffusion/image
  models cannot reliably render exact knob angles, legible labels, or cable
  endpoints that land on the correct jacks. They hallucinate controls.
- **Deterministic = debuggable.** Knob angle is a function of value; a cable is a
  bezier between two known coordinates. Same input → same image, every time.
- **Cheap and instant.** SVG renders client-side; no GPU, no per-image API cost.

The only thing an image model would buy is "pretty," and we get that from a
hand-built SVG without giving up correctness.

---

## 3. The "machine model" — one prep step

Before any UI work, distill `PROTON_SIGNAL_ROUTING.md` into a structured JSON
catalogue the app and the LLM both consume. It's mostly mechanical transcription
of tables already in the doc:

```jsonc
{
  "controls": [
    { "id": "tune1",     "type": "knob",   "range": "-13..+13 st" },
    { "id": "osc_mix",   "type": "knob",   "range": "VCO1..VCO2" },
    { "id": "vcf1_mode", "type": "switch", "values": ["LPF","BPF","HPF"] },
    { "id": "wf_mode",   "type": "switch", "values": ["AM","½","1","BP"] },
    { "id": "link",      "type": "tri",    "values": ["off","red","white"] }
    // …every knob, button, tri-state LED button from §5/§6
  ],
  "jacks": {
    "in":  [ { "id": "vcf1_freq", "kind": "cv",    "normal": "lfo1",
               "insert": "replace" }, /* …40 IN jacks, §3 */ ],
    "out": [ { "id": "asr1_out",  "kind": "cv" },  /* …24 OUT jacks, §4 */ ]
  },
  "always_on_mods": [ "adsr2->vca1", "adsr2->vca2", "lfo1->vcf1", "lfo2->vcf2" ],
  "footguns": [ "sustain0_disables_release", "freqcv_replaces_lfo", … ]
}
```

This catalogue does double duty: it's the enumerated vocabulary the LLM is
constrained to (so it can't invent a jack that doesn't exist), and it's the
lookup table the renderer uses for coordinates and value→angle mapping.

---

## 4. The patch schema (LLM output contract)

The `suggest_proton_patch` tool returns one object. Sketch:

```jsonc
{
  "concept": "Warm sub-bass with a slow filter sway",
  "knobs":   { "tune1": -12, "osc_mix": 0.4, "sub_mix1": 0.2,
               "vcf1_freq": 0.35, "vcf1_reso": 0.25,
               "mod_depth1": 0.15, "lfo1_rate": 0.1, "adsr1_atk": 0.05, … },
  "switches":{ "range1": "32",  "range2": "16", "vcf1_mode": "LPF",
               "wf_mode": "BP", "link": "off" },
  "cables":  [ ],                       // empty → rely on normals
  "warnings":[ ],
  "rationale": "32' range + sub mix for weight; LFO1→VCF1 is normalled so no
                cable needed — Mod Depth 1 low for gentle motion."
}
```

A cable is just `{ "from": "asr1_out", "to": "vca1_cv" }`. Two design rules make
output trustworthy:

1. **Prefer normals; suggest cables only when required.** Many Proton patches
   need *zero* cables because the always-on routing already does the job. The
   prompt should tell the model to add a cable only to defeat a normal or create
   a path that doesn't exist (e.g. series filters: `vcf1_out → vcf2_in`; looped
   ASR drum amp: `asr1_out → vca1_cv`). This mirrors §8 of the routing doc.
2. **Knob values are normalized 0–1 (or native units) and rendered by angle.**
   The user dials by eye — see the open-loop caveat in §7.

---

## 5. Validation layer (cheap correctness insurance)

Between the LLM and the renderer, run a deterministic check using the §3
catalogue. This is what makes suggestions reliable rather than plausible:

- **Jack legality:** every `cable.from` is an OUT jack, every `cable.to` is an IN
  jack; no duplicate destinations; no unknown ids.
- **Footgun flags** (straight from the doc): `sustain==0` while a release is
  implied → warn; cable into `vcf1_freq` while the rationale also relies on LFO 1
  there → note that the patch *replaces* the LFO (§2/§5.5); Wavefolder `AM`/`BP`
  with a Sym move → Sym has no effect (§4.1/§4.3).
- **Always-on reminders:** if the concept wants a fully open drone, surface that
  ADSR 2 → VCA is normalled and Bias must be CW to force the VCA open (§6).

These rules are already written in prose in §8 and the §300/§348 warnings — they
become a dozen assertions.

---

## 6. Build plan & effort

| Phase | Work | Rough effort |
|------:|------|------|
| 0 | Distill routing doc → `proton-model.json` (§3) | ~½ day, mechanical |
| 1 | **Panel asset + coordinate map** (the long pole) — stylized original SVG of the layout, with a tagged `<g>`/coordinate per control & jack | 1–2 days |
| 2 | Proxy endpoint + schema + system prompt (clone `tools/llm-proxy`) | ~1 day |
| 3 | Renderer: knob-angle, switch/LED state, bezier cables | 1–2 days |
| 4 | Validation + warnings layer (§5) | ~½ day |
| 5 *(optional, high-value)* | **WebMIDI SysEx auto-apply** — see §8 | 1–2 days |

A walking skeleton (hardcoded sample PatchSpec → rendered panel, no LLM yet)
is reachable in ~2 days and would prove the only unproven piece: the renderer.

---

## 7. Risks & constraints (none are blockers)

- **Open-loop by design.** Proton knobs are analog with no position readback, and
  the MIDI surface exposes only Mod Wheel (CC#1) and Sustain (CC#64) — §9/§19.
  The app *suggests*; the human dials in by eye. That's the correct scope for a
  "suggester" and matches how players use semi-modulars.
- **Panel art licensing.** Don't ship Behringer's copyrighted panel graphics.
  Build an original stylized layout (labelled boxes/knobs/jacks). It only needs
  to be *legible and correctly positioned*, not photoreal.
- **Four open routing questions** (§10 of the routing doc: wavefolder BP + CV
  jacks, Link vs patched Freq CV precedence, key-track depth, Soft self-osc
  pitch). Minor; they affect edge-case suggestions only. The validator can mark
  affected patches "verify on hardware."
- **LLM taste vs correctness.** Correctness is handled by the schema + validator;
  "does it sound good" is the model's judgement, exactly as the Plaits bank
  generator already relies on. The repo has precedent that this works well.

---

## 8. The upsell: SysEx makes this more than a picture

§9 of the routing doc enumerates a SysEx parameter for **almost every discrete
switch, mode, and routing choice** (VCF mode/link/soft/key, wavefolder mode, LFO
shape/sync, osc range/linear-FM, envelope modes, Assign In dest/depth, …).

That means a Phase-5 version using **WebMIDI** could *actually configure the
instrument*: push every switch/mode/routing setting from the suggested patch
straight to the hardware over USB SysEx, leaving the human to set only the
continuous analog knobs (which the rendered panel image shows). That turns the
app from "here's a diagram" into "the Proton is now 80% dialled in for you" — a
genuine differentiator, and the SysEx framing/IDs are already documented
(`F0 00 20 32 00 01 25 …`).

---

## 9. Recommendation

Proceed. Start with **Phase 0 + Phase 1 walking skeleton** to validate the
renderer (the only novel piece), then bolt on the LLM endpoint by cloning the
existing proxy. The instrument is already fully specified in this repo and the
NL→structured-patch pipeline is already proven here — the project is closer to a
focused build than to research.

## Sources / prior art in this repo

- [`docs/PROTON_SIGNAL_ROUTING.md`](PROTON_SIGNAL_ROUTING.md) — the complete
  instrument model this app stands on.
- [`tools/llm-proxy/`](../tools/llm-proxy/) — the proven NL→structured-patch
  service (Anthropic tool-use, JSON schema, per-session history) to clone.
- [`docs/NANOESP32_CORTHEX.md`](NANOESP32_CORTHEX.md) §`/llm` — the existing
  generate-a-patch-bank UX and `/api/patch/bank` contract for reference.
