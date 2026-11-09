'use strict';

// Must match WEB_INPUT_POLICY_FRAME_MAX on the device.
const CHUNK = 128;
const PASTE_CONFIRM = 64;
const PASTE_MAX = 2048;
const QUICK_KEYS = {
  'ctrl-c': '\u0003', 'ctrl-d': '\u0004', 'ctrl-l': '\u000c', enter: '\r', esc: '\u001b', tab: '\t',
  up: '\u001b[A', down: '\u001b[B', left: '\u001b[D', right: '\u001b[C',
};

const $ = (id) => document.getElementById(id);
const terminal = $('terminal');
const state = $('state');
const streamText = $('streamText');
const streamDot = $('streamDot');
const consoleNotice = $('consoleNotice');
const diagPanel = $('diagnosticsPanel');
const diagBackdrop = $('diagBackdrop');
const diagBody = $('diagnosticsBody');
const keys = $('keys');
const menuBtn = $('menuBtn');

let canWrite = false;
let usbConnected = false;
let locked = false;
let connected = false;
let empty = true;
let lastRx = 0;
let lastInput = 0;
let openedAt = 0;
let ws = null;
let backoff = 500;
let reconnectTimer = 0;

const term = new SerialiteXterm.Terminal({
  cursorBlink: true,
  convertEol: false,
  scrollback: 2000,
  fontFamily: 'ui-monospace,SFMono-Regular,Menlo,Consolas,monospace',
  fontSize: 14,
  theme: {
    background: '#002B36', foreground: '#93A1A1', cursor: '#839496',
    black: '#073642', red: '#dc322f', green: '#859900', yellow: '#b58900', blue: '#268bd2',
    magenta: '#d33682', cyan: '#2aa198', white: '#eee8d5', brightBlack: '#002b36', brightRed: '#dc322f',
    brightGreen: '#586e75', brightYellow: '#657b83', brightBlue: '#839496', brightMagenta: '#6c71c4',
    brightCyan: '#93a1a1', brightWhite: '#fdf6e3',
  },
});
const fit = new SerialiteXterm.FitAddon();
term.loadAddon(fit);
term.open(terminal);

function refitTerminal() {
  fit.fit();
}
refitTerminal();
requestAnimationFrame(refitTerminal);
setTimeout(refitTerminal, 250);
addEventListener('resize', refitTerminal);
addEventListener('orientationchange', refitTerminal);
if (window.visualViewport) {
  visualViewport.addEventListener('resize', refitTerminal);
  visualViewport.addEventListener('scroll', refitTerminal);
}
term.write('Serial console ready.\r\n');

// ---- stream indicator ------------------------------------------------------

function streamExpanded() {
  return state.classList.contains('expanded') || state.matches(':hover,:focus');
}

function renderStreamLabel() {
  streamText.textContent = streamExpanded() ? state.dataset.detail : state.dataset.compact;
}

function setStream(text, ok) {
  state.dataset.detail = text;
  state.dataset.compact = ok ? 'OK' : 'ERROR';
  state.title = text;
  renderStreamLabel();
  streamDot.className = 'dot ' + (ok ? 'ok' : 'bad');
}

function hideConsoleNotice() {
  consoleNotice.classList.add('hidden');
}

function showConsoleNotice() {
  if (!empty) return;
  consoleNotice.classList.remove('hidden');
  setStream('USB link up; no console output yet', false);
}

function checkConsoleSilence() {
  if (!connected || locked || !usbConnected || !empty) return;
  const now = Date.now();
  if ((openedAt && now - openedAt > 4000 && !lastRx) || (lastInput && now - lastInput > 3000 && !lastRx)) {
    showConsoleNotice();
  }
}

function render() {
  canWrite = connected && usbConnected && !locked;
  term.options.disableStdin = !canWrite;
  document.querySelectorAll('#keys button[data-k]').forEach((b) => { b.disabled = !canWrite; });
  terminal.setAttribute('aria-label', canWrite ? 'Serial terminal, keyboard control active' : 'Serial terminal unavailable');
  if (!usbConnected) hideConsoleNotice();
}

// ---- session and connection ------------------------------------------------

function showLocked() {
  locked = true;
  connected = false;
  setStream('Input locked; press PWR to unlock', false);
  render();
  if (ws) ws.close();
}

async function openSession() {
  try {
    const session = await Serialite.startSession();
    if (session.locked) {
      showLocked();
      return false;
    }
    locked = false;
    usbConnected = !!session.usb_connected;
    render();
    return true;
  } catch (e) {
    setStream('Session unavailable', false);
    return false;
  }
}

function scheduleReconnect() {
  if (reconnectTimer || locked) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = 0;
    connect();
  }, backoff);
  backoff = Math.min(backoff * 2, 5000);
}

function connect() {
  if (locked || (ws && ws.readyState <= 1)) return;
  setStream('Stream ...', false);
  connected = false;
  render();
  // Same scheme as the page: lab builds serve plain HTTP inside the WPA2/WPA3 AP.
  // nosemgrep: javascript.lang.security.detect-insecure-websocket.detect-insecure-websocket
  const scheme = location.protocol === 'https:' ? 'wss://' : 'ws://';
  ws = new WebSocket(scheme + location.host + '/ws');
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => {
    openedAt = Date.now();
    lastRx = 0;
    connected = true;
    backoff = 500;
    setStream('Stream OK', true);
    render();
    term.focus();
  };
  ws.onmessage = (e) => {
    lastRx = Date.now();
    hideConsoleNotice();
    setStream('Stream OK', true);
    if (empty) {
      term.clear();
      empty = false;
    }
    term.write(e.data instanceof ArrayBuffer ? new Uint8Array(e.data) : e.data);
  };
  ws.onclose = () => {
    connected = false;
    render();
    if (locked) return;
    setStream('Stream off', false);
    refreshStatus();
    scheduleReconnect();
  };
  ws.onerror = () => ws.close();
}

async function refreshStatus() {
  try {
    const r = await Serialite.get('/api/status');
    if (r.status === 401) {
      // Session gone: expired, replaced, or ended by the emergency lock.
      if (await openSession()) scheduleReconnect();
      return;
    }
    if (!r.ok) return;
    const s = await r.json();
    usbConnected = !!s.usb_connected;
    if (s.locked) {
      showLocked();
      return;
    }
    render();
  } catch (e) {
    // Network blips are expected while the AP restarts.
  }
}

async function post(url) {
  const r = await Serialite.post(url);
  const text = await r.text();
  if (!r.ok) alert(text || 'Request failed: ' + r.status);
  return r.ok;
}

// ---- input -----------------------------------------------------------------

let pending = '';
let flushTimer = 0;

function flushSend() {
  const data = pending;
  pending = '';
  flushTimer = 0;
  if (!data || !(canWrite && ws && ws.readyState === 1)) return;
  for (let i = 0; i < data.length; i += CHUNK) ws.send(data.slice(i, i + CHUNK));
}

function send(data) {
  if (!(canWrite && ws && ws.readyState === 1)) return;
  lastInput = Date.now();
  if (pending.length + data.length > CHUNK) flushSend();
  pending += data;
  if (!flushTimer) flushTimer = setTimeout(flushSend, 20);
}

function safePaste(text) {
  if (!canWrite) return;
  if (text.length > PASTE_MAX) {
    alert('Paste too large; max ' + PASTE_MAX + ' bytes');
    return;
  }
  if ((text.length > PASTE_CONFIRM || text.includes('\n')) && !confirm('Send ' + text.length + ' bytes to the physical console?')) return;
  send(text);
}

function fitHostTty() {
  if (!canWrite) return;
  fit.fit();
  send(`stty rows ${term.rows} cols ${term.cols}\r`);
  setTimeout(() => send('\u000c'), 120);
}

function focusTerminal() {
  terminal.focus();
  term.focus();
}

function terminalHasFocus() {
  return document.activeElement === terminal || terminal.contains(document.activeElement) || document.activeElement === term.textarea;
}

function toggleFullscreen() {
  const target = document.documentElement;
  if (!document.fullscreenElement) {
    if (target.requestFullscreen) target.requestFullscreen();
  } else if (document.exitFullscreen) {
    document.exitFullscreen();
  }
  setTimeout(() => {
    fit.fit();
    term.focus();
  }, 120);
}

// ---- diagnostics -----------------------------------------------------------

function row(key, value) {
  return `<div class="diag-row"><span>${Serialite.esc(key)}</span><b>${Serialite.esc(value)}</b></div>`;
}

function renderDiagnostics(d) {
  const ok = (v) => (v ? 'OK' : 'OFF');
  const events = (d.events || []).slice(-8).map((e) => `${e.timestamp_ms}ms [${e.level}] ${e.message}`).join('\n') || 'No recent events';
  diagBody.innerHTML =
    `<section class="diag-card"><h3>System Health</h3>${row('USB', ok(d.usb && d.usb.connected))}${row('AP', ok(d.ap && d.ap.started))}` +
    `${row('Clients', d.ap ? d.ap.clients : 0)}${row('Uptime', Math.floor((d.uptime_ms || 0) / 1000) + 's')}${row('Heap free', Serialite.bytes(d.heap && d.heap.free))}</section>` +
    `<section class="diag-card"><h3>Traffic</h3>${row('USB RX', Serialite.bytes(d.usb && d.usb.rx_bytes))}${row('USB TX', Serialite.bytes(d.usb && d.usb.tx_bytes))}` +
    `${row('Drops RX', d.bridge ? d.bridge.dropped_usb_rx : 0)}${row('Drops TX', d.bridge ? d.bridge.dropped_usb_tx : 0)}${row('Subscribers', d.bridge ? d.bridge.subscribers : 0)}</section>` +
    `<section class="diag-card"><h3>Recent Events</h3><pre class="diag-events">${Serialite.esc(events)}</pre></section>` +
    '<section class="diag-card"><h3>Actions</h3><div class="action-grid two"><button id="copyDiagJson">Copy JSON</button><button id="refreshDiag">Refresh</button></div></section>';
  $('refreshDiag').onclick = loadDiagnostics;
  $('copyDiagJson').onclick = () => navigator.clipboard && navigator.clipboard.writeText(JSON.stringify(d, null, 2));
}

async function loadDiagnostics() {
  diagBody.innerHTML = '<section class="diag-card"><h3>Status</h3>' + row('Loading', '...') + '</section>';
  try {
    const r = await Serialite.get('/api/diagnostics');
    if (!r.ok) throw new Error('HTTP ' + r.status);
    renderDiagnostics(await r.json());
  } catch (e) {
    diagBody.innerHTML = '<section class="diag-card"><h3>Status</h3>' + row('Diagnostics', e.message || 'Unavailable') + '</section>';
  }
}

function openDiagnostics() {
  closeMenu();
  diagBackdrop.classList.add('open');
  diagPanel.classList.add('open');
  diagPanel.setAttribute('aria-hidden', 'false');
  loadDiagnostics();
}

function closeDiagnostics() {
  diagPanel.classList.remove('open');
  diagBackdrop.classList.remove('open');
  diagPanel.setAttribute('aria-hidden', 'true');
  focusTerminal();
}

function closeMenu() {
  keys.classList.remove('open');
  menuBtn.blur();
}

// ---- wiring ----------------------------------------------------------------

term.onData((data) => send(data));
term.attachCustomKeyEventHandler((e) => {
  if (e.ctrlKey && e.key.toLowerCase() === 'l') {
    send('\u000c');
    return false;
  }
  return true;
});
addEventListener('keydown', (e) => {
  if (canWrite && e.ctrlKey && !e.altKey && !e.metaKey && e.key && e.key.toLowerCase() === 'l' && terminalHasFocus()) {
    e.preventDefault();
    e.stopPropagation();
    send('\u000c');
  }
}, true);
addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    closeMenu();
    closeDiagnostics();
  }
});

terminal.addEventListener('click', focusTerminal);
terminal.addEventListener('touchstart', () => setTimeout(focusTerminal, 0), {passive: true});
terminal.addEventListener('paste', (e) => {
  const text = (e.clipboardData || window.clipboardData).getData('text');
  e.preventDefault();
  safePaste(text);
});

document.querySelectorAll('button[data-k]').forEach((b) => {
  b.onclick = () => {
    const data = QUICK_KEYS[b.dataset.k];
    if (data) send(data);
    closeMenu();
    focusTerminal();
  };
});
$('fitTty').onclick = fitHostTty;
$('clearTerm').onclick = () => {
  term.clear();
  focusTerminal();
};
$('fullscreenBtn').onclick = toggleFullscreen;
menuBtn.onclick = () => keys.classList.toggle('open');
$('closeMenuBtn').onclick = closeMenu;
$('diagnosticsBtn').onclick = openDiagnostics;
$('closeDiagnostics').onclick = closeDiagnostics;
diagBackdrop.onclick = closeDiagnostics;
keys.onmouseleave = closeMenu;
keys.onblur = (e) => {
  if (!keys.contains(e.relatedTarget)) closeMenu();
};

state.tabIndex = 0;
state.onclick = () => {
  state.classList.add('expanded');
  renderStreamLabel();
};
state.onmouseleave = () => {
  state.classList.remove('expanded');
  state.blur();
  renderStreamLabel();
};
state.onfocus = renderStreamLabel;
state.onblur = () => {
  state.classList.remove('expanded');
  renderStreamLabel();
};

$('rotateWifi').onclick = async () => {
  if (confirm('WiFi password will rotate now and Serialite will reboot. Continue?')) {
    await post('/api/credentials/rotate');
  }
};
$('panic').onclick = async () => {
  if (!confirm('Emergency lock ends this session and refuses new ones until PWR is pressed on the device. Continue?')) return;
  if (await post('/api/emergency-lock')) {
    showLocked();
    closeMenu();
    focusTerminal();
  }
};

(async () => {
  render();
  if (await openSession()) connect();
  setInterval(refreshStatus, 2000);
  setInterval(checkConsoleSilence, 1000);
  term.focus();
})();
