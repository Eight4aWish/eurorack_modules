// CortHex diag page — WebSocket client + DOM updates.
// Read-only telemetry for v1; controls land in v2.

(function () {
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
  const clkCnt = document.getElementById('clk-cnt');
  const clkInt = document.getElementById('clk-int');
  const clkBpm = document.getElementById('clk-bpm');
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
    const url = `${proto}//${location.host}/ws`;
    ws = new WebSocket(url);

    ws.onopen = () => refreshStatus();
    ws.onclose = () => {
      refreshStatus();
      if (!reconnectTimer) {
        reconnectTimer = setTimeout(() => {
          reconnectTimer = null;
          connect();
        }, 2000);
      }
    };
    ws.onerror = () => refreshStatus();
    ws.onmessage = (event) => {
      lastMessageMs = performance.now();
      messageCount++;
      refreshStatus();
      try {
        apply(JSON.parse(event.data));
      } catch (e) {
        console.error('parse error', e, event.data);
      }
    };
  }

  // Poll status every 500 ms in case any of the event handlers misfire.
  setInterval(refreshStatus, 500);

  function apply(msg) {
    if (msg.btn) updateButtons(msg.btn);
    if (msg.audio) updateAudio(msg.audio);
    if (msg.clk) updateClock(msg.clk);
    if (msg.cv) updateCV(msg.cv);
    if (msg.sys) updateSys(msg.sys);
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

  function updateClock(c) {
    clkCnt.textContent = c.cnt;
    if (c.interval_ms && c.cnt >= 2) {
      clkInt.textContent = c.interval_ms;
      // BPM assumes the clock is at quarter-note resolution. If it's
      // 16ths, divide BPM by 4 mentally.
      clkBpm.textContent = Math.round(60000 / c.interval_ms);
    } else {
      clkInt.textContent = '—';
      clkBpm.textContent = '—';
    }
  }

  function updateCV(cv) {
    for (let i = 0; i < cvEls.length && i < cv.length; i++) {
      if (cvEls[i]) cvEls[i].textContent = cv[i];
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
    const mm = m % 60;
    return `${h}h ${mm}m`;
  }

  connect();
})();
