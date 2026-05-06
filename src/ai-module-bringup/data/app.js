// CortHex diagnostics page — telemetry only.
// Plaits controls live on /plaits (plaits.js).

(function () {
  // --- Elements ---
  const conn = document.getElementById('conn');
  const btnEls = document.querySelectorAll('.btn');
  const audioL = {
    bar: document.getElementById('audio-l-bar'),
    stats: document.getElementById('audio-l-stats'),
  };
  const audioR = {
    bar: document.getElementById('audio-r-bar'),
    stats: document.getElementById('audio-r-stats'),
  };
  const gateState = document.getElementById('gate-state');
  const gatePulses = document.getElementById('gate-pulses');
  const cvEls = [
    document.getElementById('cv-0'),
    document.getElementById('cv-1'),
    document.getElementById('cv-2'),
    document.getElementById('cv-3'),
    document.getElementById('cv-4'),
    document.getElementById('cv-5'),
  ];
  const sysUptime = document.getElementById('sys-uptime');
  const sysHeap = document.getElementById('sys-heap');
  const sysRssi = document.getElementById('sys-rssi');
  const sysIp = document.getElementById('sys-ip');
  const patchCurrent = document.getElementById('patch-current');
  const patchButtons = document.querySelectorAll('.patch-btn');
  const patchTrigger = document.getElementById('patch-trigger');
  const patchRelease = document.getElementById('patch-release');

  // Must match the order of test_patches[] in main.cpp so the active-button
  // highlight tracks telemetry's `patch` field.
  const PATCH_NAMES = ['Pluck Bass', 'Slow Pad', 'Kick', 'Drone'];

  patchButtons.forEach((btn) => {
    btn.addEventListener('click', () => {
      const n = parseInt(btn.dataset.patch, 10);
      fetch(`/api/patch/test/${n}`, { method: 'POST' }).catch(() => {});
    });
  });
  patchTrigger.addEventListener('click', () => {
    fetch('/api/patch/trigger', { method: 'POST' }).catch(() => {});
  });
  patchRelease.addEventListener('click', () => {
    fetch('/api/patch/release', { method: 'POST' }).catch(() => {});
  });
  document.getElementById('patch-reset').addEventListener('click', () => {
    fetch('/api/patch/reset', { method: 'POST' }).catch(() => {});
  });

  // --- WebSocket telemetry ---
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
      try { apply(JSON.parse(event.data)); }
      catch (e) { console.error('parse error', e, event.data); }
    };
  }
  setInterval(refreshStatus, 500);

  function apply(msg) {
    if (msg.btn) updateButtons(msg.btn);
    if (msg.audio) updateAudio(msg.audio);
    if (msg.gate) updateGate(msg.gate);
    if (msg.cv) updateCV(msg.cv);
    if (typeof msg.patch === 'string') updatePatch(msg.patch);
    if (msg.sys) updateSys(msg.sys);
  }
  function updatePatch(name) {
    patchCurrent.textContent = name ? `current: ${name}` : '—';
    patchButtons.forEach((btn) => {
      const idx = parseInt(btn.dataset.patch, 10);
      btn.classList.toggle('active', PATCH_NAMES[idx] === name);
    });
  }
  function updateButtons(states) {
    for (let i = 0; i < btnEls.length && i < states.length; i++) {
      btnEls[i].classList.toggle('pressed', !!states[i]);
    }
  }
  function updateAudio(a) {
    if (a.l) {
      audioL.stats.textContent = `dc=${a.l.dc}  pp=${a.l.pp}`;
      audioL.bar.style.width = `${Math.min(100, (a.l.pp / 4096) * 100)}%`;
    }
    if (a.r) {
      audioR.stats.textContent = `dc=${a.r.dc}  pp=${a.r.pp}`;
      audioR.bar.style.width = `${Math.min(100, (a.r.pp / 4096) * 100)}%`;
    }
  }
  function updateGate(g) {
    gateState.textContent = g.state ? 'HIGH' : 'low';
    gateState.classList.toggle('high', !!g.state);
    gatePulses.textContent = g.pulses;
  }
  function updateCV(cv) {
    for (let i = 0; i < cvEls.length && i < cv.length; i++) {
      if (cvEls[i]) {
        const v = typeof cv[i] === 'number' ? cv[i] : parseFloat(cv[i]);
        cvEls[i].textContent = isFinite(v) ? `${v.toFixed(2)} V` : '—';
      }
    }
  }
  function updateSys(s) {
    if (s.uptime_s !== undefined) sysUptime.textContent = formatUptime(s.uptime_s);
    if (s.free_heap !== undefined) sysHeap.textContent = `${Math.round(s.free_heap / 1024)} KB`;
    if (s.rssi !== undefined) sysRssi.textContent = `${s.rssi} dBm`;
    if (s.ip) sysIp.textContent = s.ip;
  }
  function formatUptime(s) {
    if (s < 60) return `${s}s`;
    const m = Math.floor(s / 60);
    const ss = s % 60;
    if (m < 60) return `${m}m ${ss}s`;
    const h = Math.floor(m / 60);
    return `${h}h ${m % 60}m`;
  }

  connect();
})();
