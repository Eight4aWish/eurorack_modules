// CortHex Plaits page — model picker + macro sliders + per-model reference.
//
// Bank colours follow the user's installed Plaits 1.2 alt firmware as
// observed on the device LEDs (which differs from the original Plaits 1.0
// orange→green→red ordering).
//
// Per-model H/T/M/OUT/AUX text for engines 0..15 lifted verbatim from the
// pichenettes-archived Plaits manual. Engines 16..23 (alt-firmware
// extensions) carry placeholder "?" descriptions until confirmed.

(function () {
  // --- Elements ---
  const conn = document.getElementById('conn');
  const modelBanks = document.getElementById('model-banks');
  const macroSliders = document.querySelectorAll('.macro-slider');
  const cv2v = document.getElementById('cv2-voltage');
  const cv3v = document.getElementById('cv3-voltage');
  const cv4v = document.getElementById('cv4-voltage');
  const cv2fn = document.getElementById('cv2-fn');
  const cv3fn = document.getElementById('cv3-fn');
  const cv4fn = document.getElementById('cv4-fn');
  const macroReadouts = { 1: cv2v, 2: cv3v, 3: cv4v };
  const macroCurrent = document.getElementById('macro-current');
  const infoOut = document.getElementById('info-out');
  const infoAux = document.getElementById('info-aux');
  const infoNote = document.getElementById('info-note');

  // --- Model data ---
  // Engine indices are CV-ordered (low V → high V). The user's installed
  // Plaits 1.2 alt firmware orders banks orange → green → red over CV:
  //   orange (engines 0–7)   Plaits 1.2 extensions
  //   green  (engines 8–15)  original Plaits 1.0 bank 1 (pitched)
  //   red    (engines 16–23) original Plaits 1.0 bank 2 (physical / noise)
  // Green/red engine text from the Mutable v1.2 manual (verbatim).
  // Orange engine text from the Rochefsky 1.2 infographic.
  const MODELS = [
    // ---- Orange bank: Plaits 1.2 extensions (engines 0–7, lowest CV) ----
    { i: 0,  name: 'Classic + Filter', bank: 'orange',
      h: 'filter / resonance character (CCW: gentle 24 dB/oct, CW: harsh 12 dB/oct)',
      t: 'filter cutoff',
      m: 'waveform and sub level',
      out: 'low-pass output',
      aux: '12 dB/oct high-pass output' },
    { i: 1,  name: 'Phase Dist',       bank: 'orange',
      h: 'distortion frequency',
      t: 'distortion amount',
      m: 'distortion asymmetry',
      out: "sync'd carrier (phase distortion)",
      aux: 'free-running carrier (phase modulation)' },
    { i: 2,  name: '6-op FM I',        bank: 'orange',
      h: 'preset selection (32 positions per mode)',
      t: 'modulator(s) level',
      m: 'envelope & modulation stretch / time-travel',
      out: "sync'd carrier (phase distortion)",
      aux: 'free-running carrier (phase modulation)',
      note: '6-op DX7 patches, two voices alternated by TRIG. With TRIG unpatched, a single voice plays as a drone and MORPH time-travels along the envelope. LEVEL acts as velocity.' },
    { i: 3,  name: '6-op FM II',       bank: 'orange',
      h: 'preset selection (32 positions per mode)',
      t: 'modulator(s) level',
      m: 'envelope & modulation stretch / time-travel',
      out: "sync'd carrier (phase distortion)",
      aux: 'free-running carrier (phase modulation)',
      note: 'Second 6-op DX7 SysEx bank (otherwise identical to 6-op FM I).' },
    { i: 4,  name: '6-op FM III',      bank: 'orange',
      h: 'preset selection (32 positions per mode)',
      t: 'modulator(s) level',
      m: 'envelope & modulation stretch / time-travel',
      out: "sync'd carrier (phase distortion)",
      aux: 'free-running carrier (phase modulation)',
      note: 'Third 6-op DX7 SysEx bank (otherwise identical to 6-op FM I).' },
    { i: 5,  name: 'Wave Terrain',     bank: 'orange',
      h: 'terrain selection (continuous interp between 8 2-D terrains)',
      t: 'path radius',
      m: 'path offset',
      out: 'direct terrain height (z)',
      aux: 'terrain height interpreted as phase distortion (sin(y+z))' },
    { i: 6,  name: 'String Machine',   bank: 'orange',
      h: 'chord',
      t: 'chorus / filter amount',
      m: 'waveform',
      out: 'voices 1 and 3 (predominantly)',
      aux: 'voices 2 and 4 (predominantly)',
      note: 'Solina-style string machine with stereo filter and chorus.' },
    { i: 7,  name: '4-voice Sqr',      bank: 'orange',
      h: 'chord',
      t: 'arpeggio type or chord inversion',
      m: 'pulse-width sync',
      out: 'square wave voices',
      aux: 'NES triangle voice',
      note: 'TRIG clocks the arpeggiator. TIMBRE attenuverter sets the envelope shape.' },

    // ---- Green bank: original Plaits 1.0 bank 1 (engines 8–15) ----
    { i: 8,  name: 'Pair VA',        bank: 'green',
      h: 'detuning between the two waves',
      t: "variable square (narrow pulse → full square → hardsync formants)",
      m: "variable saw (triangle → saw with widening notch / Braids' CSAW)",
      out: 'pair of classic VA waveforms',
      aux: "two hardsync'ed waveforms (shape by MORPH, detune by HARMONICS)",
      note: 'A narrow pulse or wide notch will sound silent.' },
    { i: 9,  name: 'Waveshaper',     bank: 'green',
      h: 'waveshaper waveform',
      t: 'wavefolder amount',
      m: 'waveform asymmetry',
      out: 'asymmetric triangle through waveshaper + wavefolder',
      aux: 'variant with another wavefolder curve' },
    { i: 10, name: '2-op FM',        bank: 'green',
      h: 'frequency ratio',
      t: 'modulation index',
      m: "feedback (op2 self past 12 o'clock; op1 phase before 12)",
      out: 'phase-modulated sine waves',
      aux: 'sub-oscillator' },
    { i: 11, name: 'Formant',        bank: 'green',
      h: 'frequency ratio between formant 1 and 2',
      t: 'formant frequency',
      m: 'formant width and shape',
      out: 'granular formant oscillator (sine-segment formants)',
      aux: "windowed-sine filtered waveforms — HARMONICS picks the filter (Braids Z***)" },
    { i: 12, name: 'Harmonic',       bank: 'green',
      h: 'number of bumps in the spectrum',
      t: 'index of the most prominent harmonic',
      m: 'bump shape (flat/wide → peaked/narrow)',
      out: 'additive harmonic oscillator',
      aux: 'subset of harmonics matching Hammond drawbars' },
    { i: 13, name: 'Wavetable',      bank: 'green',
      h: 'bank selection (4 interpolated, then 4 non-interpolated)',
      t: 'row index (waves sorted by brightness within a row)',
      m: 'column index',
      out: '8×8 wavetable oscillator',
      aux: 'low-fi output' },
    { i: 14, name: 'Chords',         bank: 'green',
      h: 'chord type',
      t: 'chord inversion and transposition',
      m: "waveform (string-machine raw → wavetable scan past 12 o'clock)",
      out: 'four-note chord',
      aux: 'root note of the chord' },
    { i: 15, name: 'Speech',         bank: 'green',
      h: 'crossfade across formant filtering / SAM / LPC vowels / LPC words',
      t: 'species selection (Daleks → chipmunks)',
      m: 'phoneme / word segment selection',
      out: 'synthesised speech',
      aux: "unfiltered vocal cords' signal",
      note: 'Patch a trigger to TRIG to utter words. FM attenuverter = intonation, MORPH attenuverter = speed.' },

    // ---- Red bank: original Plaits 1.0 bank 2 (engines 16–23, highest CV) ----
    { i: 16, name: 'Granular',       bank: 'red',
      h: 'amount of pitch randomisation',
      t: 'grain density',
      m: 'grain duration and overlap',
      out: 'swarm of 8 enveloped sawtooth waves',
      aux: 'variant with sine wave oscillators' },
    { i: 17, name: 'Filtered Noise', bank: 'red',
      h: 'filter response (LP → BP → HP)',
      t: 'clock frequency',
      m: 'filter resonance',
      out: 'variable-clock noise through resonant filter',
      aux: 'two band-pass filters with separation set by HARMONICS' },
    { i: 18, name: 'Particle',       bank: 'red',
      h: 'amount of frequency randomisation',
      t: 'particle density',
      m: "filter type (all-pass network before 12 o'clock → resonant BP after)",
      out: 'dust noise through filter network',
      aux: 'raw dust noise' },
    { i: 19, name: 'String',         bank: 'red',
      h: 'amount of inharmonicity / material selection',
      t: 'excitation brightness and dust density',
      m: 'decay time (energy absorption)',
      out: 'inharmonic string resonator',
      aux: 'raw exciter signal',
      note: 'When TRIG is unpatched, the resonator is excited by dust noise.' },
    { i: 20, name: 'Modal',          bank: 'red',
      h: 'amount of inharmonicity / material selection',
      t: 'excitation brightness and dust density',
      m: 'decay time (energy absorption)',
      out: 'modal resonator bank (mini-Rings)',
      aux: 'raw exciter signal',
      note: 'When TRIG is unpatched, the resonator is excited by dust noise.' },
    { i: 21, name: 'Bass Drum',      bank: 'red',
      h: 'attack sharpness',
      t: 'brightness',
      m: 'decay time',
      out: 'analog bass drum model',
      aux: 'emulation of another classic bass drum circuit',
      note: 'Uses its own envelope and filter — internal LPG is disabled for this engine.' },
    { i: 22, name: 'Snare',          bank: 'red',
      h: 'balance of harmonic and noisy components',
      t: 'balance between the modes of the drum',
      m: 'decay time',
      out: 'analog snare drum model',
      aux: 'emulation of another classic snare drum circuit',
      note: 'Uses its own envelope and filter — internal LPG is disabled for this engine.' },
    { i: 23, name: 'Hi-Hat',         bank: 'red',
      h: 'balance of metallic and filtered noise',
      t: 'high-pass filter cutoff',
      m: 'decay time',
      out: 'analog hi-hat model',
      aux: 'variant with tuned noise via ring-modulated square waves',
      note: 'Uses its own envelope and filter — internal LPG is disabled for this engine.' },
  ];

  // Bank presentation order (top to bottom). Mirrors the device's CV→bank
  // order: low CV → orange → green → high CV → red.
  const BANK_ORDER = [
    { id: 'orange', label: 'Orange — extended (alt fw)',   start: 0  },
    { id: 'green',  label: 'Green — pitched / synthesis',  start: 8  },
    { id: 'red',    label: 'Red — physical / noise',       start: 16 },
  ];

  const macroPending = { 1: null, 2: null, 3: null };

  // After the user taps a model, telemetry will lag by ~250 ms. During that
  // window we want to keep showing the tapped model — not flicker back to
  // whatever the firmware was reporting before our POST landed. `lockedModel`
  // pins the info panel; cleared once telemetry catches up to the same idx,
  // or after a 2 s safety timeout.
  let lockedModel = null;
  let lockedTimer = null;

  // --- Build the model picker, sectioned by bank ---
  function buildModelPicker() {
    BANK_ORDER.forEach((bank) => {
      const section = document.createElement('div');
      section.className = `model-bank bank-${bank.id}`;

      const label = document.createElement('div');
      label.className = `bank-header bank-${bank.id}`;
      label.textContent = bank.label;
      section.appendChild(label);

      const grid = document.createElement('div');
      grid.className = 'model-bank-cells';
      for (let n = 0; n < 8; n++) {
        const idx = bank.start + n;
        const m = MODELS[idx];
        const cell = document.createElement('button');
        cell.className = `model-cell bank-${bank.id}`;
        cell.dataset.i = idx;
        cell.innerHTML =
          `<span class="model-cell-slot">${n + 1}</span>` +
          `<span class="model-cell-name">${m.name}</span>`;
        cell.title = `Engine ${idx} — ${m.name}`;
        cell.addEventListener('click', () => snapToModel(idx));
        grid.appendChild(cell);
      }
      section.appendChild(grid);

      modelBanks.appendChild(section);
    });
  }

  function snapToModel(idx) {
    lockedModel = idx;
    if (lockedTimer) clearTimeout(lockedTimer);
    lockedTimer = setTimeout(() => { lockedModel = null; }, 2000);
    setActiveCell(idx);
    showModelInfo(idx);
    fetch('/api/plaits/model', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: String(idx),
    }).catch(() => {});
  }

  function setActiveCell(idx) {
    document.querySelectorAll('.model-cell').forEach((el) => {
      el.classList.toggle('active', parseInt(el.dataset.i, 10) === idx);
    });
  }

  // Push the current model's per-macro descriptions onto the slider labels
  // and the OUT/AUX info rows. Also updates the "currently controlling"
  // header above the macro stack.
  function showModelInfo(idx) {
    if (idx < 0 || idx >= MODELS.length) return;
    const m = MODELS[idx];
    const slot = (m.i % 8) + 1;
    macroCurrent.textContent =
      `Controls for ${m.name} — slot ${slot} ${m.bank}`;
    macroCurrent.className = `macro-current bank-${m.bank}`;
    cv2fn.textContent = m.t;
    cv3fn.textContent = m.h;
    cv4fn.textContent = m.m;
    infoOut.textContent = m.out;
    infoAux.textContent = m.aux;
    if (m.note) {
      infoNote.textContent = m.note;
      infoNote.style.display = '';
    } else {
      infoNote.textContent = '';
      infoNote.style.display = 'none';
    }
  }

  function postCvDebounced(ch, v) {
    if (macroPending[ch]) clearTimeout(macroPending[ch]);
    macroPending[ch] = setTimeout(() => {
      macroPending[ch] = null;
      fetch(`/api/cv/${ch}`, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: v.toFixed(4),
      }).catch(() => {});
    }, 30);
  }

  // --- Wire macro sliders (CV2-4) ---
  macroSliders.forEach((s) => {
    s.addEventListener('input', () => {
      const ch = parseInt(s.dataset.ch, 10);
      const v = parseInt(s.value, 10) / 1000.0;
      const out = macroReadouts[ch];
      if (out) out.textContent = `+${v.toFixed(3)} V`;
      postCvDebounced(ch, v);
    });
  });

  // --- WebSocket for live model index + first-frame slider sync ---
  let ws = null;
  let reconnectTimer = null;
  let lastMessageMs = 0;
  let messageCount = 0;
  const READY_STATE_LABELS = ['connecting', 'connected', 'closing', 'disconnected'];
  const READY_STATE_CLASSES = ['', 'ok', 'bad', 'bad'];

  function refreshStatus() {
    if (!ws) {
      conn.textContent = 'no ws';
      conn.className = 'bad';
      return;
    }
    let label = READY_STATE_LABELS[ws.readyState] || `state ${ws.readyState}`;
    if (ws.readyState === 1 && messageCount > 0) {
      const age = Math.round((performance.now() - lastMessageMs) / 100) / 10;
      label = `connected · ${messageCount} msg · ${age}s`;
    }
    conn.textContent = label;
    conn.className = READY_STATE_CLASSES[ws.readyState] || '';
  }

  let macroSlidersInited = false;
  function applyTelemetry(msg) {
    if (msg.plaits && typeof msg.plaits.model === 'number') {
      const idx = msg.plaits.model;
      if (idx >= 0 && idx < MODELS.length) {
        // If the user just tapped a model, don't let stale telemetry undo
        // it. Once the firmware catches up to the locked model, release.
        if (lockedModel !== null) {
          if (lockedModel === idx) {
            lockedModel = null;
            if (lockedTimer) { clearTimeout(lockedTimer); lockedTimer = null; }
          } else {
            return;  // ignore stale frame
          }
        }
        setActiveCell(idx);
        showModelInfo(idx);
      }
    }
    if (!macroSlidersInited && Array.isArray(msg.cv) && msg.cv.length >= 4) {
      [1, 2, 3].forEach((ch) => {
        const v = msg.cv[ch];
        if (typeof v !== 'number') return;
        const slider = document.querySelector(`.macro-slider[data-ch="${ch}"]`);
        if (slider) {
          slider.value = Math.round(Math.max(0, Math.min(5, v)) * 1000);
          const out = macroReadouts[ch];
          if (out) out.textContent = `+${v.toFixed(3)} V`;
        }
      });
      macroSlidersInited = true;
    }
  }

  function connect() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${proto}//${location.host}/ws`);
    ws.onopen = () => refreshStatus();
    ws.onclose = () => {
      refreshStatus();
      if (!reconnectTimer) {
        reconnectTimer = setTimeout(() => { reconnectTimer = null; connect(); }, 2000);
      }
    };
    ws.onerror = () => refreshStatus();
    ws.onmessage = (event) => {
      lastMessageMs = performance.now();
      messageCount++;
      refreshStatus();
      try { applyTelemetry(JSON.parse(event.data)); }
      catch (e) { console.error('parse error', e, event.data); }
    };
  }
  setInterval(refreshStatus, 500);

  buildModelPicker();
  showModelInfo(0);
  connect();
})();
