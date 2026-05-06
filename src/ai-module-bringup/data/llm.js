// CortHex LLM page — natural-language patch generation, bank-of-6 model.
//
// Talks to two backends:
//   1. The Mac proxy (configurable URL) at POST /generate — turns prompts
//      into 6 variations via Claude (set_patch_bank tool).
//   2. The module itself at POST /api/patch/bank — loads them into the
//      panel-button bank. Audition with hardware buttons 1–6 (or by
//      tapping a card on this page).
//
// Live state (active slot, model, CV voltages) comes from /ws telemetry.

(function () {
  // --- Element refs ---
  const conn = document.getElementById('conn');
  const proxyUrl = document.getElementById('proxy-url');
  const proxyTest = document.getElementById('proxy-test');
  const proxyStatus = document.getElementById('proxy-status');
  const resetBtn = document.getElementById('reset-session');
  const chatLog = document.getElementById('chat-log');
  const promptInput = document.getElementById('prompt-input');
  const sendBtn = document.getElementById('send-btn');
  const bankGrid = document.getElementById('bank-grid');
  const bankConcept = document.getElementById('bank-concept');
  const activeSlotLabel = document.getElementById('active-slot');
  const liveModel = document.getElementById('live-model');
  const liveCvEls = [
    document.getElementById('live-cv-0'),
    document.getElementById('live-cv-1'),
    document.getElementById('live-cv-2'),
    document.getElementById('live-cv-3'),
    document.getElementById('live-cv-4'),
    document.getElementById('live-cv-5'),
  ];

  // --- Engine names (display only) ---
  const ENGINES = [
    { name: 'Classic + Filter', bank: 'orange' },
    { name: 'Phase Dist',       bank: 'orange' },
    { name: '6-op FM I',        bank: 'orange' },
    { name: '6-op FM II',       bank: 'orange' },
    { name: '6-op FM III',      bank: 'orange' },
    { name: 'Wave Terrain',     bank: 'orange' },
    { name: 'String Machine',   bank: 'orange' },
    { name: '4-voice Sqr',      bank: 'orange' },
    { name: 'Pair VA',          bank: 'green'  },
    { name: 'Waveshaper',       bank: 'green'  },
    { name: '2-op FM',          bank: 'green'  },
    { name: 'Formant',          bank: 'green'  },
    { name: 'Harmonic',         bank: 'green'  },
    { name: 'Wavetable',        bank: 'green'  },
    { name: 'Chords',           bank: 'green'  },
    { name: 'Speech',           bank: 'green'  },
    { name: 'Granular',         bank: 'red'    },
    { name: 'Filtered Noise',   bank: 'red'    },
    { name: 'Particle',         bank: 'red'    },
    { name: 'String',           bank: 'red'    },
    { name: 'Modal',            bank: 'red'    },
    { name: 'Bass Drum',        bank: 'red'    },
    { name: 'Snare',            bank: 'red'    },
    { name: 'Hi-Hat',           bank: 'red'    },
  ];
  function engineLabel(idx) {
    if (idx == null || idx < 0 || idx >= ENGINES.length) return '—';
    const e = ENGINES[idx];
    const slot = (idx % 8) + 1;
    return `${idx}: ${e.name} (${e.bank} slot ${slot})`;
  }

  // --- Persistent state (localStorage) ---
  const LS_PROXY = 'corthex.llm.proxyUrl';
  const LS_SESSION = 'corthex.llm.sessionId';

  proxyUrl.value = localStorage.getItem(LS_PROXY) || '';
  let sessionId = localStorage.getItem(LS_SESSION);
  if (!sessionId) {
    sessionId = (crypto?.randomUUID?.() || `s-${Date.now()}-${Math.random().toString(36).slice(2)}`);
    localStorage.setItem(LS_SESSION, sessionId);
  }
  proxyUrl.addEventListener('change', () => {
    localStorage.setItem(LS_PROXY, proxyUrl.value.trim());
  });

  function proxyEndpoint(path) {
    const base = (proxyUrl.value || '').trim().replace(/\/+$/, '');
    if (!base) return null;
    return base + path;
  }

  // --- Local bank state (for rendering — module is the source of truth) ---
  let currentBank = null;       // { concept, patches: [6] } or null

  // --- Chat rendering ---
  function clearEmpty() {
    const empty = chatLog.querySelector('.chat-empty');
    if (empty) empty.remove();
  }
  function appendChatBubble(role, text, opts = {}) {
    clearEmpty();
    const wrap = document.createElement('div');
    wrap.className = `chat-bubble chat-${role}` + (opts.cls ? ' ' + opts.cls : '');
    if (opts.label) {
      const lbl = document.createElement('div');
      lbl.className = 'chat-label';
      lbl.textContent = opts.label;
      wrap.appendChild(lbl);
    }
    const body = document.createElement('div');
    body.className = 'chat-body';
    body.textContent = text;
    wrap.appendChild(body);
    chatLog.appendChild(wrap);
    chatLog.scrollTop = chatLog.scrollHeight;
    return wrap;
  }

  // --- Bank rendering ---
  function fmtMacro(label, m) {
    if (!m) return `${label}: —`;
    const env = (m.rise_ms || m.fall_ms)
      ? ` · env ${m.rise_ms}/${m.fall_ms} ms`
      : '';
    return `${label}: ${m.v >= 0 ? '+' : '−'}${Math.abs(m.v).toFixed(2)} V${env}`;
  }

  function renderBank(bank, activeSlot = 0) {
    bankGrid.innerHTML = '';
    if (!bank) {
      bankConcept.textContent = 'No bank loaded yet.';
      return;
    }
    bankConcept.textContent = bank.concept || '';
    (bank.patches || []).forEach((p, idx) => {
      const e = ENGINES[p.model] || { name: '?', bank: '?' };
      const card = document.createElement('div');
      card.className = `bank-card bank-${e.bank}` + (idx === activeSlot ? ' active' : '');
      card.dataset.slot = idx;

      const head = document.createElement('div');
      head.className = 'bank-card-head';
      head.innerHTML =
        `<span class="bank-card-num">${idx + 1}</span>` +
        `<span class="bank-card-name">${p.name || '(unnamed)'}</span>`;
      card.appendChild(head);

      const eng = document.createElement('div');
      eng.className = `bank-card-engine bank-${e.bank}`;
      eng.textContent = `${e.name}`;
      card.appendChild(eng);

      if (p.rationale) {
        const r = document.createElement('div');
        r.className = 'bank-card-rationale';
        r.textContent = p.rationale;
        card.appendChild(r);
      }

      const macros = document.createElement('div');
      macros.className = 'bank-card-macros';
      [
        ['T',  p.timbre],
        ['H',  p.harmonics],
        ['F',  p.swords_freq],
        ['R',  p.swords_res],
        ['VCA', p.vca],
      ].forEach(([lbl, m]) => {
        const row = document.createElement('div');
        row.textContent = fmtMacro(lbl, m);
        macros.appendChild(row);
      });
      card.appendChild(macros);

      bankGrid.appendChild(card);
    });
  }

  function highlightActiveSlot(activeSlot) {
    activeSlotLabel.textContent = (activeSlot != null && activeSlot >= 0)
      ? String(activeSlot + 1) : '—';
    document.querySelectorAll('.bank-card').forEach((el) => {
      const slot = parseInt(el.dataset.slot, 10);
      el.classList.toggle('active', slot === activeSlot);
    });
  }

  // --- Send prompt ---
  async function sendPrompt(text) {
    const url = proxyEndpoint('/generate');
    if (!url) {
      proxyStatus.textContent = 'set the proxy URL first';
      proxyStatus.className = 'proxy-status warn';
      return;
    }
    appendChatBubble('user', text, { label: 'you' });
    sendBtn.disabled = true;
    sendBtn.textContent = '…';
    const thinking = appendChatBubble('assistant', 'thinking…', {
      label: 'claude', cls: 'chat-thinking',
    });
    try {
      const r = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ session_id: sessionId, user_message: text }),
      });
      if (!r.ok) throw new Error(`HTTP ${r.status} ${await r.text()}`);
      const j = await r.json();
      thinking.querySelector('.chat-body').textContent =
        j.assistant_text || j.concept || '(bank loaded)';
      thinking.classList.remove('chat-thinking');
      if (j.patches && j.patches.length === 6) {
        currentBank = { concept: j.concept || '', patches: j.patches };
        renderBank(currentBank, 0);
        if (!j.applied_to_module) {
          // Proxy didn't auto-apply — push it to the module from here so the
          // panel buttons start working immediately.
          await fetch('/api/patch/bank', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ concept: currentBank.concept, patches: currentBank.patches }),
          }).catch((e) => {
            appendChatBubble('error', `Bank upload failed: ${e.message}`, { label: 'error' });
          });
        }
      } else {
        // No bank — Claude asked a clarifying question (just text)
      }
    } catch (e) {
      thinking.querySelector('.chat-body').textContent = `Error: ${e.message}`;
      thinking.classList.remove('chat-thinking');
      thinking.classList.add('chat-error');
    } finally {
      sendBtn.disabled = false;
      sendBtn.textContent = 'Send';
    }
  }

  sendBtn.addEventListener('click', () => {
    const text = promptInput.value.trim();
    if (!text) return;
    promptInput.value = '';
    sendPrompt(text);
  });
  promptInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
      e.preventDefault();
      sendBtn.click();
    }
  });

  // --- Reset chat ---
  resetBtn.addEventListener('click', async () => {
    chatLog.innerHTML = '<div class="chat-empty">Chat reset.</div>';
    currentBank = null;
    renderBank(null);
    const url = proxyEndpoint(`/sessions/${encodeURIComponent(sessionId)}/reset`);
    if (!url) return;
    try { await fetch(url, { method: 'POST' }); } catch {}
  });

  // --- Tap a card to remotely select that slot ---
  // We don't yet have a /api/patch/select endpoint; instead, re-POST the
  // single chosen patch via /api/patch (writes its values, but doesn't
  // update active_patch_idx in firmware). For now, tell the user to use
  // physical buttons. We can wire remote select later if needed.
  bankGrid.addEventListener('click', (e) => {
    const card = e.target.closest('.bank-card');
    if (!card) return;
    const slot = parseInt(card.dataset.slot, 10);
    if (Number.isNaN(slot) || !currentBank) return;
    // Hint to the user — remote select via firmware endpoint isn't wired yet.
    appendChatBubble('system',
      `Tap panel button ${slot + 1} on the module to audition this patch.`,
      { label: 'tip' });
  });

  // --- Proxy health-check (with timeout) ---
  proxyTest.addEventListener('click', async () => {
    const url = proxyEndpoint('/health');
    if (!url) {
      proxyStatus.textContent = 'set the proxy URL first';
      proxyStatus.className = 'proxy-status warn';
      return;
    }
    proxyStatus.textContent = 'testing…';
    proxyStatus.className = 'proxy-status';
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), 5000);
    try {
      const r = await fetch(url, { method: 'GET', signal: ctrl.signal });
      clearTimeout(timer);
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      const j = await r.json();
      proxyStatus.textContent = `OK · ${j.model}${j.apply_to_module ? ' · auto-applies' : ''}`;
      proxyStatus.className = 'proxy-status ok';
    } catch (e) {
      clearTimeout(timer);
      const msg = (e && e.name === 'AbortError')
        ? 'timed out (host not reachable from iPad)'
        : `unreachable: ${e.message}`;
      proxyStatus.textContent = msg;
      proxyStatus.className = 'proxy-status bad';
    }
  });

  // --- WebSocket telemetry ---
  let ws = null;
  let reconnectTimer = null;
  let lastMessageMs = 0;
  let messageCount = 0;
  const READY_LABELS = ['connecting', 'connected', 'closing', 'disconnected'];
  const READY_CLS = ['', 'ok', 'bad', 'bad'];

  function refreshConn() {
    if (!ws) { conn.textContent = 'no ws'; conn.className = 'bad'; return; }
    let label = READY_LABELS[ws.readyState] || `state ${ws.readyState}`;
    if (ws.readyState === 1 && messageCount > 0) {
      const age = Math.round((performance.now() - lastMessageMs) / 100) / 10;
      label = `connected · ${messageCount} msg · ${age}s`;
    }
    conn.textContent = label;
    conn.className = READY_CLS[ws.readyState] || '';
  }

  function applyTelemetry(msg) {
    if (Array.isArray(msg.cv)) {
      for (let i = 0; i < liveCvEls.length && i < msg.cv.length; i++) {
        const v = msg.cv[i];
        if (typeof v === 'number') liveCvEls[i].textContent = `${v.toFixed(2)} V`;
      }
    }
    if (msg.plaits && typeof msg.plaits.model === 'number') {
      liveModel.textContent = engineLabel(msg.plaits.model);
    }
    if (msg.bank && typeof msg.bank.slot === 'number') {
      highlightActiveSlot(msg.bank.slot);
    }
  }

  function connect() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${proto}//${location.host}/ws`);
    ws.onopen = () => refreshConn();
    ws.onclose = () => {
      refreshConn();
      if (!reconnectTimer) {
        reconnectTimer = setTimeout(() => { reconnectTimer = null; connect(); }, 2000);
      }
    };
    ws.onerror = () => refreshConn();
    ws.onmessage = (event) => {
      lastMessageMs = performance.now();
      messageCount++;
      refreshConn();
      try { applyTelemetry(JSON.parse(event.data)); } catch {}
    };
  }
  setInterval(refreshConn, 500);
  connect();

  // initial render — empty bank
  renderBank(null);
})();
